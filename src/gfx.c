// Minimal drawing helpers on top of the ST7565 page framebuffer.

#include <stdbool.h>
#include <string.h>

#include "driver/st7565.h"
#include "font.h"
#include "gfx.h"

void GFX_Clear(void)
{
    memset(gFrameBuffer, 0, sizeof(gFrameBuffer));
}

// Keys: inverted 7 x 7 boxes with a 5 x 5 letter cut out, indexed by code - 1
static const uint8_t KEY_ICONS[3][7] = {
    { 0x7F, 0x41, 0x7B, 0x77, 0x7B, 0x41, 0x7F },   // M
    { 0x7F, 0x41, 0x55, 0x55, 0x55, 0x5D, 0x7F },   // E
    { 0x7F, 0x41, 0x75, 0x75, 0x75, 0x7D, 0x7F },   // F
};
static const uint8_t DOT_ICON[3] = { 0x1C, 0x1C, 0x1C };   // GFX_ICON_DOT: 3 x 3, centred
static const uint8_t HOLD_ICON[7] = { 0x1C, 0x22, 0x41, 0x4D, 0x49, 0x22, 0x1C };   // GFX_ICON_HOLD: a clock

static const uint8_t (*sFont)[GFX_CHAR_W] = gFontSmall;

void GFX_SetBold(bool on) { sFont = on ? gFontSmallBold : gFontSmall; }

static bool    IsKey(char c)     { return c == 1 || c == 2 || c == 4; }   // 3 is the dot
static uint8_t KeyIndex(char c)  { return c == 4 ? 2 : (uint8_t)(c - 1); }
static uint8_t CharWidth(char c) { return IsKey(c) || c == GFX_ICON_HOLD[0] ? 8 : c == GFX_ICON_DOT[0] ? 4 : GFX_CHAR_W + 1; }

uint8_t GFX_Text(uint8_t x, uint8_t page, const char *s)
{
    if (page >= LCD_PAGES)
        return x;
    for (; *s; s++) {
        if (x + GFX_CHAR_W > LCD_WIDTH)
            break;
        const char c = *s;
        if (IsKey(c)) {
            if (x + sizeof KEY_ICONS[0] > LCD_WIDTH)
                break;
            memcpy(&gFrameBuffer[page][x], KEY_ICONS[KeyIndex(c)], sizeof KEY_ICONS[0]);
        } else if (c == GFX_ICON_DOT[0]) {
            memcpy(&gFrameBuffer[page][x], DOT_ICON, sizeof DOT_ICON);
        } else if (c == GFX_ICON_HOLD[0]) {
            if (x + sizeof HOLD_ICON > LCD_WIDTH)
                break;
            memcpy(&gFrameBuffer[page][x], HOLD_ICON, sizeof HOLD_ICON);
        } else if (c > ' ' && c <= '~') {
            memcpy(&gFrameBuffer[page][x], sFont[c - '!'], GFX_CHAR_W);
        }
        x += CharWidth(c);
    }
    return x;
}

uint8_t GFX_TextBold(uint8_t x, uint8_t page, const char *s)
{
    GFX_SetBold(true);
    x = GFX_Text(x, page, s);
    GFX_SetBold(false);
    return x;
}

uint8_t GFX_TextWidth(const char *s)
{
    uint8_t w = 0;
    for (; *s; s++) w += CharWidth(*s);
    return w;
}

void GFX_TextRight(uint8_t right_x, uint8_t page, const char *s)
{
    const uint8_t w = GFX_TextWidth(s);
    GFX_Text(w > right_x ? 0 : right_x - w, page, s);
}

void GFX_TextRightBold(uint8_t right_x, uint8_t page, const char *s)
{
    GFX_SetBold(true);
    GFX_TextRight(right_x, page, s);
    GFX_SetBold(false);
}

static uint8_t BigDigits(uint8_t x, uint8_t page, const char *s, uint8_t top_shift);

uint8_t GFX_BigDigits(uint8_t x, uint8_t page, const char *s)      { return BigDigits(x, page, s, 0); }
uint8_t GFX_BigDigitsShort(uint8_t x, uint8_t page, const char *s) { return BigDigits(x, page, s, 1); }

