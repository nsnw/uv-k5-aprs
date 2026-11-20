#ifndef AX25_H
#define AX25_H

#include <stdbool.h>
#include <stdint.h>

// Streaming AX.25 / HDLC receiver for a raw Bell 202 tone-bit stream.
// Feed it one demodulated tone bit at a time (before NRZI decoding).

#define AX25_MAX_FRAME 340   // 7*10 addresses + ctl/pid + 256 info + FCS, with slack

typedef struct {
    uint8_t  prev_tone;
    uint8_t  pattern;        // last 8 NRZI-decoded bits, newest in bit 7
    int16_t  bit_count;      // bits in `acc`, or -1 when not inside a frame
    uint8_t  acc;
    uint16_t len;
    uint8_t  frame[AX25_MAX_FRAME];
} Ax25Rx;

// Called with a complete frame (FCS already checked and stripped).
typedef void (*Ax25FrameHandler)(const uint8_t *frame, uint16_t len);

// CRC-16/X.25, the AX.25 frame check sequence (sent low byte first).
uint16_t AX25_Crc(const uint8_t *p, uint16_t n);

void AX25_Reset(Ax25Rx *rx);
void AX25_PushToneBit(Ax25Rx *rx, uint8_t tone, Ax25FrameHandler on_frame);

// "SRC>DST,PATH:info" (TNC2 style). Non-printable info bytes become '.'.
// Always NUL-terminates; returns the length written.
uint16_t AX25_FormatTNC2(const uint8_t *frame, uint16_t len, char *out, uint16_t out_size);

// Source callsign only, e.g. "VE6LY-7".
void AX25_FormatSource(const uint8_t *frame, uint16_t len, char *out, uint8_t out_size);

#endif
