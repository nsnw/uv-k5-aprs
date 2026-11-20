#include <string.h>

#include "driver/eeprom.h"
#include "station.h"
#ifdef ENABLE_TX
#include "tx.h"
#endif

static StationConfig sCfg;

static bool Valid(const StationConfig *c)
{
    return c->magic == STATION_MAGIC
        && c->txdelay_flags >= 1 && c->txdelay_flags <= 200
        && c->txtail_flags >= 1 && c->txtail_flags <= 50
        && (c->flags & ~(STATION_DIGI | STATION_AUTOBEACON)) == 0
        && c->digi_max_hops >= 1 && c->digi_max_hops <= 7
        && c->beacon_interval_min >= 1 && c->beacon_interval_min <= 1440;
}

static void Save(void)
{
    const uint8_t *p = (const uint8_t *)&sCfg;
    for (unsigned i = 0; i < sizeof sCfg; i += 8)
        EEPROM_WriteBuffer(STATION_EEPROM_ADDR + i, p + i);
}

void STATION_ApplyTxTiming(void)
{
#ifdef ENABLE_TX
    gTxConfig.preamble_flags = sCfg.txdelay_flags;
    gTxConfig.tail_flags     = sCfg.txtail_flags;
#endif
}

void STATION_Load(void)
{
    EEPROM_ReadBuffer(STATION_EEPROM_ADDR, &sCfg, sizeof sCfg);
    if (!Valid(&sCfg)) {
        memset(&sCfg, 0, sizeof sCfg);
        sCfg.magic               = STATION_MAGIC;
        sCfg.txdelay_flags       = 40;    // ~267 ms, as tested
        sCfg.txtail_flags        = 3;
        sCfg.digi_max_hops       = 2;
        sCfg.beacon_interval_min = 30;
        Save();
    }
    STATION_ApplyTxTiming();
}

const StationConfig *STATION_Get(void) { return &sCfg; }

bool STATION_Set(const StationConfig *cfg)
{
    if (!Valid(cfg))
        return false;
    sCfg = *cfg;
    Save();
    STATION_ApplyTxTiming();
    return true;
}

bool STATION_Toggle(uint8_t flag)
{
    sCfg.flags ^= flag;
    Save();
    return (sCfg.flags & flag) != 0;
}
