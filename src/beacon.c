#include <string.h>

#include "beacon.h"
#include "driver/eeprom.h"

#ifdef ENABLE_TX
#include "settings.h"
#include "station.h"
#include "tx.h"
#include "txq.h"
#endif

#define DEST_CALL        "APZK5M"   // APZ*: experimental software

static BeaconConfig sCfg;
static bool         sValid;

// ------------------------------------------------------------ validation

static bool ValidCall(const char *c)
{
    int n = 0;
    bool ended = false;
    for (int i = 0; i < 6; i++) {
        const char ch = c[i];
        if (ch == ' ' || ch == '\0') {
            ended = true;
        } else if (!ended && ((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9'))) {
            n++;
        } else {
            return false;
        }
    }
    return n >= 3;
}

static bool Valid(const BeaconConfig *c)
{
    if (c->magic != BEACON_MAGIC || !ValidCall(c->call) || c->ssid > 15 || c->path_hops > 2)
        return false;
    if (c->lat < -90 * 6000 || c->lat > 90 * 6000 || c->lon < -180 * 6000 || c->lon > 180 * 6000)
        return false;
    if (c->symbol_table < '!' || c->symbol_table > '~' || c->symbol < '!' || c->symbol > '~')
        return false;
    bool terminated = false;
    for (int i = 0; i < BEACON_COMMENT_MAX; i++) {
        const char ch = c->comment[i];
        if (ch == '\0') { terminated = true; break; }
        if (ch < 0x20 || ch > 0x7E) return false;
    }
    return terminated;
}

void BEACON_Load(void)
{
    EEPROM_ReadBuffer(BEACON_EEPROM_ADDR, &sCfg, sizeof sCfg);
    sValid = Valid(&sCfg);
}

bool BEACON_IsConfigured(void)       { return sValid; }
const BeaconConfig *BEACON_Get(void) { return &sCfg; }

bool BEACON_Set(const BeaconConfig *cfg)
{
    if (!Valid(cfg))
        return false;
    sCfg = *cfg;
    const uint8_t *p = (const uint8_t *)&sCfg;
    for (unsigned i = 0; i < sizeof sCfg; i += 8)
        EEPROM_WriteBuffer(BEACON_EEPROM_ADDR + i, p + i);
    sValid = true;
    return true;
}

// ------------------------------------------------------------ frame

static uint8_t *PutAddress(uint8_t *p, const char *call, uint8_t ssid, bool last)
{
    for (int i = 0; i < 6; i++) {
        const char ch = (call[i] && call[i] != ' ') ? call[i] : ' ';
        *p++ = (uint8_t)(ch << 1);
    }
    *p++ = (uint8_t)(0x60 | (ssid << 1) | (last ? 1 : 0));
    return p;
}

// "5102.34N" / "11404.86W"
static char *PutCoord(char *p, int32_t v, int deg_digits, char pos, char neg)
{
    const char hemi = v < 0 ? neg : pos;
    if (v < 0) v = -v;
    const int32_t deg = v / 6000, min = (v % 6000) / 100, hun = v % 100;
    if (deg_digits == 3) *p++ = (char)('0' + deg / 100);
    *p++ = (char)('0' + deg / 10 % 10);
    *p++ = (char)('0' + deg % 10);
    *p++ = (char)('0' + min / 10);
    *p++ = (char)('0' + min % 10);
    *p++ = '.';
    *p++ = (char)('0' + hun / 10);
    *p++ = (char)('0' + hun % 10);
    *p++ = hemi;
    return p;
}

uint16_t BEACON_BuildFrame(uint8_t *out, uint16_t size)
{
    if (!sValid || size < 16 + 7 * 2 + 20 + BEACON_COMMENT_MAX)
        return 0;
    const uint8_t hops = sCfg.path_hops;
    uint8_t *p = out;
    p = PutAddress(p, DEST_CALL, 0, false);
    p = PutAddress(p, sCfg.call, sCfg.ssid, hops == 0);
    if (hops >= 1) p = PutAddress(p, "WIDE1 ", 1, hops == 1);
    if (hops >= 2) p = PutAddress(p, "WIDE2 ", 1, true);
    *p++ = 0x03;                                  // UI frame
    *p++ = 0xF0;                                  // no layer 3

    char *t = (char *)p;
    *t++ = '!';                                   // position, no messaging
    t = PutCoord(t, sCfg.lat, 2, 'N', 'S');
    *t++ = sCfg.symbol_table;
    t = PutCoord(t, sCfg.lon, 3, 'E', 'W');
    *t++ = sCfg.symbol;
    for (int i = 0; i < BEACON_COMMENT_MAX && sCfg.comment[i]; i++)
        *t++ = sCfg.comment[i];
    return (uint16_t)((uint8_t *)t - out);
}

// ------------------------------------------------------------ send

BeaconResult BEACON_Send(void)
{
#ifdef ENABLE_TX
    if (!sValid)
        return BEACON_NOT_CONFIGURED;
    if (sCfg.lat == 0 && sCfg.lon == 0)
        return BEACON_NO_POSITION;
    if (!TX_IsAllowedFrequency(gSettings.frequency))
        return BEACON_TX_REFUSED;
    static uint8_t frame[TX_MAX_FRAME];
    const uint16_t len = BEACON_BuildFrame(frame, sizeof frame);
    return TXQ_Push(frame, len) ? BEACON_QUEUED : BEACON_QUEUE_FULL;
#else
    return BEACON_TX_REFUSED;
#endif
}

static uint32_t sNext_s;        // 0 = schedule from the next poll

void BEACON_RestartTimer(void) { sNext_s = 0; }

void BEACON_Poll1s(uint32_t now_s)
{
#ifdef ENABLE_TX
    const StationConfig *st = STATION_Get();
    if (!(st->flags & STATION_AUTOBEACON) || !sValid) {
        sNext_s = 0;
        return;
    }
    if (sNext_s == 0)
        sNext_s = now_s + BEACON_FIRST_S;
    if (now_s >= sNext_s) {
        BEACON_Send();          // not a TX frequency / queue full: skip this one
        sNext_s = now_s + (uint32_t)st->beacon_interval_min * 60u;
    }
#else
    (void)now_s;
#endif
}
