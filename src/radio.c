// Receive-only control of the BK4819.
//
// The register sequences follow upstream RADIO_SetupRegisters(),
// RADIO_ConfigureSquelchAndOutputPower(), RADIO_SetModulation() and
// APP_StartListening(), reduced to a single VFO with no CTCSS/DCS, VOX,
// DTMF, scrambler or compander.

#include "aprs.h"
#include "board.h"
#include "console.h"
#include "driver/bk4819.h"
#include "driver/eeprom.h"
#include "driver/system.h"
#include "kiss.h"
#include "radio.h"
#include "settings.h"
#ifdef ENABLE_TX
#include "tx.h"
#endif

// 100 Hz, 1k, 2.5k, 5k, 6.25k, 10k, 12.5k, 25k, 100k
const uint16_t RADIO_STEP_TABLE[RADIO_STEP_COUNT] = {
    10, 100, 250, 500, 625, 1000, 1250, 2500, 10000
};

// Chip limits (upstream BX4819_band1/band2 with "wide RX" enabled).
#define FREQ_MIN        1800000u    //   18 MHz
#define FREQ_GAP_LOWER 63000000u    //  630 MHz
#define FREQ_GAP_UPPER 84000000u    //  840 MHz
#define FREQ_MAX      130000000u    // 1300 MHz

// Factory squelch calibration tables (stock EEPROM layout, read-only).
#define SQUELCH_CAL_UHF 0x1E00
#define SQUELCH_CAL_VHF 0x1E60
#define SQUELCH_VHF_BELOW 17400000u  // 174 MHz

static bool sSquelchOpen;
static bool sMonitor;
static bool sAudioOpen;

// The APRS station tunes only where it can also transmit (tx.h TX_BANDS).
bool RADIO_IsTunable(uint32_t f)
{
#ifdef ENABLE_TX
    return RADIO_IsValidFrequency(f) && TX_IsInBand(f);
#else
    return RADIO_IsValidFrequency(f);
#endif
}

bool RADIO_IsValidFrequency(uint32_t f)
{
    return (f >= FREQ_MIN && f < FREQ_GAP_LOWER) || (f >= FREQ_GAP_UPPER && f <= FREQ_MAX);
}

static void SetAudio(bool open)
{
    if (open == sAudioOpen)
        return;
    sAudioOpen = open;

    if (open) {
        static const BK4819_AF_Type_t af[MOD_COUNT] = {
            [MOD_FM]  = BK4819_AF_FM,
            [MOD_AM]  = BK4819_AF_AM,
            [MOD_USB] = BK4819_AF_BASEBAND2,
        };
        BK4819_SetAF(af[gSettings.modulation]);
        BOARD_SetAudioAmp(true);
    } else {
        BOARD_SetAudioAmp(false);
        BK4819_SetAF(BK4819_AF_MUTE);
    }
    BK4819_ToggleGpioOut(BK4819_GPIO6_PIN2_GREEN, open);
}

static void UpdateAudio(void)
{
    // In APRS mode the FSK receiver owns the chip and squelch interrupts stop
    // arriving, so only the monitor opens audio there.
    SetAudio(sMonitor || (sSquelchOpen && !APRS_IsActive()));
}

static void ConfigureSquelch(uint32_t f)
{
    uint8_t open_rssi = 0, close_rssi = 0;
    uint8_t open_noise = 127, close_noise = 127;
    uint8_t open_glitch = 255, close_glitch = 255;

    if (gSettings.squelch > 0) {
        const uint16_t base = (f < SQUELCH_VHF_BELOW ? SQUELCH_CAL_VHF : SQUELCH_CAL_UHF) + gSettings.squelch;
        EEPROM_ReadBuffer(base + 0x00, &open_rssi,    1);
        EEPROM_ReadBuffer(base + 0x10, &close_rssi,   1);
        EEPROM_ReadBuffer(base + 0x20, &open_noise,   1);
        EEPROM_ReadBuffer(base + 0x30, &close_noise,  1);
        EEPROM_ReadBuffer(base + 0x40, &close_glitch, 1);
        EEPROM_ReadBuffer(base + 0x50, &open_glitch,  1);
        if (open_noise > 127)  open_noise = 127;
        if (close_noise > 127) close_noise = 127;
    }

    // Also mutes AF and re-enables the RX chain (BK4819_RX_TurnOn).
    BK4819_SetupSquelch(open_rssi, close_rssi, open_noise, close_noise, close_glitch, open_glitch);
}

