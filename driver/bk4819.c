/* Copyright 2023 Dual Tachyon
 * https://github.com/DualTachyon
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 *     Unless required by applicable law or agreed to in writing, software
 *     distributed under the License is distributed on an "AS IS" BASIS,
 *     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *     See the License for the specific language governing permissions and
 *     limitations under the License.
 */

// BK4819 RF transceiver: bit-banged 3-wire bus + receive-path helpers.
// Modified for K5-APRS: trimmed to receive functions plus the few transmit
// helpers APRS needs; tones, DTMF, CTCSS/DCS, VOX, FSK TX and roger-beep code removed. Function bodies
// below are otherwise unchanged from upstream.

#include <stdbool.h>
#include <stdint.h>

#include "bsp/dp32g030/gpio.h"
#include "bsp/dp32g030/portcon.h"
#include "driver/bk4819.h"
#include "driver/gpio.h"
#include "driver/system.h"
#include "driver/systick.h"

#ifndef ARRAY_SIZE
    #define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))
#endif

static uint16_t gBK4819_GpioOutState;

void BK4819_Init(void)
{
    GPIO_SetBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SCN);
    GPIO_SetBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SCL);
    GPIO_SetBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SDA);

    BK4819_WriteRegister(BK4819_REG_00, 0x8000);
    BK4819_WriteRegister(BK4819_REG_00, 0x0000);

    BK4819_WriteRegister(BK4819_REG_37, 0x1D0F);
    BK4819_WriteRegister(BK4819_REG_36, 0x0022);

    BK4819_InitAGC(false);
    BK4819_SetAGC(true);

    BK4819_WriteRegister(BK4819_REG_19, 0b0001000001000001);   // <15> MIC AGC  1 = disable  0 = enable

    BK4819_WriteRegister(BK4819_REG_7D, 0xE940);

    // REG_48 .. RX AF level
    //
    // <15:12> 11  ???  0 to 15
    //
    // <11:10> 0 AF Rx Gain-1
    //         0 =   0dB
    //         1 =  -6dB
    //         2 = -12dB
    //         3 = -18dB
    //
    // <9:4>   60 AF Rx Gain-2  -26dB ~ 5.5dB   0.5dB/step
    //         63 = max
    //          0 = mute
    //
    // <3:0>   15 AF DAC Gain (after Gain-1 and Gain-2) approx 2dB/step
    //         15 = max
    //          0 = min
    //
    BK4819_WriteRegister(BK4819_REG_48, //  0xB3A8);     // 1011 00 111010 1000
        (11u << 12) |     // ??? 0..15
        ( 0u << 10) |     // AF Rx Gain-1
        (58u <<  4) |     // AF Rx Gain-2
        ( 8u <<  0));     // AF DAC Gain (after Gain-1 and Gain-2)

    const uint8_t dtmf_coeffs[] = {111, 107, 103, 98, 80, 71, 58, 44, 65, 55, 37, 23, 228, 203, 181, 159};
    for (unsigned int i = 0; i < ARRAY_SIZE(dtmf_coeffs); i++)
        BK4819_WriteRegister(BK4819_REG_09, (i << 12) | dtmf_coeffs[i]);

    BK4819_WriteRegister(BK4819_REG_1F, 0x5454);
    BK4819_WriteRegister(BK4819_REG_3E, 0xA037);

    gBK4819_GpioOutState = 0x9000;

    BK4819_WriteRegister(BK4819_REG_33, 0x9000);
    BK4819_WriteRegister(BK4819_REG_3F, 0);
}

static uint16_t BK4819_ReadU16(void)
{
    unsigned int i;
    uint16_t     Value;

    PORTCON_PORTC_IE = (PORTCON_PORTC_IE & ~PORTCON_PORTC_IE_C2_MASK) | PORTCON_PORTC_IE_C2_BITS_ENABLE;
    GPIOC->DIR = (GPIOC->DIR & ~GPIO_DIR_2_MASK) | GPIO_DIR_2_BITS_INPUT;
    SYSTICK_DelayUs(1);
    Value = 0;
    for (i = 0; i < 16; i++)
    {
        Value <<= 1;
        Value |= GPIO_CheckBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SDA);
        GPIO_SetBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SCL);
        SYSTICK_DelayUs(1);
        GPIO_ClearBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SCL);
        SYSTICK_DelayUs(1);
    }
    PORTCON_PORTC_IE = (PORTCON_PORTC_IE & ~PORTCON_PORTC_IE_C2_MASK) | PORTCON_PORTC_IE_C2_BITS_DISABLE;
    GPIOC->DIR = (GPIOC->DIR & ~GPIO_DIR_2_MASK) | GPIO_DIR_2_BITS_OUTPUT;

    return Value;
}

