#ifndef NETWORK_H
#define NETWORK_H

#include "globals.h"

bool setupWifi();
void mqttReconnect();
void mqttSendFloat(const char* topic, float value);
void debugMessage(const char* message, bool retain);

#endif // NETWORK_H
