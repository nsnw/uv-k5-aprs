#include <string.h>

#include "msg.h"

#ifndef MSG_HOST_TEST
#include "aprs.h"
#include "ax25.h"
#include "beacon.h"
#include "kiss.h"
#include "settings.h"
#ifdef ENABLE_TX
#include "tx.h"
#include "txq.h"
#endif
#endif

#define ADDR_LEN     7
#define DEST_CALL    "APZK5M"
#define DUPES        16
#define DUPE_S       (30u * 60u)   // senders give up retrying well within this
#define REACK_S      5u            // a digipeated copy right after the original: one ack is enough

static AprsMessage sInbox[MSG_INBOX];   // newest first
static uint8_t     sCount;
static bool        sNew, sChanged;
static uint16_t    sNextId = 1;
static MsgQuery    sQuery;
static uint32_t    sQueryAnswered_s;
static bool        sQueryAnsweredOnce;

// Sends one copy of ":TO       :text" (radio build: msg.c below; host test: stub).
#ifdef MSG_HOST_TEST
bool MSG_TestTransmit(const char *to, const char *text);
#define Transmit MSG_TestTransmit
#else
static bool Transmit(const char *to, const char *text);
#endif
static struct { uint32_t hash, time_s; } sDupes[DUPES];
static uint8_t     sDupeNext;

// ---------------------------------------------------------------- inbox

uint8_t MSG_Count(void) { return sCount; }

uint8_t MSG_Unread(void)
{
    uint8_t n = 0;
    for (uint8_t i = 0; i < sCount; i++) n += sInbox[i].unread;
    return n;
}

bool MSG_TakeChanged(void)
{
    const bool c = sChanged;
    sChanged = false;
    return c;
}

const AprsMessage *MSG_Get(uint8_t i) { return i < sCount ? &sInbox[i] : NULL; }
void MSG_MarkRead(uint8_t i)         { if (i < sCount) sInbox[i].unread = false; }

void MSG_Delete(uint8_t i)
{
    if (i >= sCount) return;
    memmove(&sInbox[i], &sInbox[i + 1], (size_t)(sCount - i - 1) * sizeof sInbox[0]);
    sCount--;
}

bool MSG_TakeNew(void)
{
    const bool n = sNew;
    sNew = false;
    return n;
}

// A new entry at the top; when full, the oldest goes (but not one still sending).
static AprsMessage *Add(const char *call, const char *text, uint16_t text_len, uint32_t now_s)
{
    if (sCount == MSG_INBOX) {
        int drop = MSG_INBOX - 1;
        while (drop > 0 && sInbox[drop].state == MSG_SENDING) drop--;
        MSG_Delete((uint8_t)drop);
    }
    sCount++;
    memmove(&sInbox[1], &sInbox[0], (size_t)(sCount - 1) * sizeof sInbox[0]);
    AprsMessage *m = &sInbox[0];
    memset(m, 0, sizeof *m);
    strncpy(m->call, call, MSG_CALL_MAX);
    if (text_len > MSG_TEXT_MAX) text_len = MSG_TEXT_MAX;
    for (uint16_t i = 0; i < text_len; i++)
        m->text[i] = (text[i] >= ' ' && text[i] <= '~') ? text[i] : '.';
    m->time_s = now_s;
    return m;
}

static void Store(const char *from, const char *text, uint16_t text_len, uint32_t now_s)
{
    AprsMessage *m = Add(from, text, text_len, now_s);
    m->state  = MSG_IN;
    m->unread = true;
    sNew = true;
}

// An ack / reject from `from` for our message `id`.
static void Answered(const char *from, const char *id, uint16_t id_len, bool ack)
{
    for (uint8_t i = 0; i < sCount; i++) {
        AprsMessage *m = &sInbox[i];
        if (m->state == MSG_SENDING && strlen(m->id) == id_len && !memcmp(m->id, id, id_len)
            && !strcmp(m->call, from)) {
            m->state = ack ? MSG_DELIVERED : MSG_REJECTED;
            sChanged = true;
            return;
        }
    }
}

// ---------------------------------------------------------------- sending

static bool SendCopy(AprsMessage *m)
{
    char text[MSG_TEXT_MAX + 1 + 6];
    strcpy(text, m->text);
    strcat(text, "{");
    strcat(text, m->id);
    return Transmit(m->call, text);
}

