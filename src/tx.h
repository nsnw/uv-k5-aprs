#ifndef TX_H
#define TX_H

#include <stdbool.h>
#include <stdint.h>

// APRS (Bell 202 AFSK, 1200 baud) transmitter.
//
// Safety rules, enforced here for every transmission:
//   - only in TX_BANDS (6 m, 2 m, 1.25 m, 70 cm), and only with a factory power calibration
//   - at most TX_MAX_MS on air per frame; longer frames are refused before keying
//   - power never above the radio's calibrated "low" setting
// Nothing else in the firmware keys the transmitter.

#define TX_MAX_MS        3000
#define TX_MAX_FRAME     330      // bytes, without FCS

typedef enum {
    TX_OK = 0,
    TX_ERR_FREQUENCY,             // outside the TX bands
    TX_ERR_LENGTH,                // frame empty or too long
    TX_ERR_TOO_LONG_ON_AIR,       // would exceed TX_MAX_MS
    TX_ERR_NO_CALIBRATION,        // no factory power calibration for this band
} TxResult;

typedef struct {
    uint8_t  tone_gain;           // BK4819 REG_70 tone-1 tuning gain, 0..127 (deviation)
    uint8_t  power_bias;          // PA bias, clamped to the calibrated low-power value
    uint8_t  preamble_flags;      // 0x7E flags before the frame (TXDELAY), 6.67 ms each
    uint8_t  tail_flags;          // flags after the FCS (TXtail)
} TxConfig;

extern TxConfig gTxConfig;

bool     TX_IsInBand(uint32_t freq_10hz);            // in TX_BANDS (tuning is limited to these)
bool     TX_IsAllowedFrequency(uint32_t freq_10hz);  // ... and has a power calibration
uint8_t  TX_CalibratedLowBias(uint32_t freq_10hz);

// Transmits one AX.25 frame (addresses .. info, without FCS) on the current
// frequency, blocking until done. Returns to receive afterwards.
// `on_air_ms` (optional) gets the time spent transmitting.
TxResult TX_SendFrame(const uint8_t *frame, uint16_t len, uint16_t *on_air_ms);

// Every TX_SendFrame() call since boot, for confirmation from the host.
typedef struct {
    uint16_t attempts;
    uint8_t  last_result;     // TxResult
    uint16_t last_ms;         // on-air time of the last attempt, 0 if refused
} TxStats;

extern TxStats gTxStats;

// Called once per transmitted bit, right after the tone change, with most of
// a bit time (833 us) to spare; KISS uses it to keep draining the UART.
// NULL to remove.
void TX_SetBitHook(void (*hook)(void));

#endif
