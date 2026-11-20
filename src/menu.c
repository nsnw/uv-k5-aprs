#include <string.h>

#include "app.h"
#include "beacon.h"
#include "driver/st7565.h"
#include "gfx.h"
#include "memory.h"
#include "menu.h"
#include "station.h"
#include "textedit.h"

enum {
    ITEM_MEMORIES,
    ITEM_CALL,
    ITEM_SSID,
    ITEM_LAT,
    ITEM_LON,
    ITEM_COMMENT,
    ITEM_PATH,
    ITEM_AUTOBEACON,
    ITEM_INTERVAL,
    ITEM_DIGI,
    ITEM_DIGI_HOPS,
    ITEM_MSG_BEEP,
    ITEM_TX_BEEP,
    ITEM_RX_BEEP,
    ITEM_TXDELAY,
    ITEM_TXTAIL,
    ITEM_COUNT
};

// Labels are short: a row is 18 characters (7 px each).
static const char *const LABELS[ITEM_COUNT] = {
    "Memories", "Callsign", "SSID", "Lat", "Lon", "Comment", "Path", "Auto beacon",
    "Beacon every", "Digipeater", "Digi hops", "Msg beep", "TX beep", "RX beep", "TX delay", "TX tail",
};

#define ROWS    7                   // pages 1..7, one pixel down; page 0 is the status bar
#define CELL_W  (GFX_CHAR_W + 1)    // one character on screen
#define COLS    (LCD_WIDTH / CELL_W)

// Choices for UP/DOWN; a value set from the host steps to the next one.
static const uint16_t INTERVALS[] = { 1, 2, 5, 10, 15, 20, 30, 60, 120 };   // minutes
static const uint16_t DELAYS[] = {                 // flags: 53, 100, 153, 200, 253, 300 ... 1333 ms
    8, 15, 23, 30, 38, 45, 53, 60, 68, 75, 90, 105, 120, 150, 180, 200 };
static const uint16_t TAILS[] = { 1, 2, 3, 5, 8, 15, 23, 30, 45 };          // 7 ... 300 ms
#define COUNT(a) (sizeof (a) / sizeof (a)[0])

typedef enum {
    ED_NONE,
    ED_VALUE,       // UP/DOWN change it
    ED_CALL,        // 6 characters, overwrite
    ED_POS,         // DDMM.mm[N/S] / DDDMM.mm[E/W] digit template
    ED_COMMENT,     // free text, insert at the cursor (full screen)
    ED_MEMLIST,     // the memory list (its own screen)
    ED_NAME,        // a memory's name, 6 characters, overwrite (in the memory list)
} Editor;

static bool          sOpen;
static Editor        sEd;
static uint8_t       sSel;
static uint8_t       sTop;
static StationConfig sSt;           // copies being edited
static BeaconConfig  sBc;
static uint8_t       sCursor;

#define CALL_LEN        6
#define COMMENT_LEN     (BEACON_COMMENT_MAX - 1)

// Memory list: hold MENU stores the current frequency (then names it), hold
// EXIT clears; a short MENU tunes, a short EXIT goes back. Short presses act
// on release so they can be told apart from holds.
static uint8_t  sMemSel, sMemTop;
static char     sName[MEM_NAME_LEN];
static uint32_t sMemFreq;
static bool     sMenuDown, sExitDown;

// Position editor: digits, then the hemisphere.
static uint8_t sPosDigits[7];
static uint8_t sPosN;               // 6 (lat) or 7 (lon); cursor == sPosN is the hemisphere
static bool    sPosNeg;             // S / W

static bool IsBeaconItem(uint8_t i)
{
    return i == ITEM_CALL || i == ITEM_SSID || i == ITEM_LAT || i == ITEM_LON
        || i == ITEM_COMMENT || i == ITEM_PATH;
}

// ---------------------------------------------------------------- values

