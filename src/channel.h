#ifndef CHANNEL_H
#define CHANNEL_H

#include <stdbool.h>
#include <stdint.h>

// Carrier sense for transmitting (beacon, KISS). The RSSI isn't calibrated
// (a quiet channel reads -77..-87 dBm on this radio), so "busy" is relative:
// the recent average more than CHANNEL_BUSY_MARGIN_DB above the noise floor,
// or a packet being received.

#define CHANNEL_BUSY_MARGIN_DB 12

void CHANNEL_ObserveRssi(int16_t dbm);   // every RSSI reading (~100 ms)
bool CHANNEL_IsBusy(void);

#endif
