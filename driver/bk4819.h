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

#ifndef DRIVER_BK4819_h
#define DRIVER_BK4819_h

#include <stdbool.h>
#include <stdint.h>

#include "driver/bk4819-regs.h"

// Frequencies passed to this driver are in units of 10 Hz.
// The chip tunes ~18-630 MHz and ~840-1300 MHz; it does not work in between.

enum BK4819_AF_Type_t
{
    BK4819_AF_MUTE      =  0u,
    BK4819_AF_FM        =  1u,
    BK4819_AF_ALAM      =  2u,
    BK4819_AF_BEEP      =  3u,
    BK4819_AF_BASEBAND1 =  4u,  // RAW
    BK4819_AF_BASEBAND2 =  5u,  // USB
    BK4819_AF_CTCO      =  6u,
    BK4819_AF_AM        =  7u,
    BK4819_AF_FSKO      =  8u,
    BK4819_AF_UNKNOWN3  =  9u,  // BYP
};
typedef enum BK4819_AF_Type_t BK4819_AF_Type_t;

enum BK4819_FilterBandwidth_t
{
    BK4819_FILTER_BW_WIDE = 0,  // 25 kHz
    BK4819_FILTER_BW_NARROW,    // 12.5 kHz
    BK4819_FILTER_BW_NARROWER   // 6.25 kHz
};
typedef enum BK4819_FilterBandwidth_t BK4819_FilterBandwidth_t;

void     BK4819_Init(void);
uint16_t BK4819_ReadRegister(BK4819_REGISTER_t Register);
void     BK4819_WriteRegister(BK4819_REGISTER_t Register, uint16_t Data);
void     BK4819_SetRegValue(RegisterSpec s, uint16_t v);
void     BK4819_WriteU8(uint8_t Data);
void     BK4819_WriteU16(uint16_t Data);

void     BK4819_SetAGC(bool enable);
void     BK4819_InitAGC(bool amModulation);
int8_t   BK4819_GetRxGain_dB(void);

void     BK4819_ToggleGpioOut(BK4819_GPIO_PIN_t Pin, bool bSet);

void     BK4819_SetFilterBandwidth(const BK4819_FilterBandwidth_t Bandwidth, const bool weak_no_different);
void     BK4819_SetFrequency(uint32_t Frequency);
void     BK4819_SetupSquelch(
            uint8_t SquelchOpenRSSIThresh,
            uint8_t SquelchCloseRSSIThresh,
            uint8_t SquelchOpenNoiseThresh,
            uint8_t SquelchCloseNoiseThresh,
            uint8_t SquelchCloseGlitchThresh,
            uint8_t SquelchOpenGlitchThresh);
void     BK4819_SetAF(BK4819_AF_Type_t AF);
void     BK4819_RX_TurnOn(void);
void     BK4819_PickRXFilterPathBasedOnFrequency(uint32_t Frequency);

void     BK4819_DisableScramble(void);
bool     BK4819_CompanderEnabled(void);
void     BK4819_SetCompander(const unsigned int mode);
void     BK4819_DisableVox(void);

// transmit
void     BK4819_SetupPowerAmplifier(const uint8_t bias, const uint32_t frequency);
void     BK4819_ExitBypass(void);
void     BK4819_EnterTxMute(void);
void     BK4819_ExitTxMute(void);
void     BK4819_TxOn_Beep(void);
void     BK4819_ExitSubAu(void);
void     BK4819_EnableTXLink(void);

void     BK4819_Sleep(void);
void     BK4819_Idle(void);

uint16_t BK4819_GetRSSI(void);        // raw, 0.5 dB/step
int16_t  BK4819_GetRSSI_dBm(void);
uint8_t  BK4819_GetGlitchIndicator(void);
uint8_t  BK4819_GetExNoiceIndicator(void);

#endif
