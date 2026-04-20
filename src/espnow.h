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
// Call espNowGatewayTick() regularly from loop() to run the RX watchdog and
// re-initialise the ESP-NOW receiver after WiFi reconnect events.
void initEspNowGateway();
void handleEspNowReceived();
void espNowGatewayTick();

// ── Battery node (sender) ─────────────────────────────────────────────────
// Configures WiFi to the given channel, sends the payload to
// ESPNOW_GATEWAY_MAC, and blocks until an ACK arrives or
// ESPNOW_SEND_TIMEOUT_MS elapses. Cleans up esp_now on return.
// channel: WiFi channel discovered by the caller via WiFi.channel() after
//          a successful connection; cached in RTC memory across deep sleeps.
// Returns true if at least one send attempt received an ACK from the gateway.
bool espNowSend(const EspNowPayload& payload, uint8_t channel);

#endif // ESPNOW_H
