// Bell 202 AFSK transmitter for APRS.
//
// The BK4819's tone-1 generator (as upstream BK4819_TransmitTone uses for
// 1750 Hz) is switched between 1200 Hz (mark) and 2200 Hz (space) at
// 1200 baud. Bit timing comes from busy-waiting on SysTick->VAL, corrected
// for the RC oscillator's factory trim. The bit stream is standard HDLC:
// flags, LSB-first bytes with bit stuffing, FCS, flags; NRZI (a 0 bit flips
// the tone).
//
// The keying sequence follows upstream RADIO_SetTxParameters() /
// FUNCTION_Transmit() / BK4819_TransmitTone().

#ifdef ENABLE_TX

#include "ARMCM0.h"
#include "board.h"
#include "bsp/dp32g030/syscon.h"
#include "driver/bk4819.h"
#include "driver/eeprom.h"
#include "driver/system.h"
#include "hdlc_tx.h"
#include "radio.h"
#include "settings.h"
#include "tx.h"

#define BAUD            1200u
#define MARK_HZ         1200u
#define SPACE_HZ        2200u
#define TX_SETTLE_MS    50       // carrier + tone before the first flag (as upstream)

// Factory TX power calibration: 0x1ED0 + band*16 + level*3, three bias values
// (band bottom / middle / top), for the stock firmware's seven bands.
#define TXP_CAL_ADDR    0x1ED0

static const struct { uint32_t lower, upper; } BANDS[] = {   // stock frequencyBandTable
    {  5000000,  7600000 },     // 50-76 MHz
    { 10800000, 13700000 },     // 108-137
    { 13700000, 17400000 },     // 137-174
    { 17400000, 35000000 },     // 174-350
    { 35000000, 40000000 },     // 350-400
    { 40000000, 47000000 },     // 400-470
    { 47000000, 60000000 },     // 470-600
};

TxStats gTxStats;
static void (*sBitHook)(void);

void TX_SetBitHook(void (*hook)(void)) { sBitHook = hook; }

TxConfig gTxConfig = {
    .tone_gain      = 66,     // upstream TransmitTone level
    .power_bias     = 0xFF,   // i.e. the calibrated low value
    .preamble_flags = 40,     // ~267 ms; both set from station.h at boot
    .tail_flags     = 3,
};

// ------------------------------------------------------------ helpers


// Amateur bands we transmit on (10 Hz units, upper bound inclusive).
static const struct { uint32_t lower, upper; } TX_BANDS[] = {
    {  5000000,  5400000 },     // 6 m: 50-54 MHz
    { 14400000, 14800000 },     // 2 m: 144-148 MHz
    { 22200000, 22500000 },     // 1.25 m: 222-225 MHz
    { 43000000, 45000000 },     // 70 cm: 430-450 MHz
};

bool TX_IsInBand(uint32_t f)
{
    for (unsigned i = 0; i < sizeof TX_BANDS / sizeof TX_BANDS[0]; i++)
        if (f >= TX_BANDS[i].lower && f <= TX_BANDS[i].upper)
            return true;
    return false;
}

// In a TX band, and the radio has a factory power calibration there.
bool TX_IsAllowedFrequency(uint32_t f)
{
    return TX_IsInBand(f) && TX_CalibratedLowBias(f) != 0;
}

