// APRS position parsing (APRS 1.0.1 spec chapters 8, 9 and 10).

#include <limits.h>
#include <string.h>

#include "aprs_parse.h"

// ------------------------------------------------------------ helpers

static bool IsDigit(uint8_t c) { return c >= '0' && c <= '9'; }

static int Digits(const uint8_t *p, int n)   // -1 if any char is not a digit
{
    int v = 0;
    for (int i = 0; i < n; i++) {
        if (!IsDigit(p[i]))
            return -1;
        v = v * 10 + (p[i] - '0');
    }
    return v;
}

static int32_t Base91(const uint8_t *p, int n)  // -1 if out of range
{
    int32_t v = 0;
    for (int i = 0; i < n; i++) {
        if (p[i] < 33 || p[i] > 33 + 90)
            return -1;
        v = v * 91 + (p[i] - 33);
    }
    return v;
}

static int16_t KnotsToKmh(int32_t knots)
{
    return (int16_t)((knots * 1852 + 500) / 1000);
}

// Copies printable bytes, trimming leading spaces.
static void CopyComment(const uint8_t *p, const uint8_t *end, char *out, uint16_t size)
{
    uint16_t n = 0;
    while (p < end && *p == ' ')
        p++;
    for (; p < end && n + 1 < size; p++)
        if (*p >= 0x20 && *p < 0x7F)
            out[n++] = (char)*p;
    if (size)
        out[n] = '\0';
}

// Altitude in a comment: Mic-E "xxx}" (base 91, metres + 10000) near the
// start, or "/A=nnnnnn" (feet) anywhere.
static int32_t FindAltitude(const uint8_t *p, const uint8_t *end, bool mic_e)
{
    if (mic_e) {
        for (int skip = 0; skip <= 1; skip++) {
            if (p + skip + 4 <= end && p[skip + 3] == '}') {
                const int32_t v = Base91(p + skip, 3);
                if (v >= 0)
                    return v - 10000;
            }
        }
    }
    for (const uint8_t *q = p; q + 9 <= end; q++) {
        if (q[0] == '/' && q[1] == 'A' && q[2] == '=') {
            int32_t feet;
            if (q[3] == '-') {
                const int d = Digits(q + 4, 5);
                feet = d < 0 ? INT32_MIN : -d;
            } else {
                const int d = Digits(q + 3, 6);
                feet = d < 0 ? INT32_MIN : d;
            }
            if (feet != INT32_MIN)
                return (feet * 3048 + (feet >= 0 ? 5000 : -5000)) / 10000;
        }
    }
    return INT32_MIN;
}

// ------------------------------------------------------------ formats

// "DDMM.hhN" + table + "DDDMM.hhW" + symbol  (19 bytes)
static bool ParsePlain(const uint8_t *p, const uint8_t *end, AprsInfo *o)
{
    if (end - p < 19)
        return false;
    // Position ambiguity replaces trailing digits with spaces: treat as 0.
    uint8_t lat[8], lon[9];
    memcpy(lat, p, 8);
    memcpy(lon, p + 9, 9);
    for (int i = 0; i < 7; i++) if (lat[i] == ' ') lat[i] = '0';
    for (int i = 0; i < 8; i++) if (lon[i] == ' ') lon[i] = '0';

    const int lat_d = Digits(lat, 2), lat_m = Digits(lat + 2, 2), lat_h = Digits(lat + 5, 2);
    const int lon_d = Digits(lon, 3), lon_m = Digits(lon + 3, 2), lon_h = Digits(lon + 6, 2);
    if (lat_d < 0 || lat_m < 0 || lat_h < 0 || lon_d < 0 || lon_m < 0 || lon_h < 0)
        return false;
    if (lat[4] != '.' || lon[5] != '.')
        return false;
    if ((lat[7] != 'N' && lat[7] != 'S') || (lon[8] != 'E' && lon[8] != 'W'))
        return false;

    o->lat = lat_d * 6000 + lat_m * 100 + lat_h;
    o->lon = lon_d * 6000 + lon_m * 100 + lon_h;
    if (lat[7] == 'S') o->lat = -o->lat;
    if (lon[8] == 'W') o->lon = -o->lon;
    o->symbol_table = (char)p[8];
    o->symbol       = (char)p[18];

    // optional "ccc/sss" course/speed (knots)
    if (end - p >= 26 && p[22] == '/') {
        const int crs = Digits(p + 19, 3), spd = Digits(p + 23, 3);
        if (crs >= 0 && spd >= 0) {
            if (crs > 0)
                o->course = (int16_t)(crs % 360);
            o->speed_kmh = KnotsToKmh(spd);
        }
    }
    o->format = 'P';
    o->has_position = true;
    return true;
}

