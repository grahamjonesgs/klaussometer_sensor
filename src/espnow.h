#ifndef ESPNOW_H
#define ESPNOW_H

#include <stdint.h>

// Payload transmitted by a battery node to the ESP-NOW gateway.
// All sensor fields are present in every packet; NAN signals "not available".
struct __attribute__((packed)) EspNowPayload {
    char     roomName[16];   // BoardConfig.roomName, null-terminated
    float    temperature;    // °C  (NAN if unavailable)
    float    humidity;       // %RH (NAN if unavailable)
    float    batteryVolts;   // V   (0.0 if unavailable)
    uint16_t bootCount;
    uint16_t successCount;
};

// ── Gateway (receiver) ────────────────────────────────────────────────────
// Call initEspNowGateway() once after WiFi is connected.
// Call handleEspNowReceived() regularly from loop() to forward received
// packets to MQTT — safe to call even when no packet has arrived.
void initEspNowGateway();
void handleEspNowReceived();

// ── Battery node (sender) ─────────────────────────────────────────────────
// Configures WiFi to the fixed ESP-NOW channel, sends the payload to
// ESPNOW_GATEWAY_MAC, and blocks until an ACK arrives or
// ESPNOW_SEND_TIMEOUT_MS elapses. Cleans up esp_now on return.
void espNowSend(const EspNowPayload& payload);

#endif // ESPNOW_H