uint16_t BK4819_ReadRegister(BK4819_REGISTER_t Register)
{
    uint16_t Value;

    GPIO_SetBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SCN);
    GPIO_ClearBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SCL);

    SYSTICK_DelayUs(1);

    GPIO_ClearBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SCN);
    BK4819_WriteU8(Register | 0x80);
    Value = BK4819_ReadU16();
    GPIO_SetBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SCN);

    SYSTICK_DelayUs(1);

    GPIO_SetBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SCL);
    GPIO_SetBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SDA);

    return Value;
}

void BK4819_WriteRegister(BK4819_REGISTER_t Register, uint16_t Data)
{
    GPIO_SetBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SCN);
    GPIO_ClearBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SCL);

    SYSTICK_DelayUs(1);

    GPIO_ClearBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SCN);
    BK4819_WriteU8(Register);

    SYSTICK_DelayUs(1);

    BK4819_WriteU16(Data);

    SYSTICK_DelayUs(1);

    GPIO_SetBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SCN);

    SYSTICK_DelayUs(1);

    GPIO_SetBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SCL);
    GPIO_SetBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SDA);
}

void BK4819_WriteU8(uint8_t Data)
{
    unsigned int i;

    GPIO_ClearBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SCL);
    for (i = 0; i < 8; i++)
    {
        if ((Data & 0x80) == 0)
            GPIO_ClearBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SDA);
        else
            GPIO_SetBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SDA);

        SYSTICK_DelayUs(1);
        GPIO_SetBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SCL);
        SYSTICK_DelayUs(1);

        Data <<= 1;

        GPIO_ClearBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SCL);
        SYSTICK_DelayUs(1);
    }
}

void BK4819_WriteU16(uint16_t Data)
{
    unsigned int i;

    GPIO_ClearBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SCL);
    for (i = 0; i < 16; i++)
    {
        if ((Data & 0x8000) == 0)
            GPIO_ClearBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SDA);
        else
            GPIO_SetBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SDA);

        SYSTICK_DelayUs(1);
        GPIO_SetBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SCL);

        Data <<= 1;

        SYSTICK_DelayUs(1);
        GPIO_ClearBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SCL);
        SYSTICK_DelayUs(1);
    }
}


void BK4819_SetAGC(bool enable)
{
    uint16_t regVal = BK4819_ReadRegister(BK4819_REG_7E);
    if(!(regVal & (1 << 15)) == enable)
        return;

    BK4819_WriteRegister(BK4819_REG_7E, (regVal & ~(1 << 15) & ~(0b111 << 12))
        | (!enable << 15)   // 0  AGC fix mode
        | (3u << 12)       // 3  AGC fix index
    );

    // if(enable) {
    //  BK4819_WriteRegister(BK4819_REG_7B, 0x8420);
    // }
    // else {
    //  BK4819_WriteRegister(BK4819_REG_7B, 0x318C);

    //  BK4819_WriteRegister(BK4819_REG_7C, 0x595E);
    //  BK4819_WriteRegister(BK4819_REG_20, 0x8DEF);

    //  for (uint8_t i = 0; i < 8; i++) {
    //      //BK4819_WriteRegister(BK4819_REG_06, ((i << 13) | 0x2500u) + 0x036u);
    //      BK4819_WriteRegister(BK4819_REG_06, (i & 7) << 13 | 0x4A << 7 | 0x36);
    //  }
    // }
}

