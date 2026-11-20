#ifndef HDLC_TX_H
#define HDLC_TX_H

#include <stdint.h>

// HDLC bit stream for one AX.25 frame: `preamble` 0x7E flags, the frame and
// its FCS (LSB first, bit-stuffed), then `tail` flags. Bits are delivered
// before NRZI; the transmitter flips the tone on every 0 bit.
typedef void (*HdlcBitSink)(uint8_t bit);

void HDLC_EmitFrame(HdlcBitSink sink, const uint8_t *frame, uint16_t len,
                    uint16_t preamble, uint16_t tail);

#endif
