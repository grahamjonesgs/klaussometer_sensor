#include "espnow.h"
#include "globals.h"
#include "network.h"
#include <esp_now.h>
#include <esp_wifi.h>
#include <WiFi.h>
#include <math.h>
#include <string.h>
#include <time.h>

// ── Gateway receiver ──────────────────────────────────────────────────────

static volatile bool espNowDataReady = false;
static EspNowPayload espNowRxBuf     = {};

// Watchdog + re-init state. Both are touched from the main loop only, except
// reinitRequested which is set from the WiFi event callback (different task).
static uint32_t lastRxMs         = 0;
static volatile bool reinitRequested = false;

// Runs in the WiFi task context — keep it short; just copy and set flag.
static void onDataReceived(const uint8_t* mac, const uint8_t* data, int len) {
    if ((size_t)len != sizeof(EspNowPayload)) return;
    memcpy((void*)&espNowRxBuf, data, sizeof(EspNowPayload));
    espNowDataReady = true;
}

// Tear down and bring the ESP-NOW receiver back up. Called after WiFi events
// that may have desynced the ESP-NOW driver, or when the RX watchdog expires.
// Runs on the main task so it is safe to call esp_now_deinit/init here.
static void reinitEspNowReceiver() {
    esp_now_deinit();
    if (esp_now_init() != ESP_OK) {
        Serial.println("ESP-NOW: gateway re-init failed");
        return;
    }
    esp_now_register_recv_cb(onDataReceived);
    lastRxMs = millis();
    Serial.printf("ESP-NOW: gateway receiver re-initialised (channel %u)\n",
                  (unsigned)WiFi.channel());
}

// WiFi event callback — fires on the WiFi/system event task. Keep it short:
// just flag that a re-init is needed; the main loop will act on it.
static void onWifiEvent(WiFiEvent_t event) {
    switch (event) {
        case SYSTEM_EVENT_STA_CONNECTED:
        case SYSTEM_EVENT_STA_GOT_IP:
            reinitRequested = true;
            break;
        default:
            break;
    }
}

void initEspNowGateway() {
    // WiFi must already be in WIFI_STA mode (done by setupWifi)
    if (esp_now_init() != ESP_OK) {
        Serial.println("ESP-NOW: gateway init failed");
        return;
    }
    esp_now_register_recv_cb(onDataReceived);
    WiFi.onEvent(onWifiEvent);
    lastRxMs = millis();
    Serial.println("ESP-NOW: gateway ready");
}

// Call regularly from loop(). Handles deferred re-init requests from the WiFi
// event task and the RX watchdog. Cheap when nothing needs doing.
void espNowGatewayTick() {
    if (reinitRequested) {
        reinitRequested = false;
        Serial.println("ESP-NOW: WiFi reconnect detected — re-initialising receiver");
        reinitEspNowReceiver();
        return;
    }
    if (ESPNOW_RX_WATCHDOG_S > 0 && lastRxMs != 0) {
        uint32_t elapsed = millis() - lastRxMs; // unsigned wraparound safe
        if (elapsed > ESPNOW_RX_WATCHDOG_S * 1000UL) {
            Serial.printf("ESP-NOW: RX watchdog expired after %us — re-initialising\n",
                          (unsigned)(elapsed / 1000UL));
            reinitEspNowReceiver();
        }
    }
}

