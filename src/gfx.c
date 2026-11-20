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
static const uint8_t KEY_ICONS[2][7] = {
    { 0x7F, 0x41, 0x7B, 0x77, 0x7B, 0x41, 0x7F },   // M
    { 0x7F, 0x41, 0x55, 0x55, 0x55, 0x5D, 0x7F },   // E
};
static const uint8_t DOT_ICON[3] = { 0x1C, 0x1C, 0x1C };   // GFX_ICON_DOT: 3 x 3, centred

static bool    IsKey(char c)     { return c >= 1 && c <= (char)(sizeof KEY_ICONS / sizeof KEY_ICONS[0]); }
static uint8_t CharWidth(char c) { return IsKey(c) ? 8 : c == GFX_ICON_DOT[0] ? 4 : GFX_CHAR_W + 1; }

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
            memcpy(&gFrameBuffer[page][x], KEY_ICONS[c - 1], sizeof KEY_ICONS[0]);
        } else if (c == GFX_ICON_DOT[0]) {
            memcpy(&gFrameBuffer[page][x], DOT_ICON, sizeof DOT_ICON);
        } else if (c > ' ' && c <= '~') {
            memcpy(&gFrameBuffer[page][x], gFontSmall[c - '!'], GFX_CHAR_W);
        }
        x += CharWidth(c);
    }
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

void GFX_InvertPage(uint8_t page, uint8_t x0, uint8_t x1)
{
    if (page >= LCD_PAGES)
        return;
    for (uint8_t x = x0; x <= x1 && x < LCD_WIDTH; x++)
        gFrameBuffer[page][x] ^= 0xFF;
}
