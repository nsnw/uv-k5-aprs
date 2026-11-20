#ifndef GEO_H
#define GEO_H

#include <stdint.h>

// Distance and bearing between two positions in aprs_parse.h's units
// (hundredths of a minute of arc, north/east positive), in integers only.
// Flat-earth (equirectangular) approximation: well within 1% out to a few
// hundred km, which covers anything heard on VHF/UHF.

// Distance in metres; *bearing_deg (may be NULL) gets 0..359, 0 = north,
// from `from` towards `to`.
uint32_t GEO_Distance(int32_t from_lat, int32_t from_lon, int32_t to_lat, int32_t to_lon,
                      uint16_t *bearing_deg);

// "850m", "9.9km", "123km", "1234km". Buffer of at least 8 bytes.
void GEO_FormatDistance(uint32_t metres, char *out);

// 8-point compass name for a bearing: "N", "NE", ... "NW".
const char *GEO_Compass(uint16_t bearing_deg);

#endif
