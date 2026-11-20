#ifndef BATTERY_H
#define BATTERY_H

#include <stdint.h>

void     BATTERY_Init(void);      // read calibration, take initial samples
void     BATTERY_Sample(void);    // call every 500 ms
uint16_t BATTERY_Get10mV(void);   // averaged battery voltage, 10 mV units (784 = 7.84 V)

#endif
