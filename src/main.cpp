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
#define uS_TO_S_FACTOR 1000000ULL  // Conversion factor for microseconds to seconds
#define WIFI_RETRIES 5             // Number of times to retry WiFi before a restart
#define MQTT_RETRIES 5             // Number of times to retry MQTT before a restart
#define DHT_RETRIES 5              // Number of times to retry DHT reads before giving up
#define VOLT_READS 10              // Number of times to read the voltage for averaging
#define RAW_VOLTS_CONVERSION 620.5 // Mapping raw input back to voltage

// RTC Memory variables (persist during deep sleep)
RTC_DATA_ATTR int bootCount = 0;
RTC_DATA_ATTR int successCount = 0;
RTC_DATA_ATTR float initialVolts = 0.0;

// Global variables
BoardConfig boardConfig;
String macAddress;
String temperature_topic;
String humidity_topic;
String debug_topic;
String battery_topic;
WiFiClient espClient;
MqttClient mqttClient(espClient);
DHT dht(0, 0);                   // Will be re-initialized with correct values in loadBoardConfig()
long lastReadingTime = LONG_MIN; // Initialize to a large negative value
WebServer webServer(80);
float lastTemp = 0.0;
float lastHumid = 0.0;
float lastVolts = 0.0;
String lastReadingTimeStr = "N/A";

// Definitions for NTP time
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 7200;
const int daylightOffset_sec = 0;
struct tm timeinfo;
char timeStringBuff[50];

// Function prototypes
void deep_sleep(int sleepSeconds);
void debug_message(String message, bool perm);
void MQTT_reconnect();
bool setup_wifi();
void loadBoardConfig();
BoardConfig getBoardConfig(String mac);
void setup_OTA_web();
void updateFirmware();
void checkForUpdates();
String getUptime();

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
            // delay(100); // Prevents a tight loop, reducing CPU usage.
            webServer.handleClient(); // Handle OTA updates
        }
    }

    if (!setup_wifi()) {
        if (boardConfig.isBatteryPowered) {
            deep_sleep(boardConfig.timeToSleep);
        } else {
            // For mains-powered, just log an error and wait for the next cycle.
            debug_message("Failed to connect to WiFi, waiting for next cycle...", false);
            lastReadingTime = millis();
            return;
        }
    }

    checkForUpdates();

    // Reconnect MQTT if needed
    if (!mqttClient.connected()) {
        MQTT_reconnect();
    }

    // Get current time from NTP
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    if (!getLocalTime(&timeinfo)) {
        strncpy(timeStringBuff, "Time Error", sizeof(timeStringBuff) - 1);
    } else {
        strftime(timeStringBuff, sizeof(timeStringBuff) - 1, "%d/%m/%y %H:%M:%S", &timeinfo);
    }

    // Read DHT sensor data with retries
    float t, h;
    bool dhtReadSuccess = false;
    for (int i = 0; i < DHT_RETRIES; i++) {
        t = dht.readTemperature();
        h = dht.readHumidity();
        if (!isnan(t) && !isnan(h)) {
            dhtReadSuccess = true;
            break; // Success, exit the retry loop
        }
        delay(200); // Small delay between retries
    }

    if (!dhtReadSuccess) {
        debug_message(String(timeStringBuff) + " DHT read failed after " + String(DHT_RETRIES) + " retries.", true);
        if (boardConfig.isBatteryPowered) {
            deep_sleep(boardConfig.timeToSleep);
        } else {
            lastReadingTime = millis();
            return;
        }
    }

    successCount++;
    // Store the last successful readings
    lastTemp = t;
    lastHumid = h;
    lastReadingTimeStr = String(timeStringBuff);

    // Publish sensor data to MQTT
    String batteryMessage = "";
    if (boardConfig.isBatteryPowered && boardConfig.battPin > 0) {
        float totalHalfVoltageValue = 0.0;
        for (int i = 0; i < VOLT_READS; i++) {
            totalHalfVoltageValue += analogRead(boardConfig.battPin);
            delay(50);
        }
        float halfVoltageValue = totalHalfVoltageValue / VOLT_READS;
        float volts = halfVoltageValue / RAW_VOLTS_CONVERSION;
        lastVolts = volts;
        batteryMessage = " | Bat: " + String(volts, 2) + "V/" + String(initialVolts, 2) + "V";
        mqttClient.beginMessage(battery_topic.c_str());
        mqttClient.printf(String(volts, 2).c_str());
        mqttClient.endMessage();
    }

    String mqttMessage =
        String(timeStringBuff) + " | T: " + String(t, 1) + " | H: " + String(h, 0) + batteryMessage + " | Boot: " + String(bootCount) + " | Success: " + String(successCount);
    debug_message(mqttMessage, true);
    mqttClient.beginMessage(temperature_topic.c_str());
    mqttClient.printf(String(t).c_str());
    mqttClient.endMessage();
    delay(100);
    mqttClient.beginMessage(humidity_topic.c_str());
    mqttClient.printf(String(h).c_str());
    mqttClient.endMessage();

    delay(1000); // Wait for messages to be sent

    // Conditional deep sleep or time-based wait
    if (boardConfig.isBatteryPowered) {
        WiFi.disconnect();
        deep_sleep(boardConfig.timeToSleep);
    } else {
        lastReadingTime = millis();
    }
}

