// Host test for textedit.c: multi-tap entry, and UP/DOWN moving on after
// the same timeout (so "?" then a letter needs no F in between).
#include <stdio.h>
#include <string.h>

#include "textedit.h"

// drawing isn't tested here
#include "gfx.h"
uint8_t GFX_Text(uint8_t x, uint8_t p, const char *s) { (void)p; (void)s; return x; }
void GFX_TextRight(uint8_t x, uint8_t p, const char *s) { (void)x; (void)p; (void)s; }
void GFX_InvertPage(uint8_t p, uint8_t a, uint8_t b) { (void)p; (void)a; (void)b; }

static int sPass, sFail;
#define CHECK(c, ...) do { if (c) sPass++; else { sFail++; printf("FAIL %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void Press(KEY_Code_t k) { TE_Key((KeyEvent_t){ KEV_PRESS, k }); }
static void Presses(KEY_Code_t k, int n) { while (n--) Press(k); }
static void Wait(int ticks) { while (ticks--) TE_Tick10ms(); }

int main(void)
{
    char t[16];

    // multi-tap: 2 2 = B, a second later the cursor moves on
    t[0] = '\0';
    TE_Begin(t, 15, TE_TEXT, false);
    Presses(KEY_2, 2);
    CHECK(strcmp(t, "B") == 0 && TE_Cursor() == 0, "tapping B (got '%s' at %u)", t, TE_Cursor());
    Wait(99);
    CHECK(TE_Cursor() == 0, "not yet moved on");
    Wait(1);
    CHECK(TE_Cursor() == 1, "moved on after a second");

    // UP to '?' (31 steps from space), then the timeout moves on: "B?A"
    Presses(KEY_UP, '?' - ' ');
    CHECK(strcmp(t, "B?") == 0 && TE_Cursor() == 1, "cycled to ? (got '%s')", t);
    Wait(100);
    CHECK(TE_Cursor() == 2, "UP/DOWN move on after a second too (cursor %u)", TE_Cursor());
    Press(KEY_2);
    CHECK(strcmp(t, "B?A") == 0, "letter after ? (got '%s')", t);
    Wait(100);

    // ... or a number key straight away moves on too
    Presses(KEY_DOWN, 1);                  // space -> '~'
    Press(KEY_3);
    CHECK(strcmp(t, "B?A~D") == 0, "number key after UP/DOWN goes after it (got '%s')", t);
    Wait(100);

    // F after UP/DOWN moves on once, not twice
    Press(KEY_UP);
    Press(KEY_F);
    CHECK(TE_Cursor() == 6 && strcmp(t, "B?A~D!") == 0, "F after UP: one step (got '%s' at %u)", t, TE_Cursor());

    // a multi-tapped letter adjusted with UP stays one character
    Press(KEY_2); Press(KEY_UP);
    CHECK(strcmp(t, "B?A~D!B") == 0 && TE_Cursor() == 6, "tap then UP: same character (got '%s')", t);
    Wait(100);
    CHECK(TE_Cursor() == 7, "then moves on");

    // fixed width (CALL-SSID): UP/DOWN move on, but not past the end
    char c[10];
    memcpy(c, "         ", 10);
    TE_Begin(c, 9, TE_NAME, false);
    Press(KEY_UP);
    Wait(100);
    CHECK(c[0] == 'A' && TE_Cursor() == 1, "fixed: UP then moves on (got '%s' at %u)", c, TE_Cursor());
    for (int i = 0; i < 12; i++) { Press(KEY_UP); Wait(100); }
    CHECK(TE_Cursor() == 8, "fixed: stops at the last character (%u)", TE_Cursor());

    // holding UP (repeats) keeps the timer going
    t[0] = '\0';
    TE_Begin(t, 15, TE_TEXT, false);
    Press(KEY_UP);
    for (int i = 0; i < 5; i++) { Wait(60); TE_Key((KeyEvent_t){ KEV_REPEAT, KEY_UP }); }
    CHECK(TE_Cursor() == 0 && t[0] == '!' + 5, "held: still on the same character (got '%s' at %u)", t, TE_Cursor());

    printf("%d passed, %d failed\n", sPass, sFail);
    return sFail != 0;
}
