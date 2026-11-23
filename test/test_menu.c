// Host test for menu.c's editors: callsign, position and comment entry, with
// beacon/station storage, drawing and EEPROM stubbed out.
//   cc -I . -I src -o build/test_menu test/test_menu.c src/menu.c src/memory.c src/textedit.c && build/test_menu

#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "app.h"
#include "beacon.h"
#include "gfx.h"
#include "menu.h"
#include "memory.h"
#include "station.h"

uint8_t gFrameBuffer[8][128];
void APP_Redraw(void) {}
static uint32_t sFreq = 14439000;
uint32_t APP_GetFrequency(void) { return sFreq; }
void APP_SetFrequency(uint32_t f) { sFreq = f; }
static uint8_t sEeprom[8192];
void EEPROM_ReadBuffer(uint32_t a, void *p, uint32_t n) { memcpy(p, sEeprom + a, n); }
void EEPROM_WriteBuffer(uint32_t a, const void *p) { memcpy(sEeprom + a, p, 8); }
bool RADIO_IsTunable(uint32_t f) { return f >= 14400000 && f <= 14800000; }
static const char *sMsg;
void APP_ShowMessage(const char *m, bool now) { (void)now; sMsg = m; }
uint8_t GFX_Text(uint8_t x, uint8_t p, const char *s) { (void)p; return (uint8_t)(x + strlen(s) * 7); }
uint8_t GFX_TextBold(uint8_t x, uint8_t p, const char *s) { return GFX_Text(x, p, s); }
void GFX_TextRightBold(uint8_t x, uint8_t p, const char *s) { (void)x; (void)p; (void)s; }
uint8_t GFX_TextWidth(const char *s) { return (uint8_t)(strlen(s) * 7); }
void GFX_TextRight(uint8_t x, uint8_t p, const char *s) { (void)x; (void)p; (void)s; }
void GFX_InvertPage(uint8_t p, uint8_t a, uint8_t b) { (void)p; (void)a; (void)b; }
void GFX_ShiftDown1(uint8_t p) { (void)p; }
void GFX_Divide(uint8_t p) { (void)p; }
uint32_t APRS_NowSeconds(void) { return 0; }

static BeaconConfig sB = { .magic = BEACON_MAGIC, .call = "VE6HUE", .ssid = 1, .path_hops = 2,
                           .symbol_table = '/', .symbol = '[', .lat = 51 * 6000 + 234, .lon = -(114 * 6000 + 486),
                           .comment = "hello" };
static bool sConfigured = true;
bool BEACON_IsConfigured(void) { return sConfigured; }
const BeaconConfig *BEACON_Get(void) { return &sB; }
bool BEACON_Set(const BeaconConfig *c)
{
    for (int i = 0; i < 6; i++) if (c->call[i] != ' ' && (c->call[i] < '0' || c->call[i] > 'Z')) return false;
    if (c->call[0] == ' ' || c->call[2] == ' ') return false;
    sB = *c; sConfigured = true; return true;
}
void BEACON_RestartTimer(void) {}
static BeaconStatus sBs = { .magic = BEACON_STATUS_MAGIC };
const BeaconStatus *BEACON_GetStatus(void) { return &sBs; }
bool BEACON_SetStatus(const BeaconStatus *b) { if (b->type >= BEACON_TYPE_COUNT) return false; sBs = *b; return true; }
static StationConfig sS = { .magic = STATION_MAGIC, .txdelay_flags = 40, .txtail_flags = 3, .digi_max_hops = 2, .beacon_interval_min = 30 };
const StationConfig *STATION_Get(void) { return &sS; }
bool STATION_Set(const StationConfig *c) { sS = *c; return true; }
bool STATION_Toggle(uint8_t f) { sS.flags ^= f; return sS.flags & f; }

static int sPass, sFail;
#define CHECK(c, ...) do { if (c) sPass++; else { sFail++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void Key(KEY_Code_t k) { MENU_HandleKey((KeyEvent_t){ KEV_PRESS, k }); MENU_HandleKey((KeyEvent_t){ KEV_RELEASE, k }); }
static void HoldExit(void) { MENU_HandleKey((KeyEvent_t){ KEV_PRESS, KEY_EXIT }); MENU_HandleKey((KeyEvent_t){ KEV_LONG, KEY_EXIT }); }
static void Wait(void) { for (int i = 0; i < 120; i++) MENU_Tick10ms(); }
static void Taps(KEY_Code_t k, int n) { while (n--) Key(k); }
static void Down(int n) { while (n--) Key(KEY_DOWN); }
static void Top(void) { for (int i = 0; i < 30; i++) Key(KEY_UP); }
static void HoldMenu(void) { MENU_HandleKey((KeyEvent_t){ KEV_PRESS, KEY_MENU }); MENU_HandleKey((KeyEvent_t){ KEV_LONG, KEY_MENU }); MENU_HandleKey((KeyEvent_t){ KEV_RELEASE, KEY_MENU }); }
static void HoldExitList(void) { MENU_HandleKey((KeyEvent_t){ KEV_PRESS, KEY_EXIT }); MENU_HandleKey((KeyEvent_t){ KEV_LONG, KEY_EXIT }); MENU_HandleKey((KeyEvent_t){ KEV_RELEASE, KEY_EXIT }); }

