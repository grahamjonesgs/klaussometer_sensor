// Core Arduino and WiFi libraries
#include "config.h"
#include "html.h"
#include <Arduino.h>
#include <ArduinoMqttClient.h>
#include <DHT.h>
#include <HTTPClient.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiClient.h>

// Constants
const uint64_t MICROSECONDS_IN_SECOND = 1000000ULL; // Conversion factor for microseconds to seconds

// RTC Memory variables (persist during deep sleep)
RTC_DATA_ATTR int bootCount = 0;
RTC_DATA_ATTR int successCount = 0;
RTC_DATA_ATTR float lastVolts = 0.0;

struct SensorData {
    float temperature;
    float humidity;
    bool success;
};

// Global variables
BoardConfig boardConfig;
char macAddress[18];
char temperatureTopic[50];
char humidityTopic[50];
char debugTopic[50];
char batteryTopic[50];
WiFiClient espClient;
MqttClient mqttClient(espClient);
DHT dht(0, 0);                   // Will be re-initialized with correct values in loadBoardConfig()
long lastReadingTime = LONG_MIN; // Initialize to a large negative value
WebServer webServer(80);
float lastTemp = 0.0;
float lastHumid = 0.0;
char lastReadingTimeStr[50] = "N/A";
char debugBuf[256];
char batteryMessage[256] = "";

// Definitions for NTP time
const char* ntpServer = "pool.ntp.org";
const long gmtOffsetSec = 7200;
const int daylightOffsetSec = 0;
struct tm timeinfo;
char timeBuffer[50];

// Function prototypes
void deepSleep(int sleepSeconds);
void debugMessage(const char* message, bool perm);
void mqttReconnect();
bool setupWifi();
void loadBoardConfig();
BoardConfig getBoardConfig(char* mac);
void setupOtaWeb();
void updateFirmware();
void checkForUpdates();
String getUptime();
SensorData readDhtSensor();
float readBatteryVoltage();
void mqttSendFloat(const char* topic, float value);
int compareVersions(const String& v1, const String& v2);

void setup() {
    bootCount++;
    Serial.begin(115200);
    while (!Serial) {
        ; // Wait for serial port to connect.
    }
    // Load configuration based on MAC address
    loadBoardConfig();
    if (boardConfig.isBatteryPowered && boardConfig.dhtPowerPin > 0) {
        pinMode(boardConfig.dhtPowerPin, OUTPUT);
        digitalWrite(boardConfig.dhtPowerPin, HIGH); // Power on early to warm up during WiFi connect
    }
    // Initialize DHT sensor
    dht.begin();
    mqttClient.setUsernamePassword(MQTT_USER, MQTT_PASSWORD);
}

