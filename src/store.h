#ifndef STORE_H
#define STORE_H

#include <stdbool.h>
#include <stdint.h>

// Keeps the message inbox (msg.h) and the station list (aprs.h) in EEPROM so
// they survive power-off. See settings.h for the addresses.
//
// Each table is written back in the background, one 8-byte page per 10 ms
// tick and never while a packet is coming in: a page write blocks for ~8 ms,
// and the FSK FIFO must be read every ~50 ms. Pages that haven't changed are
// skipped (EEPROM_WriteBuffer compares first). Each table has a 16-byte
// header (magic, entry size, entry count) and its entries packed after it;
// writes are 8-byte aligned, so they never cross the 24C64's 32-byte pages.
//
//   messages  saved STORE_MSG_DELAY_S after they change
//   stations  saved at most every STORE_STATION_INTERVAL_S: a busy channel
//             changes the list every few seconds, and each EEPROM page is
//             good for about a million writes (5 min = ~10 years)
//
// There's no clock that runs while the radio is off, so restored entries
// are marked old (their age shows as "old").

#define STORE_MSG_ADDR            0x0100
#define STORE_STATION_ADDR        0x0500
#define STORE_END                 0x0C00
#define STORE_MSG_DELAY_S         2
#define STORE_STATION_DELAY_S     10
#define STORE_STATION_INTERVAL_S  300

void STORE_Load(void);              // at boot, after the other settings
void STORE_Poll1s(uint32_t now_s);  // notices changes
void STORE_Poll10ms(bool busy);     // writes at most one page; busy = receiving
bool STORE_Busy(void);              // a write-back is in progress (for the host test)

#endif
