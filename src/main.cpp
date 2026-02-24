#include "globals.h"
#include "network.h"
#include "ota.h"
#include "sensors.h"
#include <WiFi.h>

// Global definitions (extern-declared in globals.h)
RTC_DATA_ATTR int   bootCount    = 0;
RTC_DATA_ATTR int   successCount = 0;
RTC_DATA_ATTR float lastVolts    = 0.0f;

BoardConfig boardConfig;
char macAddress[18];

char temperatureTopic[TOPIC_BUF_LEN];
char humidityTopic[TOPIC_BUF_LEN];
char debugTopic[TOPIC_BUF_LEN];
char batteryTopic[TOPIC_BUF_LEN];
char co2Topic[TOPIC_BUF_LEN];
char pm1Topic[TOPIC_BUF_LEN];
char pm25Topic[TOPIC_BUF_LEN];
char pm10Topic[TOPIC_BUF_LEN];
char jsyVoltageTopic[TOPIC_BUF_LEN];
char jsyCurrentTopic[TOPIC_BUF_LEN];
char jsyPowerTopic[TOPIC_BUF_LEN];
char jsyPfTopic[TOPIC_BUF_LEN];
char jsyFreqTopic[TOPIC_BUF_LEN];
char jsyEnergyTopic[TOPIC_BUF_LEN];

WiFiClient espClient;
MqttClient mqttClient(espClient);
WebServer  webServer(80);
DHT        dht(0, 0); // Re-initialised in loadBoardConfig()

unsigned long lastReadingTime = 0;
float         lastTemp        = 0.0f;
float         lastHumid       = 0.0f;
char          lastReadingTimeStr[50] = "N/A";
char          debugBuf[256];
char          batteryMessage[256] = "";

// NTP settings (used only in loop)
static const char* const ntpServer        = "pool.ntp.org";
static constexpr long    gmtOffsetSec     = 7200;
static constexpr int     daylightOffsetSec = 0;

// ---------------------------------------------------------------------------

void loadBoardConfig() {
    strncpy(macAddress, WiFi.macAddress().c_str(), sizeof(macAddress) - 1);
    macAddress[sizeof(macAddress) - 1] = '\0';
    snprintf(debugBuf, sizeof(debugBuf), "Board MAC Address: %s", macAddress);
    Serial.println(debugBuf); // debugMessage not yet usable (no MQTT topic set)
    boardConfig = getBoardConfig(macAddress);

    // Always-present topics
    snprintf(temperatureTopic, sizeof(temperatureTopic), "%s%s%s", MQTT_TOPIC_USER, boardConfig.roomName, MQTT_TEMP_TOPIC);
    snprintf(humidityTopic,    sizeof(humidityTopic),    "%s%s%s", MQTT_TOPIC_USER, boardConfig.roomName, MQTT_HUMID_TOPIC);
    snprintf(debugTopic,       sizeof(debugTopic),       "%s%s%s", MQTT_TOPIC_USER, boardConfig.roomName, MQTT_DEBUG_TOPIC);
    snprintf(batteryTopic,     sizeof(batteryTopic),     "%s%s%s", MQTT_TOPIC_USER, boardConfig.roomName, MQTT_BATTERY_TOPIC);

    // Sensor-specific topics
    if (boardConfig.sensors & SENSOR_SCD41) {
        snprintf(co2Topic, sizeof(co2Topic), "%s%s%s", MQTT_TOPIC_USER, boardConfig.roomName, MQTT_CO2_TOPIC);
    }
    if (boardConfig.sensors & SENSOR_PMS5003) {
        snprintf(pm1Topic,  sizeof(pm1Topic),  "%s%s%s", MQTT_TOPIC_USER, boardConfig.roomName, MQTT_PM1_TOPIC);
        snprintf(pm25Topic, sizeof(pm25Topic), "%s%s%s", MQTT_TOPIC_USER, boardConfig.roomName, MQTT_PM25_TOPIC);
        snprintf(pm10Topic, sizeof(pm10Topic), "%s%s%s", MQTT_TOPIC_USER, boardConfig.roomName, MQTT_PM10_TOPIC);
    }
    if (boardConfig.sensors & SENSOR_JSY194G) {
        snprintf(jsyVoltageTopic, sizeof(jsyVoltageTopic), "%s%s%s", MQTT_TOPIC_USER, boardConfig.roomName, MQTT_JSY_VOLTAGE_TOPIC);
        snprintf(jsyCurrentTopic, sizeof(jsyCurrentTopic), "%s%s%s", MQTT_TOPIC_USER, boardConfig.roomName, MQTT_JSY_CURRENT_TOPIC);
        snprintf(jsyPowerTopic,   sizeof(jsyPowerTopic),   "%s%s%s", MQTT_TOPIC_USER, boardConfig.roomName, MQTT_JSY_POWER_TOPIC);
        snprintf(jsyPfTopic,      sizeof(jsyPfTopic),      "%s%s%s", MQTT_TOPIC_USER, boardConfig.roomName, MQTT_JSY_PF_TOPIC);
        snprintf(jsyFreqTopic,    sizeof(jsyFreqTopic),    "%s%s%s", MQTT_TOPIC_USER, boardConfig.roomName, MQTT_JSY_FREQ_TOPIC);
        snprintf(jsyEnergyTopic,  sizeof(jsyEnergyTopic),  "%s%s%s", MQTT_TOPIC_USER, boardConfig.roomName, MQTT_JSY_ENERGY_TOPIC);
    }

    // Re-configure DHT with correct pin from config
    if (boardConfig.sensors & SENSOR_DHT) {
        dht = DHT(boardConfig.dhtDataPin, boardConfig.dhtType);
    }
}

