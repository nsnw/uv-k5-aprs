#include <string.h>

#include "aprs.h"
#include "ax25.h"
#include "digi.h"
#include "driver/bk4819.h"
#include "driver/uart.h"
#include "kiss.h"
#include "msg.h"
#include "radio.h"
#include "settings.h"

#define IRQ_FSK_RX_SYNC       (1u << 1)
#define IRQ_FIFO_ALMOST_FULL  (1u << 12)
#define IRQ_FSK_RX_FINISHED   (1u << 13)

#define FIFO_WORDS_PER_IRQ    4

static bool        sActive;
static bool        sReceiving;
static bool        sNewPacket;
static uint16_t    sTotal;
static uint32_t    sTicks10ms;
static uint8_t     sCount;
static AprsStation sStations[APRS_MAX_STATIONS];   // most recently heard first
static Ax25Rx      sRx;

// ------------------------------------------------------------ chip setup

static void Arm(void)
{
    BK4819_WriteRegister(BK4819_REG_59, 0x4068);   // clear RX FIFO
    BK4819_WriteRegister(BK4819_REG_59, 0x1068);   // FSK RX on, scrambler off, 4-byte sync
}

void APRS_ConfigureChip(void)
{
    BK4819_WriteRegister(BK4819_REG_3F, 0);
    // <15:13> TX mode 1, <12:10> RX mode 4, <9:8> RX gain 3, <7:6> preamble 3,
    // <3:1> RX bandwidth 1, <0> FSK enable
    BK4819_WriteRegister(BK4819_REG_58, 0x33C3);
    BK4819_WriteRegister(BK4819_REG_72, 0x3065);   // 1200 baud
    BK4819_WriteRegister(BK4819_REG_5A, 0xFEFE);   // sync = NRZI image of 0x7E flags
    BK4819_WriteRegister(BK4819_REG_5B, 0xFEFE);
    BK4819_WriteRegister(BK4819_REG_5C, 0xAA30);   // chip CRC off
    BK4819_WriteRegister(BK4819_REG_5D, 0xFF00);   // 256-byte packets
    Arm();
    BK4819_WriteRegister(BK4819_REG_3F, IRQ_FSK_RX_SYNC | IRQ_FIFO_ALMOST_FULL | IRQ_FSK_RX_FINISHED);
    sReceiving = false;
    AX25_Reset(&sRx);
}

// ------------------------------------------------------------ decoding

// WIDEn-N, TRACEn-N, RELAY, ...: path aliases, not stations.
static bool IsAlias(const char *call)
{
    static const char *const ALIASES[] = { "WIDE", "TRACE", "RELAY", "ECHO", "GATE", "TEMP", "NOGATE", "RFONLY" };
    for (uint8_t i = 0; i < sizeof ALIASES / sizeof ALIASES[0]; i++) {
        const size_t n = strlen(ALIASES[i]);
        if (!strncmp(call, ALIASES[i], n))
            return true;
    }
    return false;
}

void APRS_FindVia(const uint8_t *frame, uint16_t len, char *out, uint8_t size, bool *direct)
{
    out[0] = '\0';
    *direct = true;
    if (len < 14 || (frame[13] & 1))         // no digipeaters in the path
        return;
    char call[10];
    for (uint16_t pos = 14; pos + 7 <= len; pos += 7) {
        if (frame[pos + 6] & 0x80) {         // H bit: has been repeated
            *direct = false;
            // FormatSource reads the address at offset 7 of what it's given
            AX25_FormatSource(frame + pos - 7, 14, call, sizeof call);
            if (!IsAlias(call)) {
                strncpy(out, call, size - 1);
                out[size - 1] = '\0';
            }
        }
        if (frame[pos + 6] & 1)
            break;
    }
}

uint8_t APRS_StationCount(void)  { return sCount; }

const AprsStation *APRS_GetStation(uint8_t index)
{
    return index < sCount ? &sStations[index] : NULL;
}

int APRS_FindStation(const char *source)
{
    for (uint8_t i = 0; i < sCount; i++)
        if (strcmp(sStations[i].source, source) == 0)
            return i;
    return -1;
}

AprsStation *APRS_StationTable(uint8_t **count)
{
    *count = &sCount;
    return sStations;
}

// Ages from before power-off are unknown (no clock): shown as "old".
bool APRS_RestoreStation(AprsStation *st)
{
    if (!memchr(st->source, '\0', sizeof st->source) || !st->source[0]
        || !memchr(st->via, '\0', sizeof st->via)
        || !memchr(st->comment, '\0', sizeof st->comment))
        return false;
    st->flags    = (uint8_t)((st->flags & APRS_ST_DIRECT) | APRS_ST_OLD);
    st->heard_s  = 0;
    st->direct_s = 0;
    return true;
}

#ifndef APRS_HOST_TEST

