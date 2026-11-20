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

#ifndef DRIVER_ST7565_H
#define DRIVER_ST7565_H

#include <stdint.h>

#define LCD_WIDTH  128
#define LCD_HEIGHT 64
#define LCD_PAGES  8   // each page is 8 pixel rows, LSB at the top

extern uint8_t gFrameBuffer[LCD_PAGES][LCD_WIDTH];

void ST7565_Init(void);
void ST7565_SetContrast(uint8_t contrast);
void ST7565_Blit(void);           // push gFrameBuffer to the panel
void ST7565_Fill(uint8_t value);  // fill the panel directly (bypasses gFrameBuffer)

#endif
