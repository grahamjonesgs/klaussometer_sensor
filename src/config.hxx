// Update with WiFI and MQTT definitions, save as config.h
#ifndef ESP32_CONFIG_H
#define ESP32_CONFIG_H

#include <Arduino.h>
#include <DHT.h>

/* WiFi and MQTT Credentials */
static const char* const WIFI_SSID = "xxxx";
static const char* const WIFI_PASSWORD = "xxxx";
static const char* const MQTT_SERVER = "xxxx"; // server name or IP
static const char* const MQTT_USER = "xxxx";   // username
static const char* const MQTT_PASSWORD = "xxxx"; // password
static constexpr int MQTT_PORT = 1883;

/* Topics and debug flags */
static const char* const MQTT_TOPIC_USER = "home/";
static const char* const MQTT_TEMP_TOPIC    = "/tempset-ambient/set";
static const char* const MQTT_HUMID_TOPIC   = "/tempset-humidity/set";
static const char* const MQTT_DEBUG_TOPIC   = "/debug";
static const char* const MQTT_BATTERY_TOPIC = "/battery/set";
static const char* const MQTT_CO2_TOPIC     = "/co2/set";
static const char* const MQTT_PM1_TOPIC     = "/pm1/set";
static const char* const MQTT_PM25_TOPIC    = "/pm25/set";
static const char* const MQTT_PM10_TOPIC    = "/pm10/set";
static const char* const MQTT_JSY_VOLTAGE_TOPIC  = "/ac-voltage/set";
static const char* const MQTT_JSY_CURRENT_TOPIC  = "/ac-current/set";
static const char* const MQTT_JSY_POWER_TOPIC    = "/ac-power/set";
static const char* const MQTT_JSY_PF_TOPIC       = "/ac-pf/set";
static const char* const MQTT_JSY_FREQ_TOPIC     = "/ac-freq/set";
static const char* const MQTT_JSY_ENERGY_TOPIC   = "/ac-energy/set";

// OTA Update server details
static const char* const OTA_HOST = "YOUR_SERVER_IP_OR_DOMAIN";
static constexpr int OTA_PORT = 443;
static const char* const OTA_BIN_PATH = "/sensor/firmware.bin";
static const char* const OTA_VERSION_PATH = "/sensor/version.txt";

// Other constants
static constexpr int WIFI_RETRIES = 5;               // Number of times to retry WiFi before a restart
static constexpr int MQTT_RETRIES = 5;               // Number of times to retry MQTT before a restart
static constexpr int DHT_RETRIES = 5;                // Number of times to retry DHT reads before giving up
static constexpr int VOLT_READS = 10;                // Number of times to read the voltage for averaging
static constexpr float RAW_VOLTS_CONVERSION = 620.5; // Mapping raw input back to voltage 4095 / 3.3 * voltage divider factor (2)
static constexpr int WEB_SERVER_POLL_INTERVAL_MS = 100; // Interval in ms to poll the web server for OTA updates

static const char* const FIRMWARE_VERSION = "1.0.0";

// Global debug flags (can be overridden per board)
static constexpr bool DEBUG_SERIAL = true;
static constexpr bool DEBUG_MQTT = true;

// Bitmask flags for which sensors are present on a board
enum SensorType : uint32_t {
    SENSOR_DHT     = (1 << 0), // DHT11/DHT22 temperature + humidity
    SENSOR_PMS5003 = (1 << 1), // Particulate matter (PM1.0, PM2.5, PM10) via UART
    SENSOR_SCD41   = (1 << 2), // CO2 + temperature + humidity via I2C
    SENSOR_JSY194G = (1 << 3), // JSY-MK-194G AC power meter via Modbus RTU/UART
};

// Allow combining SensorType flags with | in board config initialisers
inline SensorType operator|(SensorType a, SensorType b) {
    return static_cast<SensorType>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

struct BoardConfig {
    const char* macAddress;
    const char* roomName;
    const char* displayName;
    bool isBatteryPowered;
    int dhtDataPin;
    int dhtType;
    int dhtPowerPin;
    int ledPin;
    int battPin;
    int timeToSleep;  // in seconds
    SensorType sensors; // bitmask of SensorType flags
    int pmsRxPin;     // PMS5003 UART RX pin  (-1 if unused)
    int pmsTxPin;     // PMS5003 UART TX pin  (-1 if unused)
    int pmsPowerPin;  // PMS5003 power pin    (-1 if unused)
    int i2cSdaPin;    // SCD41 I2C SDA pin    (-1 = ESP32 default pin 21)
    int i2cSclPin;    // SCD41 I2C SCL pin    (-1 = ESP32 default pin 22)
    int jsyRxPin;     // JSY-MK-194G UART RX  (-1 if unused)
    int jsyTxPin;     // JSY-MK-194G UART TX  (-1 if unused)
    int jsyDePin;     // JSY-MK-194G RS485 DE/RE direction pin (-1 if unused)
};

// Array of board configurations
const BoardConfig boardConfigs[] = {
    // Example: mains-powered board with DHT only
    {
        "00:00:00:00:00:00", // MAC address
        "lounge",            // Room name (used in MQTT topic)
        "Lounge",            // Display name (shown in web UI)
        false,               // Battery powered
        23,                  // DHT data pin
        DHT22,               // DHT type (DHT11 or DHT22)
        0,                   // DHT power pin (0 = not used)
        2,                   // LED pin
        0,                   // Battery ADC pin (0 = not used)
        30,                  // Time to sleep / poll interval (seconds)
        SENSOR_DHT,          // Sensors present
        -1, -1, -1,          // PMS5003 pins (unused)
        -1, -1,              // SCD41 I2C pins (unused)
        -1, -1, -1           // JSY-MK-194G pins (unused)
    },
    // Example: battery-powered board with DHT only
    {
        "11:11:11:11:11:11",
        "garden",
        "Garden",
        true,
        23,
        DHT22,
        18,                  // DHT power pin (used to cut power between reads)
        2,
        35,                  // Battery ADC pin
        600,
        SENSOR_DHT,
        -1, -1, -1,
        -1, -1,
        -1, -1, -1
    },
    // Example: mains-powered board with DHT + JSY-MK-194G power meter
    // JSY wired to Serial1: RX=16, TX=17, RS485 DE/RE=4
    {
        "22:22:22:22:22:22",
        "utility",
        "Utility Room",
        false,
        23,
        DHT22,
        0,
        2,
        0,
        30,
        SENSOR_DHT | SENSOR_JSY194G,
        -1, -1, -1,          // PMS5003 pins (unused)
        -1, -1,              // SCD41 I2C pins (unused)
        16, 17, 4            // JSY-MK-194G: RX, TX, DE/RE pin
    },
    // Add more board configurations here...
};

// Function to get board configuration based on MAC address
inline BoardConfig getBoardConfig(const char* mac) {
    for (const auto& config : boardConfigs) {
        if (strcmp(mac, config.macAddress) == 0) {
            Serial.println("Found matching config for MAC: " + String(mac) + " for room: " + String(config.roomName));
            return config;
        }
    }

    Serial.println("No matching config found. Using default.");
    return {
        "00:00:00:00:00:00", "default", "Default",
        false, 4, DHT22, 0, 0, 0, 30,
        SENSOR_DHT,
        -1, -1, -1,
        -1, -1,
        -1, -1, -1
    };
}

#endif // ESP32_CONFIG_H
