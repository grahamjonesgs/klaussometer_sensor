#include "ir_ac.h"
#include "config.h"
#include <Arduino.h>
#include <IRremoteESP8266.h>
// Note: do NOT #include <IRSend.h> with angle brackets — that can resolve to a
// system file that shadows the library's own IRSend.h. ir_Samsung.h includes it
// via a relative path and provides the correct IRSend class.
#include <ir_Samsung.h>
#include <string.h>
#include <stdlib.h>

// Samsung AC temperature limits (degrees C)
static constexpr uint8_t AC_TEMP_MIN = 16;
static constexpr uint8_t AC_TEMP_MAX = 30;

static IRSamsungAc* acSender = nullptr;

// ---------------------------------------------------------------------------
// Init

void initIrAc(int txPin) {
    acSender = new IRSamsungAc((uint16_t)txPin);
    acSender->begin();
    Serial.printf("IR: transmitter ready on GPIO %d\n", txPin);
}

// ---------------------------------------------------------------------------
// Send

void sendSamsungAcCommand(bool power, uint8_t temp, AcMode mode, AcFan fan) {
    if (!acSender) return;

    // Map our enum values to the IRremoteESP8266 Samsung constants
    static const uint8_t modeMap[] = {
        kSamsungAcCool, // AC_MODE_COOL
        kSamsungAcHeat, // AC_MODE_HEAT
        kSamsungAcAuto, // AC_MODE_AUTO
        kSamsungAcFan,  // AC_MODE_FAN
        kSamsungAcDry,  // AC_MODE_DRY
    };
    static const uint8_t fanMap[] = {
        kSamsungAcFanAuto,  // AC_FAN_AUTO
        kSamsungAcFanLow,   // AC_FAN_LOW
        kSamsungAcFanMed,   // AC_FAN_MED
        kSamsungAcFanHigh,  // AC_FAN_HIGH
        kSamsungAcFanTurbo, // AC_FAN_TURBO
    };

    uint8_t clampedTemp = (temp < AC_TEMP_MIN) ? AC_TEMP_MIN
                        : (temp > AC_TEMP_MAX) ? AC_TEMP_MAX
                        : temp;

    acSender->setPower(power);
    acSender->setTemp(clampedTemp);
    acSender->setMode(modeMap[mode]);
    acSender->setFan(fanMap[fan]);
    acSender->send(IR_AC_REPEAT);
    Serial.println("IR AC: signal transmitted OK");
}

// ---------------------------------------------------------------------------
// JSON parser — no external library required.
// Finds each known key in the flat JSON string and extracts its value.

// Returns a pointer to the character immediately after the closing quote of
// the string value for the given key, or nullptr if the key is not found.
static const char* findStringValue(const char* json, const char* key,
                                   char* valueBuf, size_t bufLen) {
    // Build search pattern: "key":"
    char pattern[40];
    snprintf(pattern, sizeof(pattern), "\"%s\":\"", key);
    const char* pos = strstr(json, pattern);
    if (!pos) return nullptr;
    pos += strlen(pattern); // points at start of value
    size_t i = 0;
    while (*pos && *pos != '"' && i < bufLen - 1) {
        valueBuf[i++] = *pos++;
    }
    valueBuf[i] = '\0';
    return pos;
}

// Returns the integer value for a numeric key, or INT_MIN if not found.
static int findIntValue(const char* json, const char* key) {
    char pattern[40];
    snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    const char* pos = strstr(json, pattern);
    if (!pos) return INT_MIN;
    pos += strlen(pattern);
    // Skip optional whitespace
    while (*pos == ' ') pos++;
    if (!*pos || (!isdigit((unsigned char)*pos) && *pos != '-')) return INT_MIN;
    return (int)strtol(pos, nullptr, 10);
}

bool parseSamsungAcPayload(const char* json,
                           bool& power, uint8_t& temp,
                           AcMode& mode, AcFan& fan) {
    if (!json) return false;

    bool changed = false;
    char val[16];

    // "power"
    if (findStringValue(json, "power", val, sizeof(val))) {
        if (strcmp(val, "on") == 0)  { power = true;  changed = true; }
        if (strcmp(val, "off") == 0) { power = false; changed = true; }
    }

    // "temp"
    int t = findIntValue(json, "temp");
    if (t != INT_MIN) {
        temp = (uint8_t)t;
        changed = true;
    }

    // "mode"
    if (findStringValue(json, "mode", val, sizeof(val))) {
        if      (strcmp(val, "cool") == 0) { mode = AC_MODE_COOL; changed = true; }
        else if (strcmp(val, "heat") == 0) { mode = AC_MODE_HEAT; changed = true; }
        else if (strcmp(val, "auto") == 0) { mode = AC_MODE_AUTO; changed = true; }
        else if (strcmp(val, "fan")  == 0) { mode = AC_MODE_FAN;  changed = true; }
        else if (strcmp(val, "dry")  == 0) { mode = AC_MODE_DRY;  changed = true; }
    }

    // "fan"
    if (findStringValue(json, "fan", val, sizeof(val))) {
        if      (strcmp(val, "auto")  == 0) { fan = AC_FAN_AUTO;  changed = true; }
        else if (strcmp(val, "low")   == 0) { fan = AC_FAN_LOW;   changed = true; }
        else if (strcmp(val, "med")   == 0) { fan = AC_FAN_MED;   changed = true; }
        else if (strcmp(val, "high")  == 0) { fan = AC_FAN_HIGH;  changed = true; }
        else if (strcmp(val, "turbo") == 0) { fan = AC_FAN_TURBO; changed = true; }
    }

    return changed;
}