void BK4819_InitAGC(bool amModulation)
{
    // REG_10, REG_11, REG_12 REG_13, REG_14
    //
    // Rx AGC Gain Table[]. (Index Max->Min is 3,2,1,0,-1)
    //
    // <15:10> ???
    //
    // <9:8>   LNA Gain Short
    //         3 =   0dB  <<<       1o11                read from spectrum          reference manual
    //         2 =                  -24dB               -19                          -11
    //         1 =                  -30dB               -24                          -16
    //         0 =                  -33dB               -28                          -19
    //
    // <7:5>   LNA Gain
    //         7 =   0dB
    //         6 =  -2dB
    //         5 =  -4dB
    //         4 =  -6dB
    //         3 =  -9dB
    //         2 = -14dB <<<
    //         1 = -19dB
    //         0 = -24dB
    //
    // <4:3>   MIXER Gain
    //         3 =   0dB <<<
    //         2 =  -3dB
    //         1 =  -6dB
    //         0 =  -8dB
    //
    // <2:0>   PGA Gain
    //         7 =   0dB
    //         6 =  -3dB <<<
    //         5 =  -6dB
    //         4 =  -9dB
    //         3 = -15dB
    //         2 = -21dB
    //         1 = -27dB
    //         0 = -33dB
    //

    BK4819_WriteRegister(BK4819_REG_13, 0x03BE);  // 0x03BE / 000000 11 101 11 110 /  -7dB
    BK4819_WriteRegister(BK4819_REG_12, 0x037B);  // 0x037B / 000000 11 011 11 011 / -24dB
    BK4819_WriteRegister(BK4819_REG_11, 0x027B);  // 0x027B / 000000 10 011 11 011 / -43dB
    BK4819_WriteRegister(BK4819_REG_10, 0x007A);  // 0x007A / 000000 00 011 11 010 / -58dB
    if(amModulation) {
        BK4819_WriteRegister(BK4819_REG_14, 0x0000);
        BK4819_WriteRegister(BK4819_REG_49, (0 << 14) | (50 << 7) | (32 << 0));
    }
    else{
        BK4819_WriteRegister(BK4819_REG_14, 0x0019);  // 0x0019 / 000000 00 000 11 001 / -79dB
        BK4819_WriteRegister(BK4819_REG_49, (0 << 14) | (84 << 7) | (56 << 0)); //0x2A38 / 00 1010100 0111000 / 84, 56
    }

    BK4819_WriteRegister(BK4819_REG_7B, 0x8420);

}

int8_t BK4819_GetRxGain_dB(void)
{
    union {
        struct {
            uint16_t pga:3;
            uint16_t mixer:2;
            uint16_t lna:3;
            uint16_t lnaS:2;
        };
        uint16_t __raw;
    } agcGainReg;

    union {
        struct {
            uint16_t _ : 5;
            uint16_t agcSigStrength : 7;
            int16_t gainIdx : 3;
            uint16_t agcEnab : 1;
        };
        uint16_t __raw;
    } reg7e;

    reg7e.__raw = BK4819_ReadRegister(BK4819_REG_7E);
    uint8_t gainAddr = reg7e.gainIdx < 0 ? BK4819_REG_14 : BK4819_REG_10 + reg7e.gainIdx;
    agcGainReg.__raw = BK4819_ReadRegister(gainAddr);
    int8_t lnaShortTab[] = {-28, -24, -19, 0};
    int8_t lnaTab[] = {-24, -19, -14, -9, -6, -4, -2, 0};
    int8_t mixerTab[] = {-8, -6, -3, 0};
    int8_t pgaTab[] = {-33, -27, -21, -15, -9, -6, -3, 0};
    return lnaShortTab[agcGainReg.lnaS] + lnaTab[agcGainReg.lna] + mixerTab[agcGainReg.mixer] + pgaTab[agcGainReg.pga];
}

int16_t BK4819_GetRSSI_dBm(void)
{
    uint16_t rssi = BK4819_GetRSSI();
    return (rssi / 2) - 160;// - BK4819_GetRxGain_dB();
}

void BK4819_ToggleGpioOut(BK4819_GPIO_PIN_t Pin, bool bSet)
{
    if (bSet)
        gBK4819_GpioOutState |=  (0x40u >> Pin);
    else
        gBK4819_GpioOutState &= ~(0x40u >> Pin);

    BK4819_WriteRegister(BK4819_REG_33, gBK4819_GpioOutState);
}


