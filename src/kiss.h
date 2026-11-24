#ifndef KISS_H
#define KISS_H

#include <stdbool.h>
#include <stdint.h>

// KISS TNC on the programming cable (38400 8N1), port 0 only.
//
// While active, the serial line carries KISS instead of the text console:
//   radio -> host   every decoded AX.25 frame, as a data frame (0x00)
//   host -> radio   data frames go to the shared TX queue (txq.h: CSMA);
//                   TXDELAY (1), P (2), SlotTime (3) and TXtail (4) apply
//                   until KISS mode ends, FullDuplex/SetHardware are ignored;
//                   C0 FF C0 returns to the text console.
// Transmissions go through TX_SendFrame, so tx.h's limits (frequencies,
// time on air, power) still apply; refused frames are dropped.
//
// While active the radio is only a TNC: its own beacon, digipeater, message
// retries and replies are paused (the computer is the station); they carry
// on, with the same settings, when KISS mode ends.
//
// Whether it's on is saved (settings.h), so it comes back on at power-on.
//
// Entered with the console command `k`, by holding * in APRS mode, or
// automatically when the console receives a complete, valid KISS frame (see
// KISS_ProbeByte). Host data frames must be AX.25 UI frames (KISS_ValidAx25).

void KISS_Enter(void);
void KISS_Exit(void);
bool KISS_IsActive(void);

void KISS_PushByte(uint8_t b);          // one received serial byte

// Console mode: after a FEND (0xC0), feed the following bytes here. The first
// complete frame decides: a valid one (KISS_ValidAx25 data frame, or a
// TXDELAY / P / SlotTime / TXtail setting) switches to KISS and is handled;
// anything else is rejected and the console carries on. Line noise or a
// cable being plugged in no longer flips the radio into KISS mode.
typedef enum { KISS_PROBE_PENDING, KISS_PROBE_ACCEPTED, KISS_PROBE_REJECTED } KissProbe;
void      KISS_ProbeStart(void);
KissProbe KISS_ProbeByte(uint8_t b);
void      KISS_ProbeCancel(void);

// True for an AX.25 UI frame with PID 0xF0 (addresses .. info, no FCS).
// Data frames from the host that aren't are dropped, never transmitted.
bool KISS_ValidAx25(const uint8_t *frame, uint16_t len);
// Since power-on: data frames from the host, and those dropped here (not
// an allowed frame type, or the TX queue was full). Those the transmitter
// refuses later are TXQ_HostRefused().
typedef struct { uint16_t from_host, refused; } KissStats;
const KissStats *KISS_GetStats(void);

// Data moving on the cable lately (for the screen): KISS_FROM_HOST and/or
// KISS_TO_HOST, each held for KISS_ACTIVITY_TICKS after the last byte.
#define KISS_FROM_HOST 1
#define KISS_TO_HOST   2
#define KISS_ACTIVITY_TICKS 15             // 150 ms: visible for even one short frame
uint8_t KISS_Activity(void);

void KISS_Poll10ms(void);               // drain the UART
void KISS_SendFrame(const uint8_t *frame, uint16_t len);   // to the host

#endif
