#include "sensors.h"

SensorData readDhtSensor() {
    SensorData data;
    data.temperature = NAN;
    data.humidity    = NAN;
    data.success     = false;

    // DHT already powered on in setup() for battery boards - no warmup delay needed here

    for (int i = 0; i < DHT_RETRIES; i++) {
        data.temperature = dht.readTemperature();
        data.humidity    = dht.readHumidity();
        if (!isnan(data.temperature) && !isnan(data.humidity)) {
            data.success = true;
            break;
        }
        delay(500);
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
    analogSetAttenuation(ADC_11db); // 0-3.6V range
    analogSetWidth(12);             // 12-bit resolution

    // Discard first reading (often inaccurate)
    analogRead(boardConfig.battPin);
    delay(10);

    uint32_t total = 0;
    for (int i = 0; i < VOLT_READS; i++) {
        total += analogRead(boardConfig.battPin);
        delay(10);
    }

    float avgRaw = (float)total / VOLT_READS;
    float volts  = avgRaw / RAW_VOLTS_CONVERSION;

    // Exponential smoothing with previous reading
    if (lastVolts > 0.0) {
        volts = (volts * 0.7f) + (lastVolts * 0.3f);
    }
    lastVolts = volts;

    return volts;
}

// Read PMS5003 particulate matter sensor via UART (Serial2)
// Frame format: 32 bytes, starts with 0x42 0x4D
Pms5003Data readPms5003() {
    Pms5003Data data = {0, 0, 0, false};

    uint32_t timeout = millis() + 2000;
    while (millis() < timeout) {
        if (Serial2.available() >= 32) {
            // Discard bytes until we find the start sequence
            while (Serial2.available() >= 32 && Serial2.peek() != 0x42) {
                Serial2.read();
            }
            if (Serial2.available() < 32) break;

            uint8_t frame[32];
            Serial2.readBytes(frame, 32);

            if (frame[0] != 0x42 || frame[1] != 0x4D) continue;

            // Validate checksum (sum of bytes 0-29)
            uint16_t checksum = 0;
            for (int i = 0; i < 30; i++) checksum += frame[i];
            uint16_t frameCheck = ((uint16_t)frame[30] << 8) | frame[31];
            if (checksum != frameCheck) continue;

            // Atmospheric PM values: bytes 10-15
            data.pm1    = ((uint16_t)frame[10] << 8) | frame[11];
            data.pm25   = ((uint16_t)frame[12] << 8) | frame[13];
            data.pm10   = ((uint16_t)frame[14] << 8) | frame[15];
            data.success = true;
            return data;
        }
        delay(10);
    }
    return data;
}

// CRC8 for SCD41 (polynomial 0x31, init 0xFF)
static uint8_t scd41Crc(uint8_t* data, size_t len) {
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x80) ? (crc << 1) ^ 0x31 : (crc << 1);
        }
    }
    return crc;
}

// Send a 16-bit command to SCD41 via I2C
static bool scd41SendCmd(uint16_t cmd) {
    Wire.beginTransmission(SCD41_I2C_ADDR);
    Wire.write((cmd >> 8) & 0xFF);
    Wire.write(cmd & 0xFF);
    return Wire.endTransmission() == 0;
}

// Read CO2, temperature and humidity from SCD41 via raw I2C
Scd41Data readScd41() {
    Scd41Data data = {0, 0, 0, false};

    // Check data ready flag (command 0xE4B8)
    if (!scd41SendCmd(0xE4B8)) return data;
    delay(1);
    Wire.requestFrom((uint8_t)SCD41_I2C_ADDR, (uint8_t)3);
    if (Wire.available() < 3) return data;
    uint8_t statusMsb = Wire.read();
    uint8_t statusLsb = Wire.read();
    Wire.read(); // discard CRC
    if (((statusMsb << 8) | statusLsb) == 0) return data; // Not ready

    // Read measurement (command 0xEC05) — returns 9 bytes: CO2(2+CRC), T(2+CRC), RH(2+CRC)
    if (!scd41SendCmd(0xEC05)) return data;
    delay(1);
    Wire.requestFrom((uint8_t)SCD41_I2C_ADDR, (uint8_t)9);
    if (Wire.available() < 9) return data;

    uint8_t buf[9];
    for (int i = 0; i < 9; i++) buf[i] = Wire.read();

    // Validate CRCs
    if (scd41Crc(buf,     2) != buf[2]) return data;
    if (scd41Crc(buf + 3, 2) != buf[5]) return data;
    if (scd41Crc(buf + 6, 2) != buf[8]) return data;

    uint16_t co2Raw   = ((uint16_t)buf[0] << 8) | buf[1];
    uint16_t tempRaw  = ((uint16_t)buf[3] << 8) | buf[4];
    uint16_t humidRaw = ((uint16_t)buf[6] << 8) | buf[7];

    data.co2         = co2Raw;
    data.temperature = -45.0f + 175.0f * tempRaw  / 65535.0f;
    data.humidity    = 100.0f * humidRaw / 65535.0f;
    data.success     = (co2Raw > 0);
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
    Jsy194gData data = {0, 0, 0, 0, 0, 0, false};

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

    // Expected response: addr(1) + FC(1) + byteCount(1) + data(16) + CRC(2) = 21 bytes
    const int expectedBytes = 21;
    uint32_t timeout = millis() + 300;
    while (Serial1.available() < expectedBytes && millis() < timeout) {
        delay(1);
    }
    if (Serial1.available() < expectedBytes) return data; // Timeout

    uint8_t response[21];
    Serial1.readBytes(response, expectedBytes);

    // Validate CRC
    uint16_t respCrc = modbusCalcCrc(response, expectedBytes - 2);
    if ((respCrc & 0xFF) != response[19] || ((respCrc >> 8) & 0xFF) != response[20]) {
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

    data.voltage     = rawVoltage / 100.0f;
    data.current     = rawCurrent / 100.0f;
    data.power       = rawPower   / 10.0f;
    data.powerFactor = rawPf      / 1000.0f;
    data.frequency   = rawFreq    / 100.0f;
    data.energy      = rawEnergy  / 1000.0f; // Wh → kWh
    data.success     = true;
    return data;
}
