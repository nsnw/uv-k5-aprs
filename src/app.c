// APRS station UI (this branch boots straight into APRS mode and stays there).
//
// Split view: status, decode frequency and RSSI on top; the four most
// recently heard stations below.
//   UP / DOWN    select station. A dot before the callsign: heard direct.
//   *            list order: most recent first (with the age) / nearest
//                first (with distance and direction; needs our position)
//   MENU         station details: position, distance and bearing, speed,
//                altitude, path, comment
//   hold MENU    settings (menu.h): digipeater, beacon, TX timing, SSID
//   EXIT         (details) back to the split view
//   0-9          type a new frequency: * = decimal point, EXIT = delete
//                (hold: cancel), MENU = OK. Amateur TX bands only (tx.h).
//                In memory mode: go to that memory.
//   hold 0-9     recall that memory (memory.h; stored from the settings menu)
//   hold EXIT    frequency / memory mode. Memory mode shows the memory's
//                name in place of the frequency.
//                After our own transmissions (beacon, messages, acks; not
//                digipeats or KISS) a short beep, unless "TX beep" is off.
//                "RX beep" (off by default): a low beep for a new station
//                (NEW) or every new packet (ALL), not for digipeated copies.
//   F (#)        message inbox (inbox.h); the status bar shows a blinking
//                envelope while there are unread messages, and a new one
//                beeps (unless turned off in the settings, "Msg beep")
//   hold F (#)   queue our position beacon
//   hold *       KISS TNC on/off on the programming cable (see kiss.h)
//   SIDE1        digipeater on/off (digi.h)      } stored in EEPROM (station.h);
//   SIDE2        auto-beacon on/off (beacon.h)   } status line icon cells D and B
//   PTT          monitor

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "app.h"
#include "aprs.h"
#include "battery.h"
#include "beacon.h"
#include "beep.h"
#include "board.h"
#include "channel.h"
#include "console.h"
#include "driver/bk4819.h"
#include "driver/st7565.h"
#include "driver/systick.h"
#include "geo.h"
#include "gfx.h"
#include "input.h"
#include "kiss.h"
#include "inbox.h"
#include "memory.h"
#include "menu.h"
#include "msg.h"
#include "radio.h"
#include "settings.h"
#include "station.h"
#include "store.h"
#include "tx.h"
#include "txq.h"
#include "version.h"

#define ENTRY_MAX           10    // "MMMM.KKKHH" -> 10 Hz resolution
#define FRAC_DIGITS         5     // decimal digits of MHz in 10 Hz units
#define BACKLIGHT_TICKS     1000  // 10 s
#define SAVE_DELAY_TICKS    300   // store the frequency 3 s after the last change
#define RSSI_TICKS          10    // 100 ms
#define MESSAGE_TICKS       150   // 1.5 s
#define NEW_MSG_TICKS       500   // 5 s for "MSG FROM-CALL"
#define BATTERY_TICKS       50    // sample every 500 ms
#define SPLASH_US           1000000

static char     sEntry[ENTRY_MAX + 1];
static uint8_t  sEntryLen;
static uint16_t sBacklightTicks;
static uint16_t sSaveTicks;      // 0 = nothing pending
static uint32_t sSavedFrequency;
static uint8_t  sSavedMemory;
static uint8_t  sSavedListMode;
static uint8_t  sRssiTicks;
static int16_t  sRssi_dBm = -160;
static char     sAprsSel[10];    // selected station's callsign ("" = first)
static bool     sAprsDetail;     // details view instead of the list
static uint8_t  sAprsTop;        // first station row shown in the list
static const char *sMessage;
static uint16_t sMessageTicks;
static bool     sDirty = true;
static bool     sTransmitting;   // TX queue has the transmitter keyed
static bool     sMenuKeyDown;    // MENU pressed in the APRS view: details on release, settings on hold
static bool     sFKeyDown;       // F pressed: inbox on release, beacon on hold
static bool     sExitHeld;       // EXIT pressed in the split view: hold = memory mode
static bool     sStarKeyDown;    // * pressed (not typing): list order on release, KISS on hold

// ---------------------------------------------------------------- helpers

static void U32ToDec(uint32_t v, char *out, uint8_t width)  // zero-padded
{
    for (int i = width - 1; i >= 0; i--) {
        out[i] = '0' + (v % 10);
        v /= 10;
    }
    out[width] = '\0';
}

static void ShowMessageFor(const char *msg, uint16_t ticks)
{
    sMessage      = msg;
    sMessageTicks = ticks;
    sDirty        = true;
}

static void ShowMessage(const char *msg) { ShowMessageFor(msg, MESSAGE_TICKS); }

static void WakeBacklight(void)
{
    BOARD_SetBacklight(gSettings.backlight);
    sBacklightTicks = BACKLIGHT_TICKS;
}

// ---------------------------------------------------------------- actions

void APP_SetFrequency(uint32_t f)
{
    if (!RADIO_IsTunable(f)) {
        ShowMessage("ERROR");
        return;
    }
    gSettings.frequency = f;
    RADIO_Apply();
    if (gSettings.memory) {            // memory mode follows to a memory, else ends
        const int slot = MEM_Find(f);
        gSettings.memory = (uint8_t)(slot + 1);
    }
    sDirty = true;
}

