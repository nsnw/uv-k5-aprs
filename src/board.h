#ifndef BOARD_H
#define BOARD_H

#include <stdbool.h>
#include <stdint.h>

void BOARD_Init(void);
void BOARD_SetBacklight(uint8_t level);   // 0 = off .. 255 = full
void BOARD_SetAudioAmp(bool on);          // speaker amplifier enable
bool BOARD_IsPttPressed(void);
uint16_t BOARD_ReadBatteryRaw(void);     // SARADC counts, see battery.c

#endif
