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
static TxResult sTxResult = TX_OK;      // what the transmitter says
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
    return sTxResult;
}

// ---------------------------------------------------------------- helpers

static int sFail, sPass;
#define CHECK(cond, ...) do { if (cond) sPass++; else { sFail++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void HostSends(const uint8_t *p, unsigned n) { memcpy(sInBuf + sInLen, p, n); sInLen += n; }

static void RunTicks(int n) { for (int i = 0; i < n; i++) { KISS_Poll10ms(); TXQ_Poll10ms(); } }

// An AX.25 UI frame: APZK5M <- VE6CXZ-1, control 03, PID F0, then `info_len` info bytes.
static unsigned Ui(uint8_t *out, unsigned info_len)
{
    static const char *const calls[2] = { "APZK5M", "VE6CXZ" };
    unsigned n = 0;
    for (int a = 0; a < 2; a++) {
        for (int i = 0; i < 6; i++) out[n++] = (uint8_t)(calls[a][i] << 1);
        out[n++] = (uint8_t)(0x60 | (a ? (1 << 1) | 1 : 0));
    }
    out[n++] = 0x03;
    out[n++] = 0xF0;
    for (unsigned i = 0; i < info_len; i++) out[n++] = (uint8_t)('a' + i % 26);
    return n;
}

static uint8_t Frame[40];
static void MakeFrame(void)
{
    Ui(Frame, sizeof Frame - 16);
    Frame[20] = 0xC0;   // needs escaping
    Frame[24] = 0xDB;   // ditto
}

