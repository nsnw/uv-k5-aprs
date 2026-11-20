// Host test for kiss.c: framing both ways, escapes, parameters, the TX
// queue and CSMA, with the UART, radio and transmitter stubbed out.
//   cc -DENABLE_TX -I . -I src -o build/test_kiss test/test_kiss.c src/kiss.c src/txq.c && build/test_kiss

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "channel.h"
#include "driver/uart.h"
#include "kiss.h"
#include "station.h"
#include "tx.h"
#include "txq.h"

// ---------------------------------------------------------------- stubs

static uint8_t  sOut[2048];
static unsigned sOutLen;
static uint8_t  sInBuf[2048];
static unsigned sInLen, sInPos;
static bool     sBusy;
static uint8_t  sTxFrame[TX_MAX_FRAME];
static uint16_t sTxLen;
static int      sTxCount;
static uint8_t  sTxFlags;
static void   (*sHook)(void);
static const char *sLastMsg;

TxConfig gTxConfig = { 66, 0xFF, 40, 3 };
void STATION_ApplyTxTiming(void) { gTxConfig.preamble_flags = 40; gTxConfig.tail_flags = 3; }
void UART_Send(const void *p, uint32_t n) { memcpy(sOut + sOutLen, p, n); sOutLen += n; }
int  UART_ReadByte(void) { return sInPos < sInLen ? sInBuf[sInPos++] : -1; }
void APP_Redraw(void) {}
void APP_ShowMessage(const char *m, bool now) { (void)now; if (m) sLastMsg = m; }
static int sTxIndicator;
void APP_SetTransmitting(bool on) { sTxIndicator += on ? 1 : 0; }
bool CHANNEL_IsBusy(void) { return sBusy; }
void TX_SetBitHook(void (*h)(void)) { sHook = h; }
TxResult TX_SendFrame(const uint8_t *f, uint16_t n, uint16_t *ms)
{
    (void)ms;
    memcpy(sTxFrame, f, n);
    sTxLen = n;
    sTxCount++;
    sTxFlags = gTxConfig.preamble_flags;
    return TX_OK;
}

// ---------------------------------------------------------------- helpers

static int sFail, sPass;
#define CHECK(cond, ...) do { if (cond) sPass++; else { sFail++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void HostSends(const uint8_t *p, unsigned n) { memcpy(sInBuf + sInLen, p, n); sInLen += n; }

static void RunTicks(int n) { for (int i = 0; i < n; i++) { KISS_Poll10ms(); TXQ_Poll10ms(); } }

static uint8_t Frame[40];
static void MakeFrame(void)
{
    for (unsigned i = 0; i < sizeof Frame; i++) Frame[i] = (uint8_t)(0x40 + i);
    Frame[5] = 0xC0;   // needs escaping
    Frame[9] = 0xDB;   // ditto
}

int main(void)
{
    MakeFrame();

    // ------------------------------------------------ radio -> host
    KISS_Enter();
    CHECK(KISS_IsActive(), "active after enter");
    CHECK(sHook != NULL, "TX bit hook installed");
    KISS_SendFrame(Frame, sizeof Frame);
    CHECK(sOut[0] == 0xC0 && sOut[1] == 0x00 && sOut[sOutLen - 1] == 0xC0, "framing");
    CHECK(sOutLen == sizeof Frame + 3 + 2, "two escapes add 2 bytes (got %u)", sOutLen);
    CHECK(sOut[2 + 5] == 0xDB && sOut[2 + 6] == 0xDC, "FEND escaped");
    CHECK(sOut[2 + 10] == 0xDB && sOut[2 + 11] == 0xDD, "FESC escaped");

    // ------------------------------------------------ host -> radio: our own encoding round-trips
    HostSends(sOut, sOutLen);
    sOutLen = 0;
    sBusy = false;
    RunTicks(200);   // P = 63: transmits within a few slots
    CHECK(sTxCount == 1, "one frame sent (got %d)", sTxCount);
    CHECK(sTxLen == sizeof Frame && memcmp(sTxFrame, Frame, sizeof Frame) == 0, "frame unescaped intact");
    CHECK(sTxIndicator == 1, "TX indicator shown");

    // ------------------------------------------------ TXDELAY: 30 (300 ms) -> 45 flags
    const uint8_t txdelay[] = { 0xC0, 0x01, 30, 0xC0 };
    HostSends(txdelay, sizeof txdelay);
    RunTicks(1);
    CHECK(gTxConfig.preamble_flags == 45, "TXDELAY 300 ms -> 45 flags (got %u)", gTxConfig.preamble_flags);

    // ------------------------------------------------ busy channel holds the frame; P = 255 sends at once
    const uint8_t p255[] = { 0xC0, 0x02, 0xFF, 0xC0 };
    HostSends(p255, sizeof p255);
    sBusy = true;
    const uint8_t data[] = { 0xC0, 0x00, 1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16, 0xC0 };
    HostSends(data, sizeof data);
    RunTicks(500);
    CHECK(sTxCount == 1, "nothing sent while busy");
    sBusy = false;
    RunTicks(1);
    CHECK(sTxCount == 2 && sTxLen == 16 && sTxFlags == 45, "sent once clear, with TXDELAY");

    // ------------------------------------------------ other ports and bad escapes are ignored
    const uint8_t port1[] = { 0xC0, 0x10, 1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16, 0xC0 };
    const uint8_t badesc[] = { 0xC0, 0x00, 1,2,3,0xDB,0x41,5,6,7,8,9,10,11,12,13,14,15, 0xC0 };
    HostSends(port1, sizeof port1);
    HostSends(badesc, sizeof badesc);
    RunTicks(50);
    CHECK(sTxCount == 2, "port 1 and bad escape dropped (sent %d)", sTxCount);

    // ------------------------------------------------ TXtail: 10 (100 ms) -> 15 flags
    const uint8_t txtail[] = { 0xC0, 0x04, 10, 0xC0 };
    HostSends(txtail, sizeof txtail);
    RunTicks(1);
    CHECK(gTxConfig.tail_flags == 15, "TXtail 100 ms -> 15 flags (got %u)", gTxConfig.tail_flags);

    // ------------------------------------------------ queue holds TXQ_SLOTS frames; more are dropped
    sBusy = true;
    for (int i = 0; i < TXQ_SLOTS + 1; i++)
        HostSends(data, sizeof data);
    RunTicks(5);
    CHECK(TXQ_Count() == TXQ_SLOTS, "queue full at %d (got %u)", TXQ_SLOTS, TXQ_Count());
    sBusy = false;
    RunTicks(10);
    CHECK(sTxCount == 2 + TXQ_SLOTS, "queued frames sent, extra dropped (sent %d)", sTxCount);

    // ------------------------------------------------ bytes arriving during TX via the bit hook
    sInLen = sInPos = 0;
    HostSends(data, sizeof data);
    for (int i = 0; i < 10; i++) sHook();
    RunTicks(1);
    CHECK(sTxCount == 3 + TXQ_SLOTS, "frame read by the bit hook gets sent");

    // ------------------------------------------------ C0 FF C0 leaves KISS and restores TXDELAY
    const uint8_t ret[] = { 0xC0, 0xFF, 0xC0 };
    HostSends(ret, sizeof ret);
    RunTicks(1);
    CHECK(!KISS_IsActive(), "C0 FF C0 exits");
    CHECK(gTxConfig.preamble_flags == 40 && gTxConfig.tail_flags == 3, "station TX timing restored");
    CHECK(sHook == NULL, "hook removed");

    printf("%d passed, %d failed\n", sPass, sFail);
    return sFail != 0;
}