// Stock FREQUENCY_CalculateOutputPower(), low-power row only; 0 = no calibration.
uint8_t TX_CalibratedLowBias(uint32_t f)
{
    unsigned band = 0;
    while (band < sizeof BANDS / sizeof BANDS[0] && !(f >= BANDS[band].lower && f < BANDS[band].upper))
        band++;
    if (band == sizeof BANDS / sizeof BANDS[0])
        return 0;
    uint8_t txp[3];
    EEPROM_ReadBuffer(TXP_CAL_ADDR + band * 16 + 0 * 3, txp, 3);
    if (txp[0] == 0xFF || txp[1] == 0xFF || txp[2] == 0xFF || (txp[0] | txp[1] | txp[2]) == 0)
        return 0;   // uncalibrated: refuse to pick a power

    const int32_t lower = (int32_t)BANDS[band].lower, upper = (int32_t)BANDS[band].upper, middle = (lower + upper) / 2;
    int32_t mid = txp[1];
    if ((int32_t)f <= middle)
        mid += ((txp[1] - txp[0]) * ((int32_t)f - lower)) / (middle - lower);
    else
        mid += ((txp[2] - txp[1]) * ((int32_t)f - middle)) / (upper - middle);
    return (uint8_t)(mid < 0 ? 0 : mid > 255 ? 255 : mid);
}

static uint16_t ToneWord(uint32_t hz)   // as upstream scale_freq()
{
    return (uint16_t)(((hz * 1353245u) + (1u << 16)) >> 17);
}

static uint32_t CoreClockHz(void)       // as UART_Init(): RC oscillator trim
{
    const uint32_t delta    = SYSCON_RC_FREQ_DELTA;
    const uint32_t positive = (delta & SYSCON_RC_FREQ_DELTA_RCHF_SIG_MASK) >> SYSCON_RC_FREQ_DELTA_RCHF_SIG_SHIFT;
    const uint32_t offset   = (delta & SYSCON_RC_FREQ_DELTA_RCHF_DELTA_MASK) >> SYSCON_RC_FREQ_DELTA_RCHF_DELTA_SHIFT;
    return positive ? 48000000u + offset : 48000000u - offset;
}

// ------------------------------------------------------------ bit stream

static void EmitAll(HdlcBitSink sink, const uint8_t *frame, uint16_t len)
{
    HDLC_EmitFrame(sink, frame, len, gTxConfig.preamble_flags, gTxConfig.tail_flags);
}

static uint32_t sBitCount;
static void CountBit(uint8_t bit) { (void)bit; sBitCount++; }

// ------------------------------------------------------------ timing + tones

static uint32_t sTicksPerBit;
static uint32_t sPrevVal;
static uint32_t sElapsed;      // SysTick ticks since the first bit
static uint32_t sBitIndex;
static uint8_t  sMark;         // current tone: 1 = mark (1200), 0 = space (2200)
static uint16_t sMarkWord, sSpaceWord;

static void WaitUntil(uint32_t target)
{
    const uint32_t reload = SysTick->LOAD + 1;
    while (sElapsed < target) {
        const uint32_t v = SysTick->VAL;   // counts down, wraps to LOAD
        sElapsed += (v <= sPrevVal) ? (sPrevVal - v) : (sPrevVal + reload - v);
        sPrevVal = v;
    }
}

static void SendBit(uint8_t bit)
{
    if (!bit)
        sMark ^= 1;                        // NRZI
    WaitUntil(sBitIndex * sTicksPerBit);
    BK4819_WriteRegister(BK4819_REG_71, sMark ? sMarkWord : sSpaceWord);
    sBitIndex++;
    if (sBitHook)
        sBitHook();
}

// ------------------------------------------------------------ keying

