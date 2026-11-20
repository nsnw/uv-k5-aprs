// Loopback test: frames encoded by the firmware's transmitter bit stream
// (hdlc_tx.c + the same NRZI as tx.c) must decode with the firmware's
// receiver (ax25.c), byte for byte.
//
//   cc -I src -o build/test_hdlc_tx test/test_hdlc_tx.c src/hdlc_tx.c src/ax25.c

#include <stdio.h>
#include <string.h>

#include "ax25.h"
#include "hdlc_tx.h"

static Ax25Rx  rx;
static uint8_t tone = 1;
static uint8_t got[AX25_MAX_FRAME];
static int     got_len = -1, frames;

static void OnFrame(const uint8_t *f, uint16_t len)
{
    memcpy(got, f, len);
    got_len = len;
    frames++;
}

static void Sink(uint8_t bit)
{
    if (!bit) tone ^= 1;                // NRZI exactly as tx.c SendBit
    AX25_PushToneBit(&rx, tone, OnFrame);
}

static void Call(uint8_t *p, const char *c, int ssid, int last)
{
    for (int i = 0; i < 6; i++) p[i] = (uint8_t)((*c ? *c++ : ' ') << 1);
    p[6] = (uint8_t)(0x60 | (ssid << 1) | last);
}

int main(void)
{
    int failures = 0, cases = 0;
    uint8_t frame[400];
    for (int info_len = 0; info_len <= 300; info_len += 7) {
        for (int pattern = 0; pattern < 4; pattern++) {
            Call(frame, "APZK5M", 0, 0);
            Call(frame + 7, "VE6HUE", 1, 1);
            frame[14] = 0x03; frame[15] = 0xF0;
            for (int i = 0; i < info_len; i++) {
                const uint8_t v[4] = { (uint8_t)('A' + i % 26), 0xFF, 0x7E, (uint8_t)(i * 37) };
                frame[16 + i] = v[pattern];       // text, all-ones, flag bytes, mixed
            }
            const int len = 16 + info_len;
            AX25_Reset(&rx);
            got_len = -1;
            frames = 0;
            HDLC_EmitFrame(Sink, frame, (uint16_t)len, 10, 3);
            cases++;
            if (frames != 1 || got_len != len || memcmp(got, frame, (size_t)len)) {
                printf("FAIL info_len=%d pattern=%d frames=%d got_len=%d\n", info_len, pattern, frames, got_len);
                failures++;
            }
        }
    }
    printf("%d/%d loopback cases passed\n", cases - failures, cases);
    return failures != 0;
}
