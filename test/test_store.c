// Host test for store.c (messages and stations kept in EEPROM) and aprs.c's
// via / restore logic, with the EEPROM simulated in RAM.
//   cc -DMSG_HOST_TEST -DAPRS_HOST_TEST -I . -I src -o build/test_store test/test_store.c \
//      src/store.c src/msg.c src/aprs.c src/ax25.c && build/test_store

#include <stdio.h>
#include <string.h>

#include "aprs.h"
#include "msg.h"
#include "store.h"

// EEPROM: 8 KiB, counts page writes (EEPROM_WriteBuffer skips unchanged pages, as the driver)
static uint8_t sRom[8192];
static int     sWrites;
void EEPROM_ReadBuffer(uint16_t a, void *p, uint8_t n) { memcpy(p, sRom + a, n); }
void EEPROM_WriteBuffer(uint16_t a, const void *p)
{
    if (memcmp(sRom + a, p, 8)) { memcpy(sRom + a, p, 8); sWrites++; }
}
// aprs.c's chip set-up isn't used here
void BK4819_WriteRegister(int r, uint16_t v) { (void)r; (void)v; }
bool MSG_TestTransmit(const char *to, const char *text) { (void)to; (void)text; return true; }

static int sPass, sFail;
#define CHECK(c, ...) do { if (c) sPass++; else { sFail++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void Run(uint32_t from_s, uint32_t to_s, bool busy)   // seconds, 100 ticks each
{
    for (uint32_t t = from_s; t < to_s; t++) {
        STORE_Poll1s(t);
        for (int i = 0; i < 100; i++) STORE_Poll10ms(busy);
    }
}

static void Addr(uint8_t *p, const char *call, uint8_t ssid, bool h, bool last)
{
    const size_t n = strlen(call);
    for (size_t i = 0; i < 6; i++) p[i] = (uint8_t)((i < n ? call[i] : ' ') << 1);
    p[6] = (uint8_t)(0x60 | (ssid << 1) | (h ? 0x80 : 0) | (last ? 1 : 0));
}

