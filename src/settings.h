#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdint.h>

// EEPROM map (24C64, 8 KiB, written in 8-byte pages)
//
//   0x0000 - 0x000F  Settings_t (this file)
//   0x0010 - 0x004F  BeaconConfig (beacon.h)
//   0x0050 - 0x005F  StationConfig (station.h)
//   0x0060 - 0x00FF  MemoryEntry[10] (memory.h)
//   0x0100 - 0x04FF  message inbox (store.h)
//   0x0500 - 0x0BFF  station list (store.h)
//   0x0C00 - 0x1DFF  free
//   0x1E00 - 0x1FFF  factory calibration - never written by this firmware
//
// The stock layout is NOT used: flashing this firmware overwrites the start
// of the old channel memory. Back up the EEPROM before switching.

#define SETTINGS_EEPROM_ADDR 0x0000
#define SETTINGS_MAGIC       0x324D354Bu  // "K5M2": APRS build, frequency = last tuned

typedef enum {
    MOD_FM = 0,
    MOD_AM,
    MOD_USB,
    MOD_COUNT
} Modulation_t;

typedef struct {
    uint32_t magic;
    uint32_t frequency;   // 10 Hz units
    uint8_t  modulation;  // Modulation_t
    uint8_t  bandwidth;   // BK4819_FilterBandwidth_t
    uint8_t  step_index;  // index into STEP_TABLE
    uint8_t  squelch;     // 0 (open) .. 9
    uint8_t  backlight;   // 0..255
    uint8_t  memory;      // 0 = frequency mode, 1..10 = memory mode on memory 0..9
    uint8_t  list_by_distance;   // station list: 0 = most recent first, 1 = nearest first
    uint8_t  reserved;
} Settings_t;

_Static_assert(sizeof(Settings_t) == 16, "Settings_t must be two EEPROM pages");

extern Settings_t gSettings;

void SETTINGS_Load(void);
void SETTINGS_Save(void);

#endif
