#include <string.h>

#include "aprs.h"
#include "driver/eeprom.h"
#include "msg.h"
#include "store.h"

#define PAGE        8
#define HEADER      16
#define STORE_MAGIC 0x4B35u                     // "5K"

typedef struct {
    uint16_t magic;
    uint16_t entry_size;                        // a different layout reads as empty
    uint8_t  count;
    uint8_t  max;
    uint8_t  reserved[10];
} Header;

_Static_assert(sizeof(Header) == HEADER, "Header must be two EEPROM pages");

typedef struct {
    uint16_t addr;
    uint16_t entry_size;
    uint8_t  max;
    uint16_t delay_s;                           // after the last change ...
    uint16_t interval_s;                        // ... and at least this long after the last write
    uint8_t *entries;
    uint8_t *count;
    bool   (*restore)(void *entry);
    // state
    uint32_t saved_hash;                        // what's in EEPROM (or being written)
    uint32_t seen_hash;                         // RAM, as of the last Poll1s
    uint32_t changed_s, written_s;
    uint16_t pos;                               // next byte to write; 0xFFFF = idle
    uint16_t len;                               // bytes in this pass, header included, whole pages
} Table;

static bool RestoreMessage(void *e) { return MSG_RestoreMessage((AprsMessage *)e); }
static bool RestoreStation(void *e) { return APRS_RestoreStation((AprsStation *)e); }

static Table sTables[2];

_Static_assert(STORE_MSG_ADDR + HEADER + MSG_INBOX * sizeof(AprsMessage) <= STORE_STATION_ADDR,
               "messages overlap the stations");
_Static_assert(STORE_STATION_ADDR + HEADER + APRS_MAX_STATIONS * sizeof(AprsStation) <= STORE_END,
               "stations overrun STORE_END");

static uint32_t Hash(const Table *t)
{
    uint32_t h = 2166136261u ^ *t->count;
    const uint16_t n = (uint16_t)(*t->count * t->entry_size);
    for (uint16_t i = 0; i < n; i++) { h ^= t->entries[i]; h *= 16777619u; }
    return h;
}

static void LoadTable(Table *t)
{
    Header h;
    EEPROM_ReadBuffer(t->addr, &h, sizeof h);
    *t->count = 0;
    if (h.magic == STORE_MAGIC && h.entry_size == t->entry_size && h.count <= t->max) {
        for (uint8_t i = 0; i < h.count; i++) {
            uint8_t *e = t->entries + *t->count * t->entry_size;
            EEPROM_ReadBuffer((uint16_t)(t->addr + HEADER + i * t->entry_size), e, (uint8_t)t->entry_size);
            if (t->restore(e))
                (*t->count)++;
        }
    }
    // What's there now counts as saved: nothing to write until something changes.
    t->saved_hash = t->seen_hash = Hash(t);
    t->pos = 0xFFFF;
}

void STORE_Load(void)
{
    uint8_t *count;
    sTables[0] = (Table){ STORE_MSG_ADDR, sizeof(AprsMessage), MSG_INBOX,
                          STORE_MSG_DELAY_S, 0, NULL, NULL, RestoreMessage, 0, 0, 0, 0, 0xFFFF, 0 };
    sTables[0].entries = (uint8_t *)MSG_Table(&count);
    sTables[0].count   = count;
    sTables[1] = (Table){ STORE_STATION_ADDR, sizeof(AprsStation), APRS_MAX_STATIONS,
                          STORE_STATION_DELAY_S, STORE_STATION_INTERVAL_S, NULL, NULL, RestoreStation,
                          0, 0, 0, 0, 0xFFFF, 0 };
    sTables[1].entries = (uint8_t *)APRS_StationTable(&count);
    sTables[1].count   = count;
    for (uint8_t i = 0; i < 2; i++)
        LoadTable(&sTables[i]);
}

void STORE_Poll1s(uint32_t now_s)
{
    for (uint8_t i = 0; i < 2; i++) {
        Table *t = &sTables[i];
        const uint32_t h = Hash(t);
        if (h != t->seen_hash) {
            t->seen_hash = h;
            t->changed_s = now_s;
        }
        // A change during a pass gets another pass once this one ends.
        if (t->pos == 0xFFFF && h != t->saved_hash
            && now_s - t->changed_s >= t->delay_s
            && (t->written_s == 0 || now_s - t->written_s >= t->interval_s)) {
            t->saved_hash = h;
            t->written_s  = now_s ? now_s : 1;
            t->len        = (uint16_t)((HEADER + *t->count * t->entry_size + PAGE - 1) / PAGE * PAGE);
            t->pos        = HEADER;             // entries first, the header (count) last
        }
    }
}

void STORE_Poll10ms(bool busy)
{
    if (busy)
        return;
    for (uint8_t i = 0; i < 2; i++) {
        Table *t = &sTables[i];
        if (t->pos == 0xFFFF)
            continue;
        if (t->pos < t->len) {
            // the count may have dropped since the pass started: write what's there
            uint8_t page[PAGE] = { 0 };
            const uint16_t off = (uint16_t)(t->pos - HEADER);
            const uint16_t have = (uint16_t)(*t->count * t->entry_size);
            if (off < have)
                memcpy(page, t->entries + off, have - off < PAGE ? have - off : PAGE);
            EEPROM_WriteBuffer((uint16_t)(t->addr + t->pos), page);
            t->pos = (uint16_t)(t->pos + PAGE);
            return;
        }
        if (t->pos == t->len) {                 // first header page: magic, size, count
            Header h;
            memset(&h, 0, sizeof h);
            h.magic      = STORE_MAGIC;
            h.entry_size = t->entry_size;
            h.count      = (uint8_t)((t->len - HEADER) / t->entry_size);   // rounding up adds < 1 entry
            h.max        = t->max;
            EEPROM_WriteBuffer(t->addr, &h);
            t->pos = (uint16_t)(t->len + PAGE);
            return;
        }
        {                                       // second header page (reserved)
            Header h;
            memset(&h, 0, sizeof h);
            EEPROM_WriteBuffer((uint16_t)(t->addr + PAGE), (const uint8_t *)&h + PAGE);
            t->pos = 0xFFFF;
            return;
        }
    }
}

bool STORE_Busy(void)
{
    return sTables[0].pos != 0xFFFF || sTables[1].pos != 0xFFFF;
}