// table + YYYY + XXXX (base 91) + symbol + cs + T  (13 bytes)
static bool ParseCompressed(const uint8_t *p, const uint8_t *end, AprsInfo *o)
{
    if (end - p < 13)
        return false;
    const int32_t y = Base91(p + 1, 4), x = Base91(p + 5, 4);
    if (y < 0 || x < 0)
        return false;
    // lat = 90 - y/380926 deg, lon = -180 + x/190463 deg
    o->lat = 90 * 6000 - (int32_t)(((int64_t)y * 6000 + 190463) / 380926);
    o->lon = -180 * 6000 + (int32_t)(((int64_t)x * 6000 + 95231) / 190463);
    o->symbol_table = (char)p[0];
    o->symbol       = (char)p[9];

    const uint8_t c = p[10], s = p[11], t = p[12];
    if (c != ' ' && ((t - 33) & 0x18) != 0x10 && c >= '!' && c <= 'z') {
        // c/s = course/speed: course = c*4, speed = 1.08^s - 1 knots
        o->course = (int16_t)((c - 33) * 4);
        int32_t knots_x1000 = 1000;
        for (int i = 0; i < s - 33 && i < 100; i++)
            knots_x1000 = knots_x1000 * 108 / 100;
        o->speed_kmh = KnotsToKmh((knots_x1000 - 1000 + 500) / 1000);
    }
    o->format = 'C';
    o->has_position = true;
    return true;
}

// Mic-E: latitude etc. in the destination address, longitude in the info.
static bool ParseMicE(const uint8_t *dest, const uint8_t *p, const uint8_t *end, AprsInfo *o)
{
    if (end - p < 8)
        return false;

    int lat_digits[6];
    bool north = false, lon_offset = false, west = false;
    for (int i = 0; i < 6; i++) {
        const char c = (char)(dest[i] >> 1);
        int d;
        bool high;   // 'P'..'Z' range: N / +100 / W flags
        if (c >= '0' && c <= '9')      { d = c - '0'; high = false; }
        else if (c >= 'A' && c <= 'J') { d = c - 'A'; high = false; }
        else if (c >= 'P' && c <= 'Y') { d = c - 'P'; high = true;  }
        else if (c == 'K' || c == 'L') { d = 0;       high = false; }
        else if (c == 'Z')             { d = 0;       high = true;  }
        else return false;
        lat_digits[i] = d;
        if (i == 3) north = high;
        if (i == 4) lon_offset = high;
        if (i == 5) west = high;
    }
    const int lat_deg = lat_digits[0] * 10 + lat_digits[1];
    const int lat_min = lat_digits[2] * 10 + lat_digits[3];
    const int lat_hun = lat_digits[4] * 10 + lat_digits[5];
    if (lat_deg > 90 || lat_min > 59)
        return false;

    int lon_deg = p[0] - 28;
    if (lon_offset) lon_deg += 100;
    if (lon_deg >= 180 && lon_deg <= 189) lon_deg -= 80;
    else if (lon_deg >= 190 && lon_deg <= 199) lon_deg -= 190;
    int lon_min = p[1] - 28;
    if (lon_min >= 60) lon_min -= 60;
    const int lon_hun = p[2] - 28;
    if (lon_deg < 0 || lon_deg > 179 || lon_min < 0 || lon_min > 59 || lon_hun < 0 || lon_hun > 99)
        return false;

    o->lat = lat_deg * 6000 + lat_min * 100 + lat_hun;
    o->lon = lon_deg * 6000 + lon_min * 100 + lon_hun;
    if (!north) o->lat = -o->lat;
    if (west)   o->lon = -o->lon;

    const int sp = p[3] - 28, dc = p[4] - 28, se = p[5] - 28;
    int speed = sp * 10 + dc / 10;
    if (speed >= 800) speed -= 800;
    int course = (dc % 10) * 100 + se;
    if (course >= 400) course -= 400;
    o->speed_kmh = KnotsToKmh(speed);
    if (course > 0 && course <= 360)
        o->course = (int16_t)(course % 360);

    o->symbol       = (char)p[6];
    o->symbol_table = (char)p[7];
    o->format = 'M';
    o->has_position = true;
    return true;
}

