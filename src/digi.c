#include <string.h>

#include "digi.h"

#ifndef DIGI_HOST_TEST
#include "beacon.h"
#include "radio.h"
#include "settings.h"
#include "station.h"
#include "txq.h"
#ifdef ENABLE_TX
#include "tx.h"
#endif
#endif

#define ADDR_LEN     7
#define MAX_ADDRS    10            // destination, source, 8 digipeaters
#define H_BIT        0x80          // "has been repeated", in a digipeater's SSID byte
#define LAST_BIT     0x01          // address extension: last address
#define SSID(b)      (((b) >> 1) & 0x0F)

static uint16_t sCount;

static bool SameStation(const uint8_t *a, const uint8_t *b)
{
    return memcmp(a, b, 6) == 0 && SSID(a[6]) == SSID(b[6]);
}

// "WIDEn" with n = 1..7; returns n or 0.
static uint8_t WideN(const uint8_t *a)
{
    static const uint8_t WIDE[4] = { 'W' << 1, 'I' << 1, 'D' << 1, 'E' << 1 };
    if (memcmp(a, WIDE, 4) != 0 || a[5] != (' ' << 1))
        return 0;
    const uint8_t n = (uint8_t)((a[4] >> 1) - '0');
    return (n >= 1 && n <= 7) ? n : 0;
}

uint16_t DIGI_Rewrite(const uint8_t *in, uint16_t len, const uint8_t mycall[7],
                      uint8_t max_hops, uint8_t *out)
{
    // address fields
    uint8_t naddr = 0;
    while (naddr < MAX_ADDRS && (naddr + 1) * ADDR_LEN <= len) {
        naddr++;
        if (in[naddr * ADDR_LEN - 1] & LAST_BIT)
            break;
    }
    if (naddr < 3 || !(in[naddr * ADDR_LEN - 1] & LAST_BIT))
        return 0;                                   // no path, or malformed
    if (SameStation(in + ADDR_LEN, mycall))
        return 0;                                   // our own packet

    uint8_t i = 2;                                  // first digipeater not yet used
    for (; i < naddr; i++) {
        const uint8_t *a = in + i * ADDR_LEN;
        if (!(a[6] & H_BIT))
            break;
        if (SameStation(a, mycall))
            return 0;                               // we've repeated it already
    }
    if (i == naddr)
        return 0;                                   // path used up
    for (uint8_t j = i + 1; j < naddr; j++)
        if (SameStation(in + j * ADDR_LEN, mycall))
            return 0;                               // we're further down: leave it

    const uint8_t *a   = in + i * ADDR_LEN;
    const uint16_t pos = (uint16_t)(i * ADDR_LEN);

    if (SameStation(a, mycall)) {                   // directed via us: mark it used
        memcpy(out, in, len);
        out[pos + 6] |= H_BIT;
        return len;
    }

    const uint8_t n = WideN(a);
    const uint8_t N = SSID(a[6]);
    if (n == 0 || n > max_hops || N == 0 || N > n)
        return 0;

    // Copy up to the hop, maybe insert MYCALL*, then the counted-down hop.
    uint16_t o = 0;
    memcpy(out, in, pos);
    o = pos;
    if (naddr < MAX_ADDRS) {
        memcpy(out + o, mycall, 6);
        out[o + 6] = (uint8_t)(0x60 | (SSID(mycall[6]) << 1) | H_BIT);
        o += ADDR_LEN;
    }
    memcpy(out + o, a, ADDR_LEN);
    const uint8_t left = (uint8_t)(N - 1);
    out[o + 6] = (uint8_t)((a[6] & ~(0x0F << 1) & ~H_BIT) | (left << 1) | (left == 0 ? H_BIT : 0));
    o += ADDR_LEN;
    memcpy(out + o, in + pos + ADDR_LEN, len - pos - ADDR_LEN);   // rest of path, control, PID, info
    return (uint16_t)(o + len - pos - ADDR_LEN);
}

