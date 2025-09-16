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
#include <time.h>

// Constants
#define MICROSECONDS_IN_SECOND 1000000ULL // Conversion factor for microseconds to seconds
#define WIFI_RETRIES 5                    // Number of times to retry WiFi before a restart
#define MQTT_RETRIES 5                    // Number of times to retry MQTT before a restart
#define DHT_RETRIES 5                     // Number of times to retry DHT reads before giving up
#define VOLT_READS 10                     // Number of times to read the voltage for averaging
#define RAW_VOLTS_CONVERSION 620.5        // Mapping raw input back to voltage

// RTC Memory variables (persist during deep sleep)
RTC_DATA_ATTR int bootCount = 0;
RTC_DATA_ATTR int successCount = 0;

struct SensorData {
    float temperature;
    float humidity;
    bool success;
};

// Global variables
BoardConfig boardConfig;
char macAddress[18];
char temperature_topic[50];
char humidity_topic[50];
char debug_topic[50];
char battery_topic[50];
WiFiClient espClient;
MqttClient mqttClient(espClient);
DHT dht(0, 0);                   // Will be re-initialized with correct values in loadBoardConfig()
long lastReadingTime = LONG_MIN; // Initialize to a large negative value
WebServer webServer(80);
float lastTemp = 0.0;
float lastHumid = 0.0;
float lastVolts = 0.0;
char lastReadingTimeStr[20] = "N/A";
char debugMessage[256];
char batteryMessage[256] = "";

// Definitions for NTP time
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 7200;
const int daylightOffset_sec = 0;
struct tm timeinfo;
char timeBuffer[50];

// Function prototypes
void deep_sleep(int sleepSeconds);
void debug_message(const char* message, bool perm);
void MQTT_reconnect();
bool setup_wifi();
void loadBoardConfig();
BoardConfig getBoardConfig(char* mac);
void setup_OTA_web();
void updateFirmware();
void checkForUpdates();
String getUptime();
SensorData read_dht_sensor();
float read_battery_voltage();
void mqttSendFloat(const char* topic, float value);

void setup() {
    bootCount++;
    Serial.begin(115200);
    while (!Serial) {
        ; // Wait for serial port to connect.
    }
    // Load configuration based on MAC address
    loadBoardConfig();
    // Initialize DHT sensor
    dht.begin();
    mqttClient.setUsernamePassword(MQTT_USER, MQTT_PASSWORD);
}