void loop() {
    // For mains-powered devices, wait until the next reading time.
    if (!boardConfig.isBatteryPowered) {
        unsigned long currentTime = millis();
        unsigned long nextReadingTime = lastReadingTime + (boardConfig.timeToSleep * 1000UL);

        // Handle overflow-safe comparison
        while ((long)(currentTime - nextReadingTime) < 0) {
            webServer.handleClient();
            delay(WEB_SERVER_POLL_INTERVAL_MS); // Don't busy-wait
            currentTime = millis();
        }
    }

    if (!setupWifi()) {
        if (boardConfig.isBatteryPowered) {
            deepSleep(boardConfig.timeToSleep);
        } else {
            debugMessage("Failed to connect to WiFi, waiting for next cycle...", false);
            lastReadingTime = millis();
            return;
        }
    }

    checkForUpdates();
    if (!mqttClient.connected()) {
        mqttReconnect();
    }

    // Get current time from NTP
    configTime(gmtOffsetSec, daylightOffsetSec, ntpServer);
    if (!getLocalTime(&timeinfo)) {
        strncpy(timeBuffer, "Time Error", sizeof(timeBuffer) - 1);
    } else {
        strftime(timeBuffer, sizeof(timeBuffer) - 1, "%d/%m/%y %H:%M:%S", &timeinfo);
    }

    // Read DHT sensor data with retries
    SensorData reading = readDhtSensor();

    if (!reading.success) {
        snprintf(debugBuf, sizeof(debugBuf), "%s DHT read failed after %d retries.", timeBuffer, DHT_RETRIES);
        debugMessage(debugBuf, true);
        if (boardConfig.isBatteryPowered) {
            deepSleep(boardConfig.timeToSleep);
        } else {
            lastReadingTime = millis();
            return;
        }
    }

    successCount++;
    // Store the last successful readings and reading time
    lastTemp = reading.temperature;
    lastHumid = reading.humidity;
    strncpy(lastReadingTimeStr, timeBuffer, sizeof(lastReadingTimeStr) - 1);
    lastReadingTimeStr[sizeof(lastReadingTimeStr) - 1] = '\0';

    batteryMessage[0] = '\0'; // Clear the battery message buffer

    if (boardConfig.isBatteryPowered && boardConfig.battPin > 0) {
        lastVolts = readBatteryVoltage();
        snprintf(batteryMessage, sizeof(batteryMessage), " | Bat: %.2fV", lastVolts);
        mqttSendFloat(batteryTopic, lastVolts);
    }

    char mqttMessage[256];
    snprintf(mqttMessage, sizeof(mqttMessage), "%s | T: %.1f | H: %.0f%s | Boot: %d | Success: %d", timeBuffer, reading.temperature, reading.humidity, batteryMessage, bootCount,
             successCount);
    debugMessage(mqttMessage, true);
    mqttSendFloat(temperatureTopic, reading.temperature);
    mqttSendFloat(humidityTopic, reading.humidity);

    // Conditional deep sleep or time-based wait
    if (boardConfig.isBatteryPowered) {
        delay(1000); // Wait for messages to be sent
        deepSleep(boardConfig.timeToSleep);
    } else {
        lastReadingTime = millis();
    }
}

// Load board configuration based on MAC address
void loadBoardConfig() {
    strncpy(macAddress, WiFi.macAddress().c_str(), sizeof(macAddress) - 1);
    macAddress[sizeof(macAddress) - 1] = '\0';
    snprintf(debugBuf, sizeof(debugBuf), "Board MAC Address: %s", macAddress);
    debugMessage(debugBuf, false);
    boardConfig = getBoardConfig(macAddress);

    // Set up topics based on the retrieved configuration
    snprintf(temperatureTopic, sizeof(temperatureTopic), "%s%s%s", MQTT_TOPIC_USER, boardConfig.roomName, MQTT_TEMP_TOPIC);
    snprintf(humidityTopic, sizeof(humidityTopic), "%s%s%s", MQTT_TOPIC_USER, boardConfig.roomName, MQTT_HUMID_TOPIC);
    snprintf(debugTopic, sizeof(debugTopic), "%s%s%s", MQTT_TOPIC_USER, boardConfig.roomName, MQTT_DEBUG_TOPIC);
    snprintf(batteryTopic, sizeof(batteryTopic), "%s%s%s", MQTT_TOPIC_USER, boardConfig.roomName, MQTT_BATTERY_TOPIC);

    // Re-configure DHT with the correct pin from the config
    dht = DHT(boardConfig.dhtDataPin, boardConfig.dhtType);
}

