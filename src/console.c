#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "app.h"
#include "aprs.h"
#include "beacon.h"
#include "console.h"
#include "bsp/dp32g030/gpio.h"
#include "driver/bk4819.h"
#include "driver/keyboard.h"
#include "driver/uart.h"
#include "kiss.h"
#include "radio.h"
#include "settings.h"
#include "station.h"
#include "tx.h"
#include "version.h"

#define LINE_MAX 720   // room for "x" + a 330-byte frame in hex
#define FIFO_READ_MAX 32

static char    sLine[LINE_MAX];
static uint16_t sLineLen;
static bool    sOverflow;
static uint8_t sAutoRead = 4;

// ------------------------------------------------------------ output

static void PutHex(uint32_t v, uint8_t digits)
{
    char buf[8];
    for (int i = digits - 1; i >= 0; i--) {
        buf[i] = "0123456789ABCDEF"[v & 0xF];
        v >>= 4;
    }
    UART_Send(buf, digits);
}

static void PutDec(uint32_t v)
{
    char buf[10];
    int n = 0;
    do {
        buf[sizeof(buf) - 1 - n++] = '0' + (v % 10);
        v /= 10;
    } while (v && n < (int)sizeof(buf));
    UART_Send(buf + sizeof(buf) - n, n);
}

static void Newline(void) { UART_Send("\r\n", 2); }

static void DumpFifo(uint8_t words)
{
    UART_Send("D", 1);
    for (uint8_t i = 0; i < words; i++) {
        UART_Send(" ", 1);
        PutHex(BK4819_ReadRegister(BK4819_REG_5F), 4);
    }
    Newline();
}

// ------------------------------------------------------------ parsing

static const char *SkipSpaces(const char *p)
{
    while (*p == ' ')
        p++;
    return p;
}

static bool ParseNumber(const char **pp, uint32_t base, uint32_t *out)
{
    const char *p = SkipSpaces(*pp);
    uint32_t v = 0;
    bool any = false;
    for (;; p++) {
        uint32_t d;
        const char c = *p;
        if (c >= '0' && c <= '9')                      d = c - '0';
        else if (base == 16 && c >= 'a' && c <= 'f')  d = c - 'a' + 10;
        else if (base == 16 && c >= 'A' && c <= 'F')  d = c - 'A' + 10;
        else break;
        v = v * base + d;
        any = true;
    }
    *pp = p;
    *out = v;
    return any;
}

static void Error(const char *why)
{
    UART_Print("E ");
    UART_Print(why);
    Newline();
}

