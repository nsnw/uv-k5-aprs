#include "beep.h"
#include "board.h"
#include "driver/bk4819.h"
#include "driver/system.h"
#include "radio.h"

static uint16_t ToneWord(uint32_t hz)   // as upstream scale_freq()
{
    return (uint16_t)(((hz * 1353245u) + (1u << 16)) >> 17);
}

static void Tone(uint32_t hz, uint16_t ms)
{
    BK4819_WriteRegister(BK4819_REG_71, ToneWord(hz));
    BK4819_ExitTxMute();
    SYSTEM_DelayMs(ms);
    BK4819_EnterTxMute();
}

static void Begin(void)
{
    // as upstream BK4819_PlayTone() + AUDIO_PlayBeep()
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
    SYSTEM_DelayMs(60);                                  // let the amplifier's pop die away
}

static void End(void)
{
    SYSTEM_DelayMs(20);
    BK4819_WriteRegister(BK4819_REG_70, 0);
    BOARD_SetAudioAmp(false);
    BK4819_SetAF(BK4819_AF_MUTE);
    RADIO_Apply();                                       // receive again (re-arms APRS RX, audio state)
}

void BEEP_Message(void)
{
    Begin();
    Tone(1000, 120);
    SYSTEM_DelayMs(80);
    Tone(1500, 120);
    End();
}

void BEEP_Sent(void)
{
    Begin();
    Tone(1800, 100);
    End();
}
