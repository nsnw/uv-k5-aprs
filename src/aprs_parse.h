#ifndef APRS_PARSE_H
#define APRS_PARSE_H

#include <stdbool.h>
#include <stdint.h>

// Positions are kept in integer hundredths of a minute of arc
// (1 degree = 6000), north/east positive. No floating point needed.

#define APRS_UNKNOWN_I16 (-32768)

typedef struct {
    bool     has_position;
    int32_t  lat;          // hundredths of a minute, + = N
    int32_t  lon;          // hundredths of a minute, + = E
    char     symbol_table; // '/' or '\\' (or overlay)
    char     symbol;
    int16_t  speed_kmh;    // APRS_UNKNOWN_I16 if absent
    int16_t  course;       // degrees, APRS_UNKNOWN_I16 if absent
    int32_t  altitude_m;   // INT32_MIN if absent
    char     format;       // 'M' Mic-E, 'P' plain, 'C' compressed, 0 none
} AprsInfo;

// Parses the position part of an AX.25 frame (FCS stripped). `comment` gets
// the printable remainder of the info field (may be empty).
void APRS_Parse(const uint8_t *frame, uint16_t len, AprsInfo *out,
                char *comment, uint16_t comment_size);

// "51 02.34N" / "114 05.67W"; buffers of at least 12 bytes.
void APRS_FormatLat(int32_t lat, char *out);
void APRS_FormatLon(int32_t lon, char *out);

#endif
