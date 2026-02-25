// Template file — copy to config.h and fill in your credentials and board details.
// Board configurations are defined in config.cpp (copy config.cxx as a starting point).
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
static const char* const MQTT_JSY_VOLTAGE_TOPIC       = "/ac-voltage/set";
static const char* const MQTT_JSY_CURRENT_TOPIC       = "/ac-current/set";
static const char* const MQTT_JSY_POWER_TOPIC         = "/ac-power/set";
static const char* const MQTT_JSY_PF_TOPIC            = "/ac-pf/set";
static const char* const MQTT_JSY_FREQ_TOPIC          = "/ac-freq/set";
static const char* const MQTT_JSY_ENERGY_TOPIC        = "/ac-energy/set";
static const char* const MQTT_JSY_DAILY_ENERGY_TOPIC  = "/ac-energy-daily/set";

// OTA Update server details
static const char* const OTA_HOST = "YOUR_SERVER_IP_OR_DOMAIN";
static constexpr int OTA_PORT = 443;
static const char* const OTA_BIN_PATH = "/sensor/firmware.bin";
static const char* const OTA_VERSION_PATH = "/sensor/version.txt";
static constexpr unsigned long OTA_CHECK_INTERVAL_MS = 300000UL;  // Re-check OTA every 5 minutes (mains boards)

// Other constants
static constexpr int WIFI_RETRIES = 5;               // Number of times to retry WiFi before a restart
static constexpr int MQTT_RETRIES = 5;               // Number of times to retry MQTT before a restart
static constexpr int DHT_RETRIES = 5;                // Number of times to retry DHT reads before giving up
static constexpr int DHT_INITIAL_DELAY_MS = 1000;   // Guard delay before first DHT read (ms)
static constexpr int DHT_RETRY_DELAY_MS = 2000;     // Delay between DHT retries — DHT22 needs >=2 s between reads
static constexpr int VOLT_READS = 10;                // Number of times to read the voltage for averaging
static constexpr float RAW_VOLTS_CONVERSION = 620.5; // Mapping raw input back to voltage 4095 / 3.3 * voltage divider factor (2)
static constexpr int WEB_SERVER_POLL_INTERVAL_MS = 100; // Interval in ms to poll the web server for OTA updates

// Battery ADC
static constexpr int   ADC_BIT_WIDTH          = 12;      // 12-bit ADC resolution
static constexpr int   ADC_SETTLE_DELAY_MS    = 10;      // Settle time between ADC reads (ms)
static constexpr float VOLT_SMOOTH_NEW        = 0.7f;    // Exponential smoothing weight for new reading
static constexpr float VOLT_SMOOTH_PREV       = 0.3f;    // Exponential smoothing weight for previous reading

// SCD41
static constexpr int   SCD41_DEFAULT_SDA_PIN  = 21;      // ESP32 default I2C SDA pin
static constexpr int   SCD41_DEFAULT_SCL_PIN  = 22;      // ESP32 default I2C SCL pin
static constexpr int   SCD41_INIT_DELAY_MS    = 500;     // Delay after stopPeriodicMeasurement before start (ms)

// PMS5003
static constexpr int   PMS5003_READ_TIMEOUT_MS = 2000;   // Timeout waiting for a PMS5003 frame (ms)

// JSY-MK-194G Modbus
static constexpr int   JSY_RESPONSE_TIMEOUT_MS = 300;    // Timeout waiting for Modbus response (ms)
static constexpr int   JSY_RESPONSE_BYTES      = 21;     // Expected Modbus response frame length
static constexpr float JSY_VOLTAGE_SCALE       = 100.0f; // Raw register → V   (reg × 0.01)
static constexpr float JSY_CURRENT_SCALE       = 100.0f; // Raw register → A   (reg × 0.01)
static constexpr float JSY_POWER_SCALE         = 10.0f;  // Raw register → W   (reg × 0.1)
static constexpr float JSY_PF_SCALE            = 1000.0f;// Raw register → PF  (reg × 0.001)
static constexpr float JSY_FREQ_SCALE          = 100.0f; // Raw register → Hz  (reg × 0.01)
static constexpr float JSY_ENERGY_SCALE        = 1000.0f;// Raw Wh → kWh

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

// Board configurations are defined in config.cpp (copy config.cxx and add your boards there)
BoardConfig getBoardConfig(const char* mac);

#endif // ESP32_CONFIG_H