static char *U16ToDec(uint16_t v, char *out)   // returns the end
{
    char tmp[6];
    int n = 0;
    do { tmp[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (n) *out++ = tmp[--n];
    *out = '\0';
    return out;
}

static uint16_t FlagsToMs(uint8_t flags) { return (uint16_t)((flags * 20u + 1) / 3); }   // 6.67 ms each

static uint16_t StepTable(const uint16_t *t, unsigned n, uint16_t v, int dir)
{
    if (dir > 0) {
        for (unsigned i = 0; i < n; i++)
            if (t[i] > v) return t[i];
        return t[n - 1];
    }
    for (int i = (int)n - 1; i >= 0; i--)
        if (t[i] < v) return t[i];
    return t[0];
}

// Position digits from hundredths of a minute: DD MM hh (lat) / DDD MM hh (lon).
static void PosToDigits(int32_t v, uint8_t n, uint8_t *d)
{
    if (v < 0) v = -v;
    uint32_t deg = (uint32_t)v / 6000, min = ((uint32_t)v % 6000) / 100, hun = (uint32_t)v % 100;
    uint8_t i = 0;
    if (n == 7) d[i++] = (uint8_t)(deg / 100);
    d[i++] = (uint8_t)(deg / 10 % 10);
    d[i++] = (uint8_t)(deg % 10);
    d[i++] = (uint8_t)(min / 10);
    d[i++] = (uint8_t)(min % 10);
    d[i++] = (uint8_t)(hun / 10);
    d[i]   = (uint8_t)(hun % 10);
}

// "51 02.34N" / "114 04.86W"; *cursor_at gets the screen column of digit `cursor`.
static void FormatPos(const uint8_t *d, uint8_t n, bool neg, bool lat, char *out,
                      uint8_t cursor, uint8_t *cursor_at)
{
    const uint8_t deg = (uint8_t)(n - 4);
    uint8_t o = 0;
    for (uint8_t i = 0; i <= n; i++) {
        if (i == deg)      out[o++] = ' ';
        if (i == deg + 2)  out[o++] = '.';
        if (i == cursor && cursor_at) *cursor_at = o;
        out[o++] = i < n ? (char)('0' + d[i]) : (lat ? (neg ? 'S' : 'N') : (neg ? 'W' : 'E'));
    }
    out[o] = '\0';
}

static bool PosValid(const uint8_t *d, uint8_t n, int32_t *out)
{
    uint32_t deg = 0;
    for (uint8_t i = 0; i < n - 4; i++) deg = deg * 10 + d[i];
    const uint32_t min = d[n - 4] * 10u + d[n - 3], hun = d[n - 2] * 10u + d[n - 1];
    const uint32_t max = n == 7 ? 180 : 90;
    if (min > 59 || deg > max || (deg == max && (min || hun)))
        return false;
    *out = (int32_t)(deg * 6000 + min * 100 + hun);
    return true;
}

static void FormatValue(uint8_t item, const StationConfig *st, const BeaconConfig *bc, char *out)
{
    static const char *const PATHS[] = { "NONE", "WIDE1-1", "W1-1,W2-1" };
    out[0] = '\0';
    if (item == ITEM_MEMORIES) {
        uint8_t n = 0;
        for (uint8_t i = 0; i < MEM_COUNT; i++) n += MEM_Get(i) != NULL;
        strcpy(U16ToDec(n, out), " set");
        return;
    }
    if (IsBeaconItem(item) && !BEACON_IsConfigured()) {
        strcpy(out, "--");
        return;
    }
    switch (item) {
    case ITEM_DIGI:       strcpy(out, (st->flags & STATION_DIGI) ? "ON" : "OFF"); break;
    case ITEM_AUTOBEACON: strcpy(out, (st->flags & STATION_AUTOBEACON) ? "ON" : "OFF"); break;
    case ITEM_MSG_BEEP:   strcpy(out, (st->flags & STATION_MSG_QUIET) ? "OFF" : "ON"); break;
    case ITEM_TX_BEEP:    strcpy(out, (st->flags & STATION_TX_QUIET) ? "OFF" : "ON"); break;
    case ITEM_RX_BEEP: {
        static const char *const RX[] = { "OFF", "NEW", "ALL" };
        strcpy(out, RX[st->rx_beep <= STATION_RX_BEEP_ALL ? st->rx_beep : 0]);
        break;
    }
    case ITEM_DIGI_HOPS:
        U16ToDec(st->digi_max_hops, out);
        if (st->digi_max_hops == 1) strcat(out, " fill");
        break;
    case ITEM_INTERVAL:   strcpy(U16ToDec(st->beacon_interval_min, out), "m"); break;
    case ITEM_PATH:       strcpy(out, PATHS[bc->path_hops <= 2 ? bc->path_hops : 0]); break;
    case ITEM_TXDELAY:    strcpy(U16ToDec(FlagsToMs(st->txdelay_flags), out), "ms"); break;
    case ITEM_TXTAIL:     strcpy(U16ToDec(FlagsToMs(st->txtail_flags), out), "ms"); break;
    case ITEM_SSID:       U16ToDec(bc->ssid, out); break;
    case ITEM_CALL: {
        int n = 0;
        while (n < CALL_LEN && bc->call[n] && bc->call[n] != ' ') { out[n] = bc->call[n]; n++; }
        out[n] = '\0';
        break;
    }
    case ITEM_LAT:
    case ITEM_LON: {
        if (bc->lat == 0 && bc->lon == 0) { strcpy(out, "not set"); break; }
        const bool lat = item == ITEM_LAT;
        const int32_t v = lat ? bc->lat : bc->lon;
        uint8_t d[7];
        PosToDigits(v, lat ? 6 : 7, d);
        FormatPos(d, lat ? 6 : 7, v < 0, lat, out, 0xFF, NULL);
        break;
    }
    case ITEM_COMMENT: {
        const size_t n = strlen(bc->comment);
        if (n == 0)       strcpy(out, "none");
        else if (n <= 9)  strcpy(out, bc->comment);
        else            { memcpy(out, bc->comment, 7); strcpy(out + 7, ".."); }
        break;
    }
    }
}

static void Change(int dir)
{
    switch (sSel) {
    case ITEM_DIGI_HOPS: {
        const int v = sSt.digi_max_hops + dir;
        if (v >= 1 && v <= 7) sSt.digi_max_hops = (uint8_t)v;
        break;
    }
    case ITEM_RX_BEEP:  sSt.rx_beep = (uint8_t)((sSt.rx_beep + 3 + dir) % 3); break;
    case ITEM_INTERVAL: sSt.beacon_interval_min = StepTable(INTERVALS, COUNT(INTERVALS), sSt.beacon_interval_min, dir); break;
    case ITEM_TXDELAY:  sSt.txdelay_flags = (uint8_t)StepTable(DELAYS, COUNT(DELAYS), sSt.txdelay_flags, dir); break;
    case ITEM_TXTAIL:   sSt.txtail_flags  = (uint8_t)StepTable(TAILS, COUNT(TAILS), sSt.txtail_flags, dir); break;
    case ITEM_PATH:     sBc.path_hops = (uint8_t)((sBc.path_hops + 3 + dir) % 3); break;
    case ITEM_SSID:     sBc.ssid = (uint8_t)((sBc.ssid + 16 + dir) % 16); break;
    }
}

// ---------------------------------------------------------------- actions

static void ToggleFlag(uint8_t flag)
{
    const bool on = !(STATION_Get()->flags & flag);
    if (on && !BEACON_IsConfigured()) {
        APP_ShowMessage("NO CALLSIGN SET", false);   // both need MYCALL
        return;
    }
    STATION_Toggle(flag);
    if (flag == STATION_AUTOBEACON && on)
        BEACON_RestartTimer();
}

static void StartEdit(void)
{
    if (sSel == ITEM_MEMORIES) {
        sMenuDown = sExitDown = false;
        sEd = ED_MEMLIST;
        return;
    }
    sSt = *STATION_Get();
    if (sSel == ITEM_CALL) {
        if (BEACON_IsConfigured()) {
            sBc = *BEACON_Get();
        } else {                    // new setup: no position yet
            memset(&sBc, 0, sizeof sBc);
            sBc.magic        = BEACON_MAGIC;
            sBc.path_hops    = 1;
            sBc.symbol_table = '/';
            sBc.symbol       = '[';
        }
        TE_Begin(sBc.call, CALL_LEN, TE_CALL, true);
        sEd = ED_CALL;
        return;
    }
    if (IsBeaconItem(sSel) && !BEACON_IsConfigured()) {
        APP_ShowMessage("SET CALLSIGN FIRST", false);
        return;
    }
    sBc = *BEACON_Get();
    sCursor = 0;
    if (sSel == ITEM_LAT || sSel == ITEM_LON) {
        const bool lat = sSel == ITEM_LAT;
        const bool unset = sBc.lat == 0 && sBc.lon == 0;
        const int32_t v = lat ? sBc.lat : sBc.lon;
        sPosN   = lat ? 6 : 7;
        sPosNeg = unset ? !lat : v < 0;          // unset: N, W
        PosToDigits(v, sPosN, sPosDigits);
        sEd = ED_POS;
    } else if (sSel == ITEM_COMMENT) {
        TE_Begin(sBc.comment, COMMENT_LEN, TE_TEXT, false);
        sEd = ED_COMMENT;
    } else {
        sEd = ED_VALUE;
    }
}

static void Save(void)
{
    if (sEd == ED_NAME) {
        MEM_Set(sMemSel, sMemFreq, sName);
        APP_ShowMessage("SAVED", false);
        sEd = ED_MEMLIST;
        return;
    }
    if (sEd == ED_POS) {
        int32_t v;
        if (!PosValid(sPosDigits, sPosN, &v)) {
            APP_ShowMessage("BAD POSITION", false);
            return;                             // keep editing
        }
        if (sPosNeg) v = -v;
        if (sSel == ITEM_LAT) sBc.lat = v; else sBc.lon = v;
    }
    if (sEd == ED_COMMENT) {                    // drop trailing spaces
        uint8_t n = (uint8_t)strlen(sBc.comment);
        while (n && sBc.comment[n - 1] == ' ') sBc.comment[--n] = '\0';
    }
    const bool ok = IsBeaconItem(sSel) ? BEACON_Set(&sBc) : STATION_Set(&sSt);
    if (!ok && sEd == ED_CALL) {
        APP_ShowMessage("BAD CALLSIGN", false);     // 3-6 letters/digits, no gaps
        return;
    }
    APP_ShowMessage(ok ? "SAVED" : "NOT SAVED", false);
    sEd = ED_NONE;
}

void MENU_Open(void)
{
    sOpen = true;
    sEd   = ED_NONE;
    APP_Redraw();
}

bool MENU_IsOpen(void) { return sOpen; }

void MENU_Tick10ms(void)
{
    if (sOpen && (sEd == ED_CALL || sEd == ED_NAME || sEd == ED_COMMENT)) {
        const uint8_t cursor = TE_Cursor();
        TE_Tick10ms();
        if (TE_Cursor() != cursor)
            APP_Redraw();
    }
}

// Text fields (callsign, memory name, comment) go to textedit.h.
static void TextKey(KeyEvent_t ev)
{
    switch (TE_Key(ev)) {
    case TE_DONE:   Save(); break;
    case TE_CANCEL: sEd = sEd == ED_NAME ? ED_MEMLIST : ED_NONE; break;
    default:        break;
    }
    APP_Redraw();
}

// Position: digits, then the hemisphere.
static void PosKey(KeyEvent_t ev)
{
    const bool press  = ev.type == KEV_PRESS;
    const bool repeat = ev.type == KEV_LONG || ev.type == KEV_REPEAT;
    const KEY_Code_t k = ev.key;

    if (k <= KEY_9) {
        if (!press) return;
        if (sCursor < sPosN)
            sPosDigits[sCursor++] = (uint8_t)(k - KEY_0);
    } else if (k == KEY_UP || k == KEY_DOWN) {
        if (!press && !repeat) return;
        const int dir = k == KEY_UP ? +1 : -1;
        if (sCursor < sPosN)
            sPosDigits[sCursor] = (uint8_t)((sPosDigits[sCursor] + 10 + dir) % 10);
        else
            sPosNeg = !sPosNeg;
    } else if (k == KEY_STAR) {
        if (press && sCursor > 0) sCursor--;
    } else if (k == KEY_F) {
        if (press && sCursor < sPosN) sCursor++;
    } else if (k == KEY_EXIT) {
        if (ev.type == KEV_LONG)  sEd = ED_NONE;        // cancel
        else if (!press)          return;
        else if (sCursor)         sCursor--;
    } else if (k == KEY_MENU) {
        if (press) Save();
    } else {
        return;
    }
    APP_Redraw();
}

static void MemListKey(KeyEvent_t ev)
{
    const KEY_Code_t k = ev.key;
    const MemoryEntry *m = MEM_Get(sMemSel);
    if (k == KEY_UP || k == KEY_DOWN) {
        if (ev.type == KEV_RELEASE) return;
        if (k == KEY_DOWN && sMemSel + 1 < MEM_COUNT) sMemSel++;
        if (k == KEY_UP && sMemSel > 0)               sMemSel--;
    } else if (k == KEY_MENU) {
        if (ev.type == KEV_PRESS) {
            sMenuDown = true;
            return;
        }
        if (!sMenuDown || (ev.type != KEV_LONG && ev.type != KEV_RELEASE)) return;
        sMenuDown = false;
        if (ev.type == KEV_LONG) {              // store here, then name it
            sMemFreq = APP_GetFrequency();
            if (m) memcpy(sName, m->name, MEM_NAME_LEN);
            else   memset(sName, ' ', MEM_NAME_LEN);
            TE_Begin(sName, MEM_NAME_LEN, TE_NAME, true);
            sEd = ED_NAME;
        } else if (m) {                         // tune and close
            APP_SetFrequency(m->frequency);
            sEd   = ED_NONE;
            sOpen = false;
        } else {
            APP_ShowMessage("EMPTY", false);
        }
    } else if (k == KEY_EXIT) {
        if (ev.type == KEV_PRESS) {
            sExitDown = true;
            return;
        }
        if (!sExitDown || (ev.type != KEV_LONG && ev.type != KEV_RELEASE)) return;
        sExitDown = false;
        if (ev.type == KEV_LONG) {
            if (m) { MEM_Clear(sMemSel); APP_ShowMessage("CLEARED", false); }
        } else {
            sEd = ED_NONE;
        }
    } else {
        return;
    }
    APP_Redraw();
}

void MENU_HandleKey(KeyEvent_t ev)
{
    if (sEd == ED_MEMLIST) {
        MemListKey(ev);
        return;
    }
    if (sEd == ED_CALL || sEd == ED_COMMENT || sEd == ED_NAME) {
        TextKey(ev);
        return;
    }
    if (sEd == ED_POS) {
        PosKey(ev);
        return;
    }

    const bool press  = ev.type == KEV_PRESS;
    const bool repeat = ev.type == KEV_LONG || ev.type == KEV_REPEAT;
    switch (ev.key) {
    case KEY_UP:
    case KEY_DOWN: {
        if (!press && !repeat) return;
        const int dir = ev.key == KEY_UP ? +1 : -1;
        if (sEd == ED_VALUE)                         Change(dir);
        else if (dir < 0 && sSel + 1 < ITEM_COUNT)   sSel++;
        else if (dir > 0 && sSel > 0)                sSel--;
        break;
    }
    case KEY_MENU:
        if (!press) return;
        if (sEd == ED_VALUE)              Save();
        else if (sSel == ITEM_DIGI)       ToggleFlag(STATION_DIGI);
        else if (sSel == ITEM_AUTOBEACON) ToggleFlag(STATION_AUTOBEACON);
        else if (sSel == ITEM_MSG_BEEP)   STATION_Toggle(STATION_MSG_QUIET);   // need no callsign
        else if (sSel == ITEM_TX_BEEP)    STATION_Toggle(STATION_TX_QUIET);
        else                              StartEdit();
        break;
    case KEY_EXIT:
        if (!press) return;
        if (sEd != ED_NONE) sEd = ED_NONE;
        else                sOpen = false;
        break;
    default:
        return;
    }
    APP_Redraw();
}

// ---------------------------------------------------------------- drawing

static void InvertCell(uint8_t page, uint8_t x)
{
    GFX_InvertPage(page, x ? x - 1 : 0, (uint8_t)(x + GFX_CHAR_W - 1));
}

static void RenderList(void);

// "0 APRS    144.390", one row per memory; the name being edited has a cursor.
static void RenderMemories(void)
{
    if (sMemSel < sMemTop)          sMemTop = sMemSel;
    if (sMemSel >= sMemTop + ROWS)  sMemTop = (uint8_t)(sMemSel - ROWS + 1);
    char buf[COLS + 1];
    for (uint8_t r = 0; r < ROWS && sMemTop + r < MEM_COUNT; r++) {
        const uint8_t slot = (uint8_t)(sMemTop + r), page = (uint8_t)(1 + r);
        const bool naming = sEd == ED_NAME && slot == sMemSel;
        const MemoryEntry *m = MEM_Get(slot);
        buf[0] = (char)('0' + slot);
        buf[1] = ' ';
        for (int i = 0; i < MEM_NAME_LEN; i++)
            buf[2 + i] = m ? m->name[i] : ' ';
        if (naming) TE_FormatFixed(buf + 2);
        buf[2 + MEM_NAME_LEN] = '\0';
        GFX_Text(1, page, buf);
        const uint32_t f = naming ? sMemFreq : m ? m->frequency : 0;
        if (f) {
            char *p = U16ToDec((uint16_t)(f / 100000), buf);
            *p++ = '.';
            const uint16_t khz = (uint16_t)((f / 100) % 1000);
            *p++ = (char)('0' + khz / 100);
            *p++ = (char)('0' + khz / 10 % 10);
            *p++ = (char)('0' + khz % 10);
            *p = '\0';
        } else {
            strcpy(buf, "--");
        }
        GFX_TextRight(LCD_WIDTH - 2, page, buf);
        if (naming)
            InvertCell(page, (uint8_t)(1 + (2 + TE_Cursor()) * CELL_W));
        else if (slot == sMemSel)
            GFX_InvertPage(page, 0, LCD_WIDTH - 1);
    }
}

void MENU_Render(void)
{
    if (sEd == ED_COMMENT) {
        TE_RenderText("Comment");
    } else if (sEd == ED_MEMLIST || sEd == ED_NAME) {
        RenderMemories();
    } else {
        RenderList();
    }
    GFX_ShiftDown1(1);                          // clear of the status bar's divider
}

static void RenderList(void)
{
    if (sSel < sTop)          sTop = sSel;
    if (sSel >= sTop + ROWS)  sTop = (uint8_t)(sSel - ROWS + 1);

    char buf[COLS + 1];
    for (uint8_t r = 0; r < ROWS && sTop + r < ITEM_COUNT; r++) {
        const uint8_t item = (uint8_t)(sTop + r), page = (uint8_t)(1 + r);
        const bool editing = sEd != ED_NONE && item == sSel;
        uint8_t cursor_col = 0xFF;
        if (editing && sEd == ED_CALL) {
            TE_FormatFixed(buf);
            cursor_col = TE_Cursor();
        } else if (editing && sEd == ED_POS) {
            FormatPos(sPosDigits, sPosN, sPosNeg, item == ITEM_LAT, buf, sCursor, &cursor_col);
        } else {
            FormatValue(item, editing ? &sSt : STATION_Get(), editing ? &sBc : BEACON_Get(), buf);
        }
        GFX_Text(1, page, LABELS[item]);
        const uint8_t w = GFX_TextWidth(buf);
        GFX_TextRight(LCD_WIDTH - 2, page, buf);
        if (cursor_col != 0xFF)                     // just the cursor character
            InvertCell(page, (uint8_t)(LCD_WIDTH - 2 - w + cursor_col * CELL_W));
        else if (editing)                           // the value only
            GFX_InvertPage(page, (uint8_t)(LCD_WIDTH - 4 - w), LCD_WIDTH - 1);
        else if (item == sSel)
            GFX_InvertPage(page, 0, LCD_WIDTH - 1);
    }
}
