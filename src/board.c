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

// Pin muxing, GPIO directions and backlight PWM for the UV-K5 board.
// Register sequence taken from upstream board.c / backlight.c with FM
// radio (BK1080), SWD, ADC and settings code removed.

#include "board.h"

#include "bsp/dp32g030/gpio.h"
#include "bsp/dp32g030/portcon.h"
#include "bsp/dp32g030/pwmplus.h"
#include "bsp/dp32g030/saradc.h"
#include "bsp/dp32g030/syscon.h"
#include "driver/adc.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/st7565.h"

static void PortconInit(void)
{
    // ---- Port A: keypad (A3-A6 in, A10-A13 shared with I2C/voice), UART1 ----
    PORTCON_PORTA_SEL0 &= ~(PORTCON_PORTA_SEL0_A3_MASK | PORTCON_PORTA_SEL0_A4_MASK |
                            PORTCON_PORTA_SEL0_A5_MASK | PORTCON_PORTA_SEL0_A6_MASK);
    PORTCON_PORTA_SEL0 |= PORTCON_PORTA_SEL0_A3_BITS_GPIOA3 | PORTCON_PORTA_SEL0_A4_BITS_GPIOA4 |
                          PORTCON_PORTA_SEL0_A5_BITS_GPIOA5 | PORTCON_PORTA_SEL0_A6_BITS_GPIOA6 |
                          PORTCON_PORTA_SEL0_A7_BITS_UART1_TX;

    PORTCON_PORTA_SEL1 &= ~(PORTCON_PORTA_SEL1_A10_MASK | PORTCON_PORTA_SEL1_A11_MASK |
                            PORTCON_PORTA_SEL1_A12_MASK | PORTCON_PORTA_SEL1_A13_MASK);
    PORTCON_PORTA_SEL1 |= PORTCON_PORTA_SEL1_A8_BITS_UART1_RX |
                          PORTCON_PORTA_SEL1_A9_BITS_SARADC_CH4 |   // battery voltage
                          PORTCON_PORTA_SEL1_A10_BITS_GPIOA10 | PORTCON_PORTA_SEL1_A11_BITS_GPIOA11 |
                          PORTCON_PORTA_SEL1_A12_BITS_GPIOA12 | PORTCON_PORTA_SEL1_A13_BITS_GPIOA13 |
                          PORTCON_PORTA_SEL1_A14_BITS_SARADC_CH9;   // battery current

    PORTCON_PORTA_IE |= PORTCON_PORTA_IE_A3_BITS_ENABLE | PORTCON_PORTA_IE_A4_BITS_ENABLE |
                        PORTCON_PORTA_IE_A5_BITS_ENABLE | PORTCON_PORTA_IE_A6_BITS_ENABLE |
                        PORTCON_PORTA_IE_A8_BITS_ENABLE;
    PORTCON_PORTA_IE &= ~(PORTCON_PORTA_IE_A10_MASK | PORTCON_PORTA_IE_A11_MASK |
                          PORTCON_PORTA_IE_A12_MASK | PORTCON_PORTA_IE_A13_MASK);

    PORTCON_PORTA_PU |= PORTCON_PORTA_PU_A3_BITS_ENABLE | PORTCON_PORTA_PU_A4_BITS_ENABLE |
                        PORTCON_PORTA_PU_A5_BITS_ENABLE | PORTCON_PORTA_PU_A6_BITS_ENABLE;
    PORTCON_PORTA_PU &= ~(PORTCON_PORTA_PU_A10_MASK | PORTCON_PORTA_PU_A11_MASK |
                          PORTCON_PORTA_PU_A12_MASK | PORTCON_PORTA_PU_A13_MASK);

    PORTCON_PORTA_PD &= ~(PORTCON_PORTA_PD_A3_MASK | PORTCON_PORTA_PD_A4_MASK |
                          PORTCON_PORTA_PD_A5_MASK | PORTCON_PORTA_PD_A6_MASK |
                          PORTCON_PORTA_PD_A10_MASK | PORTCON_PORTA_PD_A11_MASK |
                          PORTCON_PORTA_PD_A12_MASK | PORTCON_PORTA_PD_A13_MASK);

    PORTCON_PORTA_OD |= PORTCON_PORTA_OD_A3_BITS_ENABLE | PORTCON_PORTA_OD_A4_BITS_ENABLE |
                        PORTCON_PORTA_OD_A5_BITS_ENABLE | PORTCON_PORTA_OD_A6_BITS_ENABLE;
    PORTCON_PORTA_OD &= ~(PORTCON_PORTA_OD_A10_MASK | PORTCON_PORTA_OD_A11_MASK |
                          PORTCON_PORTA_OD_A12_MASK | PORTCON_PORTA_OD_A13_MASK);

    // ---- Port B: SPI0 + ST7565 control lines, backlight, BK1080 enable ----
    PORTCON_PORTB_SEL0 &= ~PORTCON_PORTB_SEL0_B7_MASK;
    PORTCON_PORTB_SEL0 |= PORTCON_PORTB_SEL0_B7_BITS_SPI0_SSN;

    PORTCON_PORTB_SEL1 &= ~(PORTCON_PORTB_SEL1_B9_MASK | PORTCON_PORTB_SEL1_B11_MASK |
                            PORTCON_PORTB_SEL1_B14_MASK | PORTCON_PORTB_SEL1_B15_MASK);
    PORTCON_PORTB_SEL1 |= PORTCON_PORTB_SEL1_B8_BITS_SPI0_CLK |
                          PORTCON_PORTB_SEL1_B9_BITS_GPIOB9 |       // ST7565 A0
                          PORTCON_PORTB_SEL1_B10_BITS_SPI0_MOSI |
                          PORTCON_PORTB_SEL1_B11_BITS_GPIOB11;      // ST7565 reset (shared with SWDIO)

    PORTCON_PORTB_IE |= PORTCON_PORTB_IE_B14_BITS_ENABLE;
    PORTCON_PORTB_IE &= ~(PORTCON_PORTB_IE_B6_MASK | PORTCON_PORTB_IE_B7_MASK |
                          PORTCON_PORTB_IE_B8_MASK | PORTCON_PORTB_IE_B9_MASK |
                          PORTCON_PORTB_IE_B10_MASK | PORTCON_PORTB_IE_B11_MASK |
                          PORTCON_PORTB_IE_B15_MASK);

    PORTCON_PORTB_PU &= ~(PORTCON_PORTB_PU_B6_MASK | PORTCON_PORTB_PU_B9_MASK |
                          PORTCON_PORTB_PU_B11_MASK | PORTCON_PORTB_PU_B14_MASK |
                          PORTCON_PORTB_PU_B15_MASK);
    PORTCON_PORTB_PD &= ~(PORTCON_PORTB_PD_B6_MASK | PORTCON_PORTB_PD_B9_MASK |
                          PORTCON_PORTB_PD_B11_MASK | PORTCON_PORTB_PD_B14_MASK |
                          PORTCON_PORTB_PD_B15_MASK);
    PORTCON_PORTB_OD &= ~(PORTCON_PORTB_OD_B6_MASK | PORTCON_PORTB_OD_B9_MASK |
                          PORTCON_PORTB_OD_B11_MASK | PORTCON_PORTB_OD_B15_MASK);
    PORTCON_PORTB_OD |= PORTCON_PORTB_OD_B14_BITS_ENABLE;

    // ---- Port C: BK4819 3-wire bus, flashlight, audio amp enable, PTT ----
    PORTCON_PORTC_SEL0 &= ~(PORTCON_PORTC_SEL0_C0_MASK | PORTCON_PORTC_SEL0_C1_MASK |
                            PORTCON_PORTC_SEL0_C2_MASK | PORTCON_PORTC_SEL0_C3_MASK |
                            PORTCON_PORTC_SEL0_C4_MASK | PORTCON_PORTC_SEL0_C5_MASK);

    PORTCON_PORTC_IE |= PORTCON_PORTC_IE_C5_BITS_ENABLE;
    PORTCON_PORTC_IE &= ~(PORTCON_PORTC_IE_C0_MASK | PORTCON_PORTC_IE_C1_MASK |
                          PORTCON_PORTC_IE_C2_MASK | PORTCON_PORTC_IE_C3_MASK |
                          PORTCON_PORTC_IE_C4_MASK);

    PORTCON_PORTC_PU |= PORTCON_PORTC_PU_C5_BITS_ENABLE;
    PORTCON_PORTC_PU &= ~(PORTCON_PORTC_PU_C0_MASK | PORTCON_PORTC_PU_C1_MASK |
                          PORTCON_PORTC_PU_C2_MASK | PORTCON_PORTC_PU_C3_MASK |
                          PORTCON_PORTC_PU_C4_MASK);

    PORTCON_PORTC_PD &= ~(PORTCON_PORTC_PD_C0_MASK | PORTCON_PORTC_PD_C1_MASK |
                          PORTCON_PORTC_PD_C2_MASK | PORTCON_PORTC_PD_C3_MASK |
                          PORTCON_PORTC_PD_C4_MASK | PORTCON_PORTC_PD_C5_MASK);

    PORTCON_PORTC_OD &= ~(PORTCON_PORTC_OD_C0_MASK | PORTCON_PORTC_OD_C1_MASK |
                          PORTCON_PORTC_OD_C2_MASK | PORTCON_PORTC_OD_C3_MASK |
                          PORTCON_PORTC_OD_C4_MASK);
    PORTCON_PORTC_OD |= PORTCON_PORTC_OD_C0_BITS_DISABLE | PORTCON_PORTC_OD_C1_BITS_DISABLE |
                        PORTCON_PORTC_OD_C2_BITS_DISABLE | PORTCON_PORTC_OD_C3_BITS_DISABLE |
                        PORTCON_PORTC_OD_C4_BITS_DISABLE | PORTCON_PORTC_OD_C5_BITS_ENABLE;
}

