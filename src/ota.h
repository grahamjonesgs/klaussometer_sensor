#ifndef OTA_H
#define OTA_H

#include "globals.h"

void   setupOtaWeb();
void   checkForUpdates();
void   updateFirmware();
String getUptime();
int    compareVersions(const String& v1, const String& v2);

#endif // OTA_H
