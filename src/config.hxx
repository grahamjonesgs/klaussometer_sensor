// Update with WiFI and MQTT definitions, save as .h file
#ifndef ESP32_CONFIG_H
#define ESP32_CONFIG_H

#include <Arduino.h>

/* WiFi and MQTT Credentials */
#define WIFI_SSID "xxxx"         
#define WIFI_PASSWORD "xxxx"
#define MQTT_SERVER "xxxx"    // server name or IP
#define MQTT_USER "xxxx"      // username
#define MQTT_PASSWORD "xxxx"  // password


/* Topics and debug flags */
#define MQTT_TOPIC_USER "home/"
#define MQTT_TEMP_TOPIC "/tempset-ambient/set"
#define MQTT_HUMID_TOPIC "/tempset-humidity/set"
#define MQTT_DEBUG_TOPIC "/debug"
#define MQTT_BATTERY_TOPIC "/battery/set"

//OTA Update server details
const char* host = "YOUR_SERVER_IP_OR_DOMAIN";
const int port = 80;
const char* bin_path = "/firmware.bin";
const char* version_path = "/version.txt";

// Define the current firmware version
#define FIRMWARE_VERSION "1.0.0"

// Global debug flags (can be overridden per board)
#define DEBUG_SERIAL true
#define DEBUG_MQTT true

/* DHT Sensor Types */
#define DHT11 11
#define DHT22 22

struct BoardConfig {
  const char* macAddress;
  const char* roomName;
  bool isBatteryPowered;
  int dhtPin;
  int dhtType;
  int ledPin; // Kept for consistency, but not used in the main sketch
  int battPin;
  int timeToSleep;
};
// Array of board configurations
const BoardConfig boardConfigs[] = {
  // Example for a mains-powered board in the 'lounge'
  {
    "00:00:00:00:00:00",
    "lounge",
    false,
    23,
    DHT22,
    0, // LED pin set to 0 as it is not used
    0,
    60
  },
  // Example for a battery-powered TTGO board
  {
    "11:11:11:11:11:11",
    "garden",
    true,
    23,
    DHT22,
    0, // LED pin set to 0 as it is not used
    35,
    300
  },
  // Add more board configurations here...
};
// Function to get board configuration based on MAC address
BoardConfig getBoardConfig(String mac) {
  // Find the matching configuration
  for (const auto& config : boardConfigs) {
    if (mac.equals(config.macAddress)) {
      Serial.println("Found matching config for MAC: " + mac);
      return config;
    }
  }

  // Return a default configuration if no match is found
  Serial.println("No matching config found. Using default.");
  return {
    "00:00:00:00:00:00", // Default MAC
    "default",
    true,
    4,
    DHT22,
    0, // LED pin set to 0 as it is not used
    0,
    600
  };
}

#endif // ESP32_CONFIG_H
