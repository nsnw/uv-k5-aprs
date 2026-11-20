// HDLC framing follows the classic software-TNC approach (as in Dire Wolf's
// hdlc_rec.c): an 8-bit pattern register spots flags, aborts and stuffed
// bits; everything else is shifted into bytes LSB first.

#include <string.h>

#include "ax25.h"

#define MIN_FRAME 17   // 2 addresses (14) + control + FCS (2)

uint16_t AX25_Crc(const uint8_t *p, uint16_t n)
{
    uint16_t crc = 0xFFFF;
    while (n--) {
        crc ^= *p++;
        for (int i = 0; i < 8; i++)
            crc = (crc & 1) ? (crc >> 1) ^ 0x8408 : crc >> 1;
    }
    return crc ^ 0xFFFF;
}

void AX25_Reset(Ax25Rx *rx)
{
    rx->prev_tone = 0;
    rx->pattern   = 0;
    rx->bit_count = -1;
    rx->acc       = 0;
    rx->len       = 0;
}

static void EndOfFrame(Ax25Rx *rx, Ax25FrameHandler on_frame)
{
    // A frame ends on a flag. The flag's first 7 bits were already shifted
    // into `acc`, so a byte-aligned frame leaves exactly 7 bits there.
    if (rx->bit_count == 7 && rx->len >= MIN_FRAME) {
        const uint16_t body = rx->len - 2;
        const uint16_t fcs  = rx->frame[body] | (rx->frame[body + 1] << 8);
        if (AX25_Crc(rx->frame, body) == fcs)
            on_frame(rx->frame, body);
    }
}

void AX25_PushToneBit(Ax25Rx *rx, uint8_t tone, Ax25FrameHandler on_frame)
{
    tone &= 1;
    const uint8_t bit = (tone == rx->prev_tone);  // NRZI: no change = 1
    rx->prev_tone = tone;

    rx->pattern = (uint8_t)((rx->pattern >> 1) | (bit << 7));

    if (rx->pattern == 0x7E) {               // flag: end of one frame, start of the next
        EndOfFrame(rx, on_frame);
        rx->bit_count = 0;
        rx->acc = 0;
        rx->len = 0;
        return;
    }
    if (rx->pattern == 0xFE) {               // 7 ones: abort / idle noise
        rx->bit_count = -1;
        rx->len = 0;
        return;
    }
    if ((rx->pattern & 0xFC) == 0x7C)        // 0 after five 1s: stuffed bit, drop it
        return;
    if (rx->bit_count < 0)
        return;

    rx->acc = (uint8_t)((rx->acc >> 1) | (bit << 7));
    if (++rx->bit_count == 8) {
        rx->bit_count = 0;
        if (rx->len < AX25_MAX_FRAME) {
            rx->frame[rx->len++] = rx->acc;
        } else {                              // too long: drop until the next flag
            rx->bit_count = -1;
            rx->len = 0;
        }
    }
}

// ------------------------------------------------------------ formatting

typedef struct {
    char    *p;
    uint16_t room;   // bytes left, including space for the NUL
} Out;

static void Put(Out *o, char c)
{
    if (o->room > 1) {
        *o->p++ = c;
        o->room--;
    }
}

static void PutCall(Out *o, const uint8_t *a)
{
    for (int i = 0; i < 6; i++) {
        const char c = (char)(a[i] >> 1);
        if (c != ' ')
            Put(o, c);
    }
    const uint8_t ssid = (a[6] >> 1) & 0x0F;
    if (ssid) {
        Put(o, '-');
        if (ssid >= 10)
            Put(o, '1');
        Put(o, (char)('0' + ssid % 10));
    }
}

// Returns the number of address fields (>= 2), or 0 if malformed.
static uint8_t CountAddresses(const uint8_t *frame, uint16_t len)
{
    uint8_t n = 0;
    for (uint16_t pos = 0; pos + 7 <= len && n < 10; pos += 7) {
        n++;
        if (frame[pos + 6] & 1)
            return n >= 2 ? n : 0;
    }
    return 0;
}

uint16_t AX25_FormatTNC2(const uint8_t *frame, uint16_t len, char *out, uint16_t out_size)
{
    Out o = { out, out_size };
    const uint8_t naddr = CountAddresses(frame, len);

    if (naddr == 0) {
        static const char hex[] = "0123456789ABCDEF";
        for (uint16_t i = 0; i < len; i++) {
            Put(&o, hex[frame[i] >> 4]);
            Put(&o, hex[frame[i] & 15]);
        }
    } else {
        PutCall(&o, frame + 7);             // source
        Put(&o, '>');
        PutCall(&o, frame);                 // destination
        for (uint8_t i = 2; i < naddr; i++) {
            const uint8_t *a = frame + 7 * i;
            Put(&o, ',');
            PutCall(&o, a);
            if (a[6] & 0x80)
                Put(&o, '*');               // has-been-repeated
        }
        Put(&o, ':');
        for (uint16_t i = 7 * naddr + 2; i < len; i++) {   // skip control + PID
            const uint8_t c = frame[i];
            if (c == '\r' || c == '\n')
                continue;
            Put(&o, (c >= 0x20 && c < 0x7F) ? (char)c : '.');
        }
    }
    if (out_size)
        *o.p = '\0';
    return (uint16_t)(o.p - out);
}

void AX25_FormatSource(const uint8_t *frame, uint16_t len, char *out, uint8_t out_size)
{
    Out o = { out, out_size };
    if (len >= 14)
        PutCall(&o, frame + 7);
    if (out_size)
        *o.p = '\0';
}