// ---------------------------------------------------------------- memory mode

static const MemoryEntry *CurrentMemory(void)   // NULL in frequency mode
{
    return gSettings.memory ? MEM_Get((uint8_t)(gSettings.memory - 1)) : NULL;
}

static bool SelectMemory(uint8_t slot)
{
    const MemoryEntry *m = MEM_Get(slot);
    if (!m) {
        ShowMessage("EMPTY");
        return false;
    }
    APP_SetFrequency(m->frequency);
    if (gSettings.frequency != m->frequency)
        return false;                  // not tunable (shown as ERROR)
    gSettings.memory = (uint8_t)(slot + 1);
    return true;
}

static void ToggleMemoryMode(void)
{
    if (gSettings.memory) {
        gSettings.memory = 0;          // stay on the same frequency
        sDirty = true;
        return;
    }
    const int here = MEM_Find(gSettings.frequency);
    if (here >= 0) {
        gSettings.memory = (uint8_t)(here + 1);
        sDirty = true;
        return;
    }
    for (uint8_t i = 0; i < MEM_COUNT; i++)
        if (MEM_Get(i) && SelectMemory(i))
            return;
    ShowMessage("NO MEMORIES");
}

// The memory was cleared or overwritten (settings menu): back to frequency mode.
static void CheckMemoryMode(void)
{
    if (!gSettings.memory)
        return;
    const MemoryEntry *m = CurrentMemory();
    if (!m || m->frequency != gSettings.frequency) {
        gSettings.memory = 0;
        sDirty = true;
    }
}

// Splits the entry into MHz and fraction parts. Without an explicit '.',
// the first three digits are MHz.
static void SplitEntry(const char **mhz, uint8_t *mhz_len, const char **frac, uint8_t *frac_len)
{
    const char *dot = memchr(sEntry, '.', sEntryLen);
    if (dot) {
        *mhz_len = (uint8_t)(dot - sEntry);
        *frac    = dot + 1;
    } else {
        *mhz_len = sEntryLen < 3 ? sEntryLen : 3;
        *frac    = sEntry + *mhz_len;
    }
    *mhz      = sEntry;
    *frac_len = (uint8_t)(sEntry + sEntryLen - *frac);
}

static void ClearEntry(void)
{
    sEntryLen = 0;
    sEntry[0] = '\0';
    sDirty    = true;
}

static void CommitEntry(void)
{
    if (sEntryLen == 0)
        return;

    const char *mhz, *frac;
    uint8_t mhz_len, frac_len;
    SplitEntry(&mhz, &mhz_len, &frac, &frac_len);

    uint32_t f = 0;
    for (uint8_t i = 0; i < mhz_len; i++)
        f = f * 10 + (uint32_t)(mhz[i] - '0');
    for (uint8_t i = 0; i < FRAC_DIGITS; i++)
        f = f * 10 + (i < frac_len ? (uint32_t)(frac[i] - '0') : 0);

    ClearEntry();
    APP_SetFrequency(f);
}

static void EntryAppend(char c)
{
    if (sEntryLen >= ENTRY_MAX)
        return;

    const bool has_dot = memchr(sEntry, '.', sEntryLen) != NULL;
    if (c == '.' && (has_dot || sEntryLen == 0 || sEntryLen > 4))
        return;

    sEntry[sEntryLen++] = c;
    sEntry[sEntryLen]   = '\0';
    sDirty = true;

    const char *mhz, *frac;
    uint8_t mhz_len, frac_len;
    SplitEntry(&mhz, &mhz_len, &frac, &frac_len);
    if (frac_len >= FRAC_DIGITS)
        CommitEntry();
}

static void Render(void);

static void SendBeacon(void)
{
    if (!BEACON_IsConfigured()) {
        ShowMessage("NO BEACON SETUP");
        return;
    }
    switch (BEACON_Send()) {
    case BEACON_QUEUED:         ShowMessage("BEACON QUEUED");  break;
    case BEACON_QUEUE_FULL:     ShowMessage("TX QUEUE FULL");  break;
    case BEACON_NOT_CONFIGURED: ShowMessage("NO BEACON SETUP"); break;
    case BEACON_TX_REFUSED:     ShowMessage("NOT A TX FREQ");  break;
    case BEACON_NO_POSITION:    ShowMessage("NO POSITION SET"); break;
    }
}

// SIDE1 / SIDE2: digipeater / auto-beacon on and off (stored in EEPROM).
static void ToggleStation(uint8_t flag)
{
    if (!BEACON_IsConfigured()) {
        ShowMessage("NO CALLSIGN SET");   // both need MYCALL: `k5 beacon set`
        return;
    }
    const bool on = STATION_Toggle(flag);
    if (flag == STATION_AUTOBEACON) {
        BEACON_RestartTimer();
        ShowMessage(on ? "AUTO BEACON ON" : "AUTO BEACON OFF");
    } else {
        ShowMessage(on ? "DIGIPEATER ON" : "DIGIPEATER OFF");
    }
}

// ---------------------------------------------------------------- station list order

// Our position, if the beacon has one (distances need it).
static bool OwnPosition(int32_t *lat, int32_t *lon)
{
    if (!BEACON_IsConfigured())
        return false;
    const BeaconConfig *b = BEACON_Get();
    if (b->lat == 0 && b->lon == 0)
        return false;
    *lat = b->lat;
    *lon = b->lon;
    return true;
}