static void Execute(const char *line)
{
    const char *p = SkipSpaces(line);
    const char cmd = *p;
    if (cmd == '\0')
        return;
    p++;

    uint32_t a, b;
    switch (cmd) {
    case 'r':
        if (!ParseNumber(&p, 16, &a) || a > 0x7F) { Error("usage: r RR"); return; }
        UART_Print("R "); PutHex(a, 2); UART_Print(" ");
        PutHex(BK4819_ReadRegister((BK4819_REGISTER_t)a), 4); Newline();
        return;

    case 'w':
        if (!ParseNumber(&p, 16, &a) || a > 0x7F || !ParseNumber(&p, 16, &b) || b > 0xFFFF) {
            Error("usage: w RR VVVV");
            return;
        }
        BK4819_WriteRegister((BK4819_REGISTER_t)a, (uint16_t)b);
        UART_Print("W "); PutHex(a, 2); UART_Print(" "); PutHex(b, 4); Newline();
        return;

    case 'd':
        if (!ParseNumber(&p, 16, &a) || a == 0 || a > FIFO_READ_MAX) { Error("usage: d N (1..20)"); return; }
        DumpFifo((uint8_t)a);
        return;

    case 'p':
        if (!ParseNumber(&p, 16, &a) || a > FIFO_READ_MAX) { Error("usage: p N (0..20)"); return; }
        sAutoRead = (uint8_t)a;
        UART_Print("P "); PutHex(a, 2); Newline();
        return;

    case 't':
        if (!ParseNumber(&p, 10, &a) || !RADIO_IsTunable(a)) { Error("t: FREQ_10HZ (decimal), in an amateur band"); return; }
        gSettings.frequency = a;
        RADIO_Apply();
        APP_Redraw();
        UART_Print("T "); PutDec(a); Newline();
        return;

#ifdef ENABLE_TX
    case 's':
        UART_Print("S "); PutDec(gTxStats.attempts); UART_Print(" ");
        PutDec(gTxStats.last_result); UART_Print(" "); PutDec(gTxStats.last_ms); Newline();
        return;

    case 'x': {
        static uint8_t frame[TX_MAX_FRAME];
        uint16_t len = 0;
        p = SkipSpaces(p);
        while (*p) {
            uint32_t byte;
            const char h[3] = { p[0], p[1], 0 };
            const char *q = h;
            if (!p[1] || !ParseNumber(&q, 16, &byte) || q != h + 2) { Error("x: bad hex"); return; }
            if (len >= sizeof frame) { Error("x: frame too long"); return; }
            frame[len++] = (uint8_t)byte;
            p = SkipSpaces(p + 2);
        }
        uint16_t ms = 0;
        switch (TX_SendFrame(frame, len, &ms)) {
        case TX_OK:                  break;
        case TX_ERR_FREQUENCY:       Error("x: TX not allowed on this frequency"); return;
        case TX_ERR_LENGTH:          Error("x: frame length out of range"); return;
        case TX_ERR_TOO_LONG_ON_AIR: Error("x: would exceed the TX time limit"); return;
        case TX_ERR_NO_CALIBRATION:  Error("x: no power calibration for this band"); return;
        }
        APP_Redraw();
        UART_Print("X "); PutDec(ms); Newline();
        return;
    }

    case 'g':
        if (!ParseNumber(&p, 16, &a) || a > 0x7F) { Error("usage: g NN (00..7F)"); return; }
        gTxConfig.tone_gain = (uint8_t)a;
        UART_Print("G "); PutHex(a, 2); Newline();
        return;

    case 'b':
        if (!ParseNumber(&p, 16, &a) || a > 0xFF) { Error("usage: b NN (00..FF)"); return; }
        gTxConfig.power_bias = (uint8_t)a;
        UART_Print("B "); PutHex(a, 2); UART_Print(" ");
        PutHex(TX_CalibratedLowBias(gSettings.frequency), 2); Newline();
        return;

    case 'f':
        if (!ParseNumber(&p, 16, &a) || a < 1 || a > 0xC8) { Error("usage: f NN (01..C8)"); return; }
        gTxConfig.preamble_flags = (uint8_t)a;
        UART_Print("F "); PutHex(a, 2); Newline();
        return;
#endif

    case 'c': {
        p = SkipSpaces(p);
        if (*p) {
            BeaconConfig cfg;
            uint8_t *b = (uint8_t *)&cfg;
            for (unsigned i = 0; i < sizeof cfg; i++) {
                uint32_t byte;
                const char h[3] = { p[0], p[1], 0 };
                const char *q = h;
                if (!p[0] || !p[1] || !ParseNumber(&q, 16, &byte) || q != h + 2) { Error("c: need 64 hex bytes"); return; }
                b[i] = (uint8_t)byte;
                p = SkipSpaces(p + 2);
            }
            if (*p) { Error("c: need 64 hex bytes"); return; }
            if (!BEACON_Set(&cfg)) { Error("c: invalid beacon configuration"); return; }
            APP_Redraw();               // the status line shows the callsign
        }
        UART_Print("C ");
        if (!BEACON_IsConfigured()) {
            UART_Print("-");
        } else {
            const uint8_t *b = (const uint8_t *)BEACON_Get();
            for (unsigned i = 0; i < sizeof(BeaconConfig); i++)
                PutHex(b[i], 2);
        }
        Newline();
        return;
    }

    case 'u': {                     // beacon type + status text (BeaconStatus, beacon.h)
        p = SkipSpaces(p);
        if (*p) {
            BeaconStatus bs;
            uint8_t *b = (uint8_t *)&bs;
            for (unsigned i = 0; i < sizeof bs; i++) {
                uint32_t byte;
                const char h[3] = { p[0], p[1], 0 };
                const char *q = h;
                if (!p[0] || !p[1] || !ParseNumber(&q, 16, &byte) || q != h + 2) { Error("u: need 72 hex bytes"); return; }
                b[i] = (uint8_t)byte;
                p = SkipSpaces(p + 2);
            }
            if (*p) { Error("u: need 72 hex bytes"); return; }
            if (!BEACON_SetStatus(&bs)) { Error("u: invalid status configuration"); return; }
        }
        UART_Print("U ");
        const uint8_t *b = (const uint8_t *)BEACON_GetStatus();
        for (unsigned i = 0; i < sizeof(BeaconStatus); i++)
            PutHex(b[i], 2);
        Newline();
        return;
    }

    case 'o': {
        p = SkipSpaces(p);
        if (*p) {
            StationConfig cfg;
            uint8_t *b = (uint8_t *)&cfg;
            for (unsigned i = 0; i < sizeof cfg; i++) {
                uint32_t byte;
                const char h[3] = { p[0], p[1], 0 };
                const char *q = h;
                if (!p[0] || !p[1] || !ParseNumber(&q, 16, &byte) || q != h + 2) { Error("o: need 16 hex bytes"); return; }
                b[i] = (uint8_t)byte;
                p = SkipSpaces(p + 2);
            }
            if (*p) { Error("o: need 16 hex bytes"); return; }
            if (!STATION_Set(&cfg)) { Error("o: invalid station configuration"); return; }
            APP_Redraw();
        }
        UART_Print("O ");
        const uint8_t *b = (const uint8_t *)STATION_Get();
        for (unsigned i = 0; i < sizeof(StationConfig); i++)
            PutHex(b[i], 2);
        Newline();
        return;
    }

    case 'k':
        // KISS mode, optionally on another frequency (decimal 10 Hz units).
        if (ParseNumber(&p, 10, &a)) {
            if (!RADIO_IsTunable(a)) { Error("k: FREQ_10HZ must be in an amateur band"); return; }
            gSettings.frequency = a;
        }
        if (!APRS_IsActive())
            APRS_Enter();
        else
            RADIO_Apply();
        UART_Print("K "); PutDec(gSettings.frequency); Newline();
        KISS_Enter();
        return;

    case 'y': {     // keypad diagnostics: "Y DIR DATA0 DATA1 DATA2 DATA3 DATA4 KEY RECOVERIES"
        uint16_t raw[5];
        const uint32_t dir = GPIOA->DIR;
        KEYBOARD_ReadRaw(raw);
        UART_Print("Y "); PutHex(dir, 4);
        for (int i = 0; i < 5; i++) { UART_Print(" "); PutHex(raw[i], 4); }
        UART_Print(" "); PutHex((uint32_t)KEYBOARD_Poll(), 2);
        UART_Print(" "); PutDec(KEYBOARD_BusRecoveries()); Newline();
        return;
    }

    case 'v':
        UART_Print("V " APP_NAME " " VERSION_STRING " (" BUILD_ID ")");
        Newline();
        return;

    default:
        Error("unknown command");
        return;
    }
}