void BK4819_SetFilterBandwidth(const BK4819_FilterBandwidth_t Bandwidth, const bool weak_no_different)
{
    // REG_43
    // <15>    0 ???
    //
    // <14:12> 4 RF filter bandwidth
    //         0 = 1.7  kHz
    //         1 = 2.0  kHz
    //         2 = 2.5  kHz
    //         3 = 3.0  kHz
    //         4 = 3.75 kHz
    //         5 = 4.0  kHz
    //         6 = 4.25 kHz
    //         7 = 4.5  kHz
    // if <5> == 1, RF filter bandwidth * 2
    //
    // <11:9>  0 RF filter bandwidth when signal is weak
    //         0 = 1.7  kHz
    //         1 = 2.0  kHz
    //         2 = 2.5  kHz
    //         3 = 3.0  kHz
    //         4 = 3.75 kHz
    //         5 = 4.0  kHz
    //         6 = 4.25 kHz
    //         7 = 4.5  kHz
    // if <5> == 1, RF filter bandwidth * 2
    //
    // <8:6>   1 AFTxLPF2 filter Band Width
    //         1 = 2.5  kHz (for 12.5k channel space)
    //         2 = 2.75 kHz
    //         0 = 3.0  kHz (for 25k   channel space)
    //         3 = 3.5  kHz
    //         4 = 4.5  kHz
    //         5 = 4.25 kHz
    //         6 = 4.0  kHz
    //         7 = 3.75 kHz
    //
    // <5:4>   0 BW Mode Selection
    //         0 = 12.5k
    //         1 =  6.25k
    //         2 = 25k/20k
    //
    // <3>     1 ???
    //
    // <2>     0 Gain after FM Demodulation
    //         0 = 0dB
    //         1 = 6dB
    //
    // <1:0>   0 ???

    uint16_t val = 0;
    switch (Bandwidth)
    {
        default:
        case BK4819_FILTER_BW_WIDE: // 25kHz
            if (weak_no_different) {
                // make the RX bandwidth the same with weak signals
                val = 0x3628; // Old value 0x49a8 < v3.6
            } else {
                // with weak RX signals the RX bandwidth is reduced
                val = 0x3428; // Old value 0x45a8 < v3.6
            }
            break;

        case BK4819_FILTER_BW_NARROW:   // 12.5kHz
            if (weak_no_different) {
                // make the RX bandwidth the same with weak signals
                val = 0x3648; // Old value 0x4808 < v3.6
            } else {
                // with weak RX signals the RX bandwidth is reduced
                val = 0x3448; // Old value 0x4408 < v3.6
            }
            break;

        case BK4819_FILTER_BW_NARROWER: // 6.25kHz
            if (weak_no_different) {
                // make the RX bandwidth the same with weak signals
                val = 0x1348; // Old value 0x3658 < v3.6
            } else {
                // with weak RX signals the RX bandwidth is reduced
                val = 0x1148; // Old value 0x3658 < v3.6
            }
            break;

        /*
        case BK4819_FILTER_BW_AM:   // 8.33kHz
            if (weak_no_different) {
                // make the RX bandwidth the same with weak signals
                val = 0x4858;
            } else {
                // with weak RX signals the RX bandwidth is reduced
                val = 0x4458;
            }
            break;
        */
    }

    BK4819_WriteRegister(BK4819_REG_43, val);
}


void BK4819_SetFrequency(uint32_t Frequency)
{
    BK4819_WriteRegister(BK4819_REG_38, (Frequency >>  0) & 0xFFFF);
    BK4819_WriteRegister(BK4819_REG_39, (Frequency >> 16) & 0xFFFF);
}

