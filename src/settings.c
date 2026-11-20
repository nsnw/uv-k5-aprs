#include <string.h>

#include "driver/bk4819.h"
#include "driver/eeprom.h"
#include "radio.h"
#include "settings.h"

Settings_t gSettings;

static void Defaults(void)
{
    memset(&gSettings, 0, sizeof(gSettings));
    gSettings.magic      = SETTINGS_MAGIC;
    gSettings.frequency  = 14439000;  // 144.390 MHz, APRS
    gSettings.modulation = MOD_FM;
    gSettings.bandwidth  = BK4819_FILTER_BW_NARROW;
    gSettings.step_index = RADIO_DEFAULT_STEP_INDEX;
    gSettings.squelch    = 3;
    gSettings.backlight  = 160;
}

void SETTINGS_Load(void)
{
    EEPROM_ReadBuffer(SETTINGS_EEPROM_ADDR, &gSettings, sizeof(gSettings));

    if (gSettings.magic != SETTINGS_MAGIC
        || !RADIO_IsValidFrequency(gSettings.frequency)
        || gSettings.modulation >= MOD_COUNT
        || gSettings.bandwidth > BK4819_FILTER_BW_NARROWER
        || gSettings.step_index >= RADIO_STEP_COUNT
        || gSettings.squelch > 9
        || gSettings.memory > 10)
    {
        Defaults();
        SETTINGS_Save();
    }
    if (gSettings.list_by_distance > 1)
        gSettings.list_by_distance = 0;
}

void SETTINGS_Save(void)
{
    // EEPROM_WriteBuffer skips pages whose contents are unchanged.
    const uint8_t *p = (const uint8_t *)&gSettings;
    for (unsigned i = 0; i < sizeof(gSettings); i += 8)
        EEPROM_WriteBuffer(SETTINGS_EEPROM_ADDR + i, p + i);
}
