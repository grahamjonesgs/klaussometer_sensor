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

// Helper: append a table row with a span-wrapped value that AJAX can update
static void addRow(String& s, const char* label, const char* id, const String& value, const char* unit = "") {
    s += "<tr><td><b>";
    s += label;
    s += ":</b></td><td><span id='";
    s += id;
    s += "'>";
    s += value;
    s += "</span>";
    if (unit[0]) { s += " "; s += unit; }
    s += "</td></tr>";
}

void setupOtaWeb() {
    webServer.on("/", HTTP_GET, []() {
        String content;
        content.reserve(3072);

        // ── Device Information ──────────────────────────────────────────────
        content += "<p class='section-title'>Device Information</p>"
                   "<table class='data-table'>";
        content += "<tr><td><b>Firmware Version:</b></td><td>" + String(FIRMWARE_VERSION) + "</td></tr>";
        content += "<tr><td><b>MAC Address:</b></td><td>" + String(macAddress) + "</td></tr>";
        content += "<tr><td><b>Room:</b></td><td>" + String(boardConfig.displayName) + "</td></tr>";
        content += "<tr><td><b>Uptime:</b></td><td><span id='uptime'>" + getUptime() + "</span></td></tr>";
        content += "</table>";

        // ── Supported Sensors ───────────────────────────────────────────────
        content += "<p class='section-title'>Supported Sensors</p>"
                   "<table class='data-table'>"
                   "<tr><td><b>Sensor</b></td><td><b>Measures</b></td></tr>";
        if (boardConfig.sensors & SENSOR_DHT)
            content += "<tr><td>DHT22</td><td>Temperature &amp; Humidity</td></tr>";
        if (boardConfig.sensors & SENSOR_SCD41)
            content += "<tr><td>SCD41</td><td>CO&#8322; / Temperature / Humidity</td></tr>";
        if (boardConfig.sensors & SENSOR_PMS5003)
            content += "<tr><td>PMS5003</td><td>PM1.0 / PM2.5 / PM10</td></tr>";
        if (boardConfig.sensors & SENSOR_JSY194G)
            content += "<tr><td>JSY-MK-194G</td><td>AC Voltage / Current / Power</td></tr>";
        content += "</table>";

        // ── Current Readings ────────────────────────────────────────────────
        content += "<p class='section-title'>Current Readings</p>"
                   "<table class='data-table'>";
        addRow(content, "Last Update", "time", String(lastReadingTimeStr));

        // Temperature / Humidity (DHT or SCD41 fallback)
        bool hasDht = (boardConfig.sensors & SENSOR_DHT) && (strcmp(lastReadingTimeStr, "N/A") != 0);
        bool hasScdTempHumid = (boardConfig.sensors & SENSOR_SCD41) && !(boardConfig.sensors & SENSOR_DHT) && lastScd41Data.success;
        if (hasDht || hasScdTempHumid) {
            addRow(content, "Temperature", "temp",  String(lastTemp, 1),  "&deg;C");
            addRow(content, "Humidity",    "humid", String(lastHumid, 0), "%");
        } else if ((boardConfig.sensors & SENSOR_DHT) || (boardConfig.sensors & SENSOR_SCD41)) {
            addRow(content, "Temperature", "temp",  "N/A", "");
            addRow(content, "Humidity",    "humid", "N/A", "");
        }

        // Battery
        if (boardConfig.isBatteryPowered) {
            String voltStr = (lastVolts > 0.0f) ? String(lastVolts, 2) : "N/A";
            addRow(content, "Battery Voltage", "voltage", voltStr, lastVolts > 0.0f ? "V" : "");
        }

        // SCD41 CO2
        if (boardConfig.sensors & SENSOR_SCD41) {
            String co2Str = lastScd41Data.success ? String((int)lastScd41Data.co2) : "N/A";
            addRow(content, "CO&#8322;", "co2", co2Str, lastScd41Data.success ? "ppm" : "");
        }

        // PMS5003
        if (boardConfig.sensors & SENSOR_PMS5003) {
            String pm1Str  = lastPmsData.success ? String((int)lastPmsData.pm1)  : "N/A";
            String pm25Str = lastPmsData.success ? String((int)lastPmsData.pm25) : "N/A";
            String pm10Str = lastPmsData.success ? String((int)lastPmsData.pm10) : "N/A";
            const char* pmUnit = lastPmsData.success ? "&micro;g/m&#179;" : "";
            addRow(content, "PM1.0",  "pm1",  pm1Str,  pmUnit);
            addRow(content, "PM2.5",  "pm25", pm25Str, pmUnit);
            addRow(content, "PM10",   "pm10", pm10Str, pmUnit);
        }

        // JSY-MK-194G
        if (boardConfig.sensors & SENSOR_JSY194G) {
            if (lastJsyData.success) {
                addRow(content, "AC Voltage",    "acVoltage", String(lastJsyData.voltage, 1),     "V");
                addRow(content, "AC Current",    "acCurrent", String(lastJsyData.current, 2),     "A");
                addRow(content, "AC Power",      "acPower",   String(lastJsyData.power, 1),       "W");
                addRow(content, "Power Factor",  "acPf",      String(lastJsyData.powerFactor, 3), "");
                addRow(content, "Frequency",     "acFreq",    String(lastJsyData.frequency, 2),   "Hz");
                addRow(content, "Energy",        "acEnergy",  String(lastJsyData.energy, 3),      "kWh");
            } else {
                addRow(content, "AC Voltage",   "acVoltage", "N/A", "");
                addRow(content, "AC Current",   "acCurrent", "N/A", "");
                addRow(content, "AC Power",     "acPower",   "N/A", "");
                addRow(content, "Power Factor", "acPf",      "N/A", "");
                addRow(content, "Frequency",    "acFreq",    "N/A", "");
                addRow(content, "Energy",       "acEnergy",  "N/A", "");
            }
        }

        content += "</table>";

        String html;
        html.reserve(3072);
        html = info_html;
        html.replace("{{content}}", content);
        webServer.send(200, "text/html", html);
    });

    // ── /data — JSON for AJAX refresh ──────────────────────────────────────
    webServer.on("/data", HTTP_GET, []() {
        String json = "{";
        json += "\"time\":\"" + String(lastReadingTimeStr) + "\",";
        json += "\"uptime\":\"" + getUptime() + "\",";

        // Temperature / Humidity
        bool hasDhtReading = (boardConfig.sensors & SENSOR_DHT) && (strcmp(lastReadingTimeStr, "N/A") != 0);
        bool hasScdReading = (boardConfig.sensors & SENSOR_SCD41) && !(boardConfig.sensors & SENSOR_DHT) && lastScd41Data.success;
        if (hasDhtReading || hasScdReading) {
            json += "\"temperature\":" + String(lastTemp, 1) + ",";
            json += "\"humidity\":"    + String(lastHumid, 0) + ",";
        } else {
            json += "\"temperature\":\"N/A\",";
            json += "\"humidity\":\"N/A\",";
        }

        // Battery
        json += "\"voltage\":" + String(lastVolts, 2) + ",";

        // SCD41 CO2
        if (lastScd41Data.success) {
            json += "\"co2\":" + String((int)lastScd41Data.co2) + ",";
        } else {
            json += "\"co2\":\"N/A\",";
        }

        // PMS5003
        if (lastPmsData.success) {
            json += "\"pm1\":"  + String((int)lastPmsData.pm1)  + ",";
            json += "\"pm25\":" + String((int)lastPmsData.pm25) + ",";
            json += "\"pm10\":" + String((int)lastPmsData.pm10) + ",";
        } else {
            json += "\"pm1\":\"N/A\",";
            json += "\"pm25\":\"N/A\",";
            json += "\"pm10\":\"N/A\",";
        }

        // JSY-MK-194G (last field — no trailing comma)
        if (lastJsyData.success) {
            json += "\"acVoltage\":" + String(lastJsyData.voltage, 1)     + ",";
            json += "\"acCurrent\":" + String(lastJsyData.current, 2)     + ",";
            json += "\"acPower\":"   + String(lastJsyData.power, 1)       + ",";
            json += "\"acPf\":"      + String(lastJsyData.powerFactor, 3) + ",";
            json += "\"acFreq\":"    + String(lastJsyData.frequency, 2)   + ",";
            json += "\"acEnergy\":"  + String(lastJsyData.energy, 3);
        } else {
            json += "\"acVoltage\":\"N/A\",";
            json += "\"acCurrent\":\"N/A\",";
            json += "\"acPower\":\"N/A\",";
            json += "\"acPf\":\"N/A\",";
            json += "\"acFreq\":\"N/A\",";
            json += "\"acEnergy\":\"N/A\"";
        }

        json += "}";
        webServer.send(200, "application/json", json);
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
