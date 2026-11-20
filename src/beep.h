#ifndef BEEP_H
#define BEEP_H

// Alert tones through the speaker, from the BK4819's tone-1 generator (as
// upstream's key beeps, with AUDIO_PlayBeep()'s settling delays: the
// amplifier pops when it switches, so the tone starts 60 ms after it comes
// on and ends 20 ms before it goes off). Blocking (message ~420 ms, sent
// ~200 ms); the FSK receiver is off meanwhile
// and is set up again afterwards (RADIO_Apply). Volume follows the knob.

#define BEEP_GAIN 96               // tone-1 gain, 0..127 (upstream PlayTone: 28 quiet / 96 loud)

void BEEP_Message(void);           // new message: two rising tones
void BEEP_Sent(void);              // we transmitted (beacon, message, ack): one short high tone
void BEEP_Received(void);          // a packet came in (station.h rx_beep): one short low tone

#endif
