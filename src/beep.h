#ifndef BEEP_H
#define BEEP_H

#include <stdbool.h>
#include <stdint.h>

// Alert tones through the speaker, from the BK4819's tone-1 generator (as
// upstream's key beeps, with AUDIO_PlayBeep()'s settling delays). Volume
// follows the knob.
//
// The BK4819 can't receive while it plays a tone, so a beep waits until the
// channel has been quiet for BEEP_QUIET_TICKS (a digipeated copy usually
// follows within a second), or BEEP_MAX_WAIT_TICKS at most. It doesn't
// block: keys, the screen and the cable keep working while it plays, but
// the caller must leave the BK4819 alone (no RADIO_Poll, no transmitting)
// while BEEP_IsPlaying(). Message ~420 ms, sent ~180 ms, received ~150 ms.

#define BEEP_GAIN 96               // tone-1 gain, 0..127 (upstream PlayTone: 28 quiet / 96 loud)
#define BEEP_QUIET_TICKS    100    // 1 s
#define BEEP_MAX_WAIT_TICKS 500    // 5 s

typedef enum {                     // in priority order: one beep is pending at most
    BEEP_NONE = 0,
    BEEP_RECEIVED,                 // a packet came in (station.h rx_beep): one short low tone
    BEEP_SENT,                     // we transmitted (beacon, message, ack): one short high tone
    BEEP_MESSAGE,                  // new message: two rising tones
} BeepKind;

void BEEP_Request(BeepKind kind);  // replaces a pending beep of lower priority
// Every 10 ms, with whether the channel is quiet (no carrier, nothing being
// received, nothing waiting to be sent). True while a beep is playing.
bool BEEP_Poll10ms(bool channel_quiet);
bool BEEP_IsPlaying(void);

#endif