static void GpioInit(void)
{
    // A7/A8 (UART1) directions are left as the bootloader set them.
    GPIOA->DIR |= GPIO_DIR_10_BITS_OUTPUT | GPIO_DIR_11_BITS_OUTPUT |
                  GPIO_DIR_12_BITS_OUTPUT | GPIO_DIR_13_BITS_OUTPUT;
    GPIOA->DIR &= ~(GPIO_DIR_3_MASK | GPIO_DIR_4_MASK | GPIO_DIR_5_MASK | GPIO_DIR_6_MASK);

    GPIOB->DIR |= GPIO_DIR_9_BITS_OUTPUT | GPIO_DIR_11_BITS_OUTPUT | GPIO_DIR_15_BITS_OUTPUT;

    GPIOC->DIR |= GPIO_DIR_0_BITS_OUTPUT | GPIO_DIR_1_BITS_OUTPUT | GPIO_DIR_2_BITS_OUTPUT |
                  GPIO_DIR_3_BITS_OUTPUT | GPIO_DIR_4_BITS_OUTPUT;
    GPIOC->DIR &= ~GPIO_DIR_5_MASK; // PTT input
}

static void BacklightInit(void)
{
    const uint32_t PWM_FREQUENCY_HZ = 25000;
    PWM_PLUS0_CLKSRC |= ((48000000 / 1024 / PWM_FREQUENCY_HZ) << 16);
    PWM_PLUS0_PERIOD = 1023;

    PORTCON_PORTB_SEL0 &= ~PORTCON_PORTB_SEL0_B6_MASK;
    PORTCON_PORTB_SEL0 |= PORTCON_PORTB_SEL0_B6_BITS_PWMP0_CH0;

    PWM_PLUS0_GEN = PWMPLUS_GEN_CH0_OE_BITS_ENABLE | PWMPLUS_GEN_CH0_OUTINV_BITS_ENABLE;
    PWM_PLUS0_CFG = PWMPLUS_CFG_CNT_REP_BITS_ENABLE | PWMPLUS_CFG_COUNTER_EN_BITS_ENABLE;
}