static void KeyUp(uint32_t f, uint8_t bias)
{
    BOARD_SetAudioAmp(false);
    BK4819_WriteRegister(BK4819_REG_3F, 0);          // no interrupts while transmitting
    BK4819_ToggleGpioOut(BK4819_GPIO0_PIN28_RX_ENABLE, false);
    BK4819_SetFilterBandwidth(BK4819_FILTER_BW_WIDE, false);
    BK4819_SetFrequency(f);
    BK4819_SetCompander(0);

    BK4819_ExitBypass();                             // = BK4819_PrepareTransmit()
    BK4819_ExitTxMute();
    BK4819_TxOn_Beep();
    SYSTEM_DelayMs(10);

    BK4819_PickRXFilterPathBasedOnFrequency(f);
    BK4819_ToggleGpioOut(BK4819_GPIO1_PIN29_PA_ENABLE, true);
    SYSTEM_DelayMs(5);
    BK4819_SetupPowerAmplifier(bias, f);
    SYSTEM_DelayMs(10);
    BK4819_ExitSubAu();
    BK4819_ToggleGpioOut(BK4819_GPIO6_PIN2_GREEN, false);   // red only: not left yellow from "queued"
    BK4819_ToggleGpioOut(BK4819_GPIO5_PIN1_RED, true);

    // tone-1 generator into the TX path (as BK4819_TransmitTone)
    BK4819_EnterTxMute();
    BK4819_WriteRegister(BK4819_REG_70, BK4819_REG_70_MASK_ENABLE_TONE1
                         | ((gTxConfig.tone_gain & 0x7F) << BK4819_REG_70_SHIFT_TONE1_TUNING_GAIN));
    BK4819_WriteRegister(BK4819_REG_71, sMarkWord);
    BK4819_SetAF(BK4819_AF_MUTE);
    BK4819_EnableTXLink();
    SYSTEM_DelayMs(TX_SETTLE_MS);
    BK4819_ExitTxMute();
}

static void KeyDown(void)
{
    BK4819_EnterTxMute();
    BK4819_WriteRegister(BK4819_REG_70, 0);
    BK4819_SetupPowerAmplifier(0, 0);
    BK4819_ToggleGpioOut(BK4819_GPIO1_PIN29_PA_ENABLE, false);
    BK4819_ToggleGpioOut(BK4819_GPIO5_PIN1_RED, false);
    RADIO_Apply();                                   // back to receive (re-arms APRS RX)
}

static TxResult SendFrame(const uint8_t *frame, uint16_t len, uint16_t *on_air_ms);

TxResult TX_SendFrame(const uint8_t *frame, uint16_t len, uint16_t *on_air_ms)
{
    uint16_t ms = 0;
    const TxResult r = SendFrame(frame, len, &ms);
    gTxStats.attempts++;
    if (r == TX_OK)
        gTxStats.sent++;
    gTxStats.last_result = (uint8_t)r;
    gTxStats.last_ms     = (r == TX_OK) ? ms : 0;
    if (on_air_ms)
        *on_air_ms = gTxStats.last_ms;
    return r;
}

static TxResult SendFrame(const uint8_t *frame, uint16_t len, uint16_t *on_air_ms)
{
    const uint32_t f = gSettings.frequency;
    if (!TX_IsAllowedFrequency(f))
        return TX_ERR_FREQUENCY;
    if (len < 15 || len > TX_MAX_FRAME)
        return TX_ERR_LENGTH;
    const uint8_t cal = TX_CalibratedLowBias(f);
    if (cal == 0)
        return TX_ERR_NO_CALIBRATION;

    sBitCount = 0;
    EmitAll(CountBit, frame, len);
    const uint32_t ms = TX_SETTLE_MS + 35 + (sBitCount * 1000u + BAUD - 1) / BAUD;  // 35: keying delays
    if (ms > TX_MAX_MS)
        return TX_ERR_TOO_LONG_ON_AIR;

    const uint8_t bias = gTxConfig.power_bias < cal ? gTxConfig.power_bias : cal;
    sMarkWord    = ToneWord(MARK_HZ);
    sSpaceWord   = ToneWord(SPACE_HZ);
    sTicksPerBit = CoreClockHz() / BAUD;

    KeyUp(f, bias);

    sMark     = 1;
    sBitIndex = 0;
    sElapsed  = 0;
    sPrevVal  = SysTick->VAL;
    EmitAll(SendBit, frame, len);
    WaitUntil(sBitIndex * sTicksPerBit);             // let the last bit finish

    KeyDown();

    if (on_air_ms)
        *on_air_ms = (uint16_t)ms;
    return TX_OK;
}

#endif // ENABLE_TX
