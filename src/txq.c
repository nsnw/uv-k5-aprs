#ifdef ENABLE_TX

#include <string.h>

#include "app.h"
#include "channel.h"
#include "tx.h"
#include "txq.h"

typedef struct {
    uint16_t    len;
    uint8_t     data[TX_MAX_FRAME];
} QueuedFrame;

static QueuedFrame sQueue[TXQ_SLOTS];
static uint8_t     sHead, sCount;
static uint8_t     sPersist = TXQ_DEFAULT_P;
static uint8_t     sSlot10ms = TXQ_DEFAULT_SLOT_10MS;
static uint8_t     sWait10ms;
static uint8_t     sRandom = 0x5A;

static uint8_t Random(void)                // xorshift8
{
    sRandom ^= (uint8_t)(sRandom << 3);
    sRandom ^= (uint8_t)(sRandom >> 5);
    sRandom ^= (uint8_t)(sRandom << 4);
    return sRandom;
}

bool TXQ_Push(const uint8_t *frame, uint16_t len)
{
    if (sCount >= TXQ_SLOTS || len > TX_MAX_FRAME)
        return false;
    // The frame being sent stays at sHead until TX_SendFrame returns, so a
    // push from the TX bit hook (KISS) lands in a different slot.
    QueuedFrame *q = &sQueue[(sHead + sCount) % TXQ_SLOTS];
    memcpy(q->data, frame, len);
    q->len   = len;
    sCount++;
    return true;
}

void TXQ_Clear(void)       { sCount = 0; sWait10ms = 0; }
uint8_t TXQ_Count(void)    { return sCount; }
void TXQ_SetCsma(uint8_t p, uint8_t slot) { sPersist = p; sSlot10ms = slot; }
void TXQ_ResetCsma(void)   { TXQ_SetCsma(TXQ_DEFAULT_P, TXQ_DEFAULT_SLOT_10MS); }

void TXQ_Poll10ms(void)
{
    if (sCount == 0)
        return;
    if (sWait10ms) {
        sWait10ms--;
        return;
    }
    if (CHANNEL_IsBusy())
        return;
    if (Random() > sPersist) {
        sWait10ms = sSlot10ms;
        return;
    }

    QueuedFrame *q = &sQueue[sHead];
    APP_SetTransmitting(true);          // drawn now: the screen freezes while on air
    const TxResult r = TX_SendFrame(q->data, q->len, NULL);
    APP_SetTransmitting(false);
    sHead = (sHead + 1) % TXQ_SLOTS;
    sCount--;
    if (r != TX_OK)
        APP_ShowMessage("TX REFUSED", false);
}

#else  // receive-only build

#include "txq.h"
bool TXQ_Push(const uint8_t *f, uint16_t n) { (void)f; (void)n; return false; }
void TXQ_Poll10ms(void) {}
void TXQ_Clear(void) {}
uint8_t TXQ_Count(void) { return 0; }
void TXQ_SetCsma(uint8_t p, uint8_t s) { (void)p; (void)s; }
void TXQ_ResetCsma(void) {}

#endif
