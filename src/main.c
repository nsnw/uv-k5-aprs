#include <stdbool.h>
#include <stdint.h>

#include "app.h"
#include "battery.h"
#include "beacon.h"
#include "board.h"
#include "driver/systick.h"
#include "driver/uart.h"
#include "radio.h"
#include "settings.h"
#include "memory.h"
#include "station.h"
#include "version.h"

static volatile uint32_t sTicks;   // 10 ms ticks since boot

void SystickHandler(void);
void SystickHandler(void)          // vector table entry, every 10 ms
{
    sTicks++;
}

void Main(void);
void Main(void)
{
    SYSTICK_Init();     // 10 ms interrupt, also used for busy-wait delays
    BOARD_Init();
    UART_Init();
    UART_Print(APP_NAME " " VERSION_STRING " (" BUILD_ID ")\r\n");

    SETTINGS_Load();
    BATTERY_Init();
    BEACON_Load();
    STATION_Load();
    MEM_Load();
    RADIO_Init();
    APP_Init();

    uint32_t last = sTicks;
    for (;;) {
        // run once per elapsed tick; sleep until the next interrupt otherwise
        if (sTicks == last) {
            __asm volatile("wfi");
            continue;
        }
        last++;
        APP_Tick10ms();
    }
}
