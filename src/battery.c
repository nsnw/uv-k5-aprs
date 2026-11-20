// Battery voltage from the SARADC, scaled with the radio's factory calibration
// (EEPROM 0x1F40, 6 x uint16; entry 3 is the reading at 7.60 V), as upstream
// helper/battery.c: volts_10mV = raw * 760 / cal[3]. Averaged over 4 samples.

#include "battery.h"
#include "board.h"
#include "driver/eeprom.h"

#define CAL_ADDR      0x1F40
#define CAL_DEFAULT   1900     // used if the calibration looks unprogrammed
#define SAMPLES       4

static uint16_t sCal760;       // ADC reading that corresponds to 7.60 V
static uint16_t sSamples[SAMPLES];
static uint8_t  sNext;

void BATTERY_Init(void)
{
    uint16_t cal[6];
    EEPROM_ReadBuffer(CAL_ADDR, cal, sizeof cal);
    sCal760 = (cal[3] >= 1000 && cal[3] < 5000) ? cal[3] : CAL_DEFAULT;

    const uint16_t raw = BOARD_ReadBatteryRaw();
    for (uint8_t i = 0; i < SAMPLES; i++)
        sSamples[i] = raw;
}

void BATTERY_Sample(void)
{
    sSamples[sNext] = BOARD_ReadBatteryRaw();
    sNext = (sNext + 1) % SAMPLES;
}

uint16_t BATTERY_Get10mV(void)
{
    uint32_t sum = 0;
    for (uint8_t i = 0; i < SAMPLES; i++)
        sum += sSamples[i];
    return (uint16_t)((sum / SAMPLES) * 760u / sCal760);
}