void loop() {
    // For mains-powered devices, wait until the next reading time.
    if (!boardConfig.isBatteryPowered) {
        while (millis() - lastReadingTime < (boardConfig.timeToSleep * 1000)) {
            webServer.handleClient(); // Handle OTA updates
        }
    }

    if (!setup_wifi()) {
        if (boardConfig.isBatteryPowered) {
            deep_sleep(boardConfig.timeToSleep);
        } else {
            debug_message("Failed to connect to WiFi, waiting for next cycle...", false);
            lastReadingTime = millis();
            return;
        }
    }

    checkForUpdates();
    if (!mqttClient.connected()) {
        MQTT_reconnect();
    }

    // Get current time from NTP
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    if (!getLocalTime(&timeinfo)) {
        strncpy(timeBuffer, "Time Error", sizeof(timeBuffer) - 1);
    } else {
        strftime(timeBuffer, sizeof(timeBuffer) - 1, "%d/%m/%y %H:%M:%S", &timeinfo);
    }

    // Read DHT sensor data with retries
    SensorData reading = read_dht_sensor();

    if (!reading.success) {
        snprintf(debugMessage, sizeof(debugMessage), "%s DHT read failed after %d retries.", timeBuffer, DHT_RETRIES);
        debug_message(debugMessage, true);
        if (boardConfig.isBatteryPowered) {
            deep_sleep(boardConfig.timeToSleep);
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

    lastVolts = 0.0; // Initialize lastVolts
    batteryMessage[0] = '\0'; // Clear the battery message buffer

    if (boardConfig.isBatteryPowered && boardConfig.battPin > 0) {
        lastVolts = read_battery_voltage();
        snprintf(batteryMessage, sizeof(batteryMessage), " | Bat: %.2fV", lastVolts);
        mqttSendFloat(battery_topic, lastVolts);
    }

    char mqttMessage[256];
    snprintf(mqttMessage, sizeof(mqttMessage), "%s | T: %.1f | H: %.0f%s | Boot: %d | Success: %d", timeBuffer, reading.temperature, reading.humidity, batteryMessage, bootCount, successCount);
    debug_message(mqttMessage, true);
    mqttSendFloat(temperature_topic, reading.temperature);
    mqttSendFloat(humidity_topic, reading.humidity);

    // Conditional deep sleep or time-based wait
    if (boardConfig.isBatteryPowered) {
         delay(1000); // Wait for messages to be sent
        WiFi.disconnect();
        deep_sleep(boardConfig.timeToSleep);
    } else {
        lastReadingTime = millis();
    }
}

// Load board configuration based on MAC address
void loadBoardConfig() {
    strncpy(macAddress, WiFi.macAddress().c_str(), sizeof(macAddress) - 1);
    macAddress[sizeof(macAddress) - 1] = '\0';
    snprintf(debugMessage, sizeof(debugMessage), "Board MAC Address: %s", macAddress);
    debug_message(debugMessage, false);
    boardConfig = getBoardConfig(macAddress);

    // Set up topics based on the retrieved configuration
    snprintf(temperature_topic, sizeof(temperature_topic), "%s%s%s", MQTT_TOPIC_USER, boardConfig.roomName, MQTT_TEMP_TOPIC);
    snprintf(humidity_topic, sizeof(humidity_topic), "%s%s%s", MQTT_TOPIC_USER, boardConfig.roomName, MQTT_HUMID_TOPIC);
    snprintf(debug_topic, sizeof(debug_topic), "%s%s%s", MQTT_TOPIC_USER, boardConfig.roomName, MQTT_DEBUG_TOPIC);
    snprintf(battery_topic, sizeof(battery_topic), "%s%s%s", MQTT_TOPIC_USER, boardConfig.roomName, MQTT_BATTERY_TOPIC);

    // Re-configure DHT with the correct pin from the config
    dht = DHT(boardConfig.dhtPin, boardConfig.dhtType);
}

// Set up WiFi connection
bool setup_wifi() {
    int counter = 1;
    while (WiFi.status() != WL_CONNECTED) {
        debug_message("WiFi is not OK, reconnecting", false);
        if (counter > WIFI_RETRIES) {
            snprintf(debugMessage, sizeof(debugMessage), "WiFi connection failed after %d retries.", WIFI_RETRIES);
            debug_message(debugMessage, false);
            return false;
        }
        WiFi.disconnect();
        WiFi.mode(WIFI_STA);
        WiFi.enableSTA(true);
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        snprintf(debugMessage, sizeof(debugMessage), "Attempt %d to connect to WiFi...", counter);
        debug_message(debugMessage, false);
        delay(3000);
        counter++;
        if (WiFi.status() == WL_CONNECTED) {
            IPAddress ip = WiFi.localIP();
            char ipStr[16];
            snprintf(ipStr, sizeof(ipStr), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
            snprintf(debugMessage, sizeof(debugMessage), "WiFi is OK => IP address is: %s", ipStr);
            debug_message(debugMessage, false);
            setup_OTA_web();
        }
    }

    return true;
}

// Reconnect to MQTT broker
void MQTT_reconnect() {
    int counter = 1;
    while (!mqttClient.connected()) {
        if (counter > MQTT_RETRIES) {
            snprintf(debugMessage, sizeof(debugMessage), "[Error] MQTT connection failed after %d retries.", MQTT_RETRIES);
            debug_message(debugMessage, false);
            // This is a major error; on battery, we deep sleep. For mains, we continue the loop.
            if (boardConfig.isBatteryPowered) {
                deep_sleep(boardConfig.timeToSleep);
            } else {
                ESP.restart();
            }
        }
        debug_message("Connecting to MQTT broker...", false);
        if (mqttClient.connect(MQTT_SERVER, 1883)) {
            debug_message("MQTT link OK", false);
        } else {
            debug_message("[Error] MQTT not connected: ", false);
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
void debug_message(const char* message, bool retain) {
    char fullMessageBuffer[256];
    if (DEBUG_MQTT) {
        delay(100);
        // add firmware version to beginning of message and send to debug topic
        snprintf(fullMessageBuffer, sizeof(fullMessageBuffer), "V%s | %s", FIRMWARE_VERSION, message);
        mqttClient.beginMessage(debug_topic, retain);
        mqttClient.printf("%s", fullMessageBuffer);
        mqttClient.endMessage();

        delay(100);
    }
    if (DEBUG_SERIAL) {
        Serial.println(fullMessageBuffer);
    }
}

// Enter deep sleep for a specified number of seconds
void deep_sleep(int sleepSeconds) {
    esp_sleep_enable_timer_wakeup(sleepSeconds * MICROSECONDS_IN_SECOND);
    snprintf(debugMessage, sizeof(debugMessage), "Entering deep sleep for %d seconds...", sleepSeconds);
    debug_message(debugMessage, false);
    WiFi.disconnect();
    delay(1000);
    esp_deep_sleep_start();
}

void setup_OTA_web() {
    webServer.on("/", HTTP_GET, []() {
        String content;
        content.reserve(1024);
        content = "<p class='section-title'>Device Information</p>"
                  "<p><b>Firmware Version:</b> " +
                  String(FIRMWARE_VERSION) +
                  "</p>"
                  "<p><b>MAC Address:</b> " +
                  macAddress +
                  "</p>"
                  "<p><b>Room:</b> " +
                  String(boardConfig.roomName) +
                  "</p>"
                  "<p><b>Uptime:</b> <span id=\"uptime\">N/A</span></p>";

        if (strncmp(lastReadingTimeStr, "N/A", 3) != 0) {
            content += "<p class='section-title'>Current Readings</p>"
                       "<p><b>Last Update Time:</b> <span id=\"time\">N/A</span></p>"
                       "<p><b>Temperature:</b> <span id=\"temp\">N/A</span> &deg;C</p>"
                       "<p><b>Humidity:</b> <span id=\"humid\">N/A</span> %</p>";
        } else {
            content += "<p class='section-title'>Current Readings</p>"
                       "<p><b>Last Update Time:</b> <span id=\"time\">N/A</span></p>"
                       "<p><b>Temperature:</b> <span id=\"temp\">N/A</span></p>"
                       "<p><b>Humidity:</b> <span id=\"humid\">N/A</span></p>";
        }
        if (boardConfig.isBatteryPowered) {
            content += "<p><b>Battery Voltage:</b> <span id=\"voltage\">N/A</span> V</p>";
        }

        String html;
        html.reserve(1024);
        html = info_html;
        html.replace("{{content}}", content);
        webServer.send(200, "text/html", html);
    });

    webServer.on("/data", HTTP_GET, []() {
        char dataBuffer[256];
        snprintf(dataBuffer, sizeof(dataBuffer), "{\"temperature\":%.1f, \"humidity\":%.0f, \"voltage\":%.2f, \"time\":\"%s\", \"uptime\":\"%s\"}", lastTemp, lastHumid, lastVolts,
                 lastReadingTimeStr, getUptime().c_str());
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
                    debug_message("Update Success, rebooting...\n", true);
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
        snprintf(binUrl, sizeof(binUrl), "https://%s:%d%s", OTA_HOST, 443, OTA_VERSION_PATH);
        http.begin(binUrl);
        int httpCode = http.GET();
        if (httpCode == HTTP_CODE_OK) {
            String serverVersion = http.getString();
            serverVersion.trim();
            if (serverVersion.compareTo(FIRMWARE_VERSION) > 0) {
                snprintf(debugMessage, sizeof(debugMessage), "New firmware version available: %s (current: %s)", serverVersion.c_str(), FIRMWARE_VERSION);
                debug_message(debugMessage, true);
                // Start the update
                updateFirmware();
            }
        } else {
            debug_message("Error fetching version file.", true);
        }
        http.end();
    } else {
        debug_message("WiFi not connected. Cannot check for updates.", false);
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
            debug_message("Beginning OTA update. This may take a few moments...", true);
            WiFiClient& client = http.getStream();
            size_t written = Update.writeStream(client);
            if (written == contentLength) {
                debug_message("OTA update written successfully.", true);
            } else {
                debug_message("OTA update failed to write completely.", true);
            }
            if (Update.end()) {
                debug_message("Update finished successfully. Restarting...", true);
                ESP.restart();
            } else {
                snprintf(debugMessage, sizeof(debugMessage), "Error during OTA update: %s", Update.errorString());
                debug_message(debugMessage, true);
            }
        } else {
            debug_message("Not enough space to start OTA update.", true);
        }
    } else {
        snprintf(debugMessage, sizeof(debugMessage), "HTTP GET failed, error: %d", httpCode);
        debug_message(debugMessage, true);
    }
    http.end();
}

String getUptime() {
    unsigned long uptime_ms = millis();
    unsigned long seconds = uptime_ms / 1000;

    unsigned long days = seconds / (24 * 3600);
    unsigned long hours = (seconds % (24 * 3600)) / 3600;
    unsigned long minutes = (seconds % 3600) / 60;
    unsigned long remaining_seconds = seconds % 60;

    // Create the formatted string
    String uptime_str;
    uptime_str.reserve(64);
    uptime_str = "";
    uptime_str += days;
    uptime_str += " days, ";
    uptime_str += hours;
    uptime_str += " hours, ";
    uptime_str += minutes;
    uptime_str += " minutes, ";
    uptime_str += remaining_seconds;
    uptime_str += " seconds";

    return uptime_str;
}

SensorData read_dht_sensor() {
    SensorData data;
    data.temperature = NAN;
    data.humidity = NAN;
    data.success = false;

    for (int i = 0; i < DHT_RETRIES; i++) {
        data.temperature = dht.readTemperature();
        data.humidity = dht.readHumidity();
        if (!isnan(data.temperature) && !isnan(data.humidity)) {
            data.success = true;
            break; // Success, exit the retry loop
        }
        delay(200); // Small delay between retries
    }
    return data;
}

float read_battery_voltage() {
    if (!boardConfig.isBatteryPowered || boardConfig.battPin <= 0) {
        return 0.0; // Return 0 if not battery powered or no pin is set
    }

    float totalHalfVoltageValue = 0.0;
    for (int i = 0; i < VOLT_READS; i++) {
        totalHalfVoltageValue += analogRead(boardConfig.battPin);
        delay(50);
    }
    float halfVoltageValue = totalHalfVoltageValue / VOLT_READS;
    float volts = halfVoltageValue / RAW_VOLTS_CONVERSION;

    return volts;
}