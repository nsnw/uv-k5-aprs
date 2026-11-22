#ifndef APP_H
#define APP_H

#include <stdbool.h>
#include <stdint.h>

void APP_Init(void);
void APP_Tick10ms(void);   // called from the main loop once per 10 ms tick
void APP_Redraw(void);     // request a screen refresh (e.g. after external changes)

// Centre-of-screen message for 1.5 s (NULL clears it); `now` draws it
// immediately, for use before blocking work such as transmitting.
void APP_ShowMessage(const char *msg, bool now);

uint32_t APP_GetFrequency(void);
void     APP_SetFrequency(uint32_t f);   // retunes; ERROR message if not tunable

// The pages (stations, messages, radio, packet) sit under a two-line status
// bar: their first page. The settings and the text editors keep a one-line
// bar and start on page 1.
#define APP_TOP_PAGE 2

// Transmitter keyed: on = show T in the status icon and draw immediately.
void APP_SetTransmitting(bool on);

#endif
