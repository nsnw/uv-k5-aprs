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
void INBOX_Close(void);                // leaving the messages page
void INBOX_Compose(void);              // a new message: asks for the callsign (F on the page)
void INBOX_ComposeTo(const char *call); // a new message to `call`: straight to the text
bool INBOX_IsComposing(void);          // typing a callsign or text (SIDE1 = upper/lower case)
void INBOX_HandleKey(KeyEvent_t ev);   // all key events while open, except PTT
void INBOX_Render(void);               // pages 1-7; page 0 is the caller's status bar
void INBOX_Tick10ms(void);             // text entry timing

#endif
