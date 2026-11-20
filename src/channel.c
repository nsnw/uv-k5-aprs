#include "aprs.h"
#include "channel.h"

// The floor drops to any quieter reading at once and rises 1 dB/s otherwise.
#define RSSI_AVG 5                  // readings averaged (~0.5 s)

static int16_t sFloor_x10;          // noise floor, 0.1 dB units (0 = no data yet)
static int16_t sRecent[RSSI_AVG];
static uint8_t sRecentCount, sRecentNext;

void CHANNEL_ObserveRssi(int16_t dbm)
{
    sRecent[sRecentNext] = dbm;
    sRecentNext = (sRecentNext + 1) % RSSI_AVG;
    if (sRecentCount < RSSI_AVG)
        sRecentCount++;

    const int16_t x10 = (int16_t)(dbm * 10);
    if (sFloor_x10 == 0 || x10 < sFloor_x10)
        sFloor_x10 = x10;
    else
        sFloor_x10++;               // +0.1 dB per reading = ~1 dB/s
}

bool CHANNEL_IsBusy(void)
{
    if (APRS_IsReceiving())
        return true;
    if (sRecentCount < RSSI_AVG)
        return true;                // not enough history yet: be conservative
    int32_t sum = 0;
    for (uint8_t i = 0; i < RSSI_AVG; i++)
        sum += sRecent[i];
    const int32_t avg_x10 = sum * 10 / RSSI_AVG;
    return avg_x10 > sFloor_x10 + CHANNEL_BUSY_MARGIN_DB * 10;
}