static void ConfigureModulation(void)
{
    const bool am  = gSettings.modulation == MOD_AM;
    const bool usb = gSettings.modulation == MOD_USB;

    BK4819_SetRegValue(afDacGainRegSpec, 0xF);
    BK4819_WriteRegister(BK4819_REG_3D, usb ? 0 : 0x2AAB);
    BK4819_SetRegValue(afcDisableRegSpec, gSettings.modulation != MOD_FM);

    BK4819_SetAGC(true);
    BK4819_InitAGC(am);
}

void RADIO_Apply(void)
{
    const uint32_t f = gSettings.frequency;

    sSquelchOpen = false;
    SetAudio(false);

    BK4819_SetFilterBandwidth((BK4819_FilterBandwidth_t)gSettings.bandwidth, false);

    BK4819_ToggleGpioOut(BK4819_GPIO5_PIN1_RED, false);
    BK4819_ToggleGpioOut(BK4819_GPIO1_PIN29_PA_ENABLE, false);

    // Drain any pending interrupts, then mask everything while we reconfigure.
    while (BK4819_ReadRegister(BK4819_REG_0C) & 1u) {
        BK4819_WriteRegister(BK4819_REG_02, 0);
        SYSTEM_DelayMs(1);
    }
    BK4819_WriteRegister(BK4819_REG_3F, 0);

    BK4819_SetFrequency(f);
    ConfigureSquelch(f);
    BK4819_PickRXFilterPathBasedOnFrequency(f);
    BK4819_ToggleGpioOut(BK4819_GPIO0_PIN28_RX_ENABLE, true);

    BK4819_WriteRegister(BK4819_REG_48,
        (11u << 12) |   // undocumented
        ( 0u << 10) |   // AF RX gain 1: 0 dB
        (58u <<  4) |   // AF RX gain 2
        ( 8u <<  0));   // AF DAC gain

    BK4819_DisableScramble();
    BK4819_SetCompander(0);
    BK4819_DisableVox();
    ConfigureModulation();

    BK4819_WriteRegister(BK4819_REG_3F, BK4819_REG_3F_SQUELCH_FOUND | BK4819_REG_3F_SQUELCH_LOST);

    if (APRS_IsActive())
        APRS_ConfigureChip();

    UpdateAudio();
}

void RADIO_Init(void)
{
    BK4819_Init();
    RADIO_Apply();
}

void RADIO_Poll(void)
{
    while (BK4819_ReadRegister(BK4819_REG_0C) & 1u) {
        BK4819_WriteRegister(BK4819_REG_02, 0);
        const uint16_t status = BK4819_ReadRegister(BK4819_REG_02);
        if (!KISS_IsActive())
            CONSOLE_OnRadioInterrupt(status);
        APRS_OnInterrupt(status);

        if (status & BK4819_REG_02_SQUELCH_LOST)
            sSquelchOpen = true;    // "lost" = squelch no longer closed
        if (status & BK4819_REG_02_SQUELCH_FOUND)
            sSquelchOpen = false;
    }
    UpdateAudio();
}

void RADIO_SetMonitor(bool on)
{
    sMonitor = on;
    UpdateAudio();
}

bool RADIO_IsAudioOpen(void)  { return sAudioOpen; }
bool RADIO_IsMonitoring(void) { return sMonitor; }

int16_t RADIO_GetRSSI_dBm(void)
{
    return BK4819_GetRSSI_dBm();
}
