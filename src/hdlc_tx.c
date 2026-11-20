#include "ax25.h"
#include "hdlc_tx.h"

static void EmitFlags(HdlcBitSink sink, uint16_t n)
{
    while (n--)
        for (int i = 0; i < 8; i++)
            sink((0x7E >> i) & 1);
}

void HDLC_EmitFrame(HdlcBitSink sink, const uint8_t *frame, uint16_t len,
                    uint16_t preamble, uint16_t tail)
{
    const uint16_t fcs = AX25_Crc(frame, len);
    EmitFlags(sink, preamble);

    uint8_t ones = 0;
    for (uint16_t k = 0; k < len + 2u; k++) {
        const uint8_t byte = (k < len) ? frame[k]
                           : (k == len) ? (uint8_t)(fcs & 0xFF) : (uint8_t)(fcs >> 8);
        for (int i = 0; i < 8; i++) {
            const uint8_t bit = (byte >> i) & 1;
            sink(bit);
            if (bit) {
                if (++ones == 5) {
                    sink(0);        // stuffed zero
                    ones = 0;
                }
            } else {
                ones = 0;
            }
        }
    }

    EmitFlags(sink, tail);
}