// ------------------------------------------------------------ duplicates

#if defined(ENABLE_TX) || defined(DIGI_HOST_TEST)   // the receive-only build never repeats
static struct { uint32_t hash, time_s; } sDupes[DIGI_DUPES];
static uint8_t sDupeNext;

static uint32_t Fnv(uint32_t h, const uint8_t *p, uint16_t n, uint8_t mask_last)
{
    for (uint16_t i = 0; i < n; i++) {
        const uint8_t b = (i == n - 1) ? (uint8_t)(p[i] & mask_last) : p[i];
        h = (h ^ b) * 16777619u;
    }
    return h;
}

// Source, destination and everything after the path, ignoring the path itself.
static uint32_t FrameHash(const uint8_t *f, uint16_t len)
{
    uint16_t end = ADDR_LEN;
    while (end + ADDR_LEN <= len && !(f[end + ADDR_LEN - 1] & LAST_BIT))
        end += ADDR_LEN;
    end += ADDR_LEN;                               // past the last address
    if (end > len) end = len;
    uint32_t h = 2166136261u;
    h = Fnv(h, f, ADDR_LEN, 0x1E);                 // destination (SSID only)
    h = Fnv(h, f + ADDR_LEN, ADDR_LEN, 0x1E);      // source
    return Fnv(h, f + end, (uint16_t)(len - end), 0xFF);
}

static bool IsDupe(uint32_t hash, uint32_t now_s)
{
    for (uint8_t i = 0; i < DIGI_DUPES; i++)
        if (sDupes[i].hash == hash && sDupes[i].time_s != 0 && now_s - sDupes[i].time_s < DIGI_DUPE_S)
            return true;
    return false;
}

static void Remember(uint32_t hash, uint32_t now_s)
{
    sDupes[sDupeNext].hash   = hash;
    sDupes[sDupeNext].time_s = now_s ? now_s : 1;   // 0 = empty
    sDupeNext = (sDupeNext + 1) % DIGI_DUPES;
}

#endif

uint16_t DIGI_Count(void) { return sCount; }

#ifndef DIGI_HOST_TEST
void DIGI_OnFrame(const uint8_t *frame, uint16_t len, uint32_t now_s)
{
#ifdef ENABLE_TX
    const StationConfig *st = STATION_Get();
    if (!(st->flags & STATION_DIGI) || !BEACON_IsConfigured()
        || !TX_IsAllowedFrequency(gSettings.frequency) || len + ADDR_LEN > TX_MAX_FRAME)
        return;

    const BeaconConfig *b = BEACON_Get();
    uint8_t mycall[ADDR_LEN];
    for (int i = 0; i < 6; i++)
        mycall[i] = (uint8_t)(((b->call[i] && b->call[i] != ' ') ? b->call[i] : ' ') << 1);
    mycall[6] = (uint8_t)(0x60 | (b->ssid << 1));

    static uint8_t out[TX_MAX_FRAME];
    const uint16_t n = DIGI_Rewrite(frame, len, mycall, st->digi_max_hops, out);
    if (n == 0)
        return;
    const uint32_t h = FrameHash(frame, len);
    if (IsDupe(h, now_s))
        return;
    Remember(h, now_s);
    if (TXQ_Push(out, n))
        sCount++;
#else
    (void)frame; (void)len; (void)now_s;
#endif
}
#else
// Host test entry: dupe logic without the radio.
bool DIGI_TestDupe(const uint8_t *f, uint16_t len, uint32_t now_s);
bool DIGI_TestDupe(const uint8_t *f, uint16_t len, uint32_t now_s)
{
    const uint32_t h = FrameHash(f, len);
    if (IsDupe(h, now_s)) return true;
    Remember(h, now_s);
    return false;
}
void DIGI_OnFrame(const uint8_t *f, uint16_t l, uint32_t n) { (void)f; (void)l; (void)n; }
#endif
