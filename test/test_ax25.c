// Host test: feeds BK4819 FIFO words (one capture per input line, hex,
// space-separated) through the firmware's AX.25 decoder and prints frames.
// Words are unpacked exactly as the firmware does: low byte first, MSB first.
//
//   cc -I src -o build/test_ax25 test/test_ax25.c src/ax25.c
//   python3 test/captures.py LOG.jsonl | build/test_ax25

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ax25.h"

static int frames;

static void OnFrame(const uint8_t *f, uint16_t len)
{
    char text[400];
    AX25_FormatTNC2(f, len, text, sizeof text);
    printf("FRAME %s\n", text);
    frames++;
}

int main(void)
{
    static char line[1 << 16];
    Ax25Rx rx;
    int captures = 0;
    while (fgets(line, sizeof line, stdin)) {
        AX25_Reset(&rx);
        captures++;
        for (char *tok = strtok(line, " \n"); tok; tok = strtok(NULL, " \n")) {
            const unsigned w = (unsigned)strtoul(tok, NULL, 16);
            const uint8_t bytes[2] = { w & 0xFF, w >> 8 };
            for (int b = 0; b < 2; b++)
                for (int i = 7; i >= 0; i--)
                    AX25_PushToneBit(&rx, (bytes[b] >> i) & 1, OnFrame);
        }
    }
    printf("%d captures, %d frames\n", captures, frames);
    return 0;
}
