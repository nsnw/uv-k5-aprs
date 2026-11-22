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

#define APRS_DIRECT_WINDOW_S 30      // a digipeated copy this soon after a direct one is the same packet
#define APRS_ST_DIRECT       (1u << 0) // its latest packet was heard direct (see APRS_DIRECT_WINDOW_S)
#define APRS_ST_OLD          (1u << 1) // restored from EEPROM at boot: heard before power-off

typedef struct {
    char     source[10];               // "VE6LY-7"
    char     via[10];                  // the digipeater we last heard it through (not WIDEn etc.), "" = none
    uint32_t heard_s;                  // APRS_NowSeconds() when last heard
    uint32_t direct_s;                 // ... and last heard direct
    uint16_t packets;
    uint8_t  flags;                    // APRS_ST_*
    uint8_t  reserved;
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
const char *APRS_LastPacket(uint32_t *heard_s);   // TNC2 line of the latest frame, "" if none
bool     APRS_TakeNewPacket(void);     // true once after each decoded packet

// What the latest packet was, for the RX beep: a station not in the list (or
// only from before power-off), a new packet from a known one, or a repeat
// (the same position and comment within APRS_DIRECT_WINDOW_S: a digipeated copy).
typedef enum { APRS_RX_REPEAT, APRS_RX_PACKET, APRS_RX_NEW_STATION } AprsRxKind;
AprsRxKind APRS_LastRxKind(void);

// "Directs= CALL CALL ...": stations heard direct in the last 30 minutes, for
// the ?APRSD query (msg.c). Up to `size` - 1 characters.
void APRS_FormatDirects(char *out, uint8_t size);

// For store.h: the station table (most recently heard first) and its count,
// and the fix-up for an entry read back from EEPROM (false: drop it).
AprsStation *APRS_StationTable(uint8_t **count);
bool APRS_RestoreStation(AprsStation *st);

// The via logic, for the host test: the callsign of the last digipeater that
// repeated the frame, skipping generic aliases (WIDEn, TRACEn, RELAY ...).
// "" if heard direct or only aliases are marked. *direct: no digipeater marked.
void APRS_FindVia(const uint8_t *frame, uint16_t len, char *out, uint8_t size, bool *direct);

#endif
