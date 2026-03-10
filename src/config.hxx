// Template file — copy to config.h and fill in your credentials and board details.
// Board configurations are defined in config.cpp (copy config.cpp.example as a starting point).
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

// NTP time zone settings
static constexpr long GMT_OFFSET_SEC      = 7200; // UTC+2 (adjust for your time zone)
static constexpr int  DAYLIGHT_OFFSET_SEC = 0;    // Additional DST offset (0 if DST not in use)

// Other constants
static constexpr int WIFI_RETRIES = 5;               // Number of times to retry WiFi before a restart
static constexpr int MQTT_RETRIES = 5;               // Number of times to retry MQTT before a restart
static constexpr int DHT_RETRIES = 5;                // Number of times to retry DHT reads before giving up
static constexpr int DHT_INITIAL_DELAY_MS = 2000;   // Guard delay before first DHT read (ms) — DHT22 minimum is 1 s; 2 s gives outdoor margin
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
static constexpr int   SCD41_INIT_DELAY_MS    = 500;     // Delay after stopPeriodicMeasurement (ms) — datasheet min 500
static constexpr int   SCD41_REINIT_DELAY_MS  = 20;      // Delay after reinit() before next command (ms)

// PMS5003
static constexpr int           PMS5003_READ_TIMEOUT_MS  = 2000;    // Timeout waiting for a PMS5003 frame (ms)
static constexpr unsigned long PMS5003_READ_INTERVAL_MS = 120000UL; // PMS read cycle (ms); independent of main loop
static constexpr unsigned long PMS5003_WARMUP_MS        =  30000UL; // Warm-up after power-on before stable readings (ms)

// JSY-MK-194G Modbus
static constexpr int   JSY_RESPONSE_TIMEOUT_MS = 300;    // Timeout waiting for Modbus response (ms)
static constexpr int   JSY_RESPONSE_BYTES      = 21;     // Expected Modbus response frame length
static constexpr float JSY_VOLTAGE_SCALE       = 100.0f; // Raw register → V   (reg × 0.01)
static constexpr float JSY_CURRENT_SCALE       = 100.0f; // Raw register → A   (reg × 0.01)
static constexpr float JSY_POWER_SCALE         = 10.0f;  // Raw register → W   (reg × 0.1)
static constexpr float JSY_PF_SCALE            = 1000.0f;// Raw register → PF  (reg × 0.001)
static constexpr float JSY_FREQ_SCALE          = 100.0f; // Raw register → Hz  (reg × 0.01)
static constexpr float JSY_ENERGY_SCALE        = 1000.0f;// Raw Wh → kWh

// ESP-NOW
// Read the gateway board's MAC from its serial output on first boot:
//   "Board MAC Address: XX:XX:XX:XX:XX:XX"
// Enter the 6 bytes below in order.
static const uint8_t  ESPNOW_GATEWAY_MAC[6]    = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static constexpr uint32_t ESPNOW_SEND_TIMEOUT_MS  = 2000; // Max ms to wait for delivery ACK
static constexpr int   ESPNOW_OTA_BOOT_INTERVAL  = 6;    // Connect WiFi for OTA every N boots (~1h at 10-min sleep)
static constexpr int   ESPNOW_RETRY_SLEEP_S      = 60;   // Short sleep after a failed DHT read or ESP-NOW send (s)
// WiFi channel is discovered automatically on first boot and cached in RTC memory.
// No manual channel configuration is required.

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
    bool     isBatteryPowered;
    int8_t   dhtDataPin;
    uint8_t  dhtType;
    int8_t   dhtPowerPin;
    int8_t   dhtGndPin;     // GPIO driven LOW as DHT GND (saves a GND pin); 0 = not used
    int8_t   battPin;
    uint16_t timeToSleep;   // in seconds
    SensorType sensors;     // bitmask of SensorType flags
    int8_t   pmsRxPin;      // PMS5003 UART RX pin  (-1 if unused)
    int8_t   pmsTxPin;      // PMS5003 UART TX pin  (-1 if unused)
    int8_t   pmsPowerPin;   // PMS5003 power pin    (-1 if unused)
    int8_t   i2cSdaPin;     // SCD41 I2C SDA pin    (-1 = ESP32 default pin 21)
    int8_t   i2cSclPin;     // SCD41 I2C SCL pin    (-1 = ESP32 default pin 22)
    int8_t   jsyRxPin;      // JSY-MK-194G UART RX  (-1 if unused)
    int8_t   jsyTxPin;      // JSY-MK-194G UART TX  (-1 if unused)
    int8_t   jsyDePin;      // JSY-MK-194G RS485 DE/RE direction pin (-1 if unused)
    // ESP-NOW
    bool     isEspNowGateway; // true = receive ESP-NOW packets from battery nodes and forward to MQTT
    bool     useEspNow;       // true = transmit sensor data via ESP-NOW instead of direct WiFi+MQTT
};

// Board configurations are defined in config.cpp (copy config.cxx and add your boards there)
BoardConfig getBoardConfig(const char* mac);

#endif // ESP32_CONFIG_H