// Load board configuration based on MAC address
void loadBoardConfig() {
    macAddress = WiFi.macAddress();
    debug_message("Board MAC Address: " + macAddress, false);
    boardConfig = getBoardConfig(macAddress);

    // Set up topics based on the retrieved configuration
    temperature_topic = String(MQTT_TOPIC_USER) + String(boardConfig.roomName) + String(MQTT_TEMP_TOPIC);
    humidity_topic = String(MQTT_TOPIC_USER) + String(boardConfig.roomName) + String(MQTT_HUMID_TOPIC);
    debug_topic = String(MQTT_TOPIC_USER) + String(boardConfig.roomName) + String(MQTT_DEBUG_TOPIC);
    battery_topic = String(MQTT_TOPIC_USER) + String(boardConfig.roomName) + String(MQTT_BATTERY_TOPIC);

    // Re-configure DHT with the correct pin from the config
    dht = DHT(boardConfig.dhtPin, boardConfig.dhtType);
}

// Set up WiFi connection
bool setup_wifi() {
    int counter = 1;
    while (WiFi.status() != WL_CONNECTED) {
        debug_message("WiFi is not OK, reconnecting", false);
        if (counter > WIFI_RETRIES) {
            debug_message("WiFi connection failed after " + String(WIFI_RETRIES) + " retries.", false);
            return false;
        }
        WiFi.disconnect();
        WiFi.mode(WIFI_STA);
        WiFi.enableSTA(true);
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        debug_message("Connecting to " + String(WIFI_SSID) + "...", false);
        delay(3000);
        counter++;
        if (WiFi.status() == WL_CONNECTED) {
            debug_message("WiFi is OK => IP address is: " + WiFi.localIP().toString(), false);
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
            debug_message("[Error] MQTT connection failed after " + String(MQTT_RETRIES) + " retries.", false);
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

// Send debug message to MQTT and/or Serial
void debug_message(String message, bool retain) {
    if (DEBUG_MQTT) {
        delay(100);
        // add firmware version to beginning of message and send to debug topic
        message = "V" + String(FIRMWARE_VERSION) + " | " + message;
        mqttClient.beginMessage(debug_topic.c_str(), retain);
        mqttClient.printf(message.c_str());
        mqttClient.endMessage();

        delay(100);
    }
    if (DEBUG_SERIAL) {
        Serial.println(message);
    }
}

// Enter deep sleep for a specified number of seconds
void deep_sleep(int sleepSeconds) {
    esp_sleep_enable_timer_wakeup(sleepSeconds * uS_TO_S_FACTOR);
    debug_message("Sleep for " + String(sleepSeconds) + " Seconds", false);
    WiFi.disconnect();
    delay(1000);
    esp_deep_sleep_start();
}

void setup_OTA_web() {
    webServer.on("/", HTTP_GET, []() {
        String content = "<p class='section-title'>Board Details</p>"
                         "<p><b>Firmware Version:</b> " +
                         String(FIRMWARE_VERSION) +
                         "</p>"
                         "<p><b>MAC Address:</b> " +
                         macAddress +
                         "</p>"
                         "<p><b>Room:</b> " +
                         String(boardConfig.roomName) + 
                         "</p>"
                         "<p><b>Uptime:</b> " +
                         getUptime() +
                         "</p>";

        content += "<p class='section-title'>Last Reading</p>"
                   "<p><b>Time:</b> " +
                   lastReadingTimeStr +
                   "</p>"
                   "<p><b>Temperature:</b> " +
                   String(lastTemp, 1) +
                   " &deg;C</p>"
                   "<p><b>Humidity:</b> " +
                   String(lastHumid, 0) + " %</p>";

        // Conditionally add battery information
        if (boardConfig.isBatteryPowered) {
            content += "<p><b>Battery Voltage:</b> " + String(lastVolts, 2) + " V</p>";
        }

        String html = info_html;
        html.replace("{{content}}", content);
        webServer.send(200, "text/html", html);
    });

    webServer.on("/update", HTTP_GET, []() {
        String htmlPage = ota_html;
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
        String url = "https://" + String(OTA_HOST) + ":" + String("443") + String(OTA_VERSION_PATH);
        http.begin(url.c_str());
        int httpCode = http.GET();
        if (httpCode == HTTP_CODE_OK) {
            String serverVersion = http.getString();
            serverVersion.trim();
            if (serverVersion.compareTo(FIRMWARE_VERSION) > 0) {
                String message = "New firmware version available: " + serverVersion + " (current: " + String(FIRMWARE_VERSION) + ")";
                debug_message(message, true);
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
    String binUrl = "https://" + String(OTA_HOST) + ":" + String("443") + String(OTA_BIN_PATH);
    http.begin(binUrl.c_str());
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
                debug_message("Update failed. Error: " + String(Update.errorString()), true);
            }
        } else {
            debug_message("Not enough space to start OTA update.", true);
        }
    } else {
        debug_message("HTTP GET failed, error: " + String(http.errorToString(httpCode).c_str()), true);
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
  String uptime_str = "";
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