void BK4819_SetupSquelch(
        uint8_t SquelchOpenRSSIThresh,
        uint8_t SquelchCloseRSSIThresh,
        uint8_t SquelchOpenNoiseThresh,
        uint8_t SquelchCloseNoiseThresh,
        uint8_t SquelchCloseGlitchThresh,
        uint8_t SquelchOpenGlitchThresh)
{
    // REG_70
    //
    // <15>   0 Enable TONE1
    //        1 = Enable
    //        0 = Disable
    //
    // <14:8> 0 TONE1 tuning gain
    //        0 ~ 127
    //
    // <7>    0 Enable TONE2
    //        1 = Enable
    //        0 = Disable
    //
    // <6:0>  0 TONE2/FSK tuning gain
    //        0 ~ 127
    //
    BK4819_WriteRegister(BK4819_REG_70, 0);

    // Glitch threshold for Squelch = close
    //
    // 0 ~ 255
    //
    BK4819_WriteRegister(BK4819_REG_4D, 0xA000 | SquelchCloseGlitchThresh);

    // REG_4E
    //
    // <15:14> 1 ???
    //
    // <13:11> 5 Squelch = open  Delay Setting
    //         0 ~ 7
    //
    // <10:9>  7 Squelch = close Delay Setting
    //         0 ~ 3
    //
    // <8>     0 ???
    //
    // <7:0>   8 Glitch threshold for Squelch = open
    //         0 ~ 255
    //
    BK4819_WriteRegister(BK4819_REG_4E,  // 01 101 11 1 00000000

        // original (*)
    (1u << 14) |                  //  1 ???
    (5u << 11) |                  // *5  squelch = open  delay .. 0 ~ 7
    (6u <<  9) |                  // *3  squelch = close delay .. 0 ~ 3
    SquelchOpenGlitchThresh);     //  0 ~ 255


    // REG_4F
    //
    // <14:8> 47 Ex-noise threshold for Squelch = close
    //        0 ~ 127
    //
    // <7>    ???
    //
    // <6:0>  46 Ex-noise threshold for Squelch = open
    //        0 ~ 127
    //
    BK4819_WriteRegister(BK4819_REG_4F, ((uint16_t)SquelchCloseNoiseThresh << 8) | SquelchOpenNoiseThresh);

    // REG_78
    //
    // <15:8> 72 RSSI threshold for Squelch = open    0.5dB/step
    //
    // <7:0>  70 RSSI threshold for Squelch = close   0.5dB/step
    //
    BK4819_WriteRegister(BK4819_REG_78, ((uint16_t)SquelchOpenRSSIThresh   << 8) | SquelchCloseRSSIThresh);

    BK4819_SetAF(BK4819_AF_MUTE);

    BK4819_RX_TurnOn();
}

void BK4819_SetAF(BK4819_AF_Type_t AF)
{
    // AF Output Inverse Mode = Inverse
    // Undocumented bits 0x2040
    //
//  BK4819_WriteRegister(BK4819_REG_47, 0x6040 | (AF << 8));
    BK4819_WriteRegister(BK4819_REG_47, (6u << 12) | (AF << 8) | (1u << 6));
}

void BK4819_SetRegValue(RegisterSpec s, uint16_t v) {
  uint16_t reg = BK4819_ReadRegister(s.num);
  reg &= ~(s.mask << s.offset);
  BK4819_WriteRegister(s.num, reg | (v << s.offset));
}

void BK4819_RX_TurnOn(void)
{
    // DSP Voltage Setting = 1
    // ANA LDO = 2.7v
    // VCO LDO = 2.7v
    // RF LDO  = 2.7v
    // PLL LDO = 2.7v
    // ANA LDO bypass
    // VCO LDO bypass
    // RF LDO  bypass
    // PLL LDO bypass
    // Reserved bit is 1 instead of 0
    // Enable  DSP
    // Enable  XTAL
    // Enable  Band Gap
    //
    BK4819_WriteRegister(BK4819_REG_37, 0x1F0F);  // 0001111100001111

    // Turn off everything
    BK4819_WriteRegister(BK4819_REG_30, 0);


    BK4819_WriteRegister(BK4819_REG_30,
        BK4819_REG_30_ENABLE_VCO_CALIB |
        BK4819_REG_30_DISABLE_UNKNOWN |
        BK4819_REG_30_ENABLE_RX_LINK |
        BK4819_REG_30_ENABLE_AF_DAC |
        BK4819_REG_30_ENABLE_DISC_MODE |
        BK4819_REG_30_ENABLE_PLL_VCO |
        BK4819_REG_30_DISABLE_PA_GAIN |
        BK4819_REG_30_DISABLE_MIC_ADC |
        BK4819_REG_30_DISABLE_TX_DSP |
        BK4819_REG_30_ENABLE_RX_DSP );
}

void BK4819_PickRXFilterPathBasedOnFrequency(uint32_t Frequency)
{
    if (Frequency < 28000000)
    {   // VHF
        BK4819_ToggleGpioOut(BK4819_GPIO4_PIN32_VHF_LNA, true);
        BK4819_ToggleGpioOut(BK4819_GPIO3_PIN31_UHF_LNA, false);
    }
    else
    if (Frequency == 0xFFFFFFFF)
    {   // OFF
        BK4819_ToggleGpioOut(BK4819_GPIO4_PIN32_VHF_LNA, false);
        BK4819_ToggleGpioOut(BK4819_GPIO3_PIN31_UHF_LNA, false);
    }
    else
    {   // UHF
        BK4819_ToggleGpioOut(BK4819_GPIO4_PIN32_VHF_LNA, false);
        BK4819_ToggleGpioOut(BK4819_GPIO3_PIN31_UHF_LNA, true);
    }
}

