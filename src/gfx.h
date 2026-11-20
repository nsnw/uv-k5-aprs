#ifndef GFX_H
#define GFX_H

#include <stdint.h>

#define GFX_CHAR_W 6   // small font glyph width (plus 1 px spacing)
#define GFX_BIG_W  10  // big digit glyph width (plus 2 px spacing), 2 pages tall

// In GFX_Text strings: the MENU and EXIT keys, drawn as inverted "M" / "E"
// boxes one pixel wider than a character, e.g. GFX_KEY_MENU " REPLY".
#define GFX_KEY_MENU "\x01"
#define GFX_KEY_EXIT "\x02"

void    GFX_Clear(void);
uint8_t GFX_Text(uint8_t x, uint8_t page, const char *s);        // returns next x
uint8_t GFX_TextWidth(const char *s);
void    GFX_TextRight(uint8_t right_x, uint8_t page, const char *s);
uint8_t GFX_BigDigits(uint8_t x, uint8_t page, const char *s);   // digits, '-', '.'
// The same, one pixel shorter: the middle row is dropped, so the digits start
// on the third row of `page` instead of the second.
uint8_t GFX_BigDigitsShort(uint8_t x, uint8_t page, const char *s);
// The small font at twice the size (12 x 14 px, 13 px per character), its
// top row at pixel row y. ORs into the framebuffer. Returns the next x.
uint8_t GFX_TextDouble(uint8_t x, uint8_t y, const char *s);
uint8_t GFX_TextDoubleWidth(const char *s);
void    GFX_HLine(uint8_t x0, uint8_t x1, uint8_t y);
void    GFX_InvertPage(uint8_t page, uint8_t x0, uint8_t x1);
// Moves pages first_page..7 down one pixel (the bottom row is dropped: small
// text never uses it), freeing the top row of first_page for a divider.
void    GFX_ShiftDown1(uint8_t first_page);

#endif
