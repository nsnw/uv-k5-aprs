#include <stddef.h>

#include "beep.h"
#include "board.h"
#include "driver/bk4819.h"
#include "driver/system.h"
#include "radio.h"

typedef struct {
    uint16_t hz;      // 0: silence
    uint8_t  ticks;   // 10 ms each; 0 ends the sequence
} Segment;

// Each starts with 60 ms of silence for the amplifier's switch-on pop to die
// away, and ends with 20 ms before it is switched off (upstream AUDIO_PlayBeep).
static const Segment MESSAGE[]  = { {0, 6}, {1000, 12}, {0, 8}, {1500, 12}, {0, 2}, {0, 0} };
static const Segment SENT[]     = { {0, 6}, {1800, 10}, {0, 2}, {0, 0} };
static const Segment RECEIVED[] = { {0, 6}, {700, 7},   {0, 2}, {0, 0} };

static BeepKind       sPending;
static uint16_t       sWaitTicks;    // since the pending beep was requested
static uint16_t       sQuietTicks;   // channel quiet for this long
static const Segment *sSeg;          // playing: the current segment
static uint8_t        sLeft;         // ticks left in it

static uint16_t ToneWord(uint32_t hz)   // as upstream scale_freq()
{
    return (uint16_t)(((hz * 1353245u) + (1u << 16)) >> 17);
}

static void Begin(void)
{
    // as upstream BK4819_PlayTone() + AUDIO_PlayBeep(); leaves receive
    BK4819_WriteRegister(BK4819_REG_3F, 0);              // no interrupts meanwhile
    BK4819_EnterTxMute();
    BK4819_SetAF(BK4819_AF_BEEP);
    BK4819_WriteRegister(BK4819_REG_70, BK4819_REG_70_ENABLE_TONE1
                         | ((BEEP_GAIN & 0x7F) << BK4819_REG_70_SHIFT_TONE1_TUNING_GAIN));
    BK4819_WriteRegister(BK4819_REG_30, 0);
    BK4819_WriteRegister(BK4819_REG_30, BK4819_REG_30_ENABLE_AF_DAC | BK4819_REG_30_ENABLE_DISC_MODE
                         | BK4819_REG_30_ENABLE_TX_DSP);
    SYSTEM_DelayMs(2);
    BOARD_SetAudioAmp(true);
}

static void End(void)
{
    BK4819_WriteRegister(BK4819_REG_70, 0);
    BOARD_SetAudioAmp(false);
    BK4819_SetAF(BK4819_AF_MUTE);
    RADIO_Apply();                                       // receive again (re-arms APRS RX, audio state)
}

static void StartSegment(void)
{
    sLeft = sSeg->ticks;
    if (sSeg->hz) {
        BK4819_WriteRegister(BK4819_REG_71, ToneWord(sSeg->hz));
        BK4819_ExitTxMute();
    }
}

void BEEP_Request(BeepKind kind)
{
    if (kind > sPending) {
        if (sPending == BEEP_NONE)
            sWaitTicks = 0;
        sPending = kind;
    }
}

bool BEEP_IsPlaying(void) { return sSeg != NULL; }

bool BEEP_Poll10ms(bool channel_quiet)
{
    if (sSeg) {
        if (--sLeft)
            return true;
        BK4819_EnterTxMute();
        sSeg++;
        if (sSeg->ticks == 0) {
            sSeg = NULL;
            End();
            sQuietTicks = 0;       // RX was off: start counting afresh
            return false;
        }
        StartSegment();
        return true;
    }

    if (!channel_quiet)
        sQuietTicks = 0;
    else if (sQuietTicks < BEEP_QUIET_TICKS)
        sQuietTicks++;
    if (sPending == BEEP_NONE)
        return false;
    if (++sWaitTicks < BEEP_MAX_WAIT_TICKS && sQuietTicks < BEEP_QUIET_TICKS)
        return false;

    sSeg = sPending == BEEP_MESSAGE ? MESSAGE : sPending == BEEP_SENT ? SENT : RECEIVED;
    sPending = BEEP_NONE;
    Begin();
    StartSegment();
    return true;
}
