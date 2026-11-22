#include <string.h>

#include "driver/st7565.h"
#include "gfx.h"
#include "textedit.h"

#define MULTITAP_TICKS  100         // 1 s
#define CELL_W          (GFX_CHAR_W + 1)
#define COLS            (LCD_WIDTH / CELL_W)
#define TEXT_ROWS       4

static const char *const KEYS_UPPER[10] = {
    " 0", ".,-/!?'()@#:;1", "ABC2", "DEF3", "GHI4", "JKL5", "MNO6", "PQRS7", "TUV8", "WXYZ9",
};
static const char *const KEYS_LOWER[10] = {
    " 0", ".,-/!?'()@#:;1", "abc2", "def3", "ghi4", "jkl5", "mno6", "pqrs7", "tuv8", "wxyz9",
};
static const char *const KEYS_CALL[10] = {
    "0", "1", "ABC2", "DEF3", "GHI4", "JKL5", "MNO6", "PQRS7", "TUV8", "WXYZ9",
};
static const char *const KEYS_NAME[10] = {
    " 0", "-1", "ABC2", "DEF3", "GHI4", "JKL5", "MNO6", "PQRS7", "TUV8", "WXYZ9",
};
static const char CALL_CHARS[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
static const char NAME_CHARS[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-";

static char   *sBuf;
static uint8_t sMax;
static TeMode  sMode;
static uint8_t sCursor;
static int8_t  sTapKey = -1;        // key being multi-tapped, -1 = none
static uint8_t sTapIndex;
static uint8_t sTapTicks;
static bool    sFresh;
static bool    sLower;

static bool    Fixed(void) { return sMode != TE_TEXT; }
static uint8_t Len(void)   { return Fixed() ? sMax : (uint8_t)strlen(sBuf); }

void TE_Begin(char *buf, uint8_t max, TeMode mode, bool fresh)
{
    sBuf    = buf;
    sMax    = max;
    sMode   = mode;
    sTapKey = -1;
    sFresh  = fresh && mode != TE_TEXT;
    sCursor = mode == TE_TEXT ? (uint8_t)strlen(buf) : 0;
    if (Fixed())
        for (uint8_t i = 0; i < max; i++)
            if (!buf[i]) buf[i] = ' ';
}

uint8_t TE_Cursor(void) { return sCursor; }
bool    TE_Lower(void)  { return sLower; }

static void EndTap(void)
{
    if (sTapKey < 0)
        return;
    sTapKey = -1;
    if (Fixed() ? sCursor + 1 < sMax : sCursor < Len())
        sCursor++;
}

static void Keypad(uint8_t key)
{
    const char *keys = (sMode == TE_CALL ? KEYS_CALL : sMode == TE_NAME ? KEYS_NAME
                        : sLower ? KEYS_LOWER : KEYS_UPPER)[key];
    if (sFresh) {
        memset(sBuf, ' ', sMax);
        sFresh = false;
    }
    if (sTapKey == (int8_t)key) {
        sTapIndex = (uint8_t)((sTapIndex + 1) % strlen(keys));
    } else {
        EndTap();
        sTapIndex = 0;
        if (!Fixed()) {                 // insert a new character at the cursor
            const uint8_t n = Len();
            if (n >= sMax)
                return;                 // full
            memmove(&sBuf[sCursor + 1], &sBuf[sCursor], (size_t)(n - sCursor + 1));
        }
        sTapKey = (int8_t)key;
    }
    sBuf[sCursor] = keys[sTapIndex];
    sTapTicks = MULTITAP_TICKS;
}

static void Cycle(int dir)              // UP/DOWN on the character at the cursor
{
    sTapKey = -1;
    sFresh  = false;
    if (Fixed()) {
        const char *set = sMode == TE_CALL ? CALL_CHARS : NAME_CHARS;
        const int n = (int)strlen(set);
        const char *p = strchr(set, sBuf[sCursor]);
        const int i = p ? (int)(p - set) : 0;
        sBuf[sCursor] = set[(i + n + dir) % n];
        return;
    }
    if (sCursor == Len()) {             // at the end: add a character to cycle
        if (sCursor >= sMax)
            return;
        sBuf[sCursor] = ' ';
        sBuf[sCursor + 1] = '\0';
    }
    const int c = sBuf[sCursor] - ' ', n = '~' - ' ' + 1;
    sBuf[sCursor] = (char)(' ' + (c + n + dir) % n);
}

static void MoveCursor(int dir)
{
    const bool was_tapping = sTapKey >= 0;
    EndTap();                           // moves right by itself
    sFresh = false;
    if (dir > 0 && was_tapping)
        return;
    const uint8_t max = Fixed() ? sMax - 1 : Len();
    if (dir < 0 && sCursor > 0)   sCursor--;
    if (dir > 0 && sCursor < max) sCursor++;
}

static void Delete(void)
{
    EndTap();
    sFresh = false;
    if (Fixed()) {                      // clear this character, or the previous one
        if (sBuf[sCursor] == ' ' && sCursor)
            sCursor--;
        sBuf[sCursor] = ' ';
    } else if (sCursor > 0) {           // backspace
        memmove(&sBuf[sCursor - 1], &sBuf[sCursor], (size_t)(Len() - sCursor + 1));
        sCursor--;
    }
}

TeResult TE_Key(KeyEvent_t ev)
{
    const bool press  = ev.type == KEV_PRESS;
    const bool repeat = ev.type == KEV_LONG || ev.type == KEV_REPEAT;
    const KEY_Code_t k = ev.key;

    if (k <= KEY_9) {
        if (press) Keypad((uint8_t)(k - KEY_0));
    } else if (k == KEY_UP || k == KEY_DOWN) {
        if (press || repeat) Cycle(k == KEY_UP ? +1 : -1);
    } else if (k == KEY_STAR || k == KEY_F) {
        if (press) MoveCursor(k == KEY_F ? +1 : -1);
    } else if (k == KEY_SIDE1) {
        if (press && !Fixed()) { EndTap(); sLower = !sLower; }
    } else if (k == KEY_EXIT) {
        if (ev.type == KEV_LONG) return TE_CANCEL;
        if (press) Delete();
    } else if (k == KEY_MENU) {
        if (press) { EndTap(); return TE_DONE; }
    }
    return TE_CONTINUE;
}

void TE_Tick10ms(void)
{
    if (sTapKey >= 0 && sTapTicks && --sTapTicks == 0)
        EndTap();
}

void TE_FormatFixed(char *out)
{
    for (uint8_t i = 0; i < sMax; i++)
        out[i] = sBuf[i] == ' ' ? '_' : sBuf[i];
    out[sMax] = '\0';
}

static char *U8ToDec(uint8_t v, char *out)
{
    if (v >= 100) *out++ = (char)('0' + v / 100);
    if (v >= 10)  *out++ = (char)('0' + v / 10 % 10);
    *out++ = (char)('0' + v % 10);
    *out = '\0';
    return out;
}

void TE_DrawWrapped(const char *s, uint8_t first_page, uint8_t rows)
{
    char line[COLS + 1];
    const uint8_t n = (uint8_t)strlen(s);
    for (uint8_t row = 0; row * COLS < n && row < rows; row++) {
        const uint8_t start = (uint8_t)(row * COLS);
        const uint8_t len = (uint8_t)(n - start < COLS ? n - start : COLS);
        memcpy(line, s + start, len);
        line[len] = '\0';
        GFX_Text(1, (uint8_t)(first_page + row), line);
    }
}

void TE_DrawText(uint8_t first_page, uint8_t rows)
{
    TE_DrawWrapped(sBuf, first_page, rows);
    const uint8_t cr = (uint8_t)(sCursor / COLS), cc = (uint8_t)(sCursor % COLS);
    if (cr < rows) {
        const uint8_t x = (uint8_t)(1 + cc * CELL_W);
        GFX_InvertPage((uint8_t)(first_page + cr), x - 1, (uint8_t)(x + GFX_CHAR_W - 1));
    }
}

void TE_FormatCount(char *out)
{
    strcpy(U8ToDec(Len(), out), "/");
    U8ToDec(sMax, out + strlen(out));
}

void TE_DrawHints(uint8_t page, const char *chars, const char *done)
{
    char line[COLS + 1];
    if (!chars) {
        strcpy(line, "1.,-/ 0 space ");
        strcat(line, sLower ? "abc" : "ABC");   // SIDE1 switches
        chars = line;
    }
    GFX_Text(1, page, chars);
    char ok[COLS + 1] = GFX_KEY_MENU " ";
    strcat(ok, done);
    GFX_Text(1, (uint8_t)(page + 1), ok);
    GFX_TextRight(LCD_WIDTH - 1, (uint8_t)(page + 1), GFX_ICON_HOLD GFX_KEY_EXIT " CANCEL");
}

void TE_RenderText(const char *label)
{
    char count[8];
    TE_DrawText(1, TEXT_ROWS);
    TE_FormatCount(count);
    GFX_Text(1, 5, label);
    GFX_TextRight(LCD_WIDTH - 1, 5, count);
    TE_DrawHints(6, NULL, "SAVE");
}
