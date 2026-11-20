#include <string.h>

#include "app.h"
#include "kiss.h"
#include "station.h"
#include "txq.h"
#include "driver/uart.h"

#ifdef ENABLE_TX
#include "tx.h"
#define KISS_MAX_FRAME  TX_MAX_FRAME
#else
#define KISS_MAX_FRAME  330
#endif

#define FEND   0xC0
#define FESC   0xDB
#define TFEND  0xDC
#define TFESC  0xDD

#define CMD_DATA        0x0
#define CMD_TXDELAY     0x1
#define CMD_P           0x2
#define CMD_SLOTTIME    0x3
#define CMD_TXTAIL      0x4
#define CMD_RETURN      0xFF

#define DRAIN_PER_CALL  16      // bytes per KISS_Poll / TX bit hook call
#define FLAG_X100_MS    667     // one 0x7E flag = 8 bits at 1200 baud = 6.67 ms

static bool sActive;
static bool sExitRequested;     // C0 FF C0 seen; handled outside the TX hook
static uint8_t sP = TXQ_DEFAULT_P, sSlot = TXQ_DEFAULT_SLOT_10MS;

// receive state (host -> radio)
static uint8_t  sIn[1 + KISS_MAX_FRAME];   // command byte + frame
static uint16_t sInLen;
static bool     sInFrame, sEscape, sInBad;

#ifdef ENABLE_TX
static uint8_t TenMsToFlags(uint8_t v, uint8_t max)
{
    const uint32_t flags = ((uint32_t)v * 1000u + FLAG_X100_MS - 1) / FLAG_X100_MS;
    return (uint8_t)(flags < 1 ? 1 : flags > max ? max : flags);
}
#endif

// ------------------------------------------------------------ host -> radio

static void HandleFrame(const uint8_t *p, uint16_t n)
{
    const uint8_t cmd = p[0];
    if (cmd == CMD_RETURN) {
        sExitRequested = true;
        return;
    }
    if (cmd >> 4)                          // only port 0
        return;
    // Parameters last until KISS mode ends, then the station's own apply.
    switch (cmd & 0x0F) {
    case CMD_DATA:
        TXQ_Push(p + 1, n - 1);   // full: dropped (KISS has no flow control)
        break;
#ifdef ENABLE_TX
    case CMD_TXDELAY:
        if (n >= 2) gTxConfig.preamble_flags = TenMsToFlags(p[1], 200);
        break;
    case CMD_TXTAIL:
        if (n >= 2) gTxConfig.tail_flags = TenMsToFlags(p[1], 50);
        break;
#endif
    case CMD_P:
        if (n >= 2) { sP = p[1]; TXQ_SetCsma(sP, sSlot); }
        break;
    case CMD_SLOTTIME:
        if (n >= 2) { sSlot = p[1]; TXQ_SetCsma(sP, sSlot); }
        break;
    default:                               // FullDuplex, SetHardware
        break;
    }
}

void KISS_PushByte(uint8_t b)
{
    if (b == FEND) {
        if (sInFrame && sInLen > 0 && !sInBad)
            HandleFrame(sIn, sInLen);
        sInFrame = true;                   // a closing FEND also opens the next frame
        sInLen   = 0;
        sEscape  = false;
        sInBad   = false;
        return;
    }
    if (!sInFrame || sInBad)
        return;
    if (sEscape) {
        sEscape = false;
        if (b == TFEND)      b = FEND;
        else if (b == TFESC) b = FESC;
        else { sInBad = true; return; }
    } else if (b == FESC) {
        sEscape = true;
        return;
    }
    if (sInLen < sizeof sIn)
        sIn[sInLen++] = b;
    else
        sInBad = true;                     // too long to transmit anyway
}

// ------------------------------------------------------------ radio -> host

void KISS_SendFrame(const uint8_t *frame, uint16_t len)
{
    static const uint8_t start[2] = { FEND, CMD_DATA };
    static const uint8_t esc_fend[2] = { FESC, TFEND };
    static const uint8_t esc_fesc[2] = { FESC, TFESC };
    UART_Send(start, 2);
    uint16_t run = 0;                      // bytes not needing escapes, sent in runs
    for (uint16_t i = 0; i < len; i++) {
        if (frame[i] != FEND && frame[i] != FESC)
            continue;
        UART_Send(frame + run, i - run);
        UART_Send(frame[i] == FEND ? esc_fend : esc_fesc, 2);
        run = i + 1;
    }
    UART_Send(frame + run, len - run);
    UART_Send(start, 1);
}

// ------------------------------------------------------------ mode

static void Drain(void)                    // also the TX bit hook: keep it short
{
    // Stop at C0 FF C0 so the console gets whatever follows it.
    int c;
    for (int i = 0; i < DRAIN_PER_CALL && !sExitRequested && (c = UART_ReadByte()) >= 0; i++)
        KISS_PushByte((uint8_t)c);
}

void KISS_Enter(void)
{
    if (sActive)
        return;
    sActive        = true;
    sExitRequested = false;
#ifdef ENABLE_TX
    TX_SetBitHook(Drain);   // the UART ring holds ~67 ms; a frame takes up to 3 s
#endif
    APP_Redraw();
}

void KISS_Exit(void)
{
    if (!sActive)
        return;
    sActive  = false;
    sInFrame = false;
#ifdef ENABLE_TX
    TX_SetBitHook(NULL);
#endif
    sP = TXQ_DEFAULT_P;
    sSlot = TXQ_DEFAULT_SLOT_10MS;
    TXQ_ResetCsma();
    STATION_ApplyTxTiming();
    APP_Redraw();
}

bool KISS_IsActive(void) { return sActive; }

void KISS_Poll10ms(void)
{
    // Up to 160 bytes per tick (the line delivers ~38): keeps up easily.
    for (int i = 0; i < 10 && !sExitRequested; i++)
        Drain();
    if (sExitRequested)
        KISS_Exit();
}