// SARADC: channel 4 = battery voltage (A9), channel 9 = battery current (A14).
// Configuration as upstream BOARD_ADC_Init().
static void AdcInit(void)
{
    ADC_Config_t Config;
    Config.CLK_SEL            = SYSCON_CLK_SEL_W_SARADC_SMPL_VALUE_DIV2;
    Config.CH_SEL             = ADC_CH4 | ADC_CH9;
    Config.AVG                = SARADC_CFG_AVG_VALUE_8_SAMPLE;
    Config.CONT               = SARADC_CFG_CONT_VALUE_SINGLE;
    Config.MEM_MODE           = SARADC_CFG_MEM_MODE_VALUE_CHANNEL;
    Config.SMPL_CLK           = SARADC_CFG_SMPL_CLK_VALUE_INTERNAL;
    Config.SMPL_WIN           = SARADC_CFG_SMPL_WIN_VALUE_15_CYCLE;
    Config.SMPL_SETUP         = SARADC_CFG_SMPL_SETUP_VALUE_1_CYCLE;
    Config.ADC_TRIG           = SARADC_CFG_ADC_TRIG_VALUE_CPU;
    Config.CALIB_KD_VALID     = SARADC_CALIB_KD_VALID_VALUE_YES;
    Config.CALIB_OFFSET_VALID = SARADC_CALIB_OFFSET_VALID_VALUE_YES;
    Config.DMA_EN             = SARADC_CFG_DMA_EN_VALUE_DISABLE;
    Config.IE_CHx_EOC         = SARADC_IE_CHx_EOC_VALUE_NONE;
    Config.IE_FIFO_FULL       = SARADC_IE_FIFO_FULL_VALUE_DISABLE;
    Config.IE_FIFO_HFULL      = SARADC_IE_FIFO_HFULL_VALUE_DISABLE;
    ADC_Configure(&Config);
    ADC_Enable();
    ADC_SoftReset();
}

