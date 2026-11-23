#ifndef BEACON_H
#define BEACON_H

#include <stdbool.h>
#include <stdint.h>

// APRS beacon: a position report (fixed position), a status report (">text",
// no position: APRS101 ch. 16) or both, per the beacon type. Stored in
// EEPROM, set on the radio (settings menu) or from the host (`k5 beacon set`).
// Sent on demand (hold F) or automatically (station.h STATION_AUTOBEACON).
//
// Without a position (none typed in yet, e.g. a digipeater) a position
// beacon falls back to the status report, so a callsign is all it needs.
//
// EEPROM 0x0010-0x004F BeaconConfig, 0x0C00-0x0C47 BeaconStatus (settings.h).

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

// Status report and beacon type, EEPROM 0x0C00.
#define BEACON_STATUS_EEPROM_ADDR  0x0C00
#define BEACON_STATUS_MAGIC        0x3154354Bu   // "K5T1"
#define BEACON_STATUS_MAX          62            // APRS status text limit (no timestamp)

typedef enum {
    BEACON_TYPE_POSITION = 0,  // position report (status if no position is set)
    BEACON_TYPE_STATUS,        // status report only
    BEACON_TYPE_BOTH,          // both (just the status if no position is set)
    BEACON_TYPE_COUNT
} BeaconType;

typedef struct {
    uint32_t magic;
    uint8_t  type;             // BeaconType
    uint8_t  reserved[3];
    char     text[64];         // up to BEACON_STATUS_MAX, NUL-terminated; "" = K5-APRS + version
} BeaconStatus;

_Static_assert(sizeof(BeaconStatus) == 72, "BeaconStatus must be 9 EEPROM pages");

typedef enum {
    BEACON_QUEUED = 0,         // goes out when the channel is clear (txq.h)
    BEACON_NOT_CONFIGURED,
    BEACON_QUEUE_FULL,
    BEACON_TX_REFUSED,         // not a TX frequency (tx.h)
    BEACON_NO_POSITION,        // a position was asked for (BEACON_SendPosition) but isn't set
    BEACON_STATUS_QUEUED,      // queued, as a status report only
} BeaconResult;

void BEACON_Load(void);
bool BEACON_IsConfigured(void);
const BeaconConfig *BEACON_Get(void);

// Validates and stores a new configuration; false if invalid (nothing written).
bool BEACON_Set(const BeaconConfig *cfg);

bool BEACON_HasPosition(void);                 // configured, with a position
const BeaconStatus *BEACON_GetStatus(void);    // defaults (position, "") if never set
bool BEACON_SetStatus(const BeaconStatus *s);  // validates and stores; false if invalid

// Build the AX.25 frames (no FCS); return the length, 0 if not configured /
// no position. The status text is "K5-APRS <version>" when none is set.
// The position report is plain ("!5102.34N/11404.86W[") or compressed
// ("!/5L!!<*e7>[ sT", 6 bytes shorter: station.h STATION_COMPRESSED).
uint16_t BEACON_BuildFrame(uint8_t *out, uint16_t size, bool compressed);   // position report
uint16_t BEACON_BuildStatusFrame(uint8_t *out, uint16_t size);   // status report

// Queues a beacon on the current frequency, per the beacon type.
BeaconResult BEACON_Send(void);
BeaconResult BEACON_SendPosition(void);        // ?APRSP: the position report only
BeaconResult BEACON_SendStatus(void);          // ?APRSS: the status report only

// Auto-beacon (station.h STATION_AUTOBEACON): call once a second. The first
// beacon goes BEACON_FIRST_S after boot, switching it on or changing the
// beacon (position, symbol, comment, path, status...), then one every
// beacon_interval_min. Fast start (unless STATION_NO_FAST_START): the gaps
// begin at 1 minute and double up to the interval (1, 2, 4, 8, 10, 10 ... for
// 10 minutes), so the station shows up on maps quickly without filling the
// channel. A manual beacon (hold F) doesn't change the schedule.
#define BEACON_FIRST_S 10
void BEACON_Poll1s(uint32_t now_s);
void BEACON_RestartTimer(void);
uint32_t BEACON_SecondsToNext(uint32_t now_s);   // 0 = none scheduled (auto-beacon off, or due)

#endif