// ------------------------------------------------------------ public

void APRS_Parse(const uint8_t *frame, uint16_t len, AprsInfo *out,
                char *comment, uint16_t comment_size)
{
    memset(out, 0, sizeof *out);
    out->speed_kmh  = APRS_UNKNOWN_I16;
    out->course     = APRS_UNKNOWN_I16;
    out->altitude_m = INT32_MIN;
    if (comment_size)
        comment[0] = '\0';

    // find end of the address field
    uint16_t pos = 0;
    while (pos + 7 <= len && !(frame[pos + 6] & 1))
        pos += 7;
    pos += 7;              // past the last address
    pos += 2;              // control + PID
    if (pos >= len || len < 16)
        return;

    const uint8_t *info = frame + pos;
    const uint8_t *end  = frame + len;
    const uint8_t type  = info[0];
    const uint8_t *p    = info + 1;
    const uint8_t *rest = p;   // comment starts here if nothing parses

    switch (type) {
    case '!': case '=':
    case '/': case '@':
        if (type == '/' || type == '@') {   // 7-char timestamp first
            if (end - p < 7) break;
            p += 7;
        }
        if (IsDigit(*p) || *p == ' ') {
            if (ParsePlain(p, end, out))
                rest = p + 19 + ((out->speed_kmh != APRS_UNKNOWN_I16) ? 7 : 0);
        } else if (ParseCompressed(p, end, out)) {
            rest = p + 13;
        }
        break;

    case '`': case '\'': case 0x1C: case 0x1D:
        if (ParseMicE(frame, p, end, out))
            rest = p + 8;
        break;

    default:
        break;
    }

    // trailing CR/LF/spaces are common; trim before looking at suffixes
    while (end > rest && (end[-1] == '\r' || end[-1] == '\n' || end[-1] == ' '))
        end--;

    if (out->has_position)
        out->altitude_m = FindAltitude(rest, end, out->format == 'M');

    if (out->format == 'M') {
        // Mic-E type/manufacturer markers (APRS 1.2 Mic-E addendum):
        // '>' ... '^'/'=' Kenwood handhelds, ']' ... '=' Kenwood mobiles,
        // '`'/'\'' ... XY two-character manufacturer suffix.
        const uint8_t lead = (rest < end) ? *rest : 0;
        if (lead == '>' || lead == ']' || lead == '`' || lead == '\'') {
            rest++;
            if (lead == '>' || lead == ']') {
                if (end > rest && (end[-1] == '^' || end[-1] == '='))
                    end--;
            } else if (end - rest >= 2) {
                end -= 2;
            }
        }
        // altitude "xxx}" right at the start
        if (end - rest >= 4 && rest[3] == '}' && Base91(rest, 3) >= 0)
            rest += 4;
    }
    CopyComment(rest, end, comment, comment_size);

    // drop a "/A=nnnnnn" altitude token from plain comments
    char *a = strstr(comment, "/A=");
    if (a && strlen(a) >= 9)
        memmove(a, a + 9, strlen(a + 9) + 1);
}

static void FormatCoord(int32_t v, int deg_digits, char pos, char neg, char *out)
{
    const char hemi = v < 0 ? neg : pos;
    if (v < 0) v = -v;
    const int32_t deg = v / 6000;
    const int32_t min = (v % 6000) / 100;
    const int32_t hun = v % 100;
    char *o = out;
    if (deg_digits == 3) *o++ = (char)('0' + deg / 100);
    *o++ = (char)('0' + deg / 10 % 10);
    *o++ = (char)('0' + deg % 10);
    *o++ = ' ';
    *o++ = (char)('0' + min / 10);
    *o++ = (char)('0' + min % 10);
    *o++ = '.';
    *o++ = (char)('0' + hun / 10);
    *o++ = (char)('0' + hun % 10);
    *o++ = hemi;
    *o = '\0';
}

void APRS_FormatLat(int32_t lat, char *out) { FormatCoord(lat, 2, 'N', 'S', out); }
void APRS_FormatLon(int32_t lon, char *out) { FormatCoord(lon, 3, 'E', 'W', out); }