uint16_t BOARD_ReadBatteryRaw(void)
{
    ADC_Start();
    while (!ADC_CheckEndOfConversion(ADC_CH9)) {}
    (void)ADC_GetValue(ADC_CH9);   // battery current, unused
    return ADC_GetValue(ADC_CH4);
}

void BOARD_Init(void)
{
    SYSCON_DEV_CLK_GATE = 0
        | SYSCON_DEV_CLK_GATE_GPIOA_BITS_ENABLE
        | SYSCON_DEV_CLK_GATE_GPIOB_BITS_ENABLE
        | SYSCON_DEV_CLK_GATE_GPIOC_BITS_ENABLE
        | SYSCON_DEV_CLK_GATE_UART1_BITS_ENABLE
        | SYSCON_DEV_CLK_GATE_SPI0_BITS_ENABLE
        | SYSCON_DEV_CLK_GATE_SARADC_BITS_ENABLE
        | SYSCON_DEV_CLK_GATE_PWM_PLUS0_BITS_ENABLE;

    PortconInit();
    GpioInit();
    I2C_Recover();      // in case the EEPROM was left mid-transfer by a reset
    BacklightInit();
    AdcInit();
    ST7565_Init();
}

void BOARD_SetBacklight(uint8_t level)
{
    PWM_PLUS0_CH0_COMP = (uint32_t)level * 4;
}

void BOARD_SetAudioAmp(bool on)
{
    if (on)
        GPIO_SetBit(&GPIOC->DATA, GPIOC_PIN_AUDIO_PATH);
    else
        GPIO_ClearBit(&GPIOC->DATA, GPIOC_PIN_AUDIO_PATH);
}

bool BOARD_IsPttPressed(void)
{
    return !GPIO_CheckBit(&GPIOC->DATA, GPIOC_PIN_PTT);
}