void deepSleep(int sleepSeconds) {
    esp_sleep_enable_timer_wakeup((uint64_t)sleepSeconds * MICROSECONDS_IN_SECOND);
    snprintf(debugBuf, sizeof(debugBuf), "Entering deep sleep for %d seconds...", sleepSeconds);
    debugMessage(debugBuf, false);
    WiFi.disconnect();
    esp_deep_sleep_start();
}

// ---------------------------------------------------------------------------

void setup() {
    bootCount++;
    Serial.begin(115200);
    loadBoardConfig();

    if (boardConfig.sensors & SENSOR_DHT) {
        if (boardConfig.isBatteryPowered && boardConfig.dhtPowerPin > 0) {
            pinMode(boardConfig.dhtPowerPin, OUTPUT);
            digitalWrite(boardConfig.dhtPowerPin, HIGH); // Power on early to warm up
        }
        dht.begin();
    }

    if (boardConfig.sensors & SENSOR_PMS5003) {
        Serial2.begin(9600, SERIAL_8N1, boardConfig.pmsRxPin, boardConfig.pmsTxPin);
        if (boardConfig.pmsPowerPin >= 0) {
            pinMode(boardConfig.pmsPowerPin, OUTPUT);
            digitalWrite(boardConfig.pmsPowerPin, HIGH);
        }
    }

    if (boardConfig.sensors & SENSOR_SCD41) {
        int sdaPin = boardConfig.i2cSdaPin >= 0 ? boardConfig.i2cSdaPin : 21;
        int sclPin = boardConfig.i2cSclPin >= 0 ? boardConfig.i2cSclPin : 22;
        Wire.begin(sdaPin, sclPin);
        // stopPeriodicMeasurement in case of warm restart, then start
        Wire.beginTransmission(SCD41_I2C_ADDR); Wire.write(0x3F); Wire.write(0x86); Wire.endTransmission();
        delay(500);
        Wire.beginTransmission(SCD41_I2C_ADDR); Wire.write(0x21); Wire.write(0xB1); Wire.endTransmission();
        delay(100);
    }

    if (boardConfig.sensors & SENSOR_JSY194G) {
        Serial1.begin(9600, SERIAL_8N1, boardConfig.jsyRxPin, boardConfig.jsyTxPin);
        if (boardConfig.jsyDePin >= 0) {
            pinMode(boardConfig.jsyDePin, OUTPUT);
            digitalWrite(boardConfig.jsyDePin, LOW); // Default: receive mode
        }
    }

    mqttClient.setUsernamePassword(MQTT_USER, MQTT_PASSWORD);
}