void BK4819_DisableScramble(void)
{
    const uint16_t Value = BK4819_ReadRegister(BK4819_REG_31);
    BK4819_WriteRegister(BK4819_REG_31, Value & ~(1u << 1));
}

bool BK4819_CompanderEnabled(void)
{
    return (BK4819_ReadRegister(BK4819_REG_31) & (1u << 3)) ? true : false;
}

void BK4819_SetCompander(const unsigned int mode)
{
    // mode 0 .. OFF
    // mode 1 .. TX
    // mode 2 .. RX
    // mode 3 .. TX and RX

    const uint16_t r31 = BK4819_ReadRegister(BK4819_REG_31);

    if (mode == 0)
    {   // disable
        BK4819_WriteRegister(BK4819_REG_31, r31 & ~(1u << 3));
        return;
    }

    // REG_29
    //
    // <15:14> 10 Compress (AF Tx) Ratio
    //         00 = Disable
    //         01 = 1.333:1
    //         10 = 2:1
    //         11 = 4:1
    //
    // <13:7>  86 Compress (AF Tx) 0 dB point (dB)
    //
    // <6:0>   64 Compress (AF Tx) noise point (dB)
    //
    const uint16_t compress_ratio    = (mode == 1 || mode >= 3) ? 2 : 0;  // 2:1
    const uint16_t compress_0dB      = 86;
    const uint16_t compress_noise_dB = 64;
//  AB40  10 1010110 1000000
    BK4819_WriteRegister(BK4819_REG_29, // (BK4819_ReadRegister(BK4819_REG_29) & ~(3u << 14)) | (compress_ratio << 14));
        (compress_ratio    << 14) |
        (compress_0dB      <<  7) |
        (compress_noise_dB <<  0));

    // REG_28
    //
    // <15:14> 01 Expander (AF Rx) Ratio
    //         00 = Disable
    //         01 = 1:2
    //         10 = 1:3
    //         11 = 1:4
    //
    // <13:7>  86 Expander (AF Rx) 0 dB point (dB)
    //
    // <6:0>   56 Expander (AF Rx) noise point (dB)
    //
    const uint16_t expand_ratio    = (mode >= 2) ? 1 : 0;   // 1:2
    const uint16_t expand_0dB      = 86;
    const uint16_t expand_noise_dB = 56;
//  6B38  01 1010110 0111000
    BK4819_WriteRegister(BK4819_REG_28, // (BK4819_ReadRegister(BK4819_REG_28) & ~(3u << 14)) | (expand_ratio << 14));
        (expand_ratio    << 14) |
        (expand_0dB      <<  7) |
        (expand_noise_dB <<  0));

    // enable
    BK4819_WriteRegister(BK4819_REG_31, r31 | (1u << 3));
}

void BK4819_DisableVox(void)
{
    const uint16_t Value = BK4819_ReadRegister(BK4819_REG_31);
    BK4819_WriteRegister(BK4819_REG_31, Value & 0xFFFB);
}

void BK4819_Sleep(void)
{
    BK4819_WriteRegister(BK4819_REG_30, 0);
    BK4819_WriteRegister(BK4819_REG_37, 0x1D00);
}


void BK4819_Idle(void)
{
    BK4819_WriteRegister(BK4819_REG_30, 0x0000);
}


uint16_t BK4819_GetRSSI(void)
{
    return BK4819_ReadRegister(BK4819_REG_67) & 0x01FF;
}

uint8_t  BK4819_GetGlitchIndicator(void)
{
    return BK4819_ReadRegister(BK4819_REG_63) & 0x00FF;
}

uint8_t  BK4819_GetExNoiceIndicator(void)
{
    return BK4819_ReadRegister(BK4819_REG_65) & 0x007F;
}


// ---- transmit helpers (upstream, unchanged) ----

