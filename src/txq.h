#ifndef TXQ_H
#define TXQ_H

#include <stdbool.h>
#include <stdint.h>

// Transmit queue shared by KISS, the digipeater and the beacon. Frames
// (AX.25 without FCS) wait for a clear channel (channel.h), then go out
// with p-persistent CSMA: each slot, transmit with probability (P+1)/256.
// Everything still passes through TX_SendFrame and its limits.

#define TXQ_SLOTS       3
#define TXQ_DEFAULT_P   63
#define TXQ_DEFAULT_SLOT_10MS 10

// The status icon shows T while a frame is on air. False if the queue is
// full or the frame too long.
bool TXQ_Push(const uint8_t *frame, uint16_t len);
// The same for frames this station originates (beacon, messages, acks,
// query answers), as opposed to digipeated or KISS frames: once one has been
// sent, TXQ_TakeLocalSent() returns true (once), for the TX beep.
bool TXQ_PushLocal(const uint8_t *frame, uint16_t len);
bool TXQ_TakeLocalSent(void);
// Frames from the KISS host: counted when TX_SendFrame refuses them
// (frequency, time on air ...), for the KISS page.
bool     TXQ_PushHost(const uint8_t *frame, uint16_t len);
uint16_t TXQ_HostRefused(void);
void TXQ_Poll10ms(void);
void TXQ_Clear(void);
uint8_t TXQ_Count(void);

void TXQ_SetCsma(uint8_t persist, uint8_t slot_10ms);   // KISS P / SlotTime
void TXQ_ResetCsma(void);

#endif
