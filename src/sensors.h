#ifndef SENSORS_H
#define SENSORS_H

#include "globals.h"

void        initScd41(int sdaPin, int sclPin);
SensorData  readDhtSensor();
float       readBatteryVoltage();
Pms5003Data readPms5003();
Scd41Data   readScd41();
Jsy194gData readJsy194g();

#endif // SENSORS_H
