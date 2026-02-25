#ifndef GLOBALS_H
#define GLOBALS_H

#include "config.h"
#include <Arduino.h>
#include <ArduinoMqttClient.h>
#include <DHT.h>
#include <WebServer.h>
#include <WiFiClient.h>
#include <Wire.h>

// Constants
static constexpr uint64_t MICROSECONDS_IN_SECOND = 1000000ULL;
static constexpr uint8_t  SCD41_I2C_ADDR         = 0x62;
static constexpr size_t   TOPIC_BUF_LEN           = 80;

// Sensor data structs
struct SensorData  { float temperature; float humidity; bool success; };
struct Pms5003Data { float pm1; float pm25; float pm10;
                     float pm1Cf; float pm25Cf; float pm10Cf; // CF=1 standard particle values
                     bool success; };
struct Scd41Data   { float co2; float temperature; float humidity; bool success; };
struct Jsy194gData {
    float voltage; float current; float power;
    float powerFactor; float frequency; float energy;
    bool success;
};

// RTC Memory (persists across deep sleep)
extern RTC_DATA_ATTR int   bootCount;
extern RTC_DATA_ATTR int   successCount;
extern RTC_DATA_ATTR float lastVolts;

// Board config and identification
extern BoardConfig boardConfig;
extern char macAddress[18];

// MQTT topic buffers
extern char temperatureTopic[TOPIC_BUF_LEN];
extern char humidityTopic[TOPIC_BUF_LEN];
extern char debugTopic[TOPIC_BUF_LEN];
extern char batteryTopic[TOPIC_BUF_LEN];
extern char co2Topic[TOPIC_BUF_LEN];
extern char pm1Topic[TOPIC_BUF_LEN];
extern char pm25Topic[TOPIC_BUF_LEN];
extern char pm10Topic[TOPIC_BUF_LEN];
extern char jsyVoltageTopic[TOPIC_BUF_LEN];
extern char jsyCurrentTopic[TOPIC_BUF_LEN];
extern char jsyPowerTopic[TOPIC_BUF_LEN];
extern char jsyPfTopic[TOPIC_BUF_LEN];
extern char jsyFreqTopic[TOPIC_BUF_LEN];
extern char jsyEnergyTopic[TOPIC_BUF_LEN];
extern char jsyDailyEnergyTopic[TOPIC_BUF_LEN];

// Network objects
extern WiFiClient espClient;
extern MqttClient mqttClient;
extern WebServer  webServer;

// Sensor objects
extern DHT* dht;

// State
extern unsigned long lastReadingTime;
extern float         lastTemp;
extern float         lastHumid;
extern char          lastReadingTimeStr[50];
extern char          debugBuf[256];
extern char          batteryMessage[256];

// Last known sensor readings (for web UI; success=false means no reading yet)
extern Pms5003Data lastPmsData;
extern Scd41Data   lastScd41Data;
extern Jsy194gData lastJsyData;

#endif // GLOBALS_H
