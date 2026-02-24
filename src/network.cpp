#include "network.h"
#include "ota.h"
#include <WiFi.h>

bool setupWifi() {
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
        if (WiFi.status() == WL_CONNECTED) {
            IPAddress ip = WiFi.localIP();
            char ipStr[16];
            snprintf(ipStr, sizeof(ipStr), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
            snprintf(debugBuf, sizeof(debugBuf), "WiFi is OK => IP address is: %s", ipStr);
            debugMessage(debugBuf, false);
        } else {
            delay(3000);
        }
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
            if (boardConfig.isBatteryPowered) {
                ESP.restart(); // caller (main) handles deep sleep after reconnect fails
            } else {
                ESP.restart();
            }
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
        delay(100);
        mqttClient.beginMessage(debugTopic, retain);
        mqttClient.printf("%s", fullMessageBuffer);
        mqttClient.endMessage();
        delay(100);
    }

    if (DEBUG_SERIAL) {
        Serial.println(fullMessageBuffer);
    }
}
