#ifndef INPUT_H
#define INPUT_H

#include <stdbool.h>

#include "driver/keyboard.h"

typedef enum {
    KEV_NONE = 0,
    KEV_PRESS,     // key went down (after debounce)
    KEV_LONG,      // held for INPUT_LONG_MS
    KEV_REPEAT,    // auto-repeat while still held after KEV_LONG
    KEV_RELEASE,   // key went up
} KeyEventType_t;

typedef struct {
    KeyEventType_t type;
    KEY_Code_t     key;   // KEY_PTT is reported here too
} KeyEvent_t;

// Call every 10 ms. Returns at most one event per call.
KeyEvent_t INPUT_Poll10ms(void);

#endif