// Distance (m) and bearing to station i; false if it or we have no position.
static bool StationDistance(const AprsStation *st, uint32_t *m, uint16_t *bearing)
{
    int32_t lat, lon;
    if (!st->info.has_position || !OwnPosition(&lat, &lon))
        return false;
    *m = GEO_Distance(lat, lon, st->info.lat, st->info.lon, bearing);
    return true;
}

static bool ByDistance(void)
{
    int32_t lat, lon;
    return gSettings.list_by_distance && OwnPosition(&lat, &lon);
}

// Station indices in display order: most recently heard first, or nearest
// first (stations without a position after those with one, by recency).
static uint8_t ListOrder(uint8_t *order)
{
    const uint8_t n = APRS_StationCount();
    uint32_t key[APRS_MAX_STATIONS];
    const bool by_distance = ByDistance();
    for (uint8_t i = 0; i < n; i++) {
        uint16_t b;
        order[i] = i;
        key[i] = 0;
        if (by_distance && !StationDistance(APRS_GetStation(i), &key[i], &b))
            key[i] = UINT32_MAX;
    }
    for (uint8_t i = 1; i < n; i++) {            // insertion sort, stable
        const uint8_t o = order[i];
        uint8_t j = i;
        for (; j > 0 && key[order[j - 1]] > key[o]; j--)
            order[j] = order[j - 1];
        order[j] = o;
    }
    return n;
}

// Selection follows the callsign, since the list reorders as packets arrive.
static int AprsSelectedIndex(void)
{
    const int i = APRS_FindStation(sAprsSel);
    if (i >= 0)
        return i;
    uint8_t order[APRS_MAX_STATIONS];
    return ListOrder(order) ? order[0] : 0;      // nothing selected yet: the top row
}

// RX beep (station.h rx_beep): for a new station, or every new packet; not
// for repeats (digipeated copies), our own packets coming back, or in KISS
// mode (the host is the station).
static bool RxBeepWanted(void)
{
    const uint8_t mode = STATION_Get()->rx_beep;
    const AprsRxKind kind = APRS_LastRxKind();
    if (mode == STATION_RX_BEEP_OFF || KISS_IsActive() || kind == APRS_RX_REPEAT
        || (mode == STATION_RX_BEEP_NEW && kind != APRS_RX_NEW_STATION))
        return false;
    if (BEACON_IsConfigured()) {
        const BeaconConfig *b = BEACON_Get();
        char mine[10];
        uint8_t n = 0;
        for (int i = 0; i < 6 && b->call[i] && b->call[i] != ' '; i++) mine[n++] = b->call[i];
        if (b->ssid) {
            mine[n++] = '-';
            if (b->ssid >= 10) mine[n++] = '1';
            mine[n++] = (char)('0' + b->ssid % 10);
        }
        mine[n] = '\0';
        if (!strcmp(APRS_GetStation(0)->source, mine))
            return false;
    }
    return true;
}

static void ToggleListOrder(void)
{
    int32_t lat, lon;
    if (!OwnPosition(&lat, &lon)) {
        ShowMessage("NO POSITION SET");
        return;
    }
    gSettings.list_by_distance = !gSettings.list_by_distance;
    ShowMessage(gSettings.list_by_distance ? "NEAREST FIRST" : "LATEST FIRST");
    sAprsTop = 0;
}

