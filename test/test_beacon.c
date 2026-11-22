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

// Status reports and the beacon type (self-checking; failures go to stderr
// and fail the run). Uses the last valid configuration read above.
static int StatusChecks(void)
{
    int fail = 0;
#define EXPECT(c, ...) do { if (!(c)) { fail++; fprintf(stderr, "FAIL: " __VA_ARGS__); fprintf(stderr, "\n"); } } while (0)
    uint8_t frame[400];
    char text[400];
    BeaconStatus bs;
    memset(&bs, 0, sizeof bs);
    bs.magic = BEACON_STATUS_MAGIC;
    bs.type  = BEACON_TYPE_STATUS;
    strcpy(bs.text, "Digi on the hill");
    EXPECT(BEACON_SetStatus(&bs), "valid status stored");
    AX25_FormatTNC2(frame, BEACON_BuildStatusFrame(frame, sizeof frame), text, sizeof text);
    EXPECT(strstr(text, ":>Digi on the hill") && !strncmp(text, "N0CALL-9>APZK5M,WIDE1-1,WIDE2-1:", 32),
           "status frame: %s", text);
    bs.text[0] = '\0';
    BEACON_SetStatus(&bs);
    AX25_FormatTNC2(frame, BEACON_BuildStatusFrame(frame, sizeof frame), text, sizeof text);
    EXPECT(strstr(text, ":>K5-APRS"), "empty status text -> K5-APRS (%s)", text);
    strcpy(bs.text, "a|b");
    EXPECT(!BEACON_SetStatus(&bs), "| rejected");
    memset(bs.text, 'x', 62); bs.text[62] = '\0';
    EXPECT(BEACON_SetStatus(&bs), "62 characters allowed");
    memset(bs.text, 'x', 63); bs.text[63] = '\0';
    EXPECT(!BEACON_SetStatus(&bs), "63 characters rejected");
    bs.text[0] = '\0'; bs.type = BEACON_TYPE_COUNT;
    EXPECT(!BEACON_SetStatus(&bs), "bad type rejected");
    EXPECT(BEACON_GetStatus()->type == BEACON_TYPE_STATUS, "rejected ones don't change it");

    // no position: no position report, still a status report
    BeaconConfig c = *BEACON_Get();
    EXPECT(BEACON_HasPosition(), "has a position");
    c.lat = c.lon = 0;
    EXPECT(BEACON_Set(&c), "callsign without a position is a valid configuration");
    EXPECT(!BEACON_HasPosition() && BEACON_BuildFrame(frame, sizeof frame) == 0, "no position report");
    EXPECT(BEACON_BuildStatusFrame(frame, sizeof frame) > 0, "status report without a position");
    return fail != 0;
}

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
    return StatusChecks();
}
