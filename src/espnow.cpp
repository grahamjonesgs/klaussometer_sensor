#include "espnow.h"
#include "globals.h"
#include "network.h"
#include <esp_now.h>
#include <esp_wifi.h>
#include <WiFi.h>
#include <math.h>
#include <string.h>

// ── Gateway receiver ──────────────────────────────────────────────────────

static volatile bool espNowDataReady = false;
static EspNowPayload espNowRxBuf     = {};

// Runs in the WiFi task context — keep it short; just copy and set flag.
static void onDataReceived(const uint8_t* mac, const uint8_t* data, int len) {
    if ((size_t)len != sizeof(EspNowPayload)) return;
    memcpy((void*)&espNowRxBuf, data, sizeof(EspNowPayload));
    espNowDataReady = true;
}

void initEspNowGateway() {
    // WiFi must already be in WIFI_STA mode (done by setupWifi)
    if (esp_now_init() != ESP_OK) {
        Serial.println("ESP-NOW: gateway init failed");
        return;
    }
    esp_now_register_recv_cb(onDataReceived);
    Serial.println("ESP-NOW: gateway ready");
}

void handleEspNowReceived() {
    if (!espNowDataReady) return;

    EspNowPayload pkt;
    memcpy(&pkt, (const void*)&espNowRxBuf, sizeof(EspNowPayload));
    espNowDataReady = false;
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
    snprintf(debugBuf, sizeof(debugBuf),
             "V%s | ESP-NOW [%s] T:%.1f H:%.0f%% Bat:%.2fV Boot:%u Success:%u",
             FIRMWARE_VERSION,
             pkt.roomName, pkt.temperature, pkt.humidity,
             pkt.batteryVolts, pkt.bootCount, pkt.successCount);
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

void espNowSend(const EspNowPayload& payload, uint8_t channel) {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);

    // Set the channel to match the gateway's WiFi connection
    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);

    if (esp_now_init() != ESP_OK) {
        Serial.println("ESP-NOW: sender init failed");
        return;
    }
    esp_now_register_send_cb(onDataSent);

    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, ESPNOW_GATEWAY_MAC, 6);
    peer.channel = channel;
    peer.encrypt = false;
    esp_now_add_peer(&peer);

    espNowSendDone = false;
    espNowSendOk   = false;
    esp_now_send(ESPNOW_GATEWAY_MAC, (const uint8_t*)&payload, sizeof(payload));

    // Block until ACK or timeout
    uint32_t deadline = millis() + ESPNOW_SEND_TIMEOUT_MS;
    while (!espNowSendDone && (long)(millis() - deadline) < 0) {
        delay(1);
    }

    if (espNowSendOk) {
        Serial.println("ESP-NOW: send OK");
    } else {
        Serial.println("ESP-NOW: send failed or timed out");
    }

    esp_now_deinit();
}
