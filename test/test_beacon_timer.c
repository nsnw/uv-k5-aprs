// Host test for the auto-beacon schedule in beacon.c (built with ENABLE_TX):
// the first beacon BEACON_FIRST_S after start, then fast start (1, 2, 4 ...
// minutes up to the interval) unless it's off, and both start over when the
// beacon is changed or the auto-beacon is switched on again.
#include <stdio.h>
#include <string.h>

#include "beacon.h"
#include "settings.h"
#include "station.h"
#include "tx.h"
#include "txq.h"

static int sFails, sPasses;
#define CHECK(c, ...) do { if (c) sPasses++; else { sFails++; printf("FAIL %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

Settings_t gSettings;
static StationConfig sSt = { .magic = STATION_MAGIC, .flags = STATION_AUTOBEACON, .beacon_interval_min = 10 };
const StationConfig *STATION_Get(void) { return &sSt; }
bool TX_IsAllowedFrequency(uint32_t f) { (void)f; return true; }
static int sSent;
bool TXQ_PushLocal(const uint8_t *f, uint16_t n) { (void)f; (void)n; sSent++; return true; }
void EEPROM_ReadBuffer(uint16_t a, void *p, uint8_t n) { (void)a; memset(p, 0xFF, n); }
void EEPROM_WriteBuffer(uint16_t a, const void *p) { (void)a; (void)p; }

static uint32_t sNow;

// Runs the clock until the next beacon (at most `limit` s); returns the gap
// since the previous one, or 0 if none was sent.
static uint32_t NextGap(uint32_t limit)
{
    const uint32_t from = sNow;
    const int before = sSent;
    while (sNow - from < limit) {
        BEACON_Poll1s(++sNow);
        if (sSent != before)
            return sNow - from;
    }
    return 0;
}

static void Gaps(const char *what, const uint32_t *want, int n)
{
    for (int i = 0; i < n; i++) {
        const uint32_t got = NextGap(3600);
        CHECK(got == want[i], "%s: gap %d is %u s, want %u s", what, i, got, want[i]);
    }
}

int main(void)
{
    BeaconConfig c;
    memset(&c, 0, sizeof c);
    c.magic = BEACON_MAGIC;
    memcpy(c.call, "VE6LY ", 6);
    c.symbol_table = '/';
    c.symbol = '[';
    c.lat = 51 * 6000;
    c.lon = -114 * 6000;
    CHECK(BEACON_Set(&c), "config stored");

    // from boot: 10 s, then 1, 2, 4, 8 minutes, then the 10-minute interval
    BEACON_Poll1s(sNow);
    const uint32_t fast[] = { BEACON_FIRST_S - 1 + 1, 60, 120, 240, 480, 600, 600 };
    Gaps("fast start", fast, 7);

    // changing the beacon starts it over; an unchanged one doesn't
    sNow += 5;
    CHECK(BEACON_Set(&c), "same config stored");
    CHECK(BEACON_SecondsToNext(sNow) == 595, "unchanged: still due at the interval (%u)", BEACON_SecondsToNext(sNow));
    c.lat += 1;
    BEACON_Set(&c);
    const uint32_t again[] = { BEACON_FIRST_S + 1, 60, 120 };   // +1: scheduled on the next poll
    Gaps("after a change", again, 3);

    // a manual beacon doesn't change the schedule
    const uint32_t due = BEACON_SecondsToNext(sNow);
    BEACON_Send();
    CHECK(BEACON_SecondsToNext(sNow) == due, "manual beacon leaves the schedule (%u)", BEACON_SecondsToNext(sNow));

    // fast start off: straight to the interval
    sSt.flags |= STATION_NO_FAST_START;
    BEACON_RestartTimer();
    const uint32_t slow[] = { BEACON_FIRST_S + 1, 600, 600 };
    Gaps("no fast start", slow, 3);

    // a short interval: never more often than it, nor than once a minute
    sSt.flags &= (uint8_t)~STATION_NO_FAST_START;
    sSt.beacon_interval_min = 2;
    BEACON_RestartTimer();
    const uint32_t shortgaps[] = { BEACON_FIRST_S + 1, 60, 120, 120 };
    Gaps("2-minute interval", shortgaps, 4);
    sSt.beacon_interval_min = 1;
    BEACON_RestartTimer();
    const uint32_t oneminute[] = { BEACON_FIRST_S + 1, 60, 60 };
    Gaps("1-minute interval", oneminute, 3);

    // auto-beacon off: nothing
    sSt.flags &= (uint8_t)~STATION_AUTOBEACON;
    CHECK(NextGap(7200) == 0, "auto-beacon off: no beacons");

    printf("%d passed, %d failed\n", sPasses, sFails);
    return sFails != 0;
}