// KISS-wraps `f` (no escapes needed) after command byte `cmd`
static unsigned Wrap(uint8_t *out, uint8_t cmd, const uint8_t *f, unsigned n)
{
    out[0] = 0xC0; out[1] = cmd;
    memcpy(out + 2, f, n);
    out[2 + n] = 0xC0;
    return n + 3;
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
    CHECK(sOut[2 + 20] == 0xDB && sOut[2 + 21] == 0xDC, "FEND escaped");
    CHECK(sOut[2 + 25] == 0xDB && sOut[2 + 26] == 0xDD, "FESC escaped");

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
    uint8_t ui16[16], data[19];
    Ui(ui16, 0);
    Wrap(data, 0x00, ui16, sizeof ui16);
    HostSends(data, sizeof data);
    RunTicks(500);
    CHECK(sTxCount == 1, "nothing sent while busy");
    sBusy = false;
    RunTicks(1);
    CHECK(sTxCount == 2 && sTxLen == 16 && sTxFlags == 45, "sent once clear, with TXDELAY");

    // ------------------------------------------------ other ports and bad escapes are ignored
    uint8_t port1[19], badesc[19];
    Wrap(port1, 0x10, ui16, sizeof ui16);
    Wrap(badesc, 0x00, ui16, sizeof ui16);
    badesc[5] = 0xDB; badesc[6] = 0x41;
    const uint8_t junk[] = { 0xC0, 0x00, 1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16, 0xC0 };
    HostSends(port1, sizeof port1);
    HostSends(badesc, sizeof badesc);
    HostSends(junk, sizeof junk);
    RunTicks(50);
    CHECK(sTxCount == 2, "port 1, bad escape and a non-AX.25 data frame dropped (sent %d)", sTxCount);

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

    // ------------------------------------------------ AX.25 check
    uint8_t f[64];
    unsigned n = Ui(f, 5);
    CHECK(KISS_ValidAx25(f, (uint16_t)n), "UI frame valid");
    f[14] = 0x13;  CHECK(KISS_ValidAx25(f, (uint16_t)n), "P/F bit allowed");
    f[14] = 0x3F;  CHECK(!KISS_ValidAx25(f, (uint16_t)n), "not a UI frame");
    f[14] = 0x03; f[15] = 0xCF;  CHECK(!KISS_ValidAx25(f, (uint16_t)n), "other PID");
    f[15] = 0xF0; f[0] = (uint8_t)('a' << 1);  CHECK(!KISS_ValidAx25(f, (uint16_t)n), "lower case");
    f[0] = (uint8_t)('A' << 1); f[6] |= 1;  CHECK(!KISS_ValidAx25(f, (uint16_t)n), "one address only");
    f[6] &= (uint8_t)~1; f[13] &= (uint8_t)~1;  CHECK(!KISS_ValidAx25(f, (uint16_t)n), "no end of addresses");
    CHECK(!KISS_ValidAx25(f, 15), "too short");

    // ------------------------------------------------ console probing: only a real KISS frame switches
    sInLen = sInPos = 0;
    const int before = sTxCount;
    struct { const uint8_t *b; unsigned n; KissProbe want; const char *what; } probes[] = {
        { junk, sizeof junk, KISS_PROBE_REJECTED, "junk data frame" },
        { badesc, sizeof badesc, KISS_PROBE_REJECTED, "bad escape" },
        { ret, sizeof ret, KISS_PROBE_REJECTED, "C0 FF C0" },
        { port1, sizeof port1, KISS_PROBE_REJECTED, "port 1" },
        { txdelay, sizeof txdelay, KISS_PROBE_ACCEPTED, "TXDELAY" },
        { data, sizeof data, KISS_PROBE_ACCEPTED, "valid data frame" },
    };
    for (unsigned i = 0; i < sizeof probes / sizeof probes[0]; i++) {
        KISS_Exit();
        KISS_ProbeStart();
        KissProbe r = KISS_PROBE_PENDING;
        for (unsigned k = 0; k < probes[i].n && r == KISS_PROBE_PENDING; k++)
            r = KISS_ProbeByte(probes[i].b[k]);
        CHECK(r == probes[i].want, "probe %s: %d", probes[i].what, r);
        CHECK(KISS_IsActive() == (probes[i].want == KISS_PROBE_ACCEPTED), "probe %s: mode", probes[i].what);
    }
    RunTicks(50);
    CHECK(sTxCount == before + 1, "the accepted data frame is sent (sent %d)", sTxCount - before);
    KISS_Exit();
    KISS_ProbeStart();
    const uint8_t fends[] = { 0xC0, 0xC0, 0xC0 };
    KissProbe r = KISS_PROBE_PENDING;
    for (unsigned k = 0; k < sizeof fends; k++) r = KISS_ProbeByte(fends[k]);
    CHECK(r == KISS_PROBE_PENDING && !KISS_IsActive(), "empty frames: still waiting");
    KISS_ProbeCancel();
    CHECK(KISS_ProbeByte(0x00) == KISS_PROBE_REJECTED, "after cancel: rejected");

    // ------------------------------------------------ stats: received, dropped, refused by the transmitter
    {
        KISS_Enter();
        const KissStats before = *KISS_GetStats();
        const uint16_t refused_before = TXQ_HostRefused();
        uint8_t f[40], w[48];
        const unsigned n = Ui(f, 10);
        HostSends(w, Wrap(w, 0x00, f, n));          // a good frame, sent
        f[14] = 0x3F;                               // not a UI frame: dropped here
        HostSends(w, Wrap(w, 0x00, f, n));
        RunTicks(300);
        CHECK(KISS_GetStats()->from_host == before.from_host + 2, "two frames from the host (%u)", KISS_GetStats()->from_host - before.from_host);
        CHECK(KISS_GetStats()->refused == before.refused + 1, "one dropped (%u)", KISS_GetStats()->refused - before.refused);
        CHECK(TXQ_HostRefused() == refused_before, "none refused by the transmitter yet");
        sTxResult = TX_ERR_FREQUENCY;               // e.g. tuned outside the TX bands
        f[14] = 0x03;
        HostSends(w, Wrap(w, 0x00, f, n));
        RunTicks(300);
        CHECK(TXQ_HostRefused() == refused_before + 1, "refused by the transmitter: counted (%u)", TXQ_HostRefused() - refused_before);
        CHECK(TXQ_Count() == 0, "and not retried");
        sTxResult = TX_OK;
        KISS_Exit();
    }

    printf("%d passed, %d failed\n", sPass, sFail);
    return sFail != 0;
}
