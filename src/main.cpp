#include "globals.h"
#include "espnow.h"
#include "network.h"
#include "ota.h"
#include "sensors.h"
#include <WiFi.h>

// Global definitions (extern-declared in globals.h)
RTC_DATA_ATTR int     bootCount      = 0;
RTC_DATA_ATTR int     successCount   = 0;
RTC_DATA_ATTR float   lastVolts      = 0.0f;
RTC_DATA_ATTR uint8_t rtcWifiChannel = 0;   // ESP-NOW WiFi channel; 0 = not yet discovered

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
char jsyDailyEnergyTopic[TOPIC_BUF_LEN];

WiFiClient espClient;
MqttClient mqttClient(espClient);
WebServer  webServer(80);
DHT*       dht = nullptr;

unsigned long lastReadingTime = 0;
float         lastTemp        = 0.0f;
float         lastHumid       = 0.0f;
char          lastReadingTimeStr[50] = "N/A";
char          debugBuf[256];
char          batteryMessage[256] = "";

Pms5003Data lastPmsData   = {};
Scd41Data   lastScd41Data = {};
Jsy194gData lastJsyData   = {};

static double        dayStartEnergy = -1.0; // -1 = not yet initialised (first boot)
static int           lastTmYday     = -1;   // -1 = not yet initialised (first boot)
// Sentinel: first PMS read fires after PMS5003_WARMUP_MS (not a full interval) from boot
static unsigned long lastPmsReadMs  = 0UL - (PMS5003_READ_INTERVAL_MS - PMS5003_WARMUP_MS);
static bool          pmsPoweredOn   = true; // true on boot (sensor starts powered)