// ------------------------------------------------------------ public

#define PROBE_TICKS 100                 // 1 s for a KISS frame to complete after a FEND

void CONSOLE_Poll(void)
{
    static bool    probing;
    static uint8_t probe_ticks;
    if (probing && ++probe_ticks > PROBE_TICKS) {   // no complete frame: noise
        probing = false;
        KISS_ProbeCancel();
    }
    int c;
    while ((c = UART_ReadByte()) >= 0) {
        if (probing) {                  // after a FEND: is a KISS host talking to us?
            const KissProbe r = KISS_ProbeByte((uint8_t)c);
            if (r == KISS_PROBE_ACCEPTED) {
                probing = false;
                if (!APRS_IsActive())
                    APRS_Enter();
                return;                 // the rest is read by KISS_Poll10ms()
            }
            if (r == KISS_PROBE_REJECTED)
                probing = false;
            continue;
        }
        if (c == 0xC0) {                // FEND: maybe a KISS host (see KISS_ProbeByte)
            sLineLen    = 0;
            sOverflow   = false;
            probing     = true;
            probe_ticks = 0;
            KISS_ProbeStart();
            KISS_ProbeByte((uint8_t)c);
            continue;
        }
        if (c == '\r' || c == '\n') {
            if (sOverflow)
                Error("line too long");
            else if (sLineLen) {
                sLine[sLineLen] = '\0';
                Execute(sLine);
            }
            if (KISS_IsActive()) {      // `k`: the rest is KISS
                sLineLen = 0;
                return;
            }
            sLineLen  = 0;
            sOverflow = false;
        } else if (sLineLen < LINE_MAX - 1) {
            sLine[sLineLen++] = (char)c;
        } else {
            sOverflow = true;
        }
    }
}

void CONSOLE_OnRadioInterrupt(uint16_t status)
{
    UART_Print("I ");
    PutHex(status, 4);
    Newline();

    // In APRS mode the decoder owns the FIFO.
    if ((status & BK4819_REG_02_FSK_FIFO_ALMOST_FULL) && sAutoRead && !APRS_IsActive())
        DumpFifo(sAutoRead);
}
