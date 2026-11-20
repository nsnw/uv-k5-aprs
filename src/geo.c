#include "geo.h"

#define HM_PER_DEG   6000                 // hundredths of a minute per degree
#define MM_PER_HM    18520                // 0.01 minute of latitude = 18.52 m

// cos(d) for d = 0..90 degrees, Q15
static const uint16_t COS_Q15[91] = {
    32768, 32763, 32748, 32723, 32688, 32643, 32588, 32524, 32449, 32365,
    32270, 32166, 32052, 31928, 31795, 31651, 31499, 31336, 31164, 30983,
    30792, 30592, 30382, 30163, 29935, 29698, 29452, 29197, 28932, 28660,
    28378, 28088, 27789, 27482, 27166, 26842, 26510, 26170, 25822, 25466,
    25102, 24730, 24351, 23965, 23571, 23170, 22763, 22348, 21926, 21498,
    21063, 20622, 20174, 19720, 19261, 18795, 18324, 17847, 17364, 16877,
    16384, 15886, 15384, 14876, 14365, 13848, 13328, 12803, 12275, 11743,
    11207, 10668, 10126,  9580,  9032,  8481,  7927,  7371,  6813,  6252,
     5690,  5126,  4560,  3993,  3425,  2856,  2286,  1715,  1144,   572,
        0,
};

static uint32_t Isqrt64(uint64_t v)
{
    uint64_t r = 0, bit = (uint64_t)1 << 62;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= r + bit) {
            v -= r + bit;
            r = (r >> 1) + bit;
        } else {
            r >>= 1;
        }
        bit >>= 2;
    }
    return (uint32_t)r;
}

// atan(y / x) in hundredths of a degree for 0 <= y <= x, x > 0:
// atan(z) ~ 45 z + 15.64 z (1 - z) degrees, within 0.25 degrees.
static uint32_t AtanOctant(uint32_t y, uint32_t x)
{
    const uint32_t z = (uint32_t)(((uint64_t)y << 15) / x);                // Q15
    return (4500u * z + (uint32_t)(((uint64_t)1564u * z * (32768u - z)) >> 15)) >> 15;
}

uint32_t GEO_Distance(int32_t from_lat, int32_t from_lon, int32_t to_lat, int32_t to_lon,
                      uint16_t *bearing_deg)
{
    int32_t dlon = to_lon - from_lon;
    if (dlon > 180 * HM_PER_DEG)  dlon -= 360 * HM_PER_DEG;   // the short way round
    if (dlon < -180 * HM_PER_DEG) dlon += 360 * HM_PER_DEG;
    const int32_t dlat = to_lat - from_lat;

    // cos(mean latitude), interpolated between whole degrees
    int32_t mean = (from_lat + to_lat) / 2;
    if (mean < 0) mean = -mean;
    if (mean > 90 * HM_PER_DEG) mean = 90 * HM_PER_DEG;
    const uint32_t deg = (uint32_t)mean / HM_PER_DEG, frac = (uint32_t)mean % HM_PER_DEG;
    const uint32_t cosq = deg >= 90 ? 0
        : COS_Q15[deg] - (uint32_t)(COS_Q15[deg] - COS_Q15[deg + 1]) * frac / HM_PER_DEG;

    // millimetres north (y) and east (x)
    const int64_t y = (int64_t)dlat * MM_PER_HM;
    const int64_t x = ((int64_t)dlon * MM_PER_HM * cosq) >> 15;
    const uint64_t ax = (uint64_t)(x < 0 ? -x : x), ay = (uint64_t)(y < 0 ? -y : y);

    // in metres for the square root, so the squares stay within 64 bits
    const uint64_t mx = ax / 1000, my = ay / 1000;
    const uint32_t d = Isqrt64(mx * mx + my * my);

    if (bearing_deg) {
        // >> 4: keeps the octant maths in 32 bits (16 mm resolution is plenty)
        const uint32_t sx = (uint32_t)(ax >> 4), sy = (uint32_t)(ay >> 4);
        uint32_t a = 0;                                   // hundredths of a degree
        if (sx || sy)
            a = sx >= sy ? 9000u - AtanOctant(sy, sx) : AtanOctant(sx, sy);
        // a is now the angle from north towards east in the first quadrant
        if (y < 0) a = 18000u - a;
        if (x < 0) a = 36000u - a;
        *bearing_deg = (uint16_t)(((a + 50u) / 100u) % 360u);
    }
    return d;
}

static char *PutU32(char *p, uint32_t v)
{
    char tmp[10];
    uint8_t n = 0;
    do { tmp[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (n) *p++ = tmp[--n];
    return p;
}

void GEO_FormatDistance(uint32_t m, char *out)
{
    char *p = out;
    if (m < 1000) {
        p = PutU32(p, m);
        *p++ = 'm';
    } else if (m < 9950) {                                // 1.0 .. 9.9 km
        const uint32_t tenths = (m + 50) / 100;
        p = PutU32(p, tenths / 10);
        *p++ = '.';
        *p++ = (char)('0' + tenths % 10);
        *p++ = 'k';
        *p++ = 'm';
    } else {
        p = PutU32(p, (m + 500) / 1000 > 9999 ? 9999 : (m + 500) / 1000);
        *p++ = 'k';
        *p++ = 'm';
    }
    *p = '\0';
}

const char *GEO_Compass(uint16_t b)
{
    static const char *const NAMES[8] = { "N", "NE", "E", "SE", "S", "SW", "W", "NW" };
    return NAMES[((b % 360u) * 2u + 45u) / 90u % 8u];
}