// Set up WiFi connection
bool setupWifi() {
    int counter = 1;
    while (WiFi.status() != WL_CONNECTED) {
        debugMessage("WiFi is not OK, reconnecting", false);
        if (counter > WIFI_RETRIES) {
            snprintf(debugBuf, sizeof(debugBuf), "WiFi connection failed after %d retries.", WIFI_RETRIES);
            debugMessage(debugBuf, false);
            return false;
        }
        WiFi.disconnect();
        WiFi.mode(WIFI_STA);
        WiFi.enableSTA(true);
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        snprintf(debugBuf, sizeof(debugBuf), "Attempt %d to connect to WiFi...", counter);
        debugMessage(debugBuf, false);
        delay(1000);
        counter++;
        if (WiFi.status() == WL_CONNECTED) {
            IPAddress ip = WiFi.localIP();
            char ipStr[16];
            snprintf(ipStr, sizeof(ipStr), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
            snprintf(debugBuf, sizeof(debugBuf), "WiFi is OK => IP address is: %s", ipStr);
            debugMessage(debugBuf, false);
        }
        else {
            delay(3000);
        }
    }
    if (!boardConfig.isBatteryPowered) {
        static bool webServerStarted = false;
        if (!webServerStarted) {
            setupOtaWeb();
            webServerStarted = true;
        }
    }
    return true;
}

// Reconnect to MQTT broker
void mqttReconnect() {
    int counter = 1;
    while (!mqttClient.connected()) {
        if (counter > MQTT_RETRIES) {
            snprintf(debugBuf, sizeof(debugBuf), "[Error] MQTT connection failed after %d retries.", MQTT_RETRIES);
            debugMessage(debugBuf, false);
            // This is a major error; on battery, we deep sleep. For mains, we continue the loop.
            if (boardConfig.isBatteryPowered) {
                deepSleep(boardConfig.timeToSleep);
            } else {
                ESP.restart();
            }
        }
        debugMessage("Connecting to MQTT broker...", false);
        if (mqttClient.connect(MQTT_SERVER, MQTT_PORT)) {
            debugMessage("MQTT link OK", false);
        } else {
            debugMessage("[Error] MQTT not connected: ", false);
            delay(3000);
        }
        counter++;
    }
}

void mqttSendFloat(const char* topic, float value) {
    mqttClient.beginMessage(topic);
    mqttClient.printf("%.2f", value);
    mqttClient.endMessage();
}

// Send debug message to MQTT and/or Serial
void debugMessage(const char* message, bool retain) {
    char fullMessageBuffer[256];
    snprintf(fullMessageBuffer, sizeof(fullMessageBuffer), "V%s | %s", FIRMWARE_VERSION, message);

    if (DEBUG_MQTT) {
        delay(100);
        mqttClient.beginMessage(debugTopic, retain);
        mqttClient.printf("%s", fullMessageBuffer);
        mqttClient.endMessage();
        delay(100);
    }

    if (DEBUG_SERIAL) {
        Serial.println(fullMessageBuffer);
    }
}

// Enter deep sleep for a specified number of seconds
void deepSleep(int sleepSeconds) {
    esp_sleep_enable_timer_wakeup(sleepSeconds * MICROSECONDS_IN_SECOND);
    snprintf(debugBuf, sizeof(debugBuf), "Entering deep sleep for %d seconds...", sleepSeconds);
    debugMessage(debugBuf, false);
    WiFi.disconnect();
    esp_deep_sleep_start();
}

void setupOtaWeb() {
    webServer.on("/", HTTP_GET, []() {
        String content;
        content.reserve(1024);
        content = "<p class='section-title'>Device Information</p>"
                  "<table class='data-table'>"
                  "<tr><td><b>Firmware Version:</b></td><td>" +
                  String(FIRMWARE_VERSION) +
                  "</td></tr>"
                  "<tr><td><b>MAC Address:</b></td><td>" +
                  macAddress +
                  "</td></tr>"
                  "<tr><td><b>Room:</b></td><td>" +
                  String(boardConfig.displayName) +
                  "</td></tr>"
                  "<tr><td><b>Uptime:</b></td><td><span id=\"uptime\">" +
                  getUptime().c_str() +
                  "</span></td></tr>"
                  "</table>";

        content += "<p class='section-title'>Current Readings</p>"
                   "<table class='data-table'>";

        if (strncmp(lastReadingTimeStr, "N/A", 3) == 0) {
            // No successful readings yet
            content += "<tr><td><b>Last Update Time:</b></td><td><span id=\"time\">N/A</span></td></tr>"
                       "<tr><td><b>Temperature:</b></td><td><span id=\"temp\">N/A</span></td></tr>"
                       "<tr><td><b>Humidity:</b></td><td><span id=\"humid\">N/A</span></td></tr>";
            if (boardConfig.isBatteryPowered) {
                content += "<tr><td><b>Battery Voltage:</b></td><td><span id=\"voltage\">N/A</span></td></tr>";
            }
        } else {
            // Display last known readings
            content += "<tr><td><b>Last Update Time:</b></td><td><span id=\"time\">" + String(lastReadingTimeStr) +
                       "</span></td></tr>"
                       "<tr><td><b>Temperature:</b></td><td><span id=\"temp\">" +
                       String(lastTemp, 1) +
                       "</span> &deg;C</td></tr>"
                       "<tr><td><b>Humidity:</b></td><td><span id=\"humid\">" +
                       String(lastHumid, 0) + "</span> %</td></tr>";
            if (boardConfig.isBatteryPowered) {
                content += "<tr><td><b>Battery Voltage:</b></td><td><span id=\"voltage\">" + String(lastVolts, 2) + "</span> V</td></tr>";
            }
        }

        content += "</table>";

        String html;
        html.reserve(1024);
        html = info_html;
        html.replace("{{content}}", content);
        webServer.send(200, "text/html", html);
    });

    webServer.on("/data", HTTP_GET, []() {
        char dataBuffer[256];
        if (strncmp(lastReadingTimeStr, "N/A", 3) == 0) {
            // No successful readings yet
            snprintf(dataBuffer, sizeof(dataBuffer), "{\"temperature\":\"%s\", \"humidity\":\"%s\", \"voltage\":%.2f, \"time\":\"%s\", \"uptime\":\"%s\"}", "N/A", "N/A", lastVolts,
                     lastReadingTimeStr, getUptime().c_str());
        } else {
            // Display last known readings
            snprintf(dataBuffer, sizeof(dataBuffer), "{\"temperature\":%.1f, \"humidity\":%.0f, \"voltage\":%.2f, \"time\":\"%s\", \"uptime\":\"%s\"}", lastTemp, lastHumid,
                     lastVolts, lastReadingTimeStr, getUptime().c_str());
        }
        webServer.send(200, "application/json", dataBuffer);
    });

    webServer.on("/update", HTTP_GET, []() {
        String htmlPage;
        htmlPage.reserve(1024);
        htmlPage = ota_html;
        htmlPage.replace("{{FIRMWARE_VERSION}}", FIRMWARE_VERSION);
        webServer.send(200, "text/html", htmlPage);
    });

    webServer.on(
        "/update", HTTP_POST,
        []() {
            webServer.sendHeader("Connection", "close");
            webServer.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
            delay(1000);
            ESP.restart();
        },
        []() {
            HTTPUpload& upload = webServer.upload();
            if (upload.status == UPLOAD_FILE_START) {
                if (!Update.begin(UPDATE_SIZE_UNKNOWN)) { // Start with unknown size
                    Update.printError(Serial);
                }
            } else if (upload.status == UPLOAD_FILE_WRITE) {
                if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
                    Update.printError(Serial);
                }
            } else if (upload.status == UPLOAD_FILE_END) {
                if (Update.end(true)) {
                    debugMessage("Update Success, rebooting...\n", true);
                    delay(1000);
                } else {
                    Update.printError(Serial);
                }
            }
        });

    webServer.begin();
}

