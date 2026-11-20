#ifndef STATION_H
#define STATION_H

#include <stdbool.h>
#include <stdint.h>

// Station options, stored in EEPROM at 0x0050 and set from the host
// (`k5 config set`, console `o`) or the keys (digipeater / beacon toggles).
// The callsign comes from the beacon configuration (beacon.h).

#define STATION_EEPROM_ADDR  0x0050
#define STATION_MAGIC        0x3153354Bu   // "K5S1"

#define STATION_DIGI         (1u << 0)     // digipeater on
#define STATION_AUTOBEACON   (1u << 1)     // beacon every beacon_interval_min
#define STATION_MSG_QUIET    (1u << 2)     // no beep for new messages (beep.h)
#define STATION_TX_QUIET     (1u << 3)     // no beep after our own transmissions (txq.h)

typedef struct {
    uint32_t magic;
    uint8_t  txdelay_flags;        // 0x7E flags before each frame, 1..200 (6.67 ms each: 8 bits at 1200 baud)
    uint8_t  txtail_flags;         // flags after it, 1..50
    uint8_t  flags;                // STATION_*
    uint8_t  digi_max_hops;        // largest n of WIDEn-N relayed, 1..7 (1 = fill-in, WIDE1-1 only)
    uint16_t beacon_interval_min;  // 1..1440
    uint8_t  reserved[6];
} StationConfig;

_Static_assert(sizeof(StationConfig) == 16, "StationConfig must be two EEPROM pages");

void STATION_Load(void);                       // also applies the TX timing
const StationConfig *STATION_Get(void);
bool STATION_Set(const StationConfig *cfg);    // validates, stores, applies; false if invalid
void STATION_ApplyTxTiming(void);              // gTxConfig <- txdelay/txtail (e.g. after KISS)
bool STATION_Toggle(uint8_t flag);             // flips and stores a STATION_* flag; new state

#endif
