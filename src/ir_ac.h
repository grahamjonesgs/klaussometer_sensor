#pragma once
#include <stdint.h>
#include <stdbool.h>

// AC operating modes — maps to kSamsungAc* constants
enum AcMode : uint8_t {
    AC_MODE_COOL = 0,
    AC_MODE_HEAT = 1,
    AC_MODE_AUTO = 2,
    AC_MODE_FAN  = 3,
    AC_MODE_DRY  = 4,
};

// Fan speed levels — maps to kSamsungAcFan* constants
enum AcFan : uint8_t {
    AC_FAN_AUTO  = 0,
    AC_FAN_LOW   = 1,
    AC_FAN_MED   = 2,
    AC_FAN_HIGH  = 3,
    AC_FAN_TURBO = 4,
};

// Initialise the IR transmitter on the given GPIO pin.
// Call once from setup() when SENSOR_IR_AC is present.
// Initialises both the Samsung AC sender and the generic Samsung TV sender.
void initIrAc(int txPin);

// Send a complete Samsung AC state over IR.
// temp must be in the range 16–30 °C.
void sendSamsungAcCommand(bool power, uint8_t temp, AcMode mode, AcFan fan);

// Send a Samsung TV power command (on or off).
// Uses the standard 32-bit Samsung TV power code (0xE0E040BF toggle,
// or separate on/off codes for the M8 M80C).
// If the toggle code doesn't work reliably, set useOnCode=true to try the
// dedicated power-on code instead.
void sendSamsungTvPower(bool on);

// Parse a JSON payload into AC command fields.
// Unrecognised / missing keys leave the corresponding output unchanged.
// Returns true if at least one field was successfully parsed.
//
// Expected format (all fields optional):
//   {"power":"on","temp":22,"mode":"cool","fan":"auto"}
//
// power: "on" | "off"
// mode : "cool" | "heat" | "auto" | "fan" | "dry"
// fan  : "auto" | "low" | "med" | "high" | "turbo"
// temp : integer 16–30
bool parseSamsungAcPayload(const char* json,
                           bool& power, uint8_t& temp,
                           AcMode& mode, AcFan& fan);
