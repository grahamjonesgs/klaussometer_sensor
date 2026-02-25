#include "sensors.h"
#include <PMS.h>
#include <SensirionI2cScd4x.h>
#include <esp_task_wdt.h>

// File-scope sensor objects (Serial2 / Wire initialised by setup() before first use)
static PMS               pms(Serial2);
static SensirionI2cScd4x scd4x;

SensorData readDhtSensor() {
    SensorData data;
    data.temperature = NAN;
    data.humidity    = NAN;
    data.success     = false;

    // Short guard to ensure DHT22 has had enough stable power before the first read,
    // even if WiFi/NTP completed unusually fast.
    delay(DHT_INITIAL_DELAY_MS);

    for (int i = 0; i < DHT_RETRIES; i++) {
        data.temperature = dht->readTemperature();
        data.humidity    = dht->readHumidity();
        if (!isnan(data.temperature) && !isnan(data.humidity)) {
            data.success = true;
            break;
        }
        delay(DHT_RETRY_DELAY_MS); // DHT22 requires >=2 s between reads
        esp_task_wdt_reset();      // keep watchdog alive during extended retry wait
    }

    if (boardConfig.isBatteryPowered) {
        digitalWrite(boardConfig.dhtPowerPin, LOW);
    }

    return data;
}

float readBatteryVoltage() {
    if (!boardConfig.isBatteryPowered || boardConfig.battPin <= 0) {
        return 0.0;
    }

    // Configure ADC for better accuracy
    analogSetAttenuation(ADC_11db);         // 0-3.6V range
    analogSetWidth(ADC_BIT_WIDTH);

    // Discard first reading (often inaccurate)
    analogRead(boardConfig.battPin);
    delay(ADC_SETTLE_DELAY_MS);

    uint32_t total = 0;
    for (int i = 0; i < VOLT_READS; i++) {
        total += analogRead(boardConfig.battPin);
        delay(ADC_SETTLE_DELAY_MS);
    }

    float avgRaw = (float)total / VOLT_READS;
    float volts  = avgRaw / RAW_VOLTS_CONVERSION;

    // Exponential smoothing with previous reading
    if (lastVolts > 0.0) {
        volts = (volts * VOLT_SMOOTH_NEW) + (lastVolts * VOLT_SMOOTH_PREV);
    }
    lastVolts = volts;

    return volts;
}

// Initialise SCD41 via the Sensirion library; called from setup()
void initScd41(int sdaPin, int sclPin) {
    Wire.begin(sdaPin >= 0 ? sdaPin : SCD41_DEFAULT_SDA_PIN,
               sclPin >= 0 ? sclPin : SCD41_DEFAULT_SCL_PIN);
    scd4x.begin(Wire, SCD41_I2C_ADDR);
    scd4x.stopPeriodicMeasurement(); // safe no-op on first boot
    delay(SCD41_INIT_DELAY_MS);
    scd4x.startPeriodicMeasurement();
}

// Read PMS5003 particulate matter sensor via the PMS library (Serial2)
Pms5003Data readPms5003() {
    Pms5003Data data = {};

    // Discard stale buffered frames so we read the freshest available sample
    while (Serial2.available()) Serial2.read();

    PMS::DATA pmsData;
    if (!pms.readUntil(pmsData, PMS5003_READ_TIMEOUT_MS)) return data;

    data.pm1     = pmsData.PM_AE_UG_1_0;
    data.pm25    = pmsData.PM_AE_UG_2_5;
    data.pm10    = pmsData.PM_AE_UG_10_0;
    data.pm1Cf   = pmsData.PM_SP_UG_1_0;
    data.pm25Cf  = pmsData.PM_SP_UG_2_5;
    data.pm10Cf  = pmsData.PM_SP_UG_10_0;
    data.success = true;
    return data;
}

// Read CO2, temperature and humidity from SCD41 via the Sensirion library
Scd41Data readScd41() {
    Scd41Data data = {};

    bool isDataReady = false;
    if (scd4x.getDataReadyStatus(isDataReady) || !isDataReady) return data;

    uint16_t co2 = 0;
    float    temperature = 0.0f;
    float    humidity    = 0.0f;
    if (scd4x.readMeasurement(co2, temperature, humidity) || co2 == 0) return data;

    data.co2         = co2;
    data.temperature = temperature;
    data.humidity    = humidity;
    data.success     = true;
    return data;
}