void checkForUpdates() {
    if (WiFi.status() == WL_CONNECTED) {
        HTTPClient http;
        // Check version
        char binUrl[256];
        snprintf(binUrl, sizeof(binUrl), "https://%s:%d%s", OTA_HOST, OTA_PORT, OTA_VERSION_PATH);
        http.begin(binUrl);
        int httpCode = http.GET();
        if (httpCode == HTTP_CODE_OK) {
            String serverVersion = http.getString();
            serverVersion.trim();
            if (compareVersions(serverVersion, FIRMWARE_VERSION) > 0) {
                snprintf(debugBuf, sizeof(debugBuf), "New firmware version available: %s (current: %s)", serverVersion.c_str(), FIRMWARE_VERSION);
                debugMessage(debugBuf, true);
                // Start the update
                updateFirmware();
            }
        } else {
            debugMessage("Error fetching version file.", true);
        }
        http.end();
    } else {
        debugMessage("WiFi not connected. Cannot check for updates.", false);
    }
}

void updateFirmware() {
    HTTPClient http;
    char binUrl[256];
    snprintf(binUrl, sizeof(binUrl), "https://%s:%d%s", OTA_HOST, 443, OTA_BIN_PATH);
    http.begin(binUrl);
    int httpCode = http.GET();
    if (httpCode == HTTP_CODE_OK) {
        int contentLength = http.getSize();
        bool canBegin = Update.begin(contentLength);
        if (canBegin) {
            debugMessage("Beginning OTA update. This may take a few moments...", true);
            WiFiClient& client = http.getStream();
            size_t written = Update.writeStream(client);
            if (written == contentLength) {
                debugMessage("OTA update written successfully.", true);
            } else {
                debugMessage("OTA update failed to write completely.", true);
            }
            if (Update.end()) {
                debugMessage("Update finished successfully. Restarting...", true);
                ESP.restart();
            } else {
                snprintf(debugBuf, sizeof(debugBuf), "Error during OTA update: %s", Update.errorString());
                debugMessage(debugBuf, true);
            }
        } else {
            debugMessage("Not enough space to start OTA update.", true);
        }
    } else {
        snprintf(debugBuf, sizeof(debugBuf), "HTTP GET failed, error: %d", httpCode);
        debugMessage(debugBuf, true);
    }
    http.end();
}

