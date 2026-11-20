#ifndef APRS_H
#define APRS_H

#include <stdbool.h>
#include <stdint.h>

#include "aprs_parse.h"

// APRS receive using the BK4819's FSK receiver as a raw Bell 202 bit slicer.
//
// The chip is put in FSK RX mode 4 with its sync word set to the NRZI image
// of a run of 0x7E flags (FE FE FE FE), CRC and scrambler off, and the
// longest packet length. Every APRS transmission's flag preamble triggers a
// sync; the next 256 bytes of raw tone bits land in the FIFO, 4 words per
// "almost full" interrupt. Those bits go through the software HDLC/AX.25
// decoder in ax25.c. Found experimentally with a probe tool during development.

#define APRS_FREQUENCY      14439000u  // 144.390 MHz (North America)
#define APRS_MAX_STATIONS   16
#define APRS_COMMENT_MAX    48

typedef struct {
    char     source[10];               // "VE6LY-7"
    char     via[10];                  // last digipeater that repeated it, "" = heard direct
    uint32_t heard_s;                  // APRS_NowSeconds() when last heard
    uint16_t packets;
    AprsInfo info;                     // from the latest packet
    char     comment[APRS_COMMENT_MAX];
} AprsStation;

void APRS_Enter(void);                 // at boot: stored frequency (else APRS_FREQUENCY), start receiving
bool APRS_IsActive(void);

void APRS_ConfigureChip(void);         // (re)program the BK4819 FSK receiver; after RADIO_Apply
void APRS_OnInterrupt(uint16_t status);
void APRS_Tick10ms(void);              // keeps the station clock running

uint32_t APRS_NowSeconds(void);
bool     APRS_IsReceiving(void);       // between sync and end of capture
uint16_t APRS_TotalDecoded(void);
uint8_t  APRS_StationCount(void);
const AprsStation *APRS_GetStation(uint8_t index);   // 0 = most recently heard
int      APRS_FindStation(const char *source);       // index or -1
bool     APRS_TakeNewPacket(void);     // true once after each decoded packet

#endif
