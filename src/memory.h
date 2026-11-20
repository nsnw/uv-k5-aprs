#ifndef MEMORY_H
#define MEMORY_H

#include <stdbool.h>
#include <stdint.h>

// Ten frequency memories, 0-9, stored in EEPROM at 0x0060 (see settings.h).
// Recalled by holding a digit in the APRS view; stored, named and cleared
// from the settings menu (menu.h).

#define MEM_EEPROM_ADDR  0x0060
#define MEM_COUNT        10
#define MEM_NAME_LEN     6
#define MEM_MAGIC        0x4D4Bu   // "KM"

typedef struct {
    uint32_t frequency;            // 10 Hz units; 0 or 0xFFFFFFFF = empty
    char     name[MEM_NAME_LEN];   // A-Z 0-9 '-', unused positions ' ' (may be all blank)
    uint16_t magic;                // MEM_MAGIC: old stock channel data here isn't a memory
    uint8_t  reserved[4];
} MemoryEntry;

_Static_assert(sizeof(MemoryEntry) == 16, "MemoryEntry must be two EEPROM pages");

void MEM_Load(void);
const MemoryEntry *MEM_Get(uint8_t slot);          // NULL if empty
bool MEM_Set(uint8_t slot, uint32_t frequency, const char name[MEM_NAME_LEN]);
void MEM_Clear(uint8_t slot);
int  MEM_Find(uint32_t frequency);                 // first slot on this frequency, -1 if none

#endif
