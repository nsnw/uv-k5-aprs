#ifndef MENU_H
#define MENU_H

#include <stdbool.h>

#include "input.h"

// On-radio settings screen (hold MENU in the APRS view).
//   UP / DOWN    move (while editing: change the value)
//   MENU         on/off items: toggle and save; others: edit, then MENU saves
//   EXIT         cancel the edit; otherwise close the menu
// Memories (memory.h): MENU tunes, hold MENU stores the current frequency and
// names it, hold EXIT clears.
// Station options are stored in station.h's config; callsign, SSID and beacon
// path in beacon.h's. A callsign entered here without a beacon set up gets no
// position, so the beacon stays off until `k5 beacon set` gives it one.

void MENU_Open(void);
bool MENU_IsOpen(void);
void MENU_HandleKey(KeyEvent_t ev);   // all key events while open, except PTT
void MENU_Render(void);               // pages 1-7; page 0 is the caller's status bar
void MENU_Tick10ms(void);             // multi-tap timeout for callsign entry

#endif
