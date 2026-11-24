#ifndef TEXTEDIT_H
#define TEXTEDIT_H

#include <stdbool.h>
#include <stdint.h>

#include "input.h"

// Keypad text entry, phone style: 2 = A B C 2 ..., the same key again within
// a second cycles its characters, otherwise the cursor moves on.
//   UP / DOWN    cycle the character at the cursor (moves on after a second too)
//   * / F        cursor left / right
//   SIDE1        upper / lower case (TE_TEXT)
//   EXIT         delete (hold: cancel)
//   MENU         done
// One field at a time; the caller owns the buffer.

typedef enum {
    TE_CALL,        // fixed width, overwrite, A-Z 0-9 (a callsign without SSID)
    TE_NAME,        // fixed width, overwrite, A-Z 0-9 '-' (names, CALL-SSID)
    TE_TEXT,        // NUL-terminated, insert, printable ASCII
} TeMode;

typedef enum { TE_CONTINUE, TE_DONE, TE_CANCEL } TeResult;

// Fixed modes: `buf` holds exactly `max` characters, unused ones ' '; with
// `fresh` the first keypad press clears it. TE_TEXT: up to `max` characters
// plus the NUL; the cursor starts at the end.
void     TE_Begin(char *buf, uint8_t max, TeMode mode, bool fresh);
TeResult TE_Key(KeyEvent_t ev);
void     TE_Tick10ms(void);           // multi-tap timeout
uint8_t  TE_Cursor(void);
bool     TE_Lower(void);

// Fixed modes: the field with '_' for blanks, ready to draw.
void TE_FormatFixed(char *out);

// `s` wrapped every COLS characters over `rows` pages from `first_page`.
void TE_DrawWrapped(const char *s, uint8_t first_page, uint8_t rows);
// TE_TEXT: the text being edited, the same way, with the cursor.
void TE_DrawText(uint8_t first_page, uint8_t rows);
// "12/67": TE_TEXT's length and maximum.
void TE_FormatCount(char *out);
// Two rows of hints from `page`: what the keys type (`chars`, or for
// TE_TEXT NULL: punctuation, space and the case), then "[M] <done>" and
// "(hold)[E] CANCEL".
void TE_DrawHints(uint8_t page, const char *chars, const char *done);

// TE_TEXT, full screen (pages 1-7): the text wrapped over pages 1-4 with
// the cursor, `label` and a character count on page 5, hints below.
void TE_RenderText(const char *label);

#endif
