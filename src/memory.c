#include <string.h>

#include "driver/eeprom.h"
#include "memory.h"
#include "radio.h"

static MemoryEntry sMem[MEM_COUNT];

static bool Used(const MemoryEntry *m) { return m->magic == MEM_MAGIC && RADIO_IsTunable(m->frequency); }

static void Store(uint8_t slot)
{
    const uint8_t *p = (const uint8_t *)&sMem[slot];
    for (unsigned i = 0; i < sizeof sMem[slot]; i += 8)
        EEPROM_WriteBuffer(MEM_EEPROM_ADDR + slot * sizeof sMem[slot] + i, p + i);
}

void MEM_Load(void)
{
    EEPROM_ReadBuffer(MEM_EEPROM_ADDR, sMem, sizeof sMem);
    for (uint8_t i = 0; i < MEM_COUNT; i++) {
        if (!Used(&sMem[i])) {
            memset(&sMem[i], 0, sizeof sMem[i]);
            continue;
        }
        for (int c = 0; c < MEM_NAME_LEN; c++) {   // tidy anything odd in the name
            const char ch = sMem[i].name[c];
            if (!((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '-'))
                sMem[i].name[c] = ' ';
        }
    }
}

const MemoryEntry *MEM_Get(uint8_t slot)
{
    return slot < MEM_COUNT && Used(&sMem[slot]) ? &sMem[slot] : NULL;
}

bool MEM_Set(uint8_t slot, uint32_t frequency, const char name[MEM_NAME_LEN])
{
    if (slot >= MEM_COUNT || !RADIO_IsTunable(frequency))
        return false;
    memset(&sMem[slot], 0, sizeof sMem[slot]);
    sMem[slot].frequency = frequency;
    sMem[slot].magic     = MEM_MAGIC;
    memcpy(sMem[slot].name, name, MEM_NAME_LEN);
    Store(slot);
    return true;
}

void MEM_Clear(uint8_t slot)
{
    if (slot >= MEM_COUNT)
        return;
    memset(&sMem[slot], 0, sizeof sMem[slot]);
    Store(slot);
}

int MEM_Find(uint32_t frequency)
{
    for (uint8_t i = 0; i < MEM_COUNT; i++)
        if (Used(&sMem[i]) && sMem[i].frequency == frequency)
            return i;
    return -1;
}