static void HandleKey(KeyEvent_t ev)
{
    const KEY_Code_t k = ev.key;

    if (k == KEY_PTT) {
        if (ev.type == KEV_PRESS)   RADIO_SetMonitor(true);
        if (ev.type == KEV_RELEASE) RADIO_SetMonitor(false);
        sDirty = true;
        return;
    }

    if (MENU_IsOpen()) {
        MENU_HandleKey(ev);
        return;
    }
    if (INBOX_IsOpen()) {
        INBOX_HandleKey(ev);
        return;
    }

    // F: short press = message inbox (on release), hold = send our beacon.
    if (k == KEY_F) {
        if (ev.type == KEV_PRESS) {
            sFKeyDown = true;
        } else if (ev.type == KEV_LONG && sFKeyDown) {
            sFKeyDown = false;
            SendBeacon();
        } else if (ev.type == KEV_RELEASE && sFKeyDown) {
            sFKeyDown = false;
            INBOX_Open();
        }
        sDirty = true;
        return;
    }

    // * (not typing a frequency): short press = list order on release,
    // hold = KISS TNC on / off.
    if (k == KEY_STAR && (sStarKeyDown || !sEntryLen)) {
        if (ev.type == KEV_PRESS) {
            sStarKeyDown = true;
        } else if (ev.type == KEV_LONG && sStarKeyDown) {
            sStarKeyDown = false;
            if (KISS_IsActive()) KISS_Exit(); else KISS_Enter();
        } else if (ev.type == KEV_RELEASE && sStarKeyDown) {
            sStarKeyDown = false;
            ToggleListOrder();
        }
        sDirty = true;
        return;
    }

    // MENU (not confirming a typed frequency): short press = station details
    // on release, hold = settings.
    if (k == KEY_MENU && (sMenuKeyDown || !sEntryLen)) {
        if (ev.type == KEV_PRESS) {
            sMenuKeyDown = true;
        } else if (ev.type == KEV_LONG && sMenuKeyDown) {
            sMenuKeyDown = false;
            MENU_Open();
        } else if (ev.type == KEV_RELEASE && sMenuKeyDown) {
            sMenuKeyDown = false;
            if (APRS_StationCount())
                sAprsDetail = true;
            sDirty = true;
        }
        return;
    }

    const bool press  = ev.type == KEV_PRESS;
    const bool repeat = ev.type == KEV_LONG || ev.type == KEV_REPEAT;
    if (!press && !repeat)
        return;

    // Memory mode: a digit goes to that memory; no frequency typing.
    if (gSettings.memory && !sAprsDetail && k <= KEY_9) {
        if (press)
            SelectMemory((uint8_t)(k - KEY_0));
        sDirty = true;
        return;
    }

    // Typing a frequency (split view): digits, * as the point, EXIT
    // deletes (hold: cancel), MENU confirms. Retunes the decoder.
    if (!sAprsDetail && (k <= KEY_9 || (sEntryLen && (k == KEY_STAR || k == KEY_EXIT || k == KEY_MENU)))) {
        if (k <= KEY_9) {
            const char digit = (char)('0' + (k - KEY_0));
            if (press) {
                EntryAppend(digit);
            } else if (ev.type == KEV_LONG && sEntryLen == 1 && sEntry[0] == digit) {
                ClearEntry();                       // held: recall that memory instead
                const MemoryEntry *mem = MEM_Get((uint8_t)(k - KEY_0));
                if (mem) APP_SetFrequency(mem->frequency);
                else     ShowMessage("EMPTY");
            }
        } else if (k == KEY_STAR) {
            if (press) EntryAppend('.');
        } else if (k == KEY_MENU) {
            if (press) CommitEntry();
        } else if (repeat) {
            ClearEntry();
        } else {
            sEntry[--sEntryLen] = '\0';
            sExitHeld = false;              // deleting, not switching modes
        }
        sDirty = true;
        return;
    }
    if (k == KEY_SIDE1 && press) {
        ToggleStation(STATION_DIGI);
    } else if (k == KEY_SIDE2 && press) {
        ToggleStation(STATION_AUTOBEACON);
    } else if (k == KEY_EXIT && press) {
        sExitHeld   = !sAprsDetail;         // in the details view EXIT only goes back
        sAprsDetail = false;
    } else if (k == KEY_EXIT && ev.type == KEV_LONG && sExitHeld) {
        sExitHeld = false;
        ToggleMemoryMode();
    } else if (k == KEY_UP || k == KEY_DOWN) {
        uint8_t order[APRS_MAX_STATIONS];
        const int n = ListOrder(order), sel = AprsSelectedIndex();
        int row = 0;
        while (row < n && order[row] != sel) row++;
        row += (k == KEY_DOWN) ? 1 : -1;
        if (n && row >= 0 && row < n)
            memcpy(sAprsSel, APRS_GetStation(order[row])->source, sizeof sAprsSel);
    }
    sDirty = true;
}

// ---------------------------------------------------------------- drawing


// Big digits (one pixel short) on `page` and page+1, left-aligned; the entry
// hint goes on page+2.
// Memory mode: the name (or "MEM n") double size where the frequency goes,
// the frequency small below the signal meter.
static void DrawMemory(uint8_t page, const MemoryEntry *m)
{
    char name[MEM_NAME_LEN + 1];
    uint8_t len = 0;
    for (int i = 0; i < MEM_NAME_LEN && m->name[i] != ' '; i++) name[len++] = m->name[i];
    name[len] = '\0';
    if (!len) {
        strcpy(name, "MEM ");
        name[4] = (char)('0' + gSettings.memory - 1);
        name[5] = '\0';
    }
    GFX_TextDouble(0, (uint8_t)(page * 8 + 2), name);   // the same top row as the big digits

    const uint32_t f = gSettings.frequency, mhz = f / 100000;
    char freq[10];
    const uint8_t mw = mhz >= 1000 ? 4 : mhz >= 100 ? 3 : mhz >= 10 ? 2 : 1;
    U32ToDec(mhz, freq, mw);
    freq[mw] = '.';
    U32ToDec(f % 100000 / (mhz >= 1000 ? 1000 : 100), freq + mw + 1, mhz >= 1000 ? 2 : 3);
    GFX_TextRight(LCD_WIDTH, page + 1, freq);
}