void handleEspNowReceived() {
    if (!espNowDataReady) return;

    EspNowPayload pkt;
    memcpy(&pkt, (const void*)&espNowRxBuf, sizeof(EspNowPayload));
    espNowDataReady = false;
    lastRxMs = millis(); // stamp for the RX watchdog
    pkt.roomName[sizeof(pkt.roomName) - 1] = '\0'; // guard against missing terminator

    char topic[TOPIC_BUF_LEN];

    if (!isnan(pkt.temperature)) {
        snprintf(topic, sizeof(topic), "%s%s%s", MQTT_TOPIC_USER, pkt.roomName, MQTT_TEMP_TOPIC);
        mqttSendFloat(topic, pkt.temperature);
    }
    if (!isnan(pkt.humidity)) {
        snprintf(topic, sizeof(topic), "%s%s%s", MQTT_TOPIC_USER, pkt.roomName, MQTT_HUMID_TOPIC);
        mqttSendFloat(topic, pkt.humidity);
    }
    if (pkt.batteryVolts > 0.0f) {
        snprintf(topic, sizeof(topic), "%s%s%s", MQTT_TOPIC_USER, pkt.roomName, MQTT_BATTERY_TOPIC);
        mqttSendFloat(topic, pkt.batteryVolts);
    }

    // Debug message published to the remote node's own debug topic
    // Add a timestamp (local time) so the retained debug message shows when
    // the packet was received by the gateway.
    char tsBuf[32];
    time_t now = time(nullptr);
    if (now != (time_t)0) {
        struct tm timeinfo;
        localtime_r(&now, &timeinfo);
        strftime(tsBuf, sizeof(tsBuf), "%d/%m/%y %H:%M:%S", &timeinfo);
    } else {
        strncpy(tsBuf, "Time N/A", sizeof(tsBuf));
        tsBuf[sizeof(tsBuf) - 1] = '\0';
    }

    snprintf(debugBuf, sizeof(debugBuf),
             "%s | V%s | ESP-NOW [%s] T:%.1f H:%.0f%% Bat:%.2fV Boot:%u Success:%u GwCh:%u",
             tsBuf,
             FIRMWARE_VERSION,
             pkt.roomName, pkt.temperature, pkt.humidity,
             pkt.batteryVolts, pkt.bootCount, pkt.successCount,
             (unsigned)WiFi.channel());
    Serial.println(debugBuf);

    char dbgTopic[TOPIC_BUF_LEN];
    snprintf(dbgTopic, sizeof(dbgTopic), "%s%s%s", MQTT_TOPIC_USER, pkt.roomName, MQTT_DEBUG_TOPIC);
    mqttClient.beginMessage(dbgTopic, /*retain=*/true);
    mqttClient.print(debugBuf);
    mqttClient.endMessage();
}

// ── Battery node sender ───────────────────────────────────────────────────

static volatile bool espNowSendDone = false;
static volatile bool espNowSendOk   = false;

static void onDataSent(const uint8_t* mac, esp_now_send_status_t status) {
    espNowSendOk   = (status == ESP_NOW_SEND_SUCCESS);
    espNowSendDone = true;
}

bool espNowSend(const EspNowPayload& payload, uint8_t channel) {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);

    // Set the channel to match the gateway's WiFi connection
    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);

    if (esp_now_init() != ESP_OK) {
        Serial.println("ESP-NOW: sender init failed");
        return false;
    }
    esp_now_register_send_cb(onDataSent);

    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, ESPNOW_GATEWAY_MAC, 6);
    peer.channel = channel;
    peer.encrypt = false;
    esp_now_add_peer(&peer);

    for (int attempt = 1; attempt <= 3; attempt++) {
        espNowSendDone = false;
        espNowSendOk   = false;
        esp_now_send(ESPNOW_GATEWAY_MAC, (const uint8_t*)&payload, sizeof(payload));

        // Block until ACK or timeout
        uint32_t deadline = millis() + ESPNOW_SEND_TIMEOUT_MS;
        while (!espNowSendDone && (long)(millis() - deadline) < 0) {
            delay(1);
        }

        if (espNowSendOk) {
            Serial.printf("ESP-NOW: send OK (attempt %d)\n", attempt);
            break;
        }
        Serial.printf("ESP-NOW: send failed (attempt %d)\n", attempt);
        if (attempt < 3) delay(200); // brief pause before retry
    }

    esp_now_deinit();
    return espNowSendOk;
}
