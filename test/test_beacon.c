// Host test for beacon.c: reads 64-byte BeaconConfig blobs (hex, one per line),
// stores each via BEACON_Set and prints the TNC2 form of BEACON_BuildFrame.
// test/beacon_vectors.py compares the output with the Python tool's idea of it.
//
//   cc -I . -I src -o build/test_beacon test/test_beacon.c src/beacon.c src/ax25.c

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ax25.h"
#include "beacon.h"

// EEPROM stubs
void EEPROM_ReadBuffer(uint16_t a, void *p, uint8_t n) { (void)a; memset(p, 0xFF, n); }
void EEPROM_WriteBuffer(uint16_t a, const void *p) { (void)a; (void)p; }

int main(void)
{
    static char line[1024];
    BEACON_Load();
    printf("%s\n", BEACON_IsConfigured() ? "BUG: blank EEPROM accepted" : "blank EEPROM: not configured");
    while (fgets(line, sizeof line, stdin)) {
        uint8_t blob[64];
        for (int i = 0; i < 64; i++) {
            char h[3] = { line[2 * i], line[2 * i + 1], 0 };
            blob[i] = (uint8_t)strtoul(h, NULL, 16);
        }
        BeaconConfig cfg;
        memcpy(&cfg, blob, sizeof cfg);
        if (!BEACON_Set(&cfg)) { printf("REJECTED\n"); continue; }
        uint8_t frame[400];
        char text[400];
        const uint16_t len = BEACON_BuildFrame(frame, sizeof frame);
        AX25_FormatTNC2(frame, len, text, sizeof text);
        printf("%s\n", text);
    }
    return 0;
}