static void DrawFrequency(uint8_t page)
{
    const MemoryEntry *mem = CurrentMemory();
    if (mem && !sEntryLen) {
        DrawMemory(page, mem);
        return;
    }
    char big[ENTRY_MAX + 2];
    char small[3] = { 0 };
    uint8_t n = 0;

    if (sEntryLen) {
        // what has been typed so far, with the implied point, and a '-' cursor
        const char *mhz, *frac;
        uint8_t mhz_len, frac_len;
        SplitEntry(&mhz, &mhz_len, &frac, &frac_len);
        memcpy(big + n, mhz, mhz_len);
        n += mhz_len;
        if (frac_len || memchr(sEntry, '.', sEntryLen))
            big[n++] = '.';
        memcpy(big + n, frac, frac_len);
        n += frac_len;
        big[n++] = '-';
        big[n] = '\0';
        GFX_Text(0, page + 2, GFX_KEY_MENU " OK " GFX_KEY_EXIT " DEL");
    } else {
        // MHz without leading zeros, then 3 decimals big and 2 decimals small
        const uint32_t f = gSettings.frequency;
        char frac[FRAC_DIGITS + 1];
        char mhz[6];
        const uint32_t m = f / 100000;
        U32ToDec(m, mhz, m >= 1000 ? 4 : (m >= 100 ? 3 : (m >= 10 ? 2 : 1)));
        U32ToDec(f % 100000, frac, FRAC_DIGITS);
        for (char *p = mhz; *p; p++) big[n++] = *p;
        big[n++] = '.';
        memcpy(big + n, frac, 3);
        n += 3;
        big[n] = '\0';
        small[0] = frac[3];
        small[1] = frac[4];
    }

    const uint8_t x = GFX_BigDigitsShort(0, page, big);
    // On a memory its name goes at the right, in place of the 10 Hz digits.
    const int slot = sEntryLen ? -1 : MEM_Find(gSettings.frequency);
    const MemoryEntry *m = slot >= 0 ? MEM_Get((uint8_t)slot) : NULL;
    char name[MEM_NAME_LEN + 1];
    uint8_t len = 0;
    if (m)
        for (int i = 0; i < MEM_NAME_LEN && m->name[i] != ' '; i++) name[len++] = m->name[i];
    name[len] = '\0';
    if (len)
        GFX_TextRight(LCD_WIDTH, page + 1, name);
    else if (small[0])
        GFX_Text(x + 1, page + 1, small);
}

// Draws `s` across consecutive pages, `cols` characters per line.
static void DrawWrapped(const char *s, uint8_t first_page, uint8_t last_page)
{
    const uint8_t cols = LCD_WIDTH / (GFX_CHAR_W + 1);
    char line[LCD_WIDTH / (GFX_CHAR_W + 1) + 1];
    for (uint8_t page = first_page; page <= last_page && *s; page++) {
        uint8_t n = 0;
        while (n < cols && s[n])
            n++;
        memcpy(line, s, n);
        line[n] = '\0';
        GFX_Text(0, page, line);
        s += n;
    }
}

static void FormatAge(uint32_t seconds, char *out)   // "12s", "5m", "3h", "2d"
{
    uint32_t v = seconds;
    char unit = 's';
    if (v >= 86400)     { v /= 86400; unit = 'd'; }
    else if (v >= 3600) { v /= 3600;  unit = 'h'; }
    else if (v >= 60)   { v /= 60;    unit = 'm'; }
    U32ToDec(v, out, v >= 10 ? 2 : 1);
    const uint8_t n = (uint8_t)strlen(out);
    out[n] = unit;
    out[n + 1] = '\0';
}

static void AppendInt(char *buf, int32_t v, const char *suffix)
{
    char *p = buf + strlen(buf);
    if (v < 0) { *p++ = '-'; v = -v; }
    U32ToDec((uint32_t)v, p, v >= 10000 ? 5 : v >= 1000 ? 4 : v >= 100 ? 3 : v >= 10 ? 2 : 1);
    strcat(buf, suffix);
}

static void StationAge(const AprsStation *st, char *out)   // "12s" ... "2d", or "old"
{
    if (st->flags & APRS_ST_OLD) strcpy(out, "old");
    else                         FormatAge(APRS_NowSeconds() - st->heard_s, out);
}

// One row: a dot if heard direct, the callsign, and at the right the age or
// (nearest-first order) distance and direction, dropping the direction if
// that would run into the callsign. Optionally inverted.
static void StationRow(uint8_t page, const AprsStation *st, bool selected, bool distance)
{
    if (st->flags & APRS_ST_DIRECT)
        GFX_Text(0, page, GFX_ICON_DOT);
    const uint8_t end = GFX_Text(5, page, st->source);
    char right[16];
    uint32_t m;
    uint16_t b;
    if (!distance) {
        StationAge(st, right);
    } else if (StationDistance(st, &m, &b)) {
        GEO_FormatDistance(m, right);
        const uint8_t n = (uint8_t)strlen(right);
        right[n] = ' ';
        strcpy(right + n + 1, GEO_Compass(b));
        if (GFX_TextWidth(right) + end + 2 > LCD_WIDTH - 1)
            right[n] = '\0';
    } else {
        strcpy(right, "--");
    }
    GFX_TextRight(LCD_WIDTH - 1, page, right);
    if (selected)
        GFX_InvertPage(page, 0, LCD_WIDTH - 1);
}

static void RenderAprsList(uint8_t first_page, uint8_t rows)
{
    uint8_t order[APRS_MAX_STATIONS];
    const int n = ListOrder(order), sel = AprsSelectedIndex();
    const bool distance = ByDistance();
    int row = 0;
    while (row < n && order[row] != sel) row++;
    if (row < sAprsTop)
        sAprsTop = (uint8_t)row;
    if (row >= sAprsTop + rows)
        sAprsTop = (uint8_t)(row - rows + 1);
    for (uint8_t r = 0; r < rows && sAprsTop + r < n; r++)
        StationRow(first_page + r, APRS_GetStation(order[sAprsTop + r]), sAprsTop + r == row, distance);
}