int main(void)
{
    // ---- via: the last digipeater that repeated it, skipping aliases
    uint8_t f[64];
    char via[10];
    bool direct;
    Addr(f, "APRS", 0, false, false); Addr(f + 7, "VE6LY", 7, false, true);
    APRS_FindVia(f, 14, via, sizeof via, &direct);
    CHECK(direct && !via[0], "no path: direct");
    Addr(f + 7, "VE6LY", 7, false, false);
    Addr(f + 14, "WIDE1", 1, false, false); Addr(f + 21, "WIDE2", 1, false, true);
    APRS_FindVia(f, 28, via, sizeof via, &direct);
    CHECK(direct && !via[0], "path not used yet: direct");
    Addr(f + 14, "VE6RPT", 10, true, false); Addr(f + 21, "WIDE1", 0, true, false); Addr(f + 28, "WIDE2", 1, false, true);
    APRS_FindVia(f, 35, via, sizeof via, &direct);
    CHECK(!direct && !strcmp(via, "VE6RPT-10"), "via the digipeater, not WIDE1 (%s)", via);
    Addr(f + 14, "WIDE1", 0, true, false); Addr(f + 21, "WIDE2", 1, false, true);
    APRS_FindVia(f, 28, via, sizeof via, &direct);
    CHECK(!direct && !via[0], "repeated by an alias-only digipeater: not direct, no name");
    Addr(f + 14, "VE6AA", 0, true, false); Addr(f + 21, "VE6BB", 2, true, false); Addr(f + 28, "WIDE2", 0, true, true);
    APRS_FindVia(f, 35, via, sizeof via, &direct);
    CHECK(!strcmp(via, "VE6BB-2"), "two hops: the last one (%s)", via);

    // ---- empty EEPROM: nothing loaded
    memset(sRom, 0xFF, sizeof sRom);
    STORE_Load();
    CHECK(MSG_Count() == 0 && APRS_StationCount() == 0, "blank EEPROM: empty");
    Run(1, 30, false);
    CHECK(sWrites == 0 && !STORE_Busy(), "nothing to save");

    // ---- a message is saved STORE_MSG_DELAY_S later
    char ack[6];
    MSG_HandleInfo("K1ABC", ":VE6CXZ-1 :hello there{5", 25, "VE6CXZ-1", 100, ack);
    CHECK(MSG_Count() == 1, "stored");
    Run(100, 101, false);
    CHECK(sWrites == 0, "not yet");
    Run(101, 100 + STORE_MSG_DELAY_S + 2, false);
    CHECK(sWrites > 0 && !STORE_Busy(), "saved (%d pages)", sWrites);
    const int msg_pages = sWrites;
    CHECK(msg_pages <= (16 + (int)sizeof(AprsMessage)) / 8 + 1, "only the used part written (%d)", msg_pages);

    // ---- nothing is written while a packet is coming in
    sWrites = 0;
    MSG_MarkRead(0);
    Run(200, 210, true);
    CHECK(sWrites == 0 && STORE_Busy(), "busy: waits");
    Run(210, 212, false);
    CHECK(sWrites == 1 && !STORE_Busy(), "then writes the one changed page (%d)", sWrites);

    // ---- stations: first save soon, then at most every STORE_STATION_INTERVAL_S
    uint8_t *count;
    AprsStation *st = APRS_StationTable(&count);
    memset(&st[0], 0, sizeof st[0]);
    strcpy(st[0].source, "VE6LY-7"); strcpy(st[0].via, "VE6RPT"); strcpy(st[0].comment, "mobile");
    st[0].heard_s = 300; st[0].packets = 3; st[0].flags = APRS_ST_DIRECT;
    st[0].info.has_position = true; st[0].info.lat = 51 * 6000; st[0].info.lon = -114 * 6000;
    *count = 1;
    sWrites = 0;
    Run(300, 300 + STORE_STATION_DELAY_S + 2, false);
    CHECK(sWrites > 0, "first station save after the delay (%d)", sWrites);
    sWrites = 0;
    for (uint32_t t = 320; t < 320 + STORE_STATION_INTERVAL_S - 30; t += 5) {
        st[0].heard_s = t; st[0].packets++;      // a busy channel
        Run(t, t + 5, false);
    }
    CHECK(sWrites == 0, "no rewrite within the interval (%d)", sWrites);
    Run(600, 640, false);
    CHECK(sWrites > 0 && sWrites <= 4, "one rewrite after it, changed pages only (%d)", sWrites);

    // ---- power-off and back: restored as old
    memset(st, 0, sizeof *st * APRS_MAX_STATIONS); *count = 0;
    uint8_t *mcount;
    MSG_Table(&mcount); *mcount = 0;
    STORE_Load();
    CHECK(APRS_StationCount() == 1 && !strcmp(APRS_GetStation(0)->source, "VE6LY-7"), "station back");
    const AprsStation *r = APRS_GetStation(0);
    CHECK((r->flags & APRS_ST_OLD) && (r->flags & APRS_ST_DIRECT) && r->heard_s == 0 && r->info.lat == 51 * 6000
          && !strcmp(r->comment, "mobile") && r->packets > 3, "station fields restored, marked old");
    CHECK(MSG_Count() == 1 && MSG_Get(0)->old && !strcmp(MSG_Get(0)->text, "hello there") && !MSG_Get(0)->unread,
          "message back (read), marked old");
    sWrites = 0;
    Run(700, 720, false);
    CHECK(sWrites == 0, "nothing rewritten just for loading (%d)", sWrites);

    // ---- a layout change reads as empty instead of garbage
    sRom[STORE_STATION_ADDR + 2] ^= 1;          // entry size in the header
    STORE_Load();
    CHECK(APRS_StationCount() == 0 && MSG_Count() == 1, "different layout: stations dropped, messages kept");

    // ---- a corrupt entry is dropped, the rest kept
    sRom[STORE_STATION_ADDR + 2] ^= 1;
    memset(sRom + STORE_MSG_ADDR + 16, 'A', 10); // message 0's callsign, unterminated
    STORE_Load();
    CHECK(MSG_Count() == 0 && APRS_StationCount() == 1, "bad message dropped, station kept");

    printf("%d passed, %d failed\n", sPass, sFail);
    return sFail != 0;
}
