// APRS station UI (this branch boots straight into APRS mode and stays there).
//
// Four pages under the status bar; SIDE1 / SIDE2 go to the previous / next
// (not while typing a message: SIDE1 is upper/lower case there).
// The status bar has two lines on the pages: the second is the frequency (or
// memory name) and a signal meter, and where a typed frequency shows.
//   Stations  the heard stations. The home page: EXIT comes back here.
//   Messages  the inbox (inbox.h)
//   Radio     channel, signal, packet counts, next beacon, battery
//   Packet    the latest packet heard, as TNC2 text
//
//   UP / DOWN    (stations) select a station. A dot before the callsign:
//                heard direct.
//   *            (stations) list order: most recent first (with the age) /
//                nearest first (with distance and direction; needs our
//                position)
//   MENU         (stations) station details: position, distance and
//                bearing, speed, altitude, path, comment
//   hold MENU    settings (menu.h): digipeater and auto-beacon on/off,
//                beacon, TX timing, SSID, beeps
//   EXIT         back to the station list (from details or another page)
//   0-9          type a new frequency (on the status bar): * = decimal
//                point, EXIT = delete (hold: cancel), MENU = OK. Amateur TX
//                bands only (tx.h). In memory mode: go to that memory.
//   hold 0-9     recall that memory (memory.h; stored from the settings menu)
//   hold EXIT    frequency / memory mode. Memory mode shows the memory's
//                name in place of the frequency.
//   F (#)        messages page (on it: write a new message; in the station
//                details: a message to that station); the status bar
//                shows a blinking envelope
//                while there are unread messages, and a new one beeps
//                (unless turned off in the settings, "Msg beep")
//   hold F (#)   queue our beacon (beacon.h: position and/or status)
//   hold *       KISS TNC on/off on the programming cable (see kiss.h)
//   PTT          monitor
//
// The front LED: red while transmitting, a green flash for each packet,
// green while monitoring, yellow while a frame waits to go out, a short
// yellow blink every 2 s while messages are unread (UpdateLed).
//
// After our own transmissions (beacon, messages, acks; not digipeats or
// KISS) a short beep, unless "TX beep" is off. "RX beep" (off by default):
// a low beep for a new station (NEW) or every new packet (ALL), not for
// digipeated copies. Status bar icons: digipeater and auto-beacon (only while on).

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
#define RX_LED_TICKS        15    // 150 ms green flash for each packet decoded
#define UNREAD_LED_PERIOD   200   // unread messages: a yellow blink every 2 s ...
#define UNREAD_LED_ON       15    // ... 150 ms long
#define LED_RED             1
#define LED_GREEN           2

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
static bool     sExitHeld;       // EXIT pressed (not typing): release = station list, hold = memory mode
static bool     sStarKeyDown;    // * pressed (not typing): list order on release, KISS on hold
static bool     sMsgFromDetail;  // writing a message started from the station details: go back there
static uint8_t  sRxLedTicks;     // green flash left for the latest packet
static uint8_t  sLed = 0xFF;     // LED_* last written; 0xFF = unknown (after transmitting)

typedef enum { PAGE_STATIONS, PAGE_MESSAGES, PAGE_RADIO, PAGE_PACKET, PAGE_COUNT } Page;
static uint8_t  sPage;           // Page

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
    case BEACON_STATUS_QUEUED:  ShowMessage("STATUS QUEUED");  break;
    case BEACON_QUEUE_FULL:     ShowMessage("TX QUEUE FULL");  break;
    case BEACON_NOT_CONFIGURED: ShowMessage("NO BEACON SETUP"); break;
    case BEACON_TX_REFUSED:     ShowMessage("NOT A TX FREQ");  break;
    case BEACON_NO_POSITION:    ShowMessage("NO POSITION SET"); break;
    }
}

// ---------------------------------------------------------------- station list order