static uint8_t BigDigits(uint8_t x, uint8_t page, const char *s, uint8_t top_shift)
{
    if (page + 1 >= LCD_PAGES)
        return x;
    for (; *s; s++) {
        const char c = *s;
        if (c == '.') {
            if (x + 3 <= LCD_WIDTH)
                for (uint8_t i = 0; i < 2; i++)
                    gFrameBuffer[page + 1][x + i] |= 0x60;
            x += 4;
            continue;
        }
        if (x + GFX_BIG_W > LCD_WIDTH)
            break;
        int idx = -1;
        if (c >= '0' && c <= '9')
            idx = c - '0';
        else if (c == '-')
            idx = 10;
        if (idx >= 0) {
            // shifting the top half down drops its last row (the glyph's middle)
            // OR, not copy: the page may hold other pixels (e.g. a divider line)
            for (uint8_t i = 0; i < GFX_BIG_W; i++) {
                gFrameBuffer[page][x + i]     |= (uint8_t)(gFontBigDigits[idx][i] << top_shift);
                gFrameBuffer[page + 1][x + i] |= gFontBigDigits[idx][10 + i];
            }
        }
        x += GFX_BIG_W + 2;
    }
    return x;
}

uint8_t GFX_TextDouble(uint8_t x, uint8_t y, const char *s)
{
    for (; *s; s++) {
        if (x + 2 * GFX_CHAR_W > LCD_WIDTH)
            break;
        const char c = *s;
        if (c > ' ' && c <= '~') {
            for (uint8_t i = 0; i < GFX_CHAR_W; i++) {
                const uint8_t g = gFontSmall[c - '!'][i];
                uint32_t col = 0;                 // each glyph row twice
                for (uint8_t r = 0; r < 7; r++)
                    if (g & (1u << r))
                        col |= 3u << (2 * r);
                col <<= y & 7;
                for (uint8_t p = y / 8, k = 0; k < 3 && p < LCD_PAGES; p++, k++) {
                    const uint8_t bits = (uint8_t)(col >> (8 * k));
                    gFrameBuffer[p][x + 2 * i]     |= bits;
                    gFrameBuffer[p][x + 2 * i + 1] |= bits;
                }
            }
        }
        x += 2 * GFX_CHAR_W + 1;
    }
    return x;
}

uint8_t GFX_TextDoubleWidth(const char *s)
{
    return (uint8_t)(strlen(s) * (2 * GFX_CHAR_W + 1));
}

void GFX_HLine(uint8_t x0, uint8_t x1, uint8_t y)
{
    if (y >= LCD_HEIGHT)
        return;
    for (uint8_t x = x0; x <= x1 && x < LCD_WIDTH; x++)
        gFrameBuffer[y / 8][x] |= 1u << (y % 8);
}

void GFX_ShiftDown1(uint8_t first_page)
{
    for (uint8_t x = 0; x < LCD_WIDTH; x++) {
        uint8_t carry = 0;
        for (uint8_t p = first_page; p < LCD_PAGES; p++) {
            const uint8_t b = gFrameBuffer[p][x];
            gFrameBuffer[p][x] = (uint8_t)((b << 1) | carry);
            carry = b >> 7;
        }
    }
}

// Moves pixel rows y0..63 down n rows (the bottom ones drop off).
static void ShiftRows(uint8_t y0, uint8_t n)
{
    const uint64_t above = ((uint64_t)1 << y0) - 1;
    for (uint8_t x = 0; x < LCD_WIDTH; x++) {
        uint64_t col = 0;
        for (uint8_t p = 0; p < LCD_PAGES; p++)
            col |= (uint64_t)gFrameBuffer[p][x] << (8 * p);
        col = (col & above) | ((col & ~above) << n);
        for (uint8_t p = 0; p < LCD_PAGES; p++)
            gFrameBuffer[p][x] = (uint8_t)(col >> (8 * p));
    }
}

void GFX_Divide(uint8_t page)
{
    const uint8_t y = (uint8_t)(page * 8);
    ShiftRows((uint8_t)(y + 8), 2);     // the body down two: a gap above and below the rule
    ShiftRows(y, 2);                    // all of it down two, clear of the status bar's divider
    GFX_HLine(0, LCD_WIDTH - 1, (uint8_t)(y + 10));
}

void GFX_Lower(uint8_t page)
{
    ShiftRows((uint8_t)(page * 8), 2);
}

void GFX_InvertPage(uint8_t page, uint8_t x0, uint8_t x1)
{
    if (page >= LCD_PAGES)
        return;
    for (uint8_t x = x0; x <= x1 && x < LCD_WIDTH; x++)
        gFrameBuffer[page][x] ^= 0xFF;
}