void loop() {
    // For mains-powered devices, wait until the next reading time.
    // Skip the wait on the very first run (lastReadingTime == 0).
    if (!boardConfig.isBatteryPowered && lastReadingTime > 0) {
        unsigned long nextReadingTime = lastReadingTime + (boardConfig.timeToSleep * 1000UL);
        while ((long)(millis() - nextReadingTime) < 0) {
            webServer.handleClient();
            delay(WEB_SERVER_POLL_INTERVAL_MS);
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
    char timeBuffer[50];
    struct tm timeinfo;
    configTime(gmtOffsetSec, daylightOffsetSec, ntpServer);
    if (!getLocalTime(&timeinfo)) {
        strncpy(timeBuffer, "Time Error", sizeof(timeBuffer) - 1);
    } else {
        strftime(timeBuffer, sizeof(timeBuffer) - 1, "%d/%m/%y %H:%M:%S", &timeinfo);
    }

    // Read DHT sensor if present
    if (boardConfig.sensors & SENSOR_DHT) {
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
        lastTemp  = reading.temperature;
        lastHumid = reading.humidity;
        strncpy(lastReadingTimeStr, timeBuffer, sizeof(lastReadingTimeStr) - 1);
        lastReadingTimeStr[sizeof(lastReadingTimeStr) - 1] = '\0';
        mqttSendFloat(temperatureTopic, reading.temperature);
        mqttSendFloat(humidityTopic,    reading.humidity);
    }

    // Read and publish battery voltage
    batteryMessage[0] = '\0';
    if (boardConfig.isBatteryPowered && boardConfig.battPin > 0) {
        lastVolts = readBatteryVoltage();
        snprintf(batteryMessage, sizeof(batteryMessage), " | Bat: %.2fV", lastVolts);
        mqttSendFloat(batteryTopic, lastVolts);
    }

    // Send DHT summary debug message
    if (boardConfig.sensors & SENSOR_DHT) {
        char mqttMessage[256];
        snprintf(mqttMessage, sizeof(mqttMessage), "%s | T: %.1f | H: %.0f%s | Boot: %d | Success: %d",
                 timeBuffer, lastTemp, lastHumid, batteryMessage, bootCount, successCount);
        debugMessage(mqttMessage, true);
    }

    // Read PMS5003 air quality sensor if present
    if (boardConfig.sensors & SENSOR_PMS5003) {
        Pms5003Data pms = readPms5003();
        if (!pms.success) {
            debugMessage("PMS5003 read failed.", false);
        } else {
            mqttSendFloat(pm1Topic,  pms.pm1);
            mqttSendFloat(pm25Topic, pms.pm25);
            mqttSendFloat(pm10Topic, pms.pm10);
        }
    }

    // Read SCD41 CO2 sensor if present
    if (boardConfig.sensors & SENSOR_SCD41) {
        Scd41Data scd = readScd41();
        if (!scd.success) {
            debugMessage("SCD41 read failed.", false);
        } else {
            mqttSendFloat(co2Topic, scd.co2);
            // If no DHT present, use SCD41 temperature and humidity
            if (!(boardConfig.sensors & SENSOR_DHT)) {
                mqttSendFloat(temperatureTopic, scd.temperature);
                mqttSendFloat(humidityTopic,    scd.humidity);
            }
        }
    }

    // Read JSY-MK-194G AC power meter if present
    if (boardConfig.sensors & SENSOR_JSY194G) {
        Jsy194gData jsy = readJsy194g();
        if (!jsy.success) {
            debugMessage("JSY-MK-194G read failed.", false);
        } else {
            mqttSendFloat(jsyVoltageTopic, jsy.voltage);
            mqttSendFloat(jsyCurrentTopic, jsy.current);
            mqttSendFloat(jsyPowerTopic,   jsy.power);
            mqttSendFloat(jsyPfTopic,      jsy.powerFactor);
            mqttSendFloat(jsyFreqTopic,    jsy.frequency);
            mqttSendFloat(jsyEnergyTopic,  jsy.energy);
        }
    }

    if (boardConfig.isBatteryPowered) {
        delay(1000); // Allow messages to transmit before sleeping
        deepSleep(boardConfig.timeToSleep);
    } else {
        lastReadingTime = millis();
    }
}