// Details: the callsign, symbol and age on top; then position (with the
// distance and bearing from us at the right), speed / course / altitude,
// how it was heard and the comment.
static void RenderAprsDetail(const AprsStation *st)
{
    char buf[32];
    if (st->flags & APRS_ST_DIRECT)
        GFX_Text(0, 2, GFX_ICON_DOT);
    const uint8_t x = GFX_Text(5, 2, st->source);
    StationAge(st, buf);
    GFX_TextRight(LCD_WIDTH - 1, 2, buf);

    char via[24] = "direct";
    if (!(st->flags & APRS_ST_DIRECT)) {
        strcpy(via, st->via[0] ? "via " : "via digipeater");
        strcat(via, st->via);
    }

    const AprsInfo *in = &st->info;
    if (!in->has_position) {
        GFX_InvertPage(2, 0, LCD_WIDTH - 1);
        DrawWrapped(st->comment, 3, 6);
        GFX_Text(0, 7, via);
        return;
    }
    buf[0] = in->symbol_table;   // APRS symbol, e.g. "/>" car, "/[" person
    buf[1] = in->symbol;
    buf[2] = '\0';
    GFX_Text(x + 4, 2, buf);
    GFX_InvertPage(2, 0, LCD_WIDTH - 1);

    APRS_FormatLat(in->lat, buf);
    GFX_Text(0, 3, buf);
    APRS_FormatLon(in->lon, buf);
    GFX_Text(0, 4, buf);
    uint32_t m;
    uint16_t b;
    if (StationDistance(st, &m, &b)) {
        GEO_FormatDistance(m, buf);
        GFX_TextRight(LCD_WIDTH, 3, buf);
        buf[0] = (char)('0' + b / 100);          // "047 NE"
        buf[1] = (char)('0' + b / 10 % 10);
        buf[2] = (char)('0' + b % 10);
        buf[3] = ' ';
        strcpy(buf + 4, GEO_Compass(b));
        GFX_TextRight(LCD_WIDTH, 4, buf);
    }

    buf[0] = '\0';
    if (in->speed_kmh != APRS_UNKNOWN_I16)
        AppendInt(buf, in->speed_kmh, "km/h ");
    if (in->course != APRS_UNKNOWN_I16)
        AppendInt(buf, in->course, "d ");
    if (in->altitude_m != INT32_MIN)
        AppendInt(buf, in->altitude_m, "m");
    GFX_Text(0, 5, buf);

    GFX_Text(0, 6, via);
    DrawWrapped(st->comment, 7, 7);
}

// Status icon: two cells in one box, D(igipeater) | B(eacon). Filled cell
// with the letter cut out = on; plain letter = off. They blink between the
// two when on but this isn't a TX frequency. 15 x 7 px.
static const uint8_t TINY_D[3] = { 0x1F, 0x11, 0x0E };   // 3 x 5 px letters
static const uint8_t TINY_B[3] = { 0x1F, 0x15, 0x0A };

typedef enum { CELL_OFF, CELL_LIT } CellState;

static void DrawCell(uint8_t x, const uint8_t *glyph, CellState st)
{
    uint8_t *fb = &gFrameBuffer[0][x];
    for (uint8_t i = 0; i < 6; i++) {           // interior columns; borders drawn by the caller
        const uint8_t g = (i >= 1 && i <= 3) ? (uint8_t)(glyph[i - 1] << 1) : 0;
        fb[i] = st == CELL_LIT ? (uint8_t)(0x7F ^ g) : (uint8_t)(0x41 | g);
    }
}

static void DrawStatusIcon(uint8_t x, uint8_t flags, bool can_tx)
{
    for (uint8_t i = 0; i < 3; i++)             // outer and divider lines
        gFrameBuffer[0][x + i * 7] = 0x7F;
    const CellState on = (can_tx || (APRS_NowSeconds() & 1)) ? CELL_LIT : CELL_OFF;
    DrawCell(x + 1, TINY_D, (flags & STATION_DIGI) ? on : CELL_OFF);
    DrawCell(x + 8, TINY_B, (flags & STATION_AUTOBEACON) ? on : CELL_OFF);
}

// Speaker: with sound waves while a packet is coming in or monitor is on,
// crossed out (muted) otherwise, and inverted (on a filled block) while
// transmitting. 11 x 7 px, plus a 1 px border when inverted.
static void DrawSpeaker(uint8_t x)
{
    static const uint8_t BODY[5] = { 0x1C, 0x1C, 0x3E, 0x7F, 0x7F };       // rows 0-6, as the box and text
    static const uint8_t WAVES[6] = { 0x00, 0x1C, 0x00, 0x22, 0x1C, 0x00 };
    static const uint8_t MUTED[6] = { 0x00, 0x22, 0x14, 0x08, 0x14, 0x22 };
    const uint8_t *right = (sTransmitting || RADIO_IsMonitoring() || APRS_IsReceiving()) ? WAVES : MUTED;
    memcpy(&gFrameBuffer[0][x], BODY, sizeof BODY);
    memcpy(&gFrameBuffer[0][x + sizeof BODY], right, sizeof WAVES);
    if (sTransmitting)                          // rows 0-6 only, clear of the divider
        for (uint8_t i = x - 1; i <= x + sizeof BODY + sizeof WAVES; i++)
            gFrameBuffer[0][i] ^= 0x7F;
}

