// Host test for beep.c: beeps wait for a quiet channel (or the maximum
// wait), the highest-priority request wins, the BK4819 is handed back
// (RADIO_Apply) when a beep ends, and the tone is on for the right time.
#include <stdio.h>
#include <string.h>

#include "beep.h"
#include "board.h"
#include "driver/bk4819.h"
#include "driver/system.h"
#include "radio.h"

static int sFails, sPasses;
#define CHECK(c) do { if (c) sPasses++; else { sFails++; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

static int sApply, sAmp, sToneOn, sToneTicks, sTones;
static uint16_t sToneWord;

void BK4819_WriteRegister(BK4819_REGISTER_t r, uint16_t v) { if (r == BK4819_REG_71) sToneWord = v; }
void BK4819_EnterTxMute(void) { sToneOn = 0; }
void BK4819_ExitTxMute(void)  { sToneOn = 1; sTones++; }
void BK4819_SetAF(BK4819_AF_Type_t af) { (void)af; }
void BOARD_SetAudioAmp(bool on) { sAmp = on; }
void SYSTEM_DelayMs(uint32_t ms) { (void)ms; }
void RADIO_Apply(void) { sApply++; }

// Run n ticks; returns how many of them were playing.
static int Run(int n, bool quiet)
{
    int playing = 0;
    for (int i = 0; i < n; i++) {
        if (BEEP_Poll10ms(quiet))
            playing++;
        if (sToneOn)
            sToneTicks++;
    }
    return playing;
}

static void Reset(void) { sApply = sTones = sToneTicks = 0; }

int main(void)
{
    // nothing requested: nothing happens
    CHECK(Run(1000, true) == 0 && sApply == 0);

    // waits for a quiet second
    Reset();
    BEEP_Request(BEEP_RECEIVED);
    Run(BEEP_QUIET_TICKS - 1 + 200, false);           // busy channel: still waiting
    CHECK(!BEEP_IsPlaying() && sTones == 0);
    Run(BEEP_QUIET_TICKS - 1, true);
    CHECK(!BEEP_IsPlaying());
    Run(1, true);                                     // quiet for a full second now
    CHECK(BEEP_IsPlaying() && sAmp);
    int left = Run(100, true);
    CHECK(left == 6 + 7 + 2 - 1);                     // 150 ms in all
    CHECK(sTones == 1 && sToneTicks == 7);            // 70 ms of tone
    CHECK(!BEEP_IsPlaying() && sApply == 1 && !sAmp);

    // gives up waiting after the maximum, even on a busy channel
    Reset();
    Run(BEEP_QUIET_TICKS, true);
    BEEP_Request(BEEP_SENT);
    Run(BEEP_MAX_WAIT_TICKS - 1, false);
    CHECK(!BEEP_IsPlaying());
    Run(1, false);
    CHECK(BEEP_IsPlaying());
    Run(100, false);
    CHECK(sApply == 1 && sTones == 1 && sToneTicks == 10);

    // a message beep replaces a pending lower one; a lower one doesn't replace it
    Reset();
    BEEP_Request(BEEP_RECEIVED);
    BEEP_Request(BEEP_MESSAGE);
    BEEP_Request(BEEP_SENT);
    Run(1000, true);
    CHECK(sApply == 1 && sTones == 2 && sToneTicks == 24);   // two 120 ms tones, played once

    // the quiet count restarts after a beep (receive was off)
    Reset();
    Run(1, false);
    BEEP_Request(BEEP_RECEIVED);
    Run(BEEP_QUIET_TICKS, true);
    CHECK(BEEP_IsPlaying());
    Run(15, true);                                    // 14 more playing ticks, then it ends
    CHECK(!BEEP_IsPlaying());
    BEEP_Request(BEEP_RECEIVED);
    Run(BEEP_QUIET_TICKS - 1, true);
    CHECK(!BEEP_IsPlaying());
    Run(1, true);
    CHECK(BEEP_IsPlaying());
    Run(100, true);

    printf("%d passed, %d failed\n", sPasses, sFails);
    return sFails != 0;
}
