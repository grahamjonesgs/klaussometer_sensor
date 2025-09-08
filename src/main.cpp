
// Core Arduino and WiFi libraries
#include <Arduino.h>
#include <DHT.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <time.h>

// Custom configuration and lookup table
#include "ESP32_config.h"

// Constants
#define uS_TO_S_FACTOR 1000000ULL        // Conversion factor for microseconds to seconds
#define TIME_TO_SLEEP_FIRST_WIFI_ERROR 5 // Time to sleep on initial WiFI error (seconds)
#define WIFI_RETRIES 5                   // Number of times to retry WiFi before a restart
#define MQTT_RETRIES 2                   // Number of times to retry MQTT before a restart
#define VOLT_READS 10                    // Number of times to read the voltage for averaging
#define RAW_VOLTS_CONVERSION 620.5       // Mapping raw input back to voltage

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
PubSubClient client(espClient);
DHT dht(boardConfig.dhtPin, boardConfig.dhtType);

// Definitions for NTP time
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 7200;
const int daylightOffset_sec = 3600;
struct tm timeinfo;
char timeStringBuff[50];

// Function prototypes
void deep_sleep(int sleepSeconds);
void debug_message(String message, bool perm);
void reconnect();
bool setup_wifi();
void loadBoardConfig();

/**
 * @brief Sets up the board's initial configuration.
 *
 * This function runs only once at boot. It configures the serial port,
 * determines the board's specific settings from the MAC address,
 * and initializes the necessary components like WiFi and DHT.
 */
void setup() {
    Serial.begin(115200);
    while (!Serial) {
        ; // Wait for serial port to connect.
    }

    // Load configuration based on MAC address
    loadBoardConfig();

    // Initialize DHT sensor
    dht.begin();
}

/**
 * @brief Main loop for sensor reading and MQTT publishing.
 *
 * This function contains the main logic. It runs continuously.
 * For battery-powered boards, it will execute the logic once and then
 * enter deep sleep. For mains-powered boards, it will execute the logic
 * and then delay before repeating the loop.
 */
void loop() {
    bootCount++;

    // Connect to WiFi
    if (!setup_wifi()) {
        if (boardConfig.isBatteryPowered) {
            deep_sleep(boardConfig.timeToSleep);
        } else {
            delay(boardConfig.timeToSleep * 1000);
            return;
        }
    }

    // Set MQTT server and reconnect if needed
    client.setServer(MQTT_SERVER, 1883);
    if (!client.connected()) {
        reconnect();
    }

    // Get current time from NTP
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    if (!getLocalTime(&timeinfo)) {
        strcpy(timeStringBuff, "Time Error");
    } else {
        strftime(timeStringBuff, sizeof(timeStringBuff), "%d/%m/%y %H:%M:%S", &timeinfo);
    }

    // Read DHT sensor data
    float t = dht.readTemperature();
    float h = dht.readHumidity();

    if (isnan(t) || isnan(h)) {
        debug_message(String(timeStringBuff) + " DHT Error", true);
        if (boardConfig.isBatteryPowered) {
            deep_sleep(boardConfig.timeToSleep);
        } else {
            delay(boardConfig.timeToSleep * 1000);
            return;
        }
    }

    successCount++;

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
        batteryMessage = " | Bat: " + String(volts, 2) + "V/" + String(initialVolts, 2) + "V";
        client.publish(battery_topic.c_str(), String(volts, 2).c_str(), false);
    }

    String mqttMessage =
        String(timeStringBuff) + " | T: " + String(t, 1) + " | H: " + String(h, 0) + batteryMessage + " | Boot: " + String(bootCount) + " | Success: " + String(successCount);
    debug_message(mqttMessage, true);
    client.publish(temperature_topic.c_str(), String(t).c_str(), false);
    delay(100);
    client.publish(humidity_topic.c_str(), String(h).c_str(), false);

    delay(1000); // Wait for messages to be sent

    // Conditional deep sleep
    if (boardConfig.isBatteryPowered) {
        WiFi.disconnect();
        deep_sleep(boardConfig.timeToSleep * 1000);
    } else {
        delay(boardConfig.timeToSleep * 1000);
    }
}

// Helper Functions
void loadBoardConfig() {
    macAddress = WiFi.macAddress();
    Serial.print("Board MAC Address: ");
    Serial.println(macAddress);
    boardConfig = getBoardConfig(macAddress);

    // Set up topics based on the retrieved configuration
    temperature_topic = String(MQTT_TOPIC_USER) + String(boardConfig.roomName) + String(MQTT_TEMP_TOPIC);
    humidity_topic = String(MQTT_TOPIC_USER) + String(boardConfig.roomName) + String(MQTT_HUMID_TOPIC);
    debug_topic = String(MQTT_TOPIC_USER) + String(boardConfig.roomName) + String(MQTT_DEBUG_TOPIC);
    battery_topic = String(MQTT_TOPIC_USER) + String(boardConfig.roomName) + String(MQTT_BATTERY_TOPIC);

    // Re-configure DHT with the correct pin from the config
    dht = DHT(boardConfig.dhtPin, boardConfig.dhtType);
}

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
    }
    debug_message("WiFi is OK => ESP32 IP address is: " + WiFi.localIP().toString(), false);
    return true;
}

void reconnect() {
    int counter = 1;
    while (!client.connected()) {
        if (counter > MQTT_RETRIES) {
            debug_message("[Error] MQTT connection failed after " + String(MQTT_RETRIES) + " retries.", false);
            deep_sleep(boardConfig.timeToSleep);
        }
        debug_message("Connecting to MQTT broker...", false);
        if (client.connect("ESP32Client", MQTT_USER, MQTT_PASSWORD)) {
            debug_message("MQTT link OK", false);
        } else {
            debug_message("[Error] MQTT not connected: " + String(client.state()), false);
            delay(3000);
        }
        counter++;
    }
}

void debug_message(String message, bool perm) {
    if (DEBUG_MQTT) {
        client.publish(debug_topic.c_str(), message.c_str(), perm);
        delay(100);
    }
    if (DEBUG_SERIAL) {
        Serial.println(message);
    }
}

void deep_sleep(int sleepSeconds) {
    esp_sleep_enable_timer_wakeup(sleepSeconds * uS_TO_S_FACTOR);
    debug_message("Setup ESP32 to sleep for " + String(sleepSeconds) + " Seconds", false);
    WiFi.disconnect();
    esp_deep_sleep_start();
}