// "VE6CXZ-1": MYCALL from the beacon configuration; false (and "") if unset.
static bool MyCall(char out[10])
{
    out[0] = '\0';
    if (!BEACON_IsConfigured())
        return false;
    const BeaconConfig *b = BEACON_Get();
    uint8_t n = 0;
    for (int i = 0; i < 6 && b->call[i] && b->call[i] != ' '; i++)
        out[n++] = b->call[i];
    if (b->ssid) {
        out[n++] = '-';
        if (b->ssid >= 10) out[n++] = '1';
        out[n++] = (char)('0' + b->ssid % 10);
    }
    out[n] = '\0';
    return true;
}

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
    char mine[10];
    return !MyCall(mine) || strcmp(APRS_GetStation(0)->source, mine) != 0;
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

static void SetPage(uint8_t page)
{
    if (sPage == PAGE_MESSAGES)
        INBOX_Close();
    ClearEntry();
    sAprsDetail = false;
    sExitHeld   = false;
    sPage       = page;
    if (page == PAGE_MESSAGES)
        INBOX_Open();
    sDirty = true;
}

static void HandleKey(KeyEvent_t ev)
{
    const KEY_Code_t k = ev.key;
    const bool press  = ev.type == KEV_PRESS;
    const bool repeat = ev.type == KEV_LONG || ev.type == KEV_REPEAT;

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

    // SIDE1 / SIDE2: previous / next page.
    if ((k == KEY_SIDE1 || k == KEY_SIDE2) && !INBOX_IsComposing()) {
        if (press)
            SetPage((uint8_t)((sPage + (k == KEY_SIDE2 ? 1 : PAGE_COUNT - 1)) % PAGE_COUNT));
        return;
    }

    // F: short press = messages page, or on it a new message (on release);
    // hold = send our beacon. (Typing a message, F belongs to the editor.)
    if (k == KEY_F && !INBOX_IsComposing()) {
        if (ev.type == KEV_PRESS) {
            sFKeyDown = true;
        } else if (ev.type == KEV_LONG && sFKeyDown) {
            sFKeyDown = false;
            SendBeacon();
        } else if (ev.type == KEV_RELEASE && sFKeyDown) {
            sFKeyDown = false;
            if (sPage == PAGE_MESSAGES) {
                INBOX_Compose();
            } else if (sPage == PAGE_STATIONS && sAprsDetail && APRS_StationCount()) {
                char call[10];          // a message to the station in the details
                memcpy(call, APRS_GetStation((uint8_t)AprsSelectedIndex())->source, sizeof call);
                SetPage(PAGE_MESSAGES);
                INBOX_ComposeTo(call);
                sMsgFromDetail = true;
            } else {
                SetPage(PAGE_MESSAGES);
            }
        }
        sDirty = true;
        return;
    }

    if (INBOX_IsOpen()) {
        INBOX_HandleKey(ev);
        if (sMsgFromDetail && !INBOX_IsComposing()) {   // sent or cancelled: back to the station
            sMsgFromDetail = false;
            SetPage(PAGE_STATIONS);
            sAprsDetail = APRS_StationCount() != 0;
        } else if (!INBOX_IsOpen()) {   // EXIT from the message list
            SetPage(PAGE_STATIONS);
        }
        return;
    }

    // * (not typing a frequency): short press = list order on release (on
    // the station list), hold = KISS TNC on / off.
    if (k == KEY_STAR && (sStarKeyDown || !sEntryLen)) {
        if (ev.type == KEV_PRESS) {
            sStarKeyDown = true;
        } else if (ev.type == KEV_LONG && sStarKeyDown) {
            sStarKeyDown = false;
            if (KISS_IsActive()) KISS_Exit(); else KISS_Enter();
        } else if (ev.type == KEV_RELEASE && sStarKeyDown) {
            sStarKeyDown = false;
            if (sPage == PAGE_STATIONS)
                ToggleListOrder();
        }
        sDirty = true;
        return;
    }

    // MENU (not confirming a typed frequency): short press = station details
    // on release (on the station list), hold = settings.
    if (k == KEY_MENU && (sMenuKeyDown || !sEntryLen)) {
        if (ev.type == KEV_PRESS) {
            sMenuKeyDown = true;
        } else if (ev.type == KEV_LONG && sMenuKeyDown) {
            sMenuKeyDown = false;
            MENU_Open();
        } else if (ev.type == KEV_RELEASE && sMenuKeyDown) {
            sMenuKeyDown = false;
            if (sPage == PAGE_STATIONS && APRS_StationCount())
                sAprsDetail = true;
            sDirty = true;
        }
        return;
    }

    // EXIT (not typing): back to the station list on release, from the
    // details or another page; hold = frequency / memory mode.
    if (k == KEY_EXIT && !sEntryLen) {
        if (press && sAprsDetail) {
            sAprsDetail = false;            // in the details view EXIT only goes back
        } else if (press) {
            sExitHeld = true;
        } else if (ev.type == KEV_LONG && sExitHeld) {
            sExitHeld = false;
            ToggleMemoryMode();
        } else if (ev.type == KEV_RELEASE && sExitHeld) {
            sExitHeld = false;
            if (sPage != PAGE_STATIONS)
                SetPage(PAGE_STATIONS);
        }
        sDirty = true;
        return;
    }

    if (!press && !repeat)
        return;

    // Memory mode: a digit goes to that memory; no frequency typing.
    if (gSettings.memory && !sAprsDetail && k <= KEY_9) {
        if (press)
            SelectMemory((uint8_t)(k - KEY_0));
        sDirty = true;
        return;
    }

    // Typing a frequency (on the status bar's second line, any page): digits,
    // * as the point, EXIT deletes (hold: cancel), MENU confirms.
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
        }
        sDirty = true;
        return;
    }

    if ((k == KEY_UP || k == KEY_DOWN) && sPage == PAGE_STATIONS) {
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

// ---------------------------------------------------------------- the LED

// The front LED (BK4819 GPIO5 red, GPIO6 green; both = yellow), most
// important first: red while transmitting (tx.c sets it itself), a green
// flash for each packet, green while monitoring, yellow while something
// waits to go out, a short yellow blink every 2 s while messages are unread.
static void UpdateLed(void)
{
    static uint8_t phase;
    phase = (uint8_t)((phase + 1) % UNREAD_LED_PERIOD);
    uint8_t led = 0;
    if (sRxLedTicks) {
        sRxLedTicks--;
        led = LED_GREEN;
    } else if (RADIO_IsAudioOpen()) {
        led = LED_GREEN;
    } else if (TXQ_Count()) {
        led = LED_RED | LED_GREEN;
    } else if (MSG_Unread() && phase < UNREAD_LED_ON) {
        led = LED_RED | LED_GREEN;
    }
    if (led == sLed)
        return;
    sLed = led;
    BK4819_ToggleGpioOut(BK4819_GPIO5_PIN1_RED, led & LED_RED);
    BK4819_ToggleGpioOut(BK4819_GPIO6_PIN2_GREEN, led & LED_GREEN);
}

// ---------------------------------------------------------------- drawing


// "144.390" (3 decimals; 2 from 1000 MHz). Buffer of at least 9 bytes.
static void FormatMHz(uint32_t f, char *out)
{
    const uint32_t mhz = f / 100000;
    const uint8_t mw = mhz >= 1000 ? 4 : mhz >= 100 ? 3 : mhz >= 10 ? 2 : 1;
    U32ToDec(mhz, out, mw);
    out[mw] = '.';
    U32ToDec(f % 100000 / (mhz >= 1000 ? 1000 : 100), out + mw + 1, mhz >= 1000 ? 2 : 3);
}

// The memory's name ("" if none or blank) for the current frequency.
static void MemoryName(char out[MEM_NAME_LEN + 1])
{
    const int slot = MEM_Find(gSettings.frequency);
    const MemoryEntry *m = slot >= 0 ? MEM_Get((uint8_t)slot) : NULL;
    uint8_t len = 0;
    if (m)
        for (int i = 0; i < MEM_NAME_LEN && m->name[i] != ' '; i++) out[len++] = m->name[i];
    out[len] = '\0';
}

// Signal meter, `w` px wide from x0 on `page` (rows 2-5): -130 .. -50 dBm.
static void DrawMeter(uint8_t page, uint8_t x0, uint8_t w)
{
    const int16_t lo = -130, hi = -50;
    const int16_t r = sRssi_dBm < lo ? lo : (sRssi_dBm > hi ? hi : sRssi_dBm);
    const uint8_t len = (uint8_t)((r - lo) * w / (hi - lo));
    for (uint8_t i = 0; i < w; i++)   // OR: keep anything already there (the divider)
        gFrameBuffer[page][x0 + i] |= (i < len) ? ((i % 3 == 2) ? 0x00 : 0x3C) : ((i % 3 == 0) ? 0x20 : 0x00);
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
    const uint8_t end = GFX_TextBold(5, page, st->source);
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

// Details (divided, gfx.h): the callsign, symbol and age on top; then
// position (with the distance and bearing from us at the right), speed /
// course / altitude, how it was heard and the comment, as many as fit. The
// motion line only if there is one; "direct" not at all (the dot says so).
static void RenderAprsDetail(const AprsStation *st)
{
    char buf[32];
    const uint8_t head = APP_TOP_PAGE, last = GFX_LAST_BODY_PAGE;
    uint8_t row = head + 1;
    if (st->flags & APRS_ST_DIRECT)
        GFX_Text(0, head, GFX_ICON_DOT);
    const uint8_t x = GFX_TextBold(5, head, st->source);
    StationAge(st, buf);
    GFX_TextRightBold(LCD_WIDTH - 1, head, buf);

    char via[24] = "";
    if (!(st->flags & APRS_ST_DIRECT)) {
        strcpy(via, st->via[0] ? "via " : "via digipeater");
        strcat(via, st->via);
    }

    const AprsInfo *in = &st->info;
    if (!in->has_position) {
        DrawWrapped(st->comment, row, via[0] ? last - 1 : last);
        if (via[0])
            GFX_Text(0, last, via);
        GFX_Divide(head);
        return;
    }
    buf[0] = in->symbol_table;   // APRS symbol, e.g. "/>" car, "/[" person
    buf[1] = in->symbol;
    buf[2] = '\0';
    GFX_TextBold(x + 4, head, buf);

    buf[0] = ' ';                               // " 51 02.14N": lines up with "114 05.67W"
    APRS_FormatLat(in->lat, buf + 1);
    GFX_Text(0, row, buf);
    APRS_FormatLon(in->lon, buf);
    GFX_Text(0, row + 1, buf);
    uint32_t m;
    uint16_t b;
    if (StationDistance(st, &m, &b)) {
        GEO_FormatDistance(m, buf);
        GFX_TextRight(LCD_WIDTH, row, buf);
        buf[0] = (char)('0' + b / 100);          // "047 NE"
        buf[1] = (char)('0' + b / 10 % 10);
        buf[2] = (char)('0' + b % 10);
        buf[3] = ' ';
        strcpy(buf + 4, GEO_Compass(b));
        GFX_TextRight(LCD_WIDTH, row + 1, buf);
    }
    row += 2;

    buf[0] = '\0';
    if (in->speed_kmh != APRS_UNKNOWN_I16)
        AppendInt(buf, in->speed_kmh, "km/h ");
    if (in->course != APRS_UNKNOWN_I16)
        AppendInt(buf, in->course, "d ");
    if (in->altitude_m != INT32_MIN)
        AppendInt(buf, in->altitude_m, "m");
    if (buf[0])
        GFX_Text(0, row++, buf);
    if (via[0] && row <= last)
        GFX_Text(0, row++, via);
    if (row <= last)
        DrawWrapped(st->comment, row, row);
    GFX_Divide(head);
}

// Status bar. Page 0 on every screen, left to right:
//   MYCALL (or KISS: the host is the station)
//   digipeater (two arrows) and auto-beacon (a map pin), only while on;
//     they blink if on but this isn't a TX frequency
//   a blinking envelope while there are unread messages
//   the speaker
//   the battery, upright
// On the pages, a second line (page 1): the frequency (memory mode: the
// memory's name instead, or "MEM n"), a signal meter and the page number
// ("2/4", tiny digits) at the right; while typing a frequency, what's typed
// and the key hints.
#define X_DIGI      81
#define X_BEACON    90
#define X_ENVELOPE  98
#define X_SPEAKER   110     // 109-121 while inverted
#define X_BATTERY   123     // to the right edge
#define PAGE_NUM_W  11      // "2/4", right-aligned on the second line
#define METER_W     30      // ten bars, left of the page number
#define X_METER     (LCD_WIDTH - PAGE_NUM_W - 2 - METER_W)

static void DrawIcon(uint8_t x, const uint8_t *cols, uint8_t w)
{
    for (uint8_t i = 0; i < w; i++)
        gFrameBuffer[0][x + i] |= cols[i];
}

static void DrawStationIcons(uint8_t flags, bool can_tx)
{
    static const uint8_t DIGI[7] = { 0x22, 0x72, 0x22, 0x22, 0x22, 0x27, 0x22 };   // arrows both ways
    static const uint8_t PIN[5]  = { 0x0E, 0x11, 0x25, 0x11, 0x0E };               // map pin
    if (!can_tx && (APRS_NowSeconds() & 1))
        return;
    if (flags & STATION_DIGI)
        DrawIcon(X_DIGI, DIGI, sizeof DIGI);
    if (flags & STATION_AUTOBEACON)
        DrawIcon(X_BEACON, PIN, sizeof PIN);
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

// Upright battery, 5 x 7 px: the terminal on top, four bars filling from
// the bottom (2S Li-ion, 6.8 V empty to 8.2 V full). The empty outline blinks.
static void DrawBattery(void)
{
    const int32_t v = BATTERY_Get10mV();
    int32_t bars = (v - 680 + 34) / 35;
    bars = bars < 0 ? 0 : bars > 4 ? 4 : bars;
    if (bars == 0 && (APRS_NowSeconds() & 1))
        return;
    const uint8_t fill = (uint8_t)((0x3C << (4 - bars)) & 0x3C);   // rows 5, 4, 3, 2
    uint8_t *fb = &gFrameBuffer[0][X_BATTERY];
    fb[0] = fb[4] = 0x7E;                       // sides, rows 1-6
    fb[1] = fb[2] = fb[3] = (uint8_t)(0x01 | 0x02 | 0x40 | fill);   // terminal, top, bottom
}

// "2/4" in 3 x 5 digits (rows 1-5 of `page`), right-aligned.
static void DrawPageNumber(uint8_t page)
{
    static const uint8_t TINY[10][3] = {
        { 0x30, 0x08, 0x06 },                                       // '/'
        { 0x24, 0x3E, 0x20 }, { 0x32, 0x2A, 0x24 }, { 0x22, 0x2A, 0x14 },
        { 0x0E, 0x08, 0x3E }, { 0x2E, 0x2A, 0x12 }, { 0x3C, 0x2A, 0x3A },
        { 0x02, 0x3A, 0x06 }, { 0x3E, 0x2A, 0x3E }, { 0x2E, 0x2A, 0x1E },
    };
    _Static_assert(PAGE_COUNT <= 9, "one digit");
    const uint8_t glyphs[3] = { (uint8_t)(sPage + 1), 0, PAGE_COUNT };
    uint8_t x = LCD_WIDTH - PAGE_NUM_W;
    for (uint8_t g = 0; g < 3; g++, x += 4)
        for (uint8_t i = 0; i < 3; i++)
            gFrameBuffer[page][x + i] |= TINY[glyphs[g]][i];
}

static void DrawFrequencyLine(void)
{
    char buf[ENTRY_MAX + 2];
    if (sEntryLen) {
        // what has been typed so far, with the implied point, and a '-' cursor
        const char *mhz, *frac;
        uint8_t mhz_len, frac_len, n = 0;
        SplitEntry(&mhz, &mhz_len, &frac, &frac_len);
        memcpy(buf + n, mhz, mhz_len);
        n += mhz_len;
        if (frac_len || memchr(sEntry, '.', sEntryLen))
            buf[n++] = '.';
        memcpy(buf + n, frac, frac_len);
        n += frac_len;
        buf[n++] = '-';
        buf[n] = '\0';
        GFX_TextBold(0, 1, buf);
        GFX_TextRight(LCD_WIDTH - 1, 1, GFX_KEY_MENU " OK " GFX_KEY_EXIT " DEL");
        return;
    }
    if (gSettings.memory) {                     // memory mode: the memory's name
        char name[MEM_NAME_LEN + 1];
        MemoryName(name);
        if (!name[0]) {
            strcpy(name, "MEM ");
            name[4] = (char)('0' + gSettings.memory - 1);
            name[5] = '\0';
        }
        GFX_TextBold(0, 1, name);
    } else {                                    // frequency mode: the frequency
        FormatMHz(gSettings.frequency, buf);
        GFX_TextBold(0, 1, buf);
    }
    DrawMeter(1, X_METER, METER_W);
    DrawPageNumber(1);
}

// Two lines on the pages; one in the settings and while typing a message.
static bool TwoLineStatus(void)
{
    return !MENU_IsOpen() && !INBOX_IsComposing();
}

static void DrawStatusBar(void)
{
    char buf[10];
    GFX_TextBold(0, 0, KISS_IsActive() ? "KISS" : MyCall(buf) ? buf : "NO CALL");
#ifdef ENABLE_TX
    const bool can_tx = TX_IsAllowedFrequency(gSettings.frequency);
#else
    const bool can_tx = false;
#endif
    DrawStationIcons(STATION_Get()->flags, can_tx);
    if (MSG_Unread() && (APRS_NowSeconds() & 1)) {   // blinking envelope: unread messages (F opens them)
        static const uint8_t ENVELOPE[9] = { 0x7F, 0x43, 0x45, 0x49, 0x51, 0x49, 0x45, 0x43, 0x7F };
        memcpy(&gFrameBuffer[0][X_ENVELOPE], ENVELOPE, sizeof ENVELOPE);
    }
    DrawSpeaker(X_SPEAKER);
    DrawBattery();
    if (TwoLineStatus()) {
        DrawFrequencyLine();
        GFX_HLine(0, LCD_WIDTH - 1, APP_TOP_PAGE * 8);   // the divider
    } else {
        GFX_HLine(0, LCD_WIDTH - 1, 8);
    }
}

// Label at the left of `page`, value at the right.
static void InfoRow(uint8_t page, const char *label, const char *value)
{
    GFX_Text(0, page, label);
    GFX_TextRight(LCD_WIDTH - 1, page, value);
}

// Stations page: the list, straight under the status bar (its second line
// has the frequency).
static void RenderStations(void)
{
    if (APRS_StationCount() == 0)
        GFX_Text(28, APP_TOP_PAGE + 2, "listening...");
    else
        RenderAprsList(APP_TOP_PAGE, GFX_LAST_BODY_PAGE - APP_TOP_PAGE + 1);
    GFX_Lower(APP_TOP_PAGE);
}

// Radio page (divided, gfx.h): the channel on top; signal, packets heard
// and sent, next beacon and battery.
static void RenderRadio(void)
{
    char buf[24];
    const uint8_t head = APP_TOP_PAGE, b = head + 1;
    GFX_TextBold(0, head, "Channel");
    GFX_TextRightBold(LCD_WIDTH - 1, head, CHANNEL_IsBusy() ? "busy" : "clear");

    buf[0] = '\0';
    AppendInt(buf, sRssi_dBm, "dBm");
    InfoRow(b, "Signal", buf);

    strcpy(buf, "Heard ");                      // packets decoded / transmitted
    AppendInt(buf, APRS_TotalDecoded(), "");
    char sent[12] = "";
#ifdef ENABLE_TX
    strcpy(sent, "Sent ");
    AppendInt(sent, gTxStats.attempts, "");
#endif
    InfoRow(b + 1, buf, sent);

    const uint32_t next = BEACON_SecondsToNext(APRS_NowSeconds());
    if (!(STATION_Get()->flags & STATION_AUTOBEACON)) {
        strcpy(buf, "off");
    } else if (next) {
        strcpy(buf, "in ");
        FormatAge(next, buf + 3);
    } else {
        strcpy(buf, "soon");
    }
    InfoRow(b + 2, "Beacon", buf);

    const uint16_t v = BATTERY_Get10mV();
    buf[0] = (char)('0' + v / 100 % 10);
    buf[1] = '.';
    buf[2] = (char)('0' + v / 10 % 10);
    buf[3] = (char)('0' + v % 10);
    buf[4] = 'V';
    buf[5] = '\0';
    InfoRow(b + 3, "Battery", buf);
    GFX_Divide(head);
}

// Packet page (divided, gfx.h): the latest packet heard, as TNC2 text, and
// how long ago.
static void RenderPacket(void)
{
    uint32_t heard;
    const char *line = APRS_LastPacket(&heard);
    const uint8_t head = APP_TOP_PAGE;
    GFX_TextBold(0, head, "Last packet");
    if (!line[0]) {
        GFX_Text(4, head + 2, "nothing heard yet");
    } else {
        char age[8];
        FormatAge(APRS_NowSeconds() - heard, age);
        GFX_TextRightBold(LCD_WIDTH - 1, head, age);
        DrawWrapped(line, head + 1, GFX_LAST_BODY_PAGE);
    }
    GFX_Divide(head);
}

static void Render(void)
{
    GFX_Clear();
    if (MENU_IsOpen())
        MENU_Render();
    else if (sPage == PAGE_MESSAGES)
        INBOX_Render();
    else if (sPage == PAGE_RADIO)
        RenderRadio();
    else if (sPage == PAGE_PACKET)
        RenderPacket();
    else if (sAprsDetail && APRS_StationCount())
        RenderAprsDetail(APRS_GetStation((uint8_t)AprsSelectedIndex()));
    else
        RenderStations();
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
    sLed = 0xFF;                       // tx.c drives red itself: rewrite the LED afterwards
    if (on)
        Render();
    else
        sDirty = true;
}

static void CentreDouble(uint8_t y, const char *s)
{
    const uint8_t w = GFX_TextDoubleWidth(s);
    GFX_TextDouble(w < LCD_WIDTH ? (uint8_t)((LCD_WIDTH - w) / 2) : 0, y, s);
}

// A title bar, white on black, with K5-APRS centred in it (double size);
// the version under it (small, centred, not inverted); then this radio's
// callsign (double size, centred: tells two radios apart at a glance).
static void Splash(void)
{
    char call[10];
    GFX_Clear();
    CentreDouble(1, APP_NAME);                  // rows 1-14
    GFX_Text((uint8_t)((LCD_WIDTH - GFX_TextWidth(VERSION_STRING)) / 2), 2, VERSION_STRING);   // rows 16-22
    GFX_ShiftDown1(0);                          // all down a pixel: the name in rows 2-15
    GFX_Lower(2);                               // the version two more: rows 19-25, clear of the bar
    GFX_InvertPage(0, 0, LCD_WIDTH - 1);        // the bar: rows 0-16, a pixel of margin all round
    GFX_InvertPage(1, 0, LCD_WIDTH - 1);
    for (uint8_t x = 0; x < LCD_WIDTH; x++)
        gFrameBuffer[2][x] |= 0x01;             // row 16
    gFrameBuffer[0][0] &= (uint8_t)~0x01;       // rounded corners
    gFrameBuffer[0][LCD_WIDTH - 1] &= (uint8_t)~0x01;
    gFrameBuffer[2][0] &= (uint8_t)~0x01;
    gFrameBuffer[2][LCD_WIDTH - 1] &= (uint8_t)~0x01;
    CentreDouble(38, MyCall(call) ? call : "NO CALL"); // rows 38-51, centred in rows 26-63
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
    if (packet)
        sRxLedTicks = RX_LED_TICKS;
    UpdateLed();
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