void BK4819_SetupPowerAmplifier(const uint8_t bias, const uint32_t frequency)
{
    // REG_36 <15:8> 0 PA Bias output 0 ~ 3.2V
    //               255 = 3.2V
    //                 0 = 0V
    //
    // REG_36 <7>    0
    //               1 = Enable PA-CTL output
    //               0 = Disable (Output 0 V)
    //
    // REG_36 <5:3>  7 PA gain 1 tuning
    //               7 = max
    //               0 = min
    //
    // REG_36 <2:0>  7 PA gain 2 tuning
    //               7 = max
    //               0 = min
    //
    //                                  280MHz       g1=1  g2=0 (-14.9dBm),  g1=4  g2=2 (0.13dBm)
    const uint8_t gain   = (frequency < 28000000) ? (1u << 3) | (0u << 0) : (4u << 3) | (2u << 0);
    const uint8_t enable = 1;
    BK4819_WriteRegister(BK4819_REG_36, (bias << 8) | (enable << 7) | (gain << 0));
}

void BK4819_ExitBypass(void)
{
    BK4819_SetAF(BK4819_AF_MUTE);

    // REG_7E
    //
    // <15>    0 AGC fix mode
    //         1 = fix
    //         0 = auto
    //
    // <14:12> 3 AGC fix index
    //         3 ( 3) = max
    //         2 ( 2)
    //         1 ( 1)
    //         0 ( 0)
    //         7 (-1)
    //         6 (-2)
    //         5 (-3)
    //         4 (-4) = min
    //
    // <11:6>  0 ???
    //
    // <5:3>   5 DC filter band width for Tx (MIC In)
    //         0 ~ 7
    //         0 = bypass DC filter
    //
    // <2:0>   6 DC filter band width for Rx (I.F In)
    //         0 ~ 7
    //         0 = bypass DC filter
    //

    uint16_t regVal = BK4819_ReadRegister(BK4819_REG_7E);

    // 0x302E / 0 011 000000 101 110
    BK4819_WriteRegister(BK4819_REG_7E, (regVal & ~(0b111 << 3))

        | (5u <<  3)       // 5  DC Filter band width for Tx (MIC In)

    );
}

void BK4819_EnterTxMute(void)
{
    BK4819_WriteRegister(BK4819_REG_50, 0xBB20);
}

void BK4819_ExitTxMute(void)
{
    BK4819_WriteRegister(BK4819_REG_50, 0x3B20);
}

void BK4819_TxOn_Beep(void)
{
    BK4819_WriteRegister(BK4819_REG_37, 0x1D0F);
    BK4819_WriteRegister(BK4819_REG_52, 0x028F);
    BK4819_WriteRegister(BK4819_REG_30, 0x0000);
    BK4819_WriteRegister(BK4819_REG_30, 0xC1FE);
}

void BK4819_ExitSubAu(void)
{
    // REG_51
    //
    // <15>  0
    //       1 = Enable TxCTCSS/CDCSS
    //       0 = Disable
    //
    // <14>  0
    //       1 = GPIO0Input for CDCSS
    //       0 = Normal Mode (for BK4819 v3)
    //
    // <13>  0
    //       1 = Transmit negative CDCSS code
    //       0 = Transmit positive CDCSS code
    //
    // <12>  0 CTCSS/CDCSS mode selection
    //       1 = CTCSS
    //       0 = CDCSS
    //
    // <11>  0 CDCSS 24/23bit selection
    //       1 = 24bit
    //       0 = 23bit
    //
    // <10>  0 1050HzDetectionMode
    //       1 = 1050/4 Detect Enable, CTC1 should be set to 1050/4 Hz
    //
    // <9>   0 Auto CDCSS Bw Mode
    //       1 = Disable
    //       0 = Enable
    //
    // <8>   0 Auto CTCSS Bw Mode
    //       0 = Enable
    //       1 = Disable
    //
    // <6:0> 0 CTCSS/CDCSS Tx Gain1 Tuning
    //       0   = min
    //       127 = max
    //
    BK4819_WriteRegister(BK4819_REG_51, 0x0000);
}

void BK4819_EnableTXLink(void)
{
    BK4819_WriteRegister(BK4819_REG_30,
        BK4819_REG_30_ENABLE_VCO_CALIB |
        BK4819_REG_30_ENABLE_UNKNOWN   |
        BK4819_REG_30_DISABLE_RX_LINK  |
        BK4819_REG_30_ENABLE_AF_DAC    |
        BK4819_REG_30_ENABLE_DISC_MODE |
        BK4819_REG_30_ENABLE_PLL_VCO   |
        BK4819_REG_30_ENABLE_PA_GAIN   |
        BK4819_REG_30_DISABLE_MIC_ADC  |
        BK4819_REG_30_ENABLE_TX_DSP    |
        BK4819_REG_30_DISABLE_RX_DSP);
}