bool MSG_Send(const char *to, const char *text, uint32_t now_s)
{
    AprsMessage m;
    memset(&m, 0, sizeof m);
    strncpy(m.call, to, MSG_CALL_MAX);
    strncpy(m.text, text, MSG_TEXT_MAX);
    char *p = m.id;                     // 1..9999, as text
    uint16_t v = sNextId, div = 10000;
    while (div > 1 && v < div) div /= 10;
    for (; div; div /= 10) *p++ = (char)('0' + v / div % 10);
    *p = '\0';
    if (!SendCopy(&m))
        return false;
    sNextId = sNextId >= 9999 ? 1 : (uint16_t)(sNextId + 1);
    AprsMessage *e = Add(m.call, m.text, (uint16_t)strlen(m.text), now_s);
    memcpy(e->id, m.id, sizeof e->id);
    e->state  = MSG_SENDING;
    e->tries  = 1;
    e->next_s = now_s + MSG_RETRY_S;
    return true;
}

void MSG_Poll1s(uint32_t now_s)
{
    for (uint8_t i = 0; i < sCount; i++) {
        AprsMessage *m = &sInbox[i];
        if (m->state != MSG_SENDING || now_s < m->next_s)
            continue;
        if (m->tries >= MSG_TRIES) {
            m->state = MSG_FAILED;
            sChanged = true;
        } else if (SendCopy(m)) {
            m->tries++;
            m->next_s = now_s + MSG_RETRY_S;
            sChanged  = true;
        } else {
            m->next_s = now_s + 5;      // can't send right now: try again shortly
        }
    }
}

// ---------------------------------------------------------------- duplicates

static uint32_t Fnv(uint32_t h, const char *p, uint16_t n)
{
    for (uint16_t i = 0; i < n; i++) { h ^= (uint8_t)p[i]; h *= 16777619u; }
    return h;
}

// True if seen within DUPE_S (*age_s: since last heard); remembers it either way.
static bool SeenBefore(uint32_t hash, uint32_t now_s, uint32_t *age_s)
{
    for (uint8_t i = 0; i < DUPES; i++)
        if (sDupes[i].hash == hash && sDupes[i].time_s && now_s - sDupes[i].time_s < DUPE_S) {
            *age_s = now_s - sDupes[i].time_s;
            sDupes[i].time_s = now_s;
            return true;
        }
    sDupes[sDupeNext].hash   = hash;
    sDupes[sDupeNext].time_s = now_s ? now_s : 1;
    sDupeNext = (uint8_t)((sDupeNext + 1) % DUPES);
    return false;
}

// ---------------------------------------------------------------- parsing

static bool SameCall(const char *a, uint16_t a_len, const char *b)   // a: possibly space-padded
{
    while (a_len && a[a_len - 1] == ' ') a_len--;
    const size_t b_len = strlen(b);
    if (a_len != b_len) return false;
    for (uint16_t i = 0; i < a_len; i++) {
        char c = a[i];
        if (c >= 'a' && c <= 'z') c = (char)(c - 32);
        if (c != b[i]) return false;
    }
    return true;
}

bool MSG_HandleInfo(const char *from, const char *info, uint16_t n, const char *mycall,
                    uint32_t now_s, char ack_id[6])
{
    ack_id[0] = '\0';
    // ":ADDRESSEE:text{id" with the addressee padded to 9 characters
    if (n < 11 || info[0] != ':' || info[10] != ':' || !SameCall(info + 1, 9, mycall))
        return false;
    const char *text = info + 11;
    uint16_t text_len = (uint16_t)(n - 11);

    // "ack12345" / "rej12345" (maybe "ack12}..."): the answer to a message we sent
    if (text_len >= 4 && (!strncmp(text, "ack", 3) || !strncmp(text, "rej", 3))
        && memchr(text, '{', text_len) == NULL) {
        uint16_t id_len = 0;
        while (3 + id_len < text_len && id_len < 5 && text[3 + id_len] > ' ' && text[3 + id_len] != '}')
            id_len++;
        Answered(from, text + 3, id_len, text[0] == 'a');
        return false;
    }

    // message number: after the last '{', up to 5 characters; "{MM}AA"
    // (reply-ack) acks with the part before the '}'
    const char *brace = NULL;
    for (uint16_t i = 0; i < text_len; i++)
        if (text[i] == '{') brace = text + i;
    if (brace) {
        uint8_t k = 0;
        const char *p = brace + 1;
        for (; p < text + text_len && *p != '}' && k < 5; p++)
            if (*p > ' ' && *p <= '~') ack_id[k++] = *p;
        ack_id[k] = '\0';
        // reply-ack "{MM}AA": AA acks our message AA
        if (p < text + text_len && *p == '}' && p + 1 < text + text_len) {
            uint16_t id_len = 0;
            while (p + 1 + id_len < text + text_len && id_len < 5 && p[1 + id_len] > ' ') id_len++;
            Answered(from, p + 1, id_len, true);
        }
        text_len = (uint16_t)(brace - text);
    }
    while (text_len && (text[text_len - 1] == ' ' || text[text_len - 1] == '\r' || text[text_len - 1] == '\n'))
        text_len--;

    // a query? ("?APRSP", "?PING?"; anything else starting with '?' is a message)
    static const struct { const char *text; MsgQuery q; } QUERIES[] = {
        { "?APRSP", MSG_QUERY_POSITION }, { "?APRSS", MSG_QUERY_STATUS },
        { "?APRST", MSG_QUERY_TRACE },    { "?PING?", MSG_QUERY_TRACE },
        { "?APRSD", MSG_QUERY_DIRECT },
    };
    MsgQuery query = MSG_QUERY_NONE;
    for (uint8_t i = 0; i < sizeof QUERIES / sizeof QUERIES[0]; i++) {
        const uint16_t k = (uint16_t)strlen(QUERIES[i].text);
        if (text_len >= k && !memcmp(text, QUERIES[i].text, k)
            && (text_len == k || text[k] == ' '))
            query = QUERIES[i].q;
    }

    uint32_t h = Fnv(2166136261u, from, (uint16_t)strlen(from));
    h = ack_id[0] ? Fnv(h ^ 0x5A, ack_id, (uint16_t)strlen(ack_id)) : Fnv(h ^ 0xA5, text, text_len);
    uint32_t age_s;
    if (SeenBefore(h, now_s, &age_s)) {     // a retry: ack again, don't store again
        if (age_s < REACK_S)
            ack_id[0] = '\0';
        return false;
    }
    if (query != MSG_QUERY_NONE) {          // answered (msg.c below), not stored
        if (!sQueryAnsweredOnce || now_s - sQueryAnswered_s >= MSG_QUERY_GAP_S) {
            sQuery             = query;
            sQueryAnswered_s   = now_s;
            sQueryAnsweredOnce = true;
        }
        return false;
    }
    Store(from, text, text_len, now_s);
    return true;
}

