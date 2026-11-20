// Host test for aprs_parse.c. Input: one AX.25 frame per line as hex
// (FCS stripped). Output: parsed fields, one line per frame.
//
//   cc -I src -o build/test_aprs_parse test/test_aprs_parse.c src/aprs_parse.c src/ax25.c

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aprs_parse.h"
#include "ax25.h"

int main(void)
{
    static char line[4096];
    while (fgets(line, sizeof line, stdin)) {
        uint8_t frame[1024];
        uint16_t len = 0;
        for (char *p = line; p[0] && p[1] && p[0] != '\n'; p += 2) {
            char h[3] = { p[0], p[1], 0 };
            frame[len++] = (uint8_t)strtoul(h, NULL, 16);
        }
        AprsInfo info;
        char comment[64], src[16], lat[16], lon[16];
        APRS_Parse(frame, len, &info, comment, sizeof comment);
        AX25_FormatSource(frame, len, src, sizeof src);
        if (!info.has_position) {
            printf("%s: no position | %s\n", src, comment);
            continue;
        }
        APRS_FormatLat(info.lat, lat);
        APRS_FormatLon(info.lon, lon);
        printf("%s: %c %s %s sym=%c%c", src, info.format, lat, lon, info.symbol_table, info.symbol);
        if (info.speed_kmh != APRS_UNKNOWN_I16) printf(" spd=%dkm/h", info.speed_kmh);
        if (info.course != APRS_UNKNOWN_I16)    printf(" crs=%d", info.course);
        if (info.altitude_m != INT32_MIN)       printf(" alt=%ldm", (long)info.altitude_m);
        printf(" | %s\n", comment);
    }
    return 0;
}
