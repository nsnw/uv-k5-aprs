#ifndef RADIO_H
#define RADIO_H

#include <stdbool.h>
#include <stdint.h>

// All frequencies are in 10 Hz units (14550000 = 145.500 MHz).

#define RADIO_STEP_COUNT         9
#define RADIO_DEFAULT_STEP_INDEX 6   // 12.5 kHz

extern const uint16_t RADIO_STEP_TABLE[RADIO_STEP_COUNT];

bool RADIO_IsValidFrequency(uint32_t f);
bool RADIO_IsTunable(uint32_t f);        // valid, and (TX build) in an amateur TX band

void RADIO_Init(void);      // power up the BK4819 and apply gSettings
void RADIO_Apply(void);     // re-tune after any change to gSettings
void RADIO_Poll(void);      // call every 10 ms: services squelch interrupts

void RADIO_SetMonitor(bool on);   // force audio open regardless of squelch
bool RADIO_IsAudioOpen(void);
bool RADIO_IsMonitoring(void);

int16_t RADIO_GetRSSI_dBm(void);

#endif
