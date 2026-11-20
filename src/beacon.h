#ifndef BEACON_H
#define BEACON_H

#include <stdbool.h>
#include <stdint.h>

// APRS position beacon: fixed position, stored in EEPROM and set from the host
// (`k5 beacon set`). Sent on demand from APRS mode (hold F).
//
// EEPROM 0x0010-0x004F (see settings.h for the map).

#define BEACON_EEPROM_ADDR  0x0010
#define BEACON_MAGIC        0x3142354Bu   // "K5B1"
#define BEACON_COMMENT_MAX  40            // including the NUL

typedef struct {
    uint32_t magic;
    char     call[6];          // A-Z 0-9, unused positions ' ' or '\0'
    uint8_t  ssid;             // 0..15
    uint8_t  path_hops;        // 0: none, 1: WIDE1-1, 2: WIDE1-1,WIDE2-1
    char     symbol_table;     // '/' or '\\'
    char     symbol;           // e.g. '[' person, '>' car
    uint8_t  reserved[2];
    int32_t  lat;              // hundredths of a minute, + = N
    int32_t  lon;              // hundredths of a minute, + = E
    char     comment[BEACON_COMMENT_MAX];
} BeaconConfig;

_Static_assert(sizeof(BeaconConfig) == 64, "BeaconConfig must be 8 EEPROM pages");

typedef enum {
    BEACON_QUEUED = 0,         // goes out when the channel is clear (txq.h)
    BEACON_NOT_CONFIGURED,
    BEACON_QUEUE_FULL,
    BEACON_TX_REFUSED,         // not a TX frequency (tx.h)
    BEACON_NO_POSITION,        // callsign set on the radio, position not set yet
} BeaconResult;

void BEACON_Load(void);
bool BEACON_IsConfigured(void);
const BeaconConfig *BEACON_Get(void);

// Validates and stores a new configuration; false if invalid (nothing written).
bool BEACON_Set(const BeaconConfig *cfg);

// Builds the AX.25 frame (no FCS); returns its length, 0 if not configured.
uint16_t BEACON_BuildFrame(uint8_t *out, uint16_t size);

// Queues one beacon on the current frequency.
BeaconResult BEACON_Send(void);

// Auto-beacon (station.h STATION_AUTOBEACON): call once a second. The first
// beacon goes BEACON_FIRST_S after boot or switching it on, then one every
// beacon_interval_min.
#define BEACON_FIRST_S 10
void BEACON_Poll1s(uint32_t now_s);
void BEACON_RestartTimer(void);

#endif