// Battery outline with four bars, right-aligned: 2S Li-ion, 6.8 V empty to
// 8.2 V full. The empty outline blinks.
#define BATTERY_W 17
static void DrawBattery(void)
{
    const int32_t v = BATTERY_Get10mV();
    int32_t bars = (v - 680 + 34) / 35;
    bars = bars < 0 ? 0 : bars > 4 ? 4 : bars;
    if (bars == 0 && (APRS_NowSeconds() & 1))
        return;
    uint8_t *fb = &gFrameBuffer[0][LCD_WIDTH - BATTERY_W];
    fb[0] = fb[14] = 0x7F;
    for (uint8_t i = 1; i < 14; i++) {
        const uint8_t bar = (uint8_t)((i - 2) / 3), in_bar = (uint8_t)((i - 2) % 3) < 2;
        fb[i] = (uint8_t)(0x41 | ((i >= 2 && i <= 12 && in_bar && bar < bars) ? 0x1C : 0));
    }
    fb[15] = fb[16] = 0x1C;                     // terminal
}

// Split view: status line (MYCALL or KISS, status icon, battery), the decode
// frequency with a small signal meter, then the most recently heard stations.
// Page 0 on every screen: MYCALL (or KISS: the host is the station), the
// status icon and the battery.
static void DrawStatusBar(void)
{
    char buf[16];
    if (KISS_IsActive()) {
        GFX_Text(0, 0, "KISS");
    } else if (BEACON_IsConfigured()) {
        const BeaconConfig *b = BEACON_Get();
        uint8_t n = 0;
        for (int i = 0; i < 6 && b->call[i] && b->call[i] != ' '; i++)
            buf[n++] = b->call[i];
        if (b->ssid) {
            buf[n++] = '-';
            if (b->ssid >= 10) buf[n++] = '1';
            buf[n++] = (char)('0' + b->ssid % 10);
        }
        buf[n] = '\0';
        GFX_Text(0, 0, buf);
    } else {
        GFX_Text(0, 0, "NO CALL");
    }
#ifdef ENABLE_TX
    const bool can_tx = TX_IsAllowedFrequency(gSettings.frequency);
#else
    const bool can_tx = false;
#endif
    DrawStatusIcon(66, STATION_Get()->flags, can_tx);
    DrawSpeaker(88);
    if (MSG_Unread() && (APRS_NowSeconds() & 1)) {   // blinking envelope: unread messages (F opens the inbox)
        static const uint8_t ENVELOPE[9] = { 0x7F, 0x43, 0x45, 0x49, 0x51, 0x49, 0x45, 0x43, 0x7F };
        memcpy(&gFrameBuffer[0][101], ENVELOPE, sizeof ENVELOPE);
    }
    DrawBattery();
    GFX_HLine(0, LCD_WIDTH - 1, 8);    // the divider: also the status icon's bottom edge
}

static void RenderAprsSplit(void)
{
    // (the status bar's divider is at y = 8; the short big digits start two pixels below)

    DrawFrequency(1);
    uint8_t first_row = 3;
    if (sEntryLen) {
        first_row = 4;                 // the entry hint is on page 3
    } else if (gSettings.frequency < 100000000) {   // 4-digit MHz would overlap the meter
        // 30 px bar, -130 .. -50 dBm
        const int16_t lo = -130, hi = -50;
        const int16_t r = sRssi_dBm < lo ? lo : (sRssi_dBm > hi ? hi : sRssi_dBm);
        const uint8_t w = 30, x0 = LCD_WIDTH - w, len = (uint8_t)((r - lo) * w / (hi - lo));
        for (uint8_t i = 0; i < w; i++)
            gFrameBuffer[1][x0 + i] |= (i < len) ? ((i % 3 == 2) ? 0x00 : 0x3C) : ((i % 3 == 0) ? 0x20 : 0x00);   // OR: keep the divider
    }

    if (APRS_StationCount() == 0)
        GFX_Text(28, 5, "listening...");
    else
        RenderAprsList(first_row, (uint8_t)(LCD_PAGES - first_row));
}

static void RenderAprs(void)
{
    if (sAprsDetail && APRS_StationCount()) {
        RenderAprsDetail(APRS_GetStation((uint8_t)AprsSelectedIndex()));
    } else {
        RenderAprsSplit();
    }
}

static void Render(void)
{
    GFX_Clear();
    if (MENU_IsOpen())
        MENU_Render();
    else if (INBOX_IsOpen())
        INBOX_Render();
    else
        RenderAprs();
    DrawStatusBar();                   // after the content: the menu shifts its pages down

    if (sMessage) {
        const uint8_t w = GFX_TextWidth(sMessage);
        const uint8_t x = (LCD_WIDTH - w) / 2;
        memset(gFrameBuffer[4], 0, LCD_WIDTH);
        GFX_Text(x, 4, sMessage);
        GFX_InvertPage(4, x > 2 ? x - 2 : 0, x + w);
    }

    ST7565_Blit();
}

// ---------------------------------------------------------------- entry points

uint32_t APP_GetFrequency(void) { return gSettings.frequency; }