String getUptime() {
    unsigned long uptimeMs = millis();
    unsigned long seconds = uptimeMs / 1000;

    unsigned long days = seconds / (24 * 3600);
    unsigned long hours = (seconds % (24 * 3600)) / 3600;
    unsigned long minutes = (seconds % 3600) / 60;
    unsigned long remainingSeconds = seconds % 60;

    // Determine the correct pluralization for "day"
    const char* dayLabel = (days == 1) ? " day" : " days";

    char buffer[64];

    snprintf(buffer, sizeof(buffer), "%lu%s, %02lu:%02lu:%02lu", days, dayLabel, hours, minutes, remainingSeconds);

    return String(buffer);
}

SensorData readDhtSensor() {
    SensorData data;
    data.temperature = NAN;
    data.humidity = NAN;
    data.success = false;

    // DHT already powered on in setup() for battery boards - no warmup delay needed here

    for (int i = 0; i < DHT_RETRIES; i++) {
        data.temperature = dht.readTemperature();
        data.humidity = dht.readHumidity();
        if (!isnan(data.temperature) && !isnan(data.humidity)) {
            data.success = true;
            break; // Success, exit the retry loop
        }
        delay(500); // Small delay between retries
    }

    if (boardConfig.isBatteryPowered) {
        digitalWrite(boardConfig.dhtPowerPin, LOW);
    }

    return data;
}

float readBatteryVoltage() {
    if (!boardConfig.isBatteryPowered || boardConfig.battPin <= 0) {
        return 0.0;
    }

    // Configure ADC for better accuracy
    analogSetAttenuation(ADC_11db); // 0-3.6V range
    analogSetWidth(12);             // 12-bit resolution

    // Discard first reading (often inaccurate)
    analogRead(boardConfig.battPin);
    delay(10);

    uint32_t total = 0;
    const int reads = VOLT_READS;

    for (int i = 0; i < reads; i++) {
        total += analogRead(boardConfig.battPin);
        delay(10);
    }

    float avgRaw = (float)total / reads;
    float volts = avgRaw / RAW_VOLTS_CONVERSION;

    // Apply basic smoothing with previous reading
    if (lastVolts > 0.0) {
        volts = (volts * 0.7) + (lastVolts * 0.3); // Exponential smoothing
    }
    lastVolts = volts;

    return volts;
}

int compareVersions(const String& v1, const String& v2) {
    int i = 0, j = 0;
    while (i < v1.length() || j < v2.length()) {
        int num1 = 0, num2 = 0;
        while (i < v1.length() && v1[i] != '.') {
            num1 = num1 * 10 + (v1[i] - '0');
            i++;
        }
        while (j < v2.length() && v2[j] != '.') {
            num2 = num2 * 10 + (v2[j] - '0');
            j++;
        }
        if (num1 > num2)
            return 1;
        if (num1 < num2)
            return -1;
        i++;
        j++;
    }
    return 0;
}