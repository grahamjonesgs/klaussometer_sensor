# Klaussometer Sensor

ESP32 firmware for a multi-sensor home monitoring node. Publishes readings to MQTT and exposes a local web UI. Supports mains-powered boards (continuous loop) and battery-powered boards (deep sleep between readings).

---

## Supported Hardware

### Microcontroller
- **ESP32** (NodeMCU-32S and compatible boards)

### Sensors
| Sensor | Interface | Measurements |
|---|---|---|
| DHT11 / DHT22 | GPIO (1-wire) | Temperature, humidity |
| PMS5003 | UART (Serial2) | PM1.0, PM2.5, PM10 (ambient + CF=1) |
| Sensirion SCD41 | I2C | CO2 (ppm), temperature, humidity |
| JSY-MK-194G | Modbus RTU / UART (Serial1) | AC voltage, current, power, power factor, frequency, energy (kWh), daily kWh |

Multiple sensors can be combined on one board using the `sensors` bitmask in the board configuration.

### Battery monitoring
Boards with a resistor-divider ADC circuit can report battery voltage (e.g. LILYGO T-Koala).

---

## Build Environment

### Requirements
- [PlatformIO](https://platformio.org/) (CLI or VS Code extension)
- Platform: **Espressif32 v3.5.0**
- Framework: **Arduino**

### Dependencies (managed by PlatformIO via `platformio.ini`)
| Library | Version |
|---|---|
| adafruit/DHT sensor library | ^1.4.6 |
| arduino-libraries/ArduinoMqttClient | ^0.1.8 |
| fu-hsi/PMS Library | ^1.1.0 |
| sensirion/Sensirion I2C SCD4x | ^1.1.0 |

Dependencies are fetched automatically on first build. No manual library installation is required.

### Build & flash
```bash
# Build only
pio run

# Build and flash over USB
pio run --target upload

# Monitor serial output
pio device monitor --baud 115200
```

---

## Configuration

Two files are gitignored and must be created locally before the project will compile:

```
src/config.h    ← credentials, MQTT topics, tunable constants
src/config.cpp  ← per-board pin assignments and sensor lists
```

Template files committed to the repository serve as the starting point:

| Template (committed) | Copy to (gitignored) |
|---|---|
| `src/config.hxx` | `src/config.h` |
| `config.cpp.example` | `src/config.cpp` |

### Step 1 — Create `src/config.h` from the template

```bash
cp src/config.hxx src/config.h
```

Edit `src/config.h` and fill in:

- **WiFi credentials** — `WIFI_SSID`, `WIFI_PASSWORD`
- **MQTT broker** — `MQTT_SERVER`, `MQTT_USER`, `MQTT_PASSWORD`, `MQTT_PORT`
- **MQTT topic prefix** — `MQTT_TOPIC_USER` (default `"home/"`)
- **OTA server** — `OTA_HOST`, `OTA_PORT`, `OTA_BIN_PATH`, `OTA_VERSION_PATH` (leave defaults if not using OTA)
- **Time zone** — `GMT_OFFSET_SEC` (e.g. `3600` for UTC+1, `7200` for UTC+2), `DAYLIGHT_OFFSET_SEC`
- **Tunable constants** — read intervals, retry counts, ADC calibration etc. are documented inline

### Step 2 — Create `src/config.cpp` from the template

```bash
cp config.cpp.example src/config.cpp
```

Edit `src/config.cpp` to add an entry for each physical board. Each entry is identified by its MAC address, which is printed to the serial console on boot.

#### BoardConfig struct fields

```
macAddress      — MAC address string (printed on boot, used for lookup)
roomName        — Short identifier used in MQTT topic (e.g. "lounge")
displayName     — Human-readable name shown in web UI (e.g. "Lounge")
isBatteryPowered — true = use deep sleep; false = continuous loop
dhtDataPin      — GPIO pin for DHT data line
dhtType         — DHT11 or DHT22
dhtPowerPin     — GPIO driven HIGH as DHT VCC (0 = use 3.3V rail)
dhtGndPin       — GPIO driven LOW as DHT GND (0 = use board GND pin)
battPin         — ADC pin for battery voltage divider (0 = not used)
timeToSleep     — Seconds between readings (mains) or deep sleep duration (battery)
sensors         — Bitmask: SENSOR_DHT | SENSOR_PMS5003 | SENSOR_SCD41 | SENSOR_JSY194G
pmsRxPin        — PMS5003 UART RX pin (-1 = unused)
pmsTxPin        — PMS5003 UART TX pin (-1 = unused, sensor TX not needed)
pmsPowerPin     — GPIO to switch PMS5003 power (-1 = always on)
i2cSdaPin       — SCD41 I2C SDA (-1 = ESP32 default GPIO 21)
i2cSclPin       — SCD41 I2C SCL (-1 = ESP32 default GPIO 22)
jsyRxPin        — JSY-MK-194G UART RX (-1 = unused)
jsyTxPin        — JSY-MK-194G UART TX (-1 = unused)
jsyDePin        — JSY RS485 DE/RE direction pin (-1 = unused)
```

Use `-1` for any pin that is not wired. Use `0` for power/GND pin fields to indicate the board's physical rail is used instead of a GPIO.

#### Example entries

```cpp
// Mains board — DHT22 only
{
    "AA:BB:CC:DD:EE:FF",
    "lounge", "Lounge",
    false,
    23, DHT22, 0, 0,   // data pin, type, power pin (rail), GND pin (rail)
    0, 30,             // battery pin (none), poll interval (s)
    SENSOR_DHT,
    -1, -1, -1,        // PMS5003 unused
    -1, -1,            // SCD41 unused
    -1, -1, -1         // JSY unused
},

// Battery board — DHT22, GPIO power and GND to save pins
{
    "AA:BB:CC:DD:EE:01",
    "garden", "Garden",
    true,
    23, DHT22, 18, 19, // data, type, power=GPIO18, GND=GPIO19
    35, 600,           // battery ADC pin 35, sleep 10 min
    SENSOR_DHT,
    -1, -1, -1,
    -1, -1,
    -1, -1, -1
},

// Mains board — DHT22 + SCD41 + PMS5003
{
    "AA:BB:CC:DD:EE:02",
    "office", "Office",
    false,
    23, DHT22, 19, 18, // DHT powered from GPIO to free 3.3V rail for SCD41
    0, 30,
    SENSOR_DHT | SENSOR_SCD41 | SENSOR_PMS5003,
    16, -1, 4,         // PMS5003: RX=16, TX unused, power=GPIO4
    -1, -1,            // SCD41 on default I2C pins (21/22)
    -1, -1, -1
},
```

If no entry matches the board's MAC address, `getBoardConfig()` returns the hardcoded default at the bottom of `config.cpp`. This is useful during initial bring-up.

---

## Features

### Mains boards
- Continuous loop; waits between readings using a non-blocking poll loop
- Web UI served on port 80 — shows last sensor readings and board config
- OTA firmware update check on boot and every 5 minutes

### Battery boards
- Deep sleep between readings; wake time determined by `timeToSleep`
- Boot count and success count persisted in RTC memory across sleep cycles
- Battery voltage reported to MQTT with exponential smoothing

### PMS5003 laser lifespan preservation
The PMS5003 laser is rated for ~8,000 hours. On mains boards with `pmsPowerPin` wired, the firmware power-cycles the sensor independently of the main read loop:

- Sensor powers **ON** 30 seconds before each scheduled read (warm-up)
- Sensor powers **OFF** immediately after the read
- Read interval: every 2 minutes (configurable via `PMS5003_READ_INTERVAL_MS` in `config.h`)
- Resulting duty cycle: ~25% → estimated laser life ~3.7 years (vs ~11 months always-on)

On the first boot the first read fires after the 30-second warm-up rather than waiting a full interval.

### DHT22 GPIO power and GND
On boards where 3.3V rail or GND pins are scarce (e.g. when also running SCD41), the DHT22 can be powered entirely from GPIO pins (`dhtPowerPin` driven HIGH, `dhtGndPin` driven LOW). The DHT22 draws ~1.5 mA, well within ESP32 GPIO limits.

---

## MQTT Topics

Topics follow the pattern: `{MQTT_TOPIC_USER}{roomName}{sensor_topic}`

With the default `MQTT_TOPIC_USER = "home/"` and `roomName = "lounge"`:

| Measurement | Topic |
|---|---|
| Temperature | `home/lounge/tempset-ambient/set` |
| Humidity | `home/lounge/tempset-humidity/set` |
| CO2 | `home/lounge/co2/set` |
| PM1.0 | `home/lounge/pm1/set` |
| PM2.5 | `home/lounge/pm25/set` |
| PM10 | `home/lounge/pm10/set` |
| AC Voltage | `home/lounge/ac-voltage/set` |
| AC Current | `home/lounge/ac-current/set` |
| AC Power | `home/lounge/ac-power/set` |
| Power Factor | `home/lounge/ac-pf/set` |
| Frequency | `home/lounge/ac-freq/set` |
| Energy (cumulative) | `home/lounge/ac-energy/set` |
| Energy (daily kWh) | `home/lounge/ac-energy-daily/set` |
| Battery voltage | `home/lounge/battery/set` |
| Debug | `home/lounge/debug` |
