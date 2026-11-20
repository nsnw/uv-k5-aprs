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

// ST7565 128x64 LCD over SPI0.
// Modified for APRSK5: single 8-page framebuffer, F4HWN options removed.

#include <stddef.h>
#include <stdint.h>

#include "bsp/dp32g030/gpio.h"
#include "bsp/dp32g030/spi.h"
#include "driver/gpio.h"
#include "driver/spi.h"
#include "driver/st7565.h"
#include "driver/system.h"

uint8_t gFrameBuffer[LCD_PAGES][LCD_WIDTH];

static const uint8_t CMD_SOFTWARE_RESET   = 0xE2;
static const uint8_t CMD_BIAS_SELECT      = 0xA2; // | BS     0 = 1/9, 1 = 1/7
static const uint8_t CMD_COM_DIRECTION    = 0xC0; // | MY<<3  reverse COM
static const uint8_t CMD_SEG_DIRECTION    = 0xA0; // | MX     reverse SEG
static const uint8_t CMD_INVERSE_DISPLAY  = 0xA6; // | INV
static const uint8_t CMD_ALL_PIXEL_ON     = 0xA4; // | AP
static const uint8_t CMD_REGULATION_RATIO = 0x20; // | RR
static const uint8_t CMD_SET_EV           = 0x81; // followed by contrast 0..63
static const uint8_t CMD_POWER_CIRCUIT    = 0x28; // | VB<<2 | VR<<1 | VF
static const uint8_t CMD_SET_START_LINE   = 0x40; // | line 0..63
static const uint8_t CMD_DISPLAY_ON_OFF   = 0xAE; // | D

static void WaitTxNotFull(void)
{
    while ((SPI0->FIFOST & SPI_FIFOST_TFF_MASK) != SPI_FIFOST_TFF_BITS_NOT_FULL) {}
}

static void WriteCommand(uint8_t value)
{
    GPIO_ClearBit(&GPIOB->DATA, GPIOB_PIN_ST7565_A0);
    WaitTxNotFull();
    SPI0->WDR = value;
}

static void SelectColumnAndPage(uint8_t column, uint8_t page)
{
    WriteCommand(page + 176);
    WriteCommand(((column >> 4) & 0x0F) | 0x10);
    WriteCommand(((column >> 0) & 0x0F));
    SPI_WaitForUndocumentedTxFifoStatusBit();
}

// Writes one 128-column page; if data is NULL every column gets `fill`.
static void WritePage(uint8_t page, const uint8_t *data, uint8_t fill)
{
    SelectColumnAndPage(4, page); // panel RAM is offset by 4 columns
    GPIO_SetBit(&GPIOB->DATA, GPIOB_PIN_ST7565_A0);
    for (unsigned i = 0; i < LCD_WIDTH; i++) {
        WaitTxNotFull();
        SPI0->WDR = data ? data[i] : fill;
    }
    SPI_WaitForUndocumentedTxFifoStatusBit();
}

static void HardwareReset(void)
{
    GPIO_SetBit(&GPIOB->DATA, GPIOB_PIN_ST7565_RES);
    SYSTEM_DelayMs(1);
    GPIO_ClearBit(&GPIOB->DATA, GPIOB_PIN_ST7565_RES);
    SYSTEM_DelayMs(20);
    GPIO_SetBit(&GPIOB->DATA, GPIOB_PIN_ST7565_RES);
    SYSTEM_DelayMs(120);
}

void ST7565_Init(void)
{
    static const uint8_t init_cmds[] = {
        CMD_BIAS_SELECT | 0,
        CMD_COM_DIRECTION | (0 << 3),
        CMD_SEG_DIRECTION | 1,
        CMD_INVERSE_DISPLAY | 0,
        CMD_ALL_PIXEL_ON | 0,
        CMD_REGULATION_RATIO | (4 << 0),
        CMD_SET_EV,
        31,
    };

    SPI0_Init();
    HardwareReset();
    SPI_ToggleMasterMode(&SPI0->CR, false);
    WriteCommand(CMD_SOFTWARE_RESET);
    SYSTEM_DelayMs(120);

    for (unsigned i = 0; i < sizeof(init_cmds); i++)
        WriteCommand(init_cmds[i]);

    // power-up sequence for the built-in booster/regulator/follower
    WriteCommand(CMD_POWER_CIRCUIT | 0b011);
    SYSTEM_DelayMs(1);
    WriteCommand(CMD_POWER_CIRCUIT | 0b110);
    SYSTEM_DelayMs(1);
    for (unsigned i = 0; i < 4; i++)
        WriteCommand(CMD_POWER_CIRCUIT | 0b111);
    SYSTEM_DelayMs(40);

    WriteCommand(CMD_SET_START_LINE | 0);
    WriteCommand(CMD_DISPLAY_ON_OFF | 1);
    SPI_WaitForUndocumentedTxFifoStatusBit();
    SPI_ToggleMasterMode(&SPI0->CR, true);

    ST7565_Fill(0x00);
}

void ST7565_SetContrast(uint8_t contrast)
{
    SPI_ToggleMasterMode(&SPI0->CR, false);
    WriteCommand(CMD_SET_EV);
    WriteCommand(contrast & 0x3F);
    SPI_WaitForUndocumentedTxFifoStatusBit();
    SPI_ToggleMasterMode(&SPI0->CR, true);
}

void ST7565_Blit(void)
{
    SPI_ToggleMasterMode(&SPI0->CR, false);
    WriteCommand(CMD_SET_START_LINE | 0);
    for (uint8_t page = 0; page < LCD_PAGES; page++)
        WritePage(page, gFrameBuffer[page], 0);
    SPI_ToggleMasterMode(&SPI0->CR, true);
}

void ST7565_Fill(uint8_t value)
{
    SPI_ToggleMasterMode(&SPI0->CR, false);
    for (uint8_t page = 0; page < LCD_PAGES; page++)
        WritePage(page, NULL, value);
    SPI_ToggleMasterMode(&SPI0->CR, true);
}
