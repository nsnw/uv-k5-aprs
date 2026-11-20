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
// Entered with the console command `k`, by holding * in APRS mode, or
// automatically when the console sees a KISS frame (FEND, 0xC0).

void KISS_Enter(void);
void KISS_Exit(void);
bool KISS_IsActive(void);

void KISS_PushByte(uint8_t b);          // one received serial byte
void KISS_Poll10ms(void);               // drain the UART
void KISS_SendFrame(const uint8_t *frame, uint16_t len);   // to the host

#endif
