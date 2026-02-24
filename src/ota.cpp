#include "ota.h"
#include "html.h"
#include "network.h"
#include <HTTPClient.h>
#include <Update.h>
#include <WiFi.h>

String getUptime() {
    unsigned long uptimeMs = millis();
    unsigned long seconds  = uptimeMs / 1000;

    unsigned long days             = seconds / (24 * 3600);
    unsigned long hours            = (seconds % (24 * 3600)) / 3600;
    unsigned long minutes          = (seconds % 3600) / 60;
    unsigned long remainingSeconds = seconds % 60;

    const char* dayLabel = (days == 1) ? " day" : " days";
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "%lu%s, %02lu:%02lu:%02lu", days, dayLabel, hours, minutes, remainingSeconds);
    return String(buffer);
}

int compareVersions(const String& v1, const String& v2) {
    int i = 0, j = 0;
    while (i < (int)v1.length() || j < (int)v2.length()) {
        int num1 = 0, num2 = 0;
        while (i < (int)v1.length() && v1[i] != '.') { num1 = num1 * 10 + (v1[i] - '0'); i++; }
        while (j < (int)v2.length() && v2[j] != '.') { num2 = num2 * 10 + (v2[j] - '0'); j++; }
        if (num1 > num2) return  1;
        if (num1 < num2) return -1;
        i++;
        j++;
    }
    return 0;
}

void setupOtaWeb() {
    webServer.on("/", HTTP_GET, []() {
        String content;
        content.reserve(1024);
        content = "<p class='section-title'>Device Information</p>"
                  "<table class='data-table'>"
                  "<tr><td><b>Firmware Version:</b></td><td>" + String(FIRMWARE_VERSION) +
                  "</td></tr>"
                  "<tr><td><b>MAC Address:</b></td><td>" + macAddress +
                  "</td></tr>"
                  "<tr><td><b>Room:</b></td><td>" + String(boardConfig.displayName) +
                  "</td></tr>"
                  "<tr><td><b>Uptime:</b></td><td><span id=\"uptime\">" + getUptime().c_str() +
                  "</span></td></tr>"
                  "</table>";

        content += "<p class='section-title'>Current Readings</p>"
                   "<table class='data-table'>";

        if (strncmp(lastReadingTimeStr, "N/A", 3) == 0) {
            content += "<tr><td><b>Last Update Time:</b></td><td><span id=\"time\">N/A</span></td></tr>"
                       "<tr><td><b>Temperature:</b></td><td><span id=\"temp\">N/A</span></td></tr>"
                       "<tr><td><b>Humidity:</b></td><td><span id=\"humid\">N/A</span></td></tr>";
            if (boardConfig.isBatteryPowered) {
                content += "<tr><td><b>Battery Voltage:</b></td><td><span id=\"voltage\">N/A</span></td></tr>";
            }
        } else {
            content += "<tr><td><b>Last Update Time:</b></td><td><span id=\"time\">" + String(lastReadingTimeStr) +
                       "</span></td></tr>"
                       "<tr><td><b>Temperature:</b></td><td><span id=\"temp\">" + String(lastTemp, 1) +
                       "</span> &deg;C</td></tr>"
                       "<tr><td><b>Humidity:</b></td><td><span id=\"humid\">" + String(lastHumid, 0) +
                       "</span> %</td></tr>";
            if (boardConfig.isBatteryPowered) {
                content += "<tr><td><b>Battery Voltage:</b></td><td><span id=\"voltage\">" +
                           String(lastVolts, 2) + "</span> V</td></tr>";
            }
        }

        content += "</table>";

        String html;
        html.reserve(1024);
        html = info_html;
        html.replace("{{content}}", content);
        webServer.send(200, "text/html", html);
    });

    webServer.on("/data", HTTP_GET, []() {
        char dataBuffer[256];
        if (strncmp(lastReadingTimeStr, "N/A", 3) == 0) {
            snprintf(dataBuffer, sizeof(dataBuffer),
                     "{\"temperature\":\"%s\", \"humidity\":\"%s\", \"voltage\":%.2f, \"time\":\"%s\", \"uptime\":\"%s\"}",
                     "N/A", "N/A", lastVolts, lastReadingTimeStr, getUptime().c_str());
        } else {
            snprintf(dataBuffer, sizeof(dataBuffer),
                     "{\"temperature\":%.1f, \"humidity\":%.0f, \"voltage\":%.2f, \"time\":\"%s\", \"uptime\":\"%s\"}",
                     lastTemp, lastHumid, lastVolts, lastReadingTimeStr, getUptime().c_str());
        }
        webServer.send(200, "application/json", dataBuffer);
    });

    webServer.on("/update", HTTP_GET, []() {
        String htmlPage;
        htmlPage.reserve(1024);
        htmlPage = ota_html;
        htmlPage.replace("{{FIRMWARE_VERSION}}", FIRMWARE_VERSION);
        webServer.send(200, "text/html", htmlPage);
    });

    webServer.on(
        "/update", HTTP_POST,
        []() {
            webServer.sendHeader("Connection", "close");
            webServer.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
            delay(1000);
            ESP.restart();
        },
        []() {
            HTTPUpload& upload = webServer.upload();
            if (upload.status == UPLOAD_FILE_START) {
                if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
                    Update.printError(Serial);
                }
            } else if (upload.status == UPLOAD_FILE_WRITE) {
                if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
                    Update.printError(Serial);
                }
            } else if (upload.status == UPLOAD_FILE_END) {
                if (Update.end(true)) {
                    debugMessage("Update Success, rebooting...", true);
                    delay(1000);
                } else {
                    Update.printError(Serial);
                }
            }
        });

    webServer.begin();
}

