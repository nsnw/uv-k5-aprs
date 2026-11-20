#ifndef DIGI_H
#define DIGI_H

#include <stdbool.h>
#include <stdint.h>

// APRS digipeater (New-N paradigm), on when station.h's STATION_DIGI is set.
//
// For the first path entry not yet repeated (H bit clear):
//   MYCALL          -> MYCALL*                       (directed via us)
//   WIDEn-N, n <= max hops, 1 <= N <= n
//                   -> MYCALL*,WIDEn-(N-1)   (or MYCALL*,WIDEn* when N = 1)
// MYCALL is only inserted while the path has room (8 digipeaters); then the
// hop is just counted down. Frames from us, already carrying MYCALL, or
// seen in the last DIGI_DUPE_S seconds (same source, destination and info)
// are not repeated.

#define DIGI_DUPE_S     30
#define DIGI_DUPES      16

// Pure path rewrite, for tests: `mycall` is a 7-byte AX.25 address field.
// Returns the new length (written to `out`, room for len + 7), or 0.
uint16_t DIGI_Rewrite(const uint8_t *in, uint16_t len, const uint8_t mycall[7],
                      uint8_t max_hops, uint8_t *out);

// Every received frame (FCS checked and stripped).
void DIGI_OnFrame(const uint8_t *frame, uint16_t len, uint32_t now_s);

uint16_t DIGI_Count(void);   // frames repeated since boot

#endif
