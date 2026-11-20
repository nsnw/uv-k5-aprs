// Host test for digi.c: New-N path rewriting and duplicate suppression.
//   cc -DDIGI_HOST_TEST -I . -I src -o build/test_digi test/test_digi.c src/digi.c src/ax25.c && build/test_digi

#include <stdio.h>
#include <string.h>

#include "ax25.h"
#include "digi.h"

bool DIGI_TestDupe(const uint8_t *f, uint16_t len, uint32_t now_s);

static int sPass, sFail;

// "CALL-SSID" or "CALL-SSID*" -> 7-byte address field
static void Addr(uint8_t *out, const char *s, bool last)
{
    char call[7] = "      ";
    int ssid = 0, i = 0;
    bool h = false;
    for (; *s && *s != '-' && *s != '*'; s++)
        call[i++] = *s;
    if (*s == '-') { s++; while (*s >= '0' && *s <= '9') ssid = ssid * 10 + (*s++ - '0'); }
    if (*s == '*') h = true;
    for (i = 0; i < 6; i++) out[i] = (uint8_t)(call[i] << 1);
    out[6] = (uint8_t)(0x60 | (ssid << 1) | (h ? 0x80 : 0) | (last ? 1 : 0));
}

// "SRC>DST,P1,P2:info" -> frame (no FCS)
static uint16_t Frame(uint8_t *out, const char *tnc2)
{
    char buf[256];
    strcpy(buf, tnc2);
    char *info = strchr(buf, ':');
    *info++ = '\0';
    char *calls[12];
    int n = 0;
    char *gt = strchr(buf, '>');
    *gt = '\0';
    char *src = buf, *rest = gt + 1;
    calls[n++] = strtok(rest, ",");     // destination
    calls[n++] = src;
    char *t;
    while ((t = strtok(NULL, ",")))
        calls[n++] = t;
    for (int i = 0; i < n; i++)
        Addr(out + 7 * i, calls[i], i == n - 1);
    uint16_t len = (uint16_t)(7 * n);
    out[len++] = 0x03;
    out[len++] = 0xF0;
    memcpy(out + len, info, strlen(info));
    return (uint16_t)(len + strlen(info));
}

static void Case(const char *in, uint8_t max_hops, const char *want)
{
    uint8_t f[400], out[400], mycall[7];
    Addr(mycall, "VE6HUE-1", false);
    const uint16_t len = Frame(f, in);
    const uint16_t n = DIGI_Rewrite(f, len, mycall, max_hops, out);
    char got[400] = "(not repeated)";
    if (n)
        AX25_FormatTNC2(out, n, got, sizeof got);
    if (strcmp(got, want) == 0) {
        sPass++;
    } else {
        sFail++;
        printf("FAIL %s (max %u)\n  want %s\n  got  %s\n", in, max_hops, want, got);
    }
}

int main(void)
{
    const char *NO = "(not repeated)";
    Case("VE6LY-7>APDR16,WIDE1-1:hi", 2,          "VE6LY-7>APDR16,VE6HUE-1*,WIDE1*:hi");
    Case("VE6LY-7>APDR16,WIDE2-2:hi", 2,          "VE6LY-7>APDR16,VE6HUE-1*,WIDE2-1:hi");
    Case("VE6LY-7>APDR16,WIDE2-1:hi", 2,          "VE6LY-7>APDR16,VE6HUE-1*,WIDE2*:hi");
    Case("VE6LY-7>APDR16,WIDE1-1,WIDE2-1:hi", 2,  "VE6LY-7>APDR16,VE6HUE-1*,WIDE1*,WIDE2-1:hi");
    Case("VE6LY-7>APDR16,CALGRY*,WIDE2-1:hi", 2,  "VE6LY-7>APDR16,CALGRY*,VE6HUE-1*,WIDE2*:hi");
    Case("VE6LY-7>APDR16,WIDE1*,WIDE2-1:hi", 2,   "VE6LY-7>APDR16,WIDE1*,VE6HUE-1*,WIDE2*:hi");
    Case("VE6LY-7>APDR16,WIDE3-3:hi", 2,          NO);    // more hops than allowed
    Case("VE6LY-7>APDR16,WIDE3-3:hi", 3,          "VE6LY-7>APDR16,VE6HUE-1*,WIDE3-2:hi");
    Case("VE6LY-7>APDR16,WIDE2-3:hi", 7,          NO);    // N > n: malformed / abuse
    Case("VE6LY-7>APDR16,WIDE2-1:hi", 1,          NO);    // fill-in only
    Case("VE6LY-7>APDR16,WIDE1-1:hi", 1,          "VE6LY-7>APDR16,VE6HUE-1*,WIDE1*:hi");
    Case("VE6HUE-1>APZK5M,WIDE1-1:hi", 2,         NO);    // our own packet
    Case("VE6LY-7>APDR16,VE6HUE-1:hi", 2,         "VE6LY-7>APDR16,VE6HUE-1*:hi");   // directed
    Case("VE6LY-7>APDR16,VE6HUE-1*,WIDE2-1:hi", 2, NO);   // repeated it already
    Case("VE6LY-7>APDR16,WIDE1*:hi", 2,           NO);    // path used up
    Case("VE6LY-7>APDR16:hi", 2,                  NO);    // no path
    Case("VE6LY-7>APDR16,RELAY,WIDE2-1:hi", 2,    NO);    // not ours to handle
    Case("VE6LY-7>APDR16,VE6HUE,WIDE2-1:hi", 2,   NO);    // different SSID
    Case("VE6LY-7>APDR16,WIDE1-1,VE6HUE-1:hi", 2, NO);    // we're further down the path
    Case("VE6LY-7>APDR16,A*,B*,C*,D*,E*,F*,G*,WIDE2-2:hi", 2,
         "VE6LY-7>APDR16,A*,B*,C*,D*,E*,F*,G*,WIDE2-1:hi");                        // full: count down only
    Case("VE6LY-7>APDR16,WIDEX-1:hi", 2,          NO);
    Case("VE6LY-7>APDR16,WIDE11-1:hi", 2,         NO);

    // duplicates: same source/destination/info, any path, within 30 s
    uint8_t a[300], b[300], c[300];
    const uint16_t la = Frame(a, "VE6LY-7>APDR16,WIDE1-1,WIDE2-1:hello");
    const uint16_t lb = Frame(b, "VE6LY-7>APDR16,CALGRY*,WIDE2*:hello");
    const uint16_t lc = Frame(c, "VE6LY-7>APDR16,WIDE1-1,WIDE2-1:hello again");
    bool ok = !DIGI_TestDupe(a, la, 100) && DIGI_TestDupe(b, lb, 110)
           && !DIGI_TestDupe(c, lc, 111) && !DIGI_TestDupe(a, la, 131);
    if (ok) sPass++; else { sFail++; printf("FAIL duplicate suppression\n"); }

    printf("%d passed, %d failed\n", sPass, sFail);
    return sFail != 0;
}