void APP_Redraw(void)
{
    sDirty = true;
}

void APP_ShowMessage(const char *msg, bool now)
{
    if (msg) {
        ShowMessage(msg);
    } else {
        sMessage      = NULL;
        sMessageTicks = 0;
        sDirty        = true;
    }
    if (now)
        Render();
}

void APP_SetTransmitting(bool on)
{
    sTransmitting = on;
    if (on)
        Render();
    else
        sDirty = true;
}

static void Splash(void)
{
    GFX_Clear();
    static const char *const lines[] = { "v" VERSION_STRING, "Andy Smith VE6LY" };
    GFX_TextDouble((uint8_t)((LCD_WIDTH - GFX_TextDoubleWidth(APP_NAME)) / 2), 12, APP_NAME);
    for (uint8_t i = 0; i < 2; i++) {
        const uint8_t w = GFX_TextWidth(lines[i]);
        GFX_Text(w < LCD_WIDTH ? (uint8_t)((LCD_WIDTH - w) / 2) : 0, (uint8_t)(4 + 2 * i), lines[i]);
    }
    ST7565_Blit();
    SYSTICK_DelayUs(SPLASH_US);
}

void APP_Init(void)
{
    WakeBacklight();
    Splash();
    APRS_Enter();       // this build is an APRS station only
    CheckMemoryMode();
    sSavedFrequency = gSettings.frequency;
    sSavedMemory    = gSettings.memory;
    sSavedListMode  = gSettings.list_by_distance;
    Render();
}

void APP_Tick10ms(void)
{
    const KeyEvent_t ev = INPUT_Poll10ms();
    if (ev.type != KEV_NONE) {
        if (ev.type == KEV_PRESS)
            WakeBacklight();
        HandleKey(ev);
    }

    if (KISS_IsActive())
        KISS_Poll10ms();
    else
        CONSOLE_Poll();
    TXQ_Poll10ms();
    if (TXQ_TakeLocalSent() && !(STATION_Get()->flags & STATION_TX_QUIET))
        BEEP_Sent();                   // our beacon / message / ack went out (not digipeats, KISS)
    MENU_Tick10ms();
    INBOX_Tick10ms();
    RADIO_Poll();

    if (++sRssiTicks >= RSSI_TICKS) {
        sRssiTicks = 0;
        const int16_t r = RADIO_GetRSSI_dBm();
        CHANNEL_ObserveRssi(r);
        if (r != sRssi_dBm) {
            sRssi_dBm = r;
            sDirty = true;
        }
    }

    static uint8_t batteryTicks;
    if (++batteryTicks >= BATTERY_TICKS) {
        batteryTicks = 0;
        static uint16_t lastVolts;
        BATTERY_Sample();
        if (BATTERY_Get10mV() != lastVolts) {
            lastVolts = BATTERY_Get10mV();
            sDirty = true;
        }
    }

    APRS_Tick10ms();
    const bool packet = APRS_TakeNewPacket();
    if (packet) {
        WakeBacklight();
        sDirty = true;
    }
    if (MSG_TakeChanged())
        sDirty = true;
    const bool new_message = MSG_TakeNew();
    if (packet && !new_message && RxBeepWanted()) {
        Render();
        BEEP_Received();               // ~150 ms: a digipeated copy right behind it may be missed
    }
    if (new_message) {
        static char note[4 + MSG_CALL_MAX + 1];
        strcpy(note, "MSG ");
        strcat(note, MSG_Get(0)->call);
        ShowMessageFor(note, NEW_MSG_TICKS);
        WakeBacklight();
        if (!(STATION_Get()->flags & STATION_MSG_QUIET)) {
            Render();                  // the note first, then the beep (blocks ~300 ms)
            BEEP_Message();
        }
    }
    static uint32_t lastSecond;
    if (APRS_NowSeconds() != lastSecond) {
        lastSecond = APRS_NowSeconds();   // keep the "heard ... ago" ages current
        BEACON_Poll1s(lastSecond);
        MSG_Poll1s(lastSecond);
        STORE_Poll1s(lastSecond);
        sDirty = true;
    }
    static bool lastReceiving;
    if (APRS_IsReceiving() != lastReceiving) {
        lastReceiving = APRS_IsReceiving();
        sDirty = true;
    }

    if (sMessageTicks && --sMessageTicks == 0) {
        sMessage = NULL;
        sDirty = true;
    }

    if (sBacklightTicks && --sBacklightTicks == 0)
        BOARD_SetBacklight(0);

    // Remember the frequency (keys, console or KISS), once it has settled.
    CheckMemoryMode();
    if (gSettings.frequency != sSavedFrequency || gSettings.memory != sSavedMemory
        || gSettings.list_by_distance != sSavedListMode) {
        sSavedFrequency = gSettings.frequency;
        sSavedMemory    = gSettings.memory;
        sSavedListMode  = gSettings.list_by_distance;
        sSaveTicks      = SAVE_DELAY_TICKS;
    }
    if (sSaveTicks && --sSaveTicks == 0)
        SETTINGS_Save();
    STORE_Poll10ms(APRS_IsReceiving());   // messages / stations to EEPROM, a page at a time

    if (sDirty) {
        sDirty = false;
        Render();
    }
}