MsgQuery MSG_TakeQuery(void)
{
    const MsgQuery q = sQuery;
    sQuery = MSG_QUERY_NONE;
    return q;
}

// ---------------------------------------------------------------- EEPROM (store.h)

AprsMessage *MSG_Table(uint8_t **count)
{
    *count = &sCount;
    return sInbox;
}

bool MSG_RestoreMessage(AprsMessage *m)
{
    if (!memchr(m->call, '\0', sizeof m->call) || !m->call[0]
        || !memchr(m->text, '\0', sizeof m->text) || !memchr(m->id, '\0', sizeof m->id)
        || m->state > MSG_FAILED)
        return false;
    if (m->state == MSG_SENDING)            // the retries stopped at power-off
        m->state = MSG_FAILED;
    m->time_s = m->next_s = 0;
    m->old    = true;
    uint16_t id = 0;                        // number new messages after the restored ones
    for (const char *p = m->id; *p >= '0' && *p <= '9'; p++) id = (uint16_t)(id * 10 + (*p - '0'));
    if (id >= sNextId && id < 9999) sNextId = (uint16_t)(id + 1);
    m->unread = m->unread && m->state == MSG_IN;
    return true;
}

#ifndef MSG_HOST_TEST

// ---------------------------------------------------------------- frames

static void SourceText(const uint8_t *a, char *out)   // AX.25 address -> "CALL-SSID"
{
    uint8_t n = 0;
    for (int i = 0; i < 6 && (a[i] >> 1) != ' '; i++) out[n++] = (char)(a[i] >> 1);
    const uint8_t ssid = (a[6] >> 1) & 0x0F;
    if (ssid) {
        out[n++] = '-';
        if (ssid >= 10) out[n++] = '1';
        out[n++] = (char)('0' + ssid % 10);
    }
    out[n] = '\0';
}

static void MyCall(char *out)
{
    const BeaconConfig *b = BEACON_Get();
    uint8_t n = 0;
    for (int i = 0; i < 6 && b->call[i] && b->call[i] != ' '; i++) out[n++] = b->call[i];
    if (b->ssid) {
        out[n++] = '-';
        if (b->ssid >= 10) out[n++] = '1';
        out[n++] = (char)('0' + b->ssid % 10);
    }
    out[n] = '\0';
}

#ifdef ENABLE_TX
static uint8_t *PutAddress(uint8_t *p, const char *call, uint8_t ssid, bool last)
{
    bool ended = false;                     // pad past the end with spaces
    for (int i = 0; i < 6; i++) {
        if (!ended && !call[i]) ended = true;
        const char ch = (!ended && call[i] != ' ') ? call[i] : ' ';
        *p++ = (uint8_t)(ch << 1);
    }
    *p++ = (uint8_t)(0x60 | (ssid << 1) | (last ? 1 : 0));
    return p;
}