// NTP settings (used only in loop)
static const char* const ntpServer = "pool.ntp.org";

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
        snprintf(jsyEnergyTopic,       sizeof(jsyEnergyTopic),       "%s%s%s", MQTT_TOPIC_USER, boardConfig.roomName, MQTT_JSY_ENERGY_TOPIC);
        snprintf(jsyDailyEnergyTopic,  sizeof(jsyDailyEnergyTopic),  "%s%s%s", MQTT_TOPIC_USER, boardConfig.roomName, MQTT_JSY_DAILY_ENERGY_TOPIC);
    }

    // Create DHT object now that the correct pin is known
    if (boardConfig.sensors & SENSOR_DHT) {
        dht = new DHT(boardConfig.dhtDataPin, boardConfig.dhtType);
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

    // Validate configuration — warn about combinations that cannot work correctly
    if (boardConfig.isBatteryPowered && (boardConfig.sensors & SENSOR_PMS5003)) {
        Serial.println("CONFIG WARNING: PMS5003 is not supported on battery-powered boards. "
                       "The sensor requires a 30-second warm-up that is incompatible with deep sleep. "
                       "Remove SENSOR_PMS5003 from this board's config.");
    }
    if (boardConfig.isBatteryPowered && (boardConfig.sensors & SENSOR_JSY194G)) {
        Serial.println("CONFIG WARNING: JSY-MK-194G is not supported on battery-powered boards. "
                       "Modbus RTU over UART is incompatible with deep sleep wake cycles. "
                       "Remove SENSOR_JSY194G from this board's config.");
    }

    if (boardConfig.sensors & SENSOR_DHT) {
        if (boardConfig.dhtGndPin > 0) {
            pinMode(boardConfig.dhtGndPin, OUTPUT);
            digitalWrite(boardConfig.dhtGndPin, LOW);    // GPIO as GND substitute
        }
        if (boardConfig.dhtPowerPin > 0) {
            pinMode(boardConfig.dhtPowerPin, OUTPUT);
            digitalWrite(boardConfig.dhtPowerPin, HIGH); // Power on early to warm up
        }
        dht->begin();
    }

    if (boardConfig.sensors & SENSOR_PMS5003) {
        Serial2.begin(9600, SERIAL_8N1, boardConfig.pmsRxPin, boardConfig.pmsTxPin);
        if (boardConfig.pmsPowerPin >= 0) {
            pinMode(boardConfig.pmsPowerPin, OUTPUT);
            digitalWrite(boardConfig.pmsPowerPin, HIGH);
        }
    }

    if (boardConfig.sensors & SENSOR_SCD41) {
        initScd41(boardConfig.i2cSdaPin, boardConfig.i2cSclPin);
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
    // ── ESP-NOW sender path (battery boards using ESP-NOW) ──────────────────
    // Reads sensors, transmits via ESP-NOW, optionally checks OTA, then sleeps.
    // This path never connects to WiFi for sensor data, saving ~95% of battery.
    if (boardConfig.isBatteryPowered && boardConfig.useEspNow) {
        EspNowPayload payload = {};
        strncpy(payload.roomName, boardConfig.roomName, sizeof(payload.roomName) - 1);
        payload.temperature  = NAN;
        payload.humidity     = NAN;
        payload.batteryVolts = 0.0f;
        payload.bootCount    = (uint16_t)bootCount;
        payload.successCount = (uint16_t)successCount;

        bool dhtOk = true;
        if (boardConfig.sensors & SENSOR_DHT) {
            SensorData reading = readDhtSensor();
            if (reading.success) {
                payload.temperature = reading.temperature;
                payload.humidity    = reading.humidity;
                successCount++;
                payload.successCount = (uint16_t)successCount;
            } else {
                dhtOk = false;
            }
        }

        if (boardConfig.battPin > 0) {
            payload.batteryVolts = readBatteryVoltage();
        }

        // Connect to WiFi when channel is unknown (first ever boot) or it is
        // time for an OTA check. Both cases read the current channel from the
        // AP association and cache it in RTC memory so subsequent boots skip WiFi.
        bool needsWifi = (rtcWifiChannel == 0) || (bootCount % ESPNOW_OTA_BOOT_INTERVAL == 0);
        if (needsWifi) {
            if (setupWifi()) {
                uint8_t ch = (uint8_t)WiFi.channel();
                if (ch > 0) {
                    rtcWifiChannel = ch;
                    Serial.printf("ESP-NOW: WiFi channel cached: %u\n", ch);
                }
                if (bootCount % ESPNOW_OTA_BOOT_INTERVAL == 0) {
                    checkForUpdates();
                }
            }
            WiFi.disconnect(true); // true = also disable WiFi radio
        }

        bool espNowOk = true; // not a failure if channel not yet known (first boot)
        if (rtcWifiChannel > 0) {
            espNowOk = espNowSend(payload, rtcWifiChannel);
        } else {
            Serial.println("ESP-NOW: channel not yet known — skipping send this boot");
        }

        // Retry sooner if DHT read or ESP-NOW send failed; otherwise normal interval
        int sleepSecs = (dhtOk && espNowOk) ? boardConfig.timeToSleep : ESPNOW_RETRY_SLEEP_S;
        deepSleep(sleepSecs);
        return; // deepSleep() does not return; this line is for clarity
    }

    // ── Normal path (mains boards and WiFi-connected battery boards) ─────────
    // For mains-powered devices, wait until the next reading time.
    // Skip the wait on the very first run (lastReadingTime == 0).
    if (!boardConfig.isBatteryPowered && lastReadingTime > 0) {
        unsigned long nextReadingTime = lastReadingTime + (boardConfig.timeToSleep * 1000UL);
        while ((long)(millis() - nextReadingTime) < 0) {
            if (boardConfig.isEspNowGateway) handleEspNowReceived();
            // Power on PMS5003 early so laser stabilizes before its scheduled read
            if ((boardConfig.sensors & SENSOR_PMS5003) && boardConfig.pmsPowerPin >= 0) {
                unsigned long nextPmsRead = lastPmsReadMs + PMS5003_READ_INTERVAL_MS;
                unsigned long pmsOnAt     = nextPmsRead   - PMS5003_WARMUP_MS;
                if ((long)(millis() - pmsOnAt) >= 0 && !pmsPoweredOn) {
                    digitalWrite(boardConfig.pmsPowerPin, HIGH);
                    pmsPoweredOn = true;
                }
            }
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

    // OTA: check on first boot, then every 5 minutes
    static bool          otaChecked      = false;
    static unsigned long lastOtaCheckMs  = 0;
    if (!otaChecked || millis() - lastOtaCheckMs >= OTA_CHECK_INTERVAL_MS) {
        checkForUpdates();
        otaChecked     = true;
        lastOtaCheckMs = millis();
    }

    if (!mqttClient.connected()) {
        mqttReconnect();
    }

    // ESP-NOW gateway: initialise once after MQTT is up
    if (boardConfig.isEspNowGateway) {
        static bool espNowReady = false;
        if (!espNowReady) {
            initEspNowGateway();
            espNowReady = true;
        }
    }

    // NTP: configure once — the ESP32 NTP client resyncs automatically in the background
    static bool ntpStarted = false;
    if (!ntpStarted) {
        configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, ntpServer);
        ntpStarted = true;
    }

    // Get current time from NTP
    char timeBuffer[50];
    struct tm timeinfo;
    bool timeValid = getLocalTime(&timeinfo);
    if (!timeValid) {
        strncpy(timeBuffer, "Time Error", sizeof(timeBuffer) - 1);
    } else {
        strftime(timeBuffer, sizeof(timeBuffer) - 1, "%d/%m/%y %H:%M:%S", &timeinfo);
    }

    // Stamp start of this cycle so the next wait interval is measured from here,
    // not from after sensor reads complete (avoids cumulative drift).
    if (!boardConfig.isBatteryPowered) {
        lastReadingTime = millis();
    }

    // Read DHT sensor if present
    if (boardConfig.sensors & SENSOR_DHT) {
        SensorData reading = readDhtSensor();
        if (!reading.success) {
            snprintf(debugBuf, sizeof(debugBuf), "%s DHT read failed after %d retries.", timeBuffer, DHT_RETRIES);
            debugMessage(debugBuf, true);
            if (boardConfig.isBatteryPowered) {
                deepSleep(boardConfig.timeToSleep);
            }
            // On mains boards: log and continue — other sensors (SCD41 etc.) are still read
        } else {
            successCount++;
            lastTemp  = reading.temperature;
            lastHumid = reading.humidity;
            strncpy(lastReadingTimeStr, timeBuffer, sizeof(lastReadingTimeStr) - 1);
            lastReadingTimeStr[sizeof(lastReadingTimeStr) - 1] = '\0';
            // Only publish DHT temp/humidity if SCD41 is absent; SCD41 is more accurate
            if (!(boardConfig.sensors & SENSOR_SCD41)) {
                mqttSendFloat(temperatureTopic, reading.temperature);
                mqttSendFloat(humidityTopic,    reading.humidity);
            }
        }
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

    // Read PMS5003 on its own 5-minute cycle to preserve laser lifespan
    if ((boardConfig.sensors & SENSOR_PMS5003) &&
            (millis() - lastPmsReadMs >= PMS5003_READ_INTERVAL_MS)) {
        Pms5003Data pms = readPms5003();
        lastPmsReadMs = millis(); // reset interval regardless of success/fail
        if (!pms.success) {
            debugMessage("PMS5003 read failed.", false);
        } else {
            lastPmsData = pms;
            mqttSendFloat(pm1Topic,  pms.pm1);
            mqttSendFloat(pm25Topic, pms.pm25);
            mqttSendFloat(pm10Topic, pms.pm10);
            snprintf(debugBuf, sizeof(debugBuf),
                     "%s | PM1: %.0f | PM2.5: %.0f | PM10: %.0f | CF1 PM1: %.0f | CF1 PM2.5: %.0f | CF1 PM10: %.0f",
                     timeBuffer, pms.pm1, pms.pm25, pms.pm10, pms.pm1Std, pms.pm25Std, pms.pm10Std);
            debugMessage(debugBuf, false);
        }
        // Power off after read to preserve laser lifespan
        if (boardConfig.pmsPowerPin >= 0 && !boardConfig.isBatteryPowered) {
            digitalWrite(boardConfig.pmsPowerPin, LOW);
            pmsPoweredOn = false;
        }
    }

    // Read SCD41 CO2 sensor if present
    if (boardConfig.sensors & SENSOR_SCD41) {
        Scd41Data scd = readScd41();
        if (!scd.success) {
            debugMessage("SCD41 read failed.", false);
        } else {
            lastScd41Data = scd;
            mqttSendFloat(co2Topic, scd.co2);
            // SCD41 is preferred for temperature and humidity; also used as fallback if no DHT
            lastTemp  = scd.temperature;
            lastHumid = scd.humidity;
            strncpy(lastReadingTimeStr, timeBuffer, sizeof(lastReadingTimeStr) - 1);
            lastReadingTimeStr[sizeof(lastReadingTimeStr) - 1] = '\0';
            mqttSendFloat(temperatureTopic, scd.temperature);
            mqttSendFloat(humidityTopic,    scd.humidity);
            snprintf(debugBuf, sizeof(debugBuf), "%s | CO2: %.0f ppm | T: %.1f | H: %.0f",
                     timeBuffer, scd.co2, scd.temperature, scd.humidity);
            debugMessage(debugBuf, false);
        }
    }

    // Read JSY-MK-194G AC power meter if present
    if (boardConfig.sensors & SENSOR_JSY194G) {
        Jsy194gData jsy = readJsy194g();
        if (!jsy.success) {
            debugMessage("JSY-MK-194G read failed.", false);
        } else {
            lastJsyData = jsy;
            mqttSendFloat(jsyVoltageTopic, jsy.voltage);
            mqttSendFloat(jsyCurrentTopic, jsy.current);
            mqttSendFloat(jsyPowerTopic,   jsy.power);
            mqttSendFloat(jsyPfTopic,      jsy.powerFactor);
            mqttSendFloat(jsyFreqTopic,    jsy.frequency);
            mqttSendFloat(jsyEnergyTopic,  jsy.energy);

            // Daily kWh delta — only published when NTP time is valid
            float dailyKwh = 0.0f;
            if (timeValid) {
                int todayYday = timeinfo.tm_yday;
                if (lastTmYday == -1 || todayYday != lastTmYday) {
                    dayStartEnergy = jsy.energy;
                    lastTmYday     = todayYday;
                }
                dailyKwh = jsy.energy - dayStartEnergy;
                if (dailyKwh < 0.0f) dailyKwh = 0.0f; // guard against meter reset/rollover
                mqttSendFloat(jsyDailyEnergyTopic, dailyKwh);
            }
            snprintf(debugBuf, sizeof(debugBuf),
                     "%s | V: %.1fV | I: %.2fA | P: %.1fW | PF: %.2f | F: %.1fHz | E: %.3fkWh | Day: %.3fkWh",
                     timeBuffer, jsy.voltage, jsy.current, jsy.power, jsy.powerFactor, jsy.frequency, jsy.energy, dailyKwh);
            debugMessage(debugBuf, false);
        }
    }

    // Process any ESP-NOW packets that arrived during this cycle's sensor reads
    if (boardConfig.isEspNowGateway) handleEspNowReceived();

    if (boardConfig.isBatteryPowered) {
        delay(1000); // Allow messages to transmit before sleeping
        deepSleep(boardConfig.timeToSleep);
    }
}
