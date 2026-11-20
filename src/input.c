#include "board.h"
#include "input.h"

#define DEBOUNCE_TICKS 2    // 20 ms
#define LONG_TICKS     50   // 500 ms
#define REPEAT_TICKS   10   // 100 ms

static KEY_Code_t sCandidate = KEY_INVALID;
static uint8_t    sStableTicks;
static KEY_Code_t sDown = KEY_INVALID;  // debounced key currently held
static uint16_t   sHeldTicks;
static bool       sLongSent;

KeyEvent_t INPUT_Poll10ms(void)
{
    KeyEvent_t ev = { KEV_NONE, KEY_INVALID };

    KEY_Code_t raw = BOARD_IsPttPressed() ? KEY_PTT : KEYBOARD_Poll();

    if (raw != sCandidate) {
        sCandidate   = raw;
        sStableTicks = 0;
        return ev;
    }
    if (sStableTicks < DEBOUNCE_TICKS) {
        sStableTicks++;
        return ev;
    }

    // `raw` is now debounced
    if (raw != sDown) {
        if (sDown != KEY_INVALID) {
            ev.type = KEV_RELEASE;
            ev.key  = sDown;
            sDown   = KEY_INVALID;   // report the new key on the next tick
            return ev;
        }
        sDown      = raw;
        sHeldTicks = 0;
        sLongSent  = false;
        ev.type    = KEV_PRESS;
        ev.key     = raw;
        return ev;
    }

    if (sDown == KEY_INVALID)
        return ev;

    sHeldTicks++;
    if (!sLongSent && sHeldTicks >= LONG_TICKS) {
        sLongSent  = true;
        sHeldTicks = 0;
        ev.type    = KEV_LONG;
        ev.key     = sDown;
    } else if (sLongSent && sHeldTicks >= REPEAT_TICKS) {
        sHeldTicks = 0;
        ev.type    = KEV_REPEAT;
        ev.key     = sDown;
    }
    return ev;
}