// One frame from MYCALL with the beacon's digipeater path and this info field.
static bool TransmitInfo(const char *info1, const char *info2)
{
    if (!BEACON_IsConfigured() || !TX_IsAllowedFrequency(gSettings.frequency))
        return false;
    const BeaconConfig *b = BEACON_Get();
    static uint8_t frame[TX_MAX_FRAME];
    uint8_t *p = frame;
    p = PutAddress(p, DEST_CALL, 0, false);
    p = PutAddress(p, b->call, b->ssid, b->path_hops == 0);
    if (b->path_hops >= 1) p = PutAddress(p, "WIDE1", 1, b->path_hops == 1);
    if (b->path_hops >= 2) p = PutAddress(p, "WIDE2", 1, true);
    *p++ = 0x03;
    *p++ = 0xF0;
    for (const char *t = info1; *t && p < frame + sizeof frame; t++) *p++ = (uint8_t)*t;
    for (const char *t = info2; *t && p < frame + sizeof frame; t++) *p++ = (uint8_t)*t;
    return TXQ_PushLocal(frame, (uint16_t)(p - frame));
}

// ":TO       :text"
static bool Transmit(const char *to, const char *text)
{
    char head[12];
    uint8_t k = 0;
    head[0] = ':';
    for (; to[k] && k < 9; k++) head[1 + k] = to[k];
    for (; k < 9; k++) head[1 + k] = ' ';
    head[10] = ':';
    head[11] = '\0';
    return TransmitInfo(head, text);
}

// Answers a directed query (MsgQuery) from `from`; `frame` is the query as heard.
static void AnswerQuery(MsgQuery q, const char *from, const uint8_t *frame, uint16_t len)
{
    char text[MSG_TEXT_MAX + 1];
    switch (q) {
    case MSG_QUERY_POSITION:                // nothing if no position is set
        BEACON_SendPosition();
        break;
    case MSG_QUERY_STATUS:
        BEACON_SendStatus();
        break;
    case MSG_QUERY_TRACE: {                 // "SRC>DEST,DIGI*,WIDE2*": the header as heard
        static char line[AX25_MAX_FRAME + 64];
        AX25_FormatTNC2(frame, len, line, sizeof line);
        char *colon = strchr(line, ':');
        if (colon) *colon = '\0';
        strncpy(text, line, MSG_TEXT_MAX);
        text[MSG_TEXT_MAX] = '\0';
        Transmit(from, text);
        break;
    }
    case MSG_QUERY_DIRECT:
        APRS_FormatDirects(text, sizeof text);
        Transmit(from, text);
        break;
    default:
        break;
    }
}
#else
static bool Transmit(const char *to, const char *text) { (void)to; (void)text; return false; }
#endif

void MSG_OnFrame(const uint8_t *frame, uint16_t len, uint32_t now_s)
{
    if (KISS_IsActive() || !BEACON_IsConfigured())
        return;                             // in KISS mode the host is the station
    // the info field starts after the last address, the control and PID bytes
    uint16_t i = 0;
    while (i + ADDR_LEN <= len && !(frame[i + ADDR_LEN - 1] & 1)) i += ADDR_LEN;
    i += ADDR_LEN + 2;
    if (i >= len || len < 2 * ADDR_LEN)
        return;

    char from[MSG_CALL_MAX + 1];
    SourceText(frame + ADDR_LEN, from);
    const char *info = (const char *)frame + i;
    uint16_t n = (uint16_t)(len - i);

    // third party (from APRS-IS via an IGate): "}SRC>DST,PATH:info"
    if (n && info[0] == '}') {
        const char *gt = memchr(info, '>', n), *colon = memchr(info, ':', n);
        if (!gt || !colon || gt > colon || gt - info - 1 > MSG_CALL_MAX)
            return;
        memcpy(from, info + 1, (size_t)(gt - info - 1));
        from[gt - info - 1] = '\0';
        n    = (uint16_t)(n - (colon + 1 - info));
        info = colon + 1;
    }

    char mycall[MSG_CALL_MAX + 1], ack_id[6];
    MyCall(mycall);
    MSG_HandleInfo(from, info, n, mycall, now_s, ack_id);
    const MsgQuery query = MSG_TakeQuery();
#ifdef ENABLE_TX
    if (ack_id[0]) {                        // every copy is acked: the last ack may have been lost
        char ack[9] = "ack";
        strcpy(ack + 3, ack_id);
        Transmit(from, ack);
    }
    if (query != MSG_QUERY_NONE)
        AnswerQuery(query, from, frame, len);
#else
    (void)query;
#endif
}

#endif  // MSG_HOST_TEST