// Modbus CRC16 (polynomial 0xA001)
static uint16_t modbusCalcCrc(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x0001) ? ((crc >> 1) ^ 0xA001) : (crc >> 1);
        }
    }
    return crc;
}

// Read JSY-MK-194G AC power meter via Modbus RTU over Serial1
// NOTE: Verify register addresses against your specific JSY-MK-194G datasheet/firmware version
Jsy194gData readJsy194g() {
    Jsy194gData data = {};

    // Flush any stale bytes
    while (Serial1.available()) Serial1.read();

    // Build Modbus RTU request: device=0x01, FC=0x03, start=0x0000, count=8 registers
    uint8_t request[8];
    request[0] = 0x01; // Device address
    request[1] = 0x03; // Function code: read holding registers
    request[2] = 0x00; // Start register high byte
    request[3] = 0x00; // Start register low byte
    request[4] = 0x00; // Register count high byte
    request[5] = 0x08; // Register count low byte
    uint16_t crc = modbusCalcCrc(request, 6);
    request[6] = crc & 0xFF;
    request[7] = (crc >> 8) & 0xFF;

    // Drive RS485 bus to transmit mode
    if (boardConfig.jsyDePin >= 0) {
        digitalWrite(boardConfig.jsyDePin, HIGH);
    }
    Serial1.write(request, 8);
    Serial1.flush();

    // Switch back to receive mode
    if (boardConfig.jsyDePin >= 0) {
        digitalWrite(boardConfig.jsyDePin, LOW);
    }

    // Expected response: addr(1) + FC(1) + byteCount(1) + data(16) + CRC(2) = JSY_RESPONSE_BYTES
    const int expectedBytes = JSY_RESPONSE_BYTES;
    uint32_t timeout = millis() + JSY_RESPONSE_TIMEOUT_MS;
    while (Serial1.available() < expectedBytes && millis() < timeout) {
        delay(1);
    }
    if (Serial1.available() < expectedBytes) return data; // Timeout

    uint8_t response[JSY_RESPONSE_BYTES];
    Serial1.readBytes(response, expectedBytes);

    // Validate CRC
    uint16_t respCrc = modbusCalcCrc(response, expectedBytes - 2);
    if ((respCrc & 0xFF) != response[expectedBytes - 2] || ((respCrc >> 8) & 0xFF) != response[expectedBytes - 1]) {
        return data; // CRC mismatch
    }

    // Parse registers from response bytes 3..18 (big-endian 16-bit each)
    // Register map (JSY-MK-194G, verify against your datasheet):
    // Reg 0: Voltage    ×0.01 V
    // Reg 1: Current    ×0.01 A
    // Reg 2+3: Power    32-bit signed, ×0.1 W
    // Reg 4: Power factor ×0.001
    // Reg 5: Frequency  ×0.01 Hz
    // Reg 6+7: Energy   32-bit unsigned, ×1 Wh (converted to kWh below)
    uint16_t rawVoltage = ((uint16_t)response[3]  << 8) | response[4];
    uint16_t rawCurrent = ((uint16_t)response[5]  << 8) | response[6];
    int32_t  rawPower   = (int32_t)(((uint32_t)response[7]  << 24) | ((uint32_t)response[8]  << 16)
                                  | ((uint32_t)response[9]  <<  8) |  (uint32_t)response[10]);
    uint16_t rawPf      = ((uint16_t)response[11] << 8) | response[12];
    uint16_t rawFreq    = ((uint16_t)response[13] << 8) | response[14];
    uint32_t rawEnergy  = ((uint32_t)response[15] << 24) | ((uint32_t)response[16] << 16)
                        | ((uint32_t)response[17] <<  8) |  (uint32_t)response[18];

    data.voltage     = rawVoltage / JSY_VOLTAGE_SCALE;
    data.current     = rawCurrent / JSY_CURRENT_SCALE;
    data.power       = rawPower   / JSY_POWER_SCALE;
    data.powerFactor = rawPf      / JSY_PF_SCALE;
    data.frequency   = rawFreq    / JSY_FREQ_SCALE;
    data.energy      = (double)rawEnergy / 1000.0; // explicit double division for kWh precision
    data.success     = true;
    return data;
}
