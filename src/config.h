// Update with WiFI and MQTT definitions, save as .h file
#ifndef ESP32_CONFIG_H
#define ESP32_CONFIG_H

#include <Arduino.h>
#include <DHT.h>

/* WiFi and MQTT Credentials */
const char* WIFI_SSID = "klaussjones";
const char* WIFI_PASSWORD = "magdeburg1986";
const char* MQTT_SERVER = "watsonia22.com"; // server name or IP
const char* MQTT_USER = "reporter";         // username
const char* MQTT_PASSWORD = "report";       // password

/* Topics and debug flags */
const char* MQTT_TOPIC_USER = "";
const char* MQTT_TEMP_TOPIC = "/tempset-ambient/set";
const char* MQTT_HUMID_TOPIC = "/tempset-humidity/set";
const char* MQTT_DEBUG_TOPIC = "/debug";
const char* MQTT_BATTERY_TOPIC = "/battery/set";

// OTA Update server details
const char* OTA_HOST = "watsonia22.com";
const int OTA_PORT = 80;
const char* OTA_BIN_PATH = "/sensor/firmware.bin";
const char* OTA_VERSION_PATH = "/sensor/version.txt";

// Define the current firmware version
#define FIRMWARE_VERSION "1.1.2"

// Global debug flags (can be overridden per board)
const bool DEBUG_SERIAL = true;
const bool DEBUG_MQTT = true;

struct BoardConfig {
    const char* macAddress;
    const char* roomName;
    bool isBatteryPowered;
    int dhtPin;
    int dhtType;
    int ledPin;
    int battPin;
    int timeToSleep; // in seconds
};
// Array of board configurations
const BoardConfig boardConfigs[] = {
    // Example for a mains-powered board in the 'lounge'
    {
        "30:C6:F7:44:0D:58", // Mac address of the board
        "cave",              // Room name
        false,               // Battery powered
        23,                  // DHT pin
        DHT22,               // DHT type
        2,                   // LED pin
        0,                   // Battery pin (not used for mains powered)
        30                   // Time to sleep in seconds
    },
    {
        "30:C6:F7:43:FE:B0", // Mac address of the board
        "bedroom",           // Room name
        false,               // Battery powered
        23,                  // DHT pin
        DHT22,               // DHT type
        2,                   // LED pin
        0,                   // Battery pin (not used for mains powered)
        30                   // Time to sleep in seconds
    },
    {
        "24:6F:28:A1:96:E4", // Mac address of the board
        "livingroom",        // Room name
        false,               // Battery powered
        23,                  // DHT pin
        DHT22,               // DHT type
        2,                   // LED pin
        0,                   // Battery pin (not used for mains powered)
        30                   // Time to sleep in seconds
    },
    {
        "24:6F:28:9D:A8:F0", // Mac address of the board
        "guest",             // Room name
        false,               // Battery powered
        23,                  // DHT pin
        DHT22,               // DHT type
        2,                   // LED pin
        0,                   // Battery pin (not used for mains powered)
        30                   // Time to sleep in seconds
    },
    {
        "24:0A:C4:25:91:08", // Mac address of the board
        "outside",           // Room name
        true,                // Battery powered
        23,                  // DHT pin
        DHT22,               // DHT type
        2,                   // LED pin
        35,                  // Battery pin (not used for mains powered)
        600                  // Time to sleep in seconds
    },
};

// Function to get board configuration based on MAC address
BoardConfig getBoardConfig(char* mac) {
    // Find the matching configuration
    for (const auto& config : boardConfigs) {
        if (strcmp(mac, config.macAddress) == 0) {
            Serial.println("Found matching config for MAC: " + String(mac) + " for room: " + String(config.roomName));
            return config;
        }
    }

    // Return a default configuration if no match is found
    Serial.println("No matching config found. Using default.");
    return {"00:00:00:00:00:00", // Default MAC
            "default",           false, 4, DHT22, 0, 0, 30};
}

#endif // ESP32_CONFIG_H