int main(void)
{
    memset(sEeprom, 0xFF, sizeof sEeprom);
    MEM_Load();
    MENU_Open();

    // ------------------------------------------------ memories: store 144.900 in slot 2 as DIGI
    Top();
    Key(KEY_MENU);                         // open Memories
    Down(2);
    sFreq = 14490000;
    HoldMenu();                            // store, then name it
    Taps(KEY_3, 1); Taps(KEY_4, 3); Wait(); Taps(KEY_4, 1); Wait(); Taps(KEY_4, 3);   // D I G I
    Key(KEY_MENU);
    const MemoryEntry *m2 = MEM_Get(2);
    CHECK(m2 && m2->frequency == 14490000 && memcmp(m2->name, "DIGI  ", 6) == 0, "slot 2 = 144.900 DIGI");
    CHECK(MEM_Find(14490000) == 2 && MEM_Find(14439000) == -1, "find by frequency");
    sFreq = 14439000;
    Key(KEY_MENU);                         // short MENU: tune and close
    CHECK(sFreq == 14490000 && !MENU_IsOpen(), "tuned to slot 2, menu closed");
    MENU_Open();
    Top(); Key(KEY_MENU); Top(); Down(2);   // the list remembers its selection
    HoldExitList();
    CHECK(MEM_Get(2) == NULL, "slot 2 cleared");
    Key(KEY_EXIT);                         // back to the settings list
    sMsg = NULL;
    Key(KEY_DOWN); Key(KEY_UP);            // still in the list (no crash)
    MEM_Load();
    CHECK(MEM_Get(2) == NULL, "cleared slot stays clear after reload");

    // ------------------------------------------------ blank radio: first callsign from the menu
    {
        const BeaconConfig saved = sB;
        memset(&sB, 0, sizeof sB);
        sConfigured = false;
        Top(); Down(1);                        // Callsign
        Key(KEY_MENU);
        Taps(KEY_8, 3); Wait(); Taps(KEY_3, 2); Wait(); Taps(KEY_6, 4); Wait();   // V E 6
        Taps(KEY_2, 3); Wait(); Taps(KEY_9, 2); Wait(); Taps(KEY_9, 4); Wait();   // C X Z
        Key(KEY_MENU);
        CHECK(sConfigured && !memcmp(sB.call, "VE6CXZ", 6), "first callsign saved ('%.6s', msg %s)", sB.call, sMsg ? sMsg : "-");
        Down(1); Key(KEY_MENU); Key(KEY_UP); Key(KEY_MENU);   // SSID 0 -> 1
        CHECK(sB.ssid == 1, "then the SSID can be set (%d, msg %s)", sB.ssid, sMsg ? sMsg : "-");
        sB = saved;
    }

    // ------------------------------------------------ callsign: VE6CXZ by multi-tap
    Top(); Down(1);
    Key(KEY_MENU);                         // edit Callsign
    Taps(KEY_8, 3); Taps(KEY_3, 2); Taps(KEY_6, 4); Taps(KEY_2, 3); Taps(KEY_9, 2);
    CHECK(memcmp(sB.call, "VE6HUE", 6) == 0, "not stored before MENU");
    Wait(); Taps(KEY_9, 4);                // same key twice in a row needs the pause
    Key(KEY_MENU);
    CHECK(memcmp(sB.call, "VE6CXZ", 6) == 0, "callsign VE6CXZ (got %.6s)", sB.call);

    // ------------------------------------------------ latitude 53 31.50N
    Top(); Down(3);
    Key(KEY_MENU);
    Key(KEY_5); Key(KEY_3); Key(KEY_3); Key(KEY_1); Key(KEY_5); Key(KEY_0);
    Key(KEY_MENU);
    CHECK(sB.lat == 53 * 6000 + 3150, "lat 53 31.50N (got %d)", (int)sB.lat);

    // ------------------------------------------------ longitude 113 29.40W, flip hemisphere twice
    Down(1);
    Key(KEY_MENU);
    Key(KEY_1); Key(KEY_1); Key(KEY_3); Key(KEY_2); Key(KEY_9); Key(KEY_4); Key(KEY_0);
    Key(KEY_UP); Key(KEY_UP);              // at the hemisphere: E, then W again
    Key(KEY_MENU);
    CHECK(sB.lon == -(113 * 6000 + 2940), "lon 113 29.40W (got %d)", (int)sB.lon);

    // ------------------------------------------------ invalid: 60 minutes refused, editing continues
    Key(KEY_MENU);
    Key(KEY_STAR); for (int i = 0; i < 8; i++) Key(KEY_STAR);   // cursor home
    Key(KEY_1); Key(KEY_1); Key(KEY_3); Key(KEY_6); Key(KEY_0);
    sMsg = NULL;
    Key(KEY_MENU);
    CHECK(sMsg && strcmp(sMsg, "BAD POSITION") == 0, "60 minutes refused");
    HoldExit();
    CHECK(sB.lon == -(113 * 6000 + 2940), "lon unchanged after cancel");

    // ------------------------------------------------ symbol: from the list, DOWN = next
    Down(1);
    Key(KEY_MENU); Key(KEY_DOWN); Key(KEY_MENU);
    CHECK(sB.symbol_table == '/' && sB.symbol == '>', "person -> car (got %c%c)", sB.symbol_table, sB.symbol);
    Key(KEY_MENU); Key(KEY_UP); Key(KEY_UP); Key(KEY_MENU);
    CHECK(sB.symbol == 'a', "UP past the top wraps to the end (got %c)", sB.symbol);
    sB.symbol_table = '\\'; sB.symbol = 'n';   // set from the host, not in the list
    Key(KEY_MENU); Key(KEY_DOWN); Key(KEY_MENU);
    CHECK(sB.symbol_table == '/' && sB.symbol == '[', "unknown symbol -> first in the list (got %c%c)", sB.symbol_table, sB.symbol);

    // ------------------------------------------------ comment: append " K5", backspace, lower case
    Down(1);
    Key(KEY_MENU);                         // cursor at the end of "hello"
    Key(KEY_0);                            // space
    Wait();
    Taps(KEY_5, 2); Key(KEY_5);            // K, then J: new tap only after a pause, so cycles to L
    Wait();
    Key(KEY_EXIT);                         // backspace the L
    Taps(KEY_5, 2); Wait();                // K
    Key(KEY_SIDE1);                        // lower case
    Taps(KEY_3, 2); Wait();                // e
    Key(KEY_MENU);
    CHECK(strcmp(sB.comment, "hello Ke") == 0, "comment 'hello Ke' (got '%s')", sB.comment);

    // insert in the middle: cursor to after "hello", type '!'
    Key(KEY_MENU);
    Key(KEY_STAR); Key(KEY_STAR); Key(KEY_STAR);   // before ' '
    Taps(KEY_1, 5);                        // . , - / !
    Key(KEY_MENU);
    CHECK(strcmp(sB.comment, "hello! Ke") == 0, "insert (got '%s')", sB.comment);

    // full: 39 characters max
    Key(KEY_MENU);
    for (int i = 0; i < 50; i++) { Key(KEY_2); Wait(); }
    Key(KEY_MENU);
    CHECK(strlen(sB.comment) == 39, "comment capped at 39 (got %zu)", strlen(sB.comment));

    // ------------------------------------------------ path still a value item
    Down(1);
    Key(KEY_MENU); Key(KEY_DOWN); Key(KEY_MENU);
    CHECK(sB.path_hops == 1, "path 2 -> 1 (got %u)", sB.path_hops);

    // ------------------------------------------------ compressed: an on/off flag, no callsign needed
    Down(1);
    Key(KEY_MENU);
    CHECK(sS.flags & STATION_COMPRESSED, "compressed on");
    Key(KEY_MENU);
    CHECK(!(sS.flags & STATION_COMPRESSED), "compressed off");

    // ------------------------------------------------ beacon type and status text
    Down(1);
    Key(KEY_MENU); Key(KEY_UP); Key(KEY_MENU);
    CHECK(sBs.type == BEACON_TYPE_STATUS, "POS -> STATUS (got %u)", sBs.type);
    Down(1);
    Key(KEY_MENU);
    Taps(KEY_3, 1); Wait(); Taps(KEY_4, 3); Wait(); Taps(KEY_4, 1); Wait(); Taps(KEY_4, 3); Wait();   // DIGI
    Key(KEY_0); Wait(); Key(KEY_0); Wait();   // trailing spaces are dropped
    Key(KEY_MENU);
    CHECK(strcasecmp(sBs.text, "digi") == 0, "status text '%s'", sBs.text);
    sConfigured = false;
    Key(KEY_MENU);
    CHECK(sMsg && !strcmp(sMsg, "SET CALLSIGN FIRST"), "status needs a callsign");
    sConfigured = true;

    // ------------------------------------------------ fast start: on by default (stored inverted)
    Down(3);                               // Auto beacon, Beacon every, Fast start
    CHECK(!(sS.flags & STATION_NO_FAST_START), "fast start on by default");
    Key(KEY_MENU);
    CHECK(sS.flags & STATION_NO_FAST_START, "fast start off");

    MENU_Render();
    printf("%d passed, %d failed\n", sPass, sFail);
    return sFail != 0;
}