static void OnFrame(const uint8_t *frame, uint16_t len)
{
    char source[10];
    AX25_FormatSource(frame, len, source, sizeof source);

    // Find or create the station, then move it to the front.
    int idx = APRS_FindStation(source);
    AprsStation st;
    if (idx >= 0) {
        st = sStations[idx];
    } else {
        memset(&st, 0, sizeof st);
        memcpy(st.source, source, sizeof st.source);
        idx = (sCount < APRS_MAX_STATIONS) ? sCount++ : APRS_MAX_STATIONS - 1;  // evict oldest
    }
    memmove(&sStations[1], &sStations[0], (size_t)idx * sizeof(AprsStation));

    const uint32_t now = APRS_NowSeconds();
    char via[10];
    bool direct;
    APRS_FindVia(frame, len, via, sizeof via, &direct);
    if (via[0])
        memcpy(st.via, via, sizeof st.via);
    if (direct) {
        st.flags   |= APRS_ST_DIRECT;
        st.direct_s = now;
    } else if (!(st.flags & APRS_ST_DIRECT) || now - st.direct_s > APRS_DIRECT_WINDOW_S
               || (st.flags & APRS_ST_OLD)) {
        st.flags &= (uint8_t)~APRS_ST_DIRECT;   // not just a digipeated copy of a direct packet
    }
    st.flags  &= (uint8_t)~APRS_ST_OLD;
    st.heard_s = now;
    st.packets++;
    APRS_Parse(frame, len, &st.info, st.comment, sizeof st.comment);
    sStations[0] = st;

    sTotal++;
    sNewPacket = true;

    DIGI_OnFrame(frame, len, now);
    MSG_OnFrame(frame, len, now);

    // Also pass it on over the programming cable: raw to a KISS host,
    // otherwise as a TNC2 text line.
    if (KISS_IsActive()) {
        KISS_SendFrame(frame, len);
        return;
    }
    static char line[AX25_MAX_FRAME + 64];
    AX25_FormatTNC2(frame, len, line, sizeof line);
    UART_Print("A ");
    UART_Print(line);
    UART_Print("\r\n");
}

static void FeedWord(uint16_t w)
{
    // FIFO words carry tone bits low byte first, MSB first within each byte.
    const uint8_t bytes[2] = { (uint8_t)(w & 0xFF), (uint8_t)(w >> 8) };
    for (int b = 0; b < 2; b++)
        for (int i = 7; i >= 0; i--)
            AX25_PushToneBit(&sRx, (bytes[b] >> i) & 1, OnFrame);
}

void APRS_OnInterrupt(uint16_t status)
{
    if (!sActive)
        return;

    if (status & IRQ_FSK_RX_SYNC) {
        AX25_Reset(&sRx);
        sReceiving = true;
    }
    if (status & IRQ_FIFO_ALMOST_FULL) {
        for (int i = 0; i < FIFO_WORDS_PER_IRQ; i++)
            FeedWord(BK4819_ReadRegister(BK4819_REG_5F));
    }
    if (status & IRQ_FSK_RX_FINISHED) {
        sReceiving = false;
        Arm();
    }
}

// ------------------------------------------------------------ mode

void APRS_Enter(void)
{
    if (sActive)
        return;
    if (!RADIO_IsTunable(gSettings.frequency))     // keep the stored frequency
        gSettings.frequency = APRS_FREQUENCY;
    gSettings.modulation = MOD_FM;
    gSettings.bandwidth  = BK4819_FILTER_BW_NARROW;   // as tested
    sActive = true;
    RADIO_Apply();   // calls APRS_ConfigureChip() because sActive is set
}

bool APRS_IsActive(void)         { return sActive; }
bool APRS_IsReceiving(void)      { return sReceiving; }
uint16_t APRS_TotalDecoded(void) { return sTotal; }
void APRS_Tick10ms(void)         { sTicks10ms++; }
uint32_t APRS_NowSeconds(void)   { return sTicks10ms / 100; }

bool APRS_TakeNewPacket(void)
{
    const bool n = sNewPacket;
    sNewPacket = false;
    return n;
}

void APRS_FormatDirects(char *out, uint8_t size)
{
    const uint32_t now = APRS_NowSeconds();
    strncpy(out, "Directs=", size - 1);
    out[size - 1] = '\0';
    size_t n = strlen(out);
    for (uint8_t i = 0; i < sCount; i++) {
        const AprsStation *st = &sStations[i];
        if (!(st->flags & APRS_ST_DIRECT) || (st->flags & APRS_ST_OLD) || now - st->direct_s > 30u * 60u)
            continue;
        const size_t k = strlen(st->source);
        if (n + 1 + k >= size)
            break;
        out[n++] = ' ';
        memcpy(out + n, st->source, k + 1);
        n += k;
    }
}

#endif  // APRS_HOST_TEST
