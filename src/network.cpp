#include "network.h"
#include "ota.h"
#include <WiFi.h>

bool setupWifi() {
    bool wasConnected = (WiFi.status() == WL_CONNECTED);
    int counter = 1;
    while (WiFi.status() != WL_CONNECTED) {
        debugMessage("WiFi is not OK, reconnecting", false);
        if (counter > WIFI_RETRIES) {
            snprintf(debugBuf, sizeof(debugBuf), "WiFi connection failed after %d retries.", WIFI_RETRIES);
            debugMessage(debugBuf, false);
            return false;
        }
        WiFi.disconnect();
        WiFi.mode(WIFI_STA);
        WiFi.enableSTA(true);
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        snprintf(debugBuf, sizeof(debugBuf), "Attempt %d to connect to WiFi...", counter);
        debugMessage(debugBuf, false);
        delay(1000);
        counter++;
        if (WiFi.status() != WL_CONNECTED) {
            delay(3000);
        }
    }

    if (!wasConnected) {
        Serial.print("WiFi connected. IP: ");
        Serial.println(WiFi.localIP());
    }

    if (!boardConfig.isBatteryPowered) {
        static bool webServerStarted = false;
        if (!webServerStarted) {
            setupOtaWeb();
            webServerStarted = true;
        }
    }
    return true;
}

void mqttReconnect() {
    int counter = 1;
    while (!mqttClient.connected()) {
        if (counter > MQTT_RETRIES) {
            snprintf(debugBuf, sizeof(debugBuf), "[Error] MQTT connection failed after %d retries.", MQTT_RETRIES);
            debugMessage(debugBuf, false);
            ESP.restart();
        }
        debugMessage("Connecting to MQTT broker...", false);
        if (mqttClient.connect(MQTT_SERVER, MQTT_PORT)) {
            debugMessage("MQTT link OK", false);
        } else {
            debugMessage("[Error] MQTT not connected", false);
            delay(3000);
        }
        counter++;
    }
}

void mqttSendFloat(const char* topic, float value) {
    mqttClient.beginMessage(topic);
    mqttClient.printf("%.2f", value);
    mqttClient.endMessage();
}

void debugMessage(const char* message, bool retain) {
    char fullMessageBuffer[256];
    snprintf(fullMessageBuffer, sizeof(fullMessageBuffer), "V%s | %s", FIRMWARE_VERSION, message);

    if (DEBUG_MQTT) {
        mqttClient.beginMessage(debugTopic, retain);
        mqttClient.printf("%s", fullMessageBuffer);
        mqttClient.endMessage();
    }

    if (DEBUG_SERIAL) {
        Serial.println(fullMessageBuffer);
    }
}