void checkForUpdates() {
    if (WiFi.status() != WL_CONNECTED) {
        debugMessage("WiFi not connected. Cannot check for updates.", false);
        return;
    }

    HTTPClient http;
    char versionUrl[256];
    snprintf(versionUrl, sizeof(versionUrl), "https://%s:%d%s", OTA_HOST, OTA_PORT, OTA_VERSION_PATH);
    http.begin(versionUrl);
    int httpCode = http.GET();
    if (httpCode == HTTP_CODE_OK) {
        String serverVersion = http.getString();
        serverVersion.trim();
        if (compareVersions(serverVersion, FIRMWARE_VERSION) > 0) {
            snprintf(debugBuf, sizeof(debugBuf), "New firmware available: %s (current: %s)",
                     serverVersion.c_str(), FIRMWARE_VERSION);
            debugMessage(debugBuf, true);
            updateFirmware();
        }
    } else {
        debugMessage("Error fetching version file.", true);
    }
    http.end();
}

void updateFirmware() {
    HTTPClient http;
    char binUrl[256];
    snprintf(binUrl, sizeof(binUrl), "https://%s:%d%s", OTA_HOST, OTA_PORT, OTA_BIN_PATH);
    http.begin(binUrl);
    int httpCode = http.GET();
    if (httpCode == HTTP_CODE_OK) {
        int  contentLength = http.getSize();
        bool canBegin      = Update.begin(contentLength);
        if (canBegin) {
            debugMessage("Beginning OTA update. This may take a few moments...", true);
            WiFiClient& client  = http.getStream();
            size_t      written = Update.writeStream(client);
            if (written == (size_t)contentLength) {
                debugMessage("OTA update written successfully.", true);
            } else {
                debugMessage("OTA update failed to write completely.", true);
            }
            if (Update.end()) {
                debugMessage("Update finished successfully. Restarting...", true);
                ESP.restart();
            } else {
                snprintf(debugBuf, sizeof(debugBuf), "Error during OTA update: %s", Update.errorString());
                debugMessage(debugBuf, true);
            }
        } else {
            debugMessage("Not enough space to start OTA update.", true);
        }
    } else {
        snprintf(debugBuf, sizeof(debugBuf), "HTTP GET failed, error: %d", httpCode);
        debugMessage(debugBuf, true);
    }
    http.end();
}
