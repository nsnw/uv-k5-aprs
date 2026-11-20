#ifndef INBOX_H
#define INBOX_H

#include <stdbool.h>

#include "input.h"

// Messages screen (msg.h), opened with F from the APRS view. Newest first,
// marked * unread, > sending, + delivered, x no ack / rejected.
//   UP / DOWN    select; the top row is "New message"
//   MENU         read it / write a new one; reading: reply (or send again
//                if it failed)
//   EXIT         back (reading: to the list; hold: delete the message)
// Writing uses textedit.h: the callsign (CALL-SSID), then the text.

void INBOX_Open(void);
bool INBOX_IsOpen(void);
void INBOX_HandleKey(KeyEvent_t ev);   // all key events while open, except PTT
void INBOX_Render(void);               // pages 1-7; page 0 is the caller's status bar
void INBOX_Tick10ms(void);             // text entry timing

#endif
