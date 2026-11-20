#include <string.h>

#include "app.h"
#include "aprs.h"
#include "driver/st7565.h"
#include "gfx.h"
#include "inbox.h"
#include "msg.h"
#include "textedit.h"

#define ROWS    7                       // pages 1..7, shifted one pixel down
#define CELL_W  (GFX_CHAR_W + 1)
#define COLS    (LCD_WIDTH / CELL_W)

typedef enum {
    V_LIST,                             // row 0 "New message", then the messages
    V_READ,                             // one message
    V_TO,                               // composing: the callsign
    V_TEXT,                             // composing: the text
} View;

static bool    sOpen;
static View    sView;
static uint8_t sSel, sTop;              // list row (0 = New message)
static bool    sExitDown;               // EXIT acts on release, so a hold can delete
static char    sTo[MSG_CALL_MAX + 1];   // compose: fixed width, ' ' padded while editing
static char    sText[MSG_TEXT_MAX + 1];

static const AprsMessage *Selected(void) { return sSel ? MSG_Get((uint8_t)(sSel - 1)) : NULL; }

void INBOX_Open(void)
{
    sOpen     = true;
    sView     = V_LIST;
    sSel      = MSG_Unread() ? 1 : 0;   // newest first: an unread one is at the top
    sExitDown = false;
    APP_Redraw();
}

bool INBOX_IsOpen(void) { return sOpen; }

// ---------------------------------------------------------------- compose

// "VE6HUE-7": 1-6 letters/digits with at least one of each, optional -SSID 0-15.
static bool ValidCall(char *c)          // trims the padding
{
    uint8_t n = MSG_CALL_MAX;
    while (n && c[n - 1] == ' ') n--;
    c[n] = '\0';
    const char *dash = strchr(c, '-');
    const uint8_t base = dash ? (uint8_t)(dash - c) : n;
    bool letter = false, digit = false;
    if (base < 3 || base > 6) return false;
    for (uint8_t i = 0; i < base; i++) {
        if (c[i] >= 'A' && c[i] <= 'Z')      letter = true;
        else if (c[i] >= '0' && c[i] <= '9') digit = true;
        else return false;
    }
    if (dash) {
        const char *s = dash + 1;
        if (!*s || strlen(s) > 2) return false;
        uint8_t v = 0;
        for (; *s; s++) {
            if (*s < '0' || *s > '9') return false;
            v = (uint8_t)(v * 10 + (*s - '0'));
        }
        if (v < 1 || v > 15) return false;
    }
    return letter && digit;
}

static void ComposeTo(const char *call)  // NULL: ask for the callsign first
{
    sText[0] = '\0';
    if (call) {
        strncpy(sTo, call, MSG_CALL_MAX);
        sTo[MSG_CALL_MAX] = '\0';
        TE_Begin(sText, MSG_TEXT_MAX, TE_TEXT, false);
        sView = V_TEXT;
    } else {
        memset(sTo, ' ', MSG_CALL_MAX);
        sTo[MSG_CALL_MAX] = '\0';
        TE_Begin(sTo, MSG_CALL_MAX, TE_NAME, false);
        sView = V_TO;
    }
}

static void Send(void)
{
    uint8_t n = (uint8_t)strlen(sText);
    while (n && sText[n - 1] == ' ') sText[--n] = '\0';
    if (n == 0) {
        APP_ShowMessage("NO TEXT", false);
        return;
    }
    if (!MSG_Send(sTo, sText, APRS_NowSeconds())) {
        APP_ShowMessage("CAN'T SEND HERE", false);   // no callsign, not a TX freq, queue full
        return;
    }
    sView = V_LIST;
    sSel  = 1;                                       // the new message, at the top
}

static void ComposeKey(KeyEvent_t ev)
{
    switch (TE_Key(ev)) {
    case TE_CANCEL:
        sView = V_LIST;
        break;
    case TE_DONE:
        if (sView == V_TEXT) {
            Send();
        } else if (ValidCall(sTo)) {
            TE_Begin(sText, MSG_TEXT_MAX, TE_TEXT, false);
            sView = V_TEXT;
        } else {
            for (uint8_t i = (uint8_t)strlen(sTo); i < MSG_CALL_MAX; i++) sTo[i] = ' ';
            APP_ShowMessage("BAD CALLSIGN", false);
        }
        break;
    default:
        break;
    }
    APP_Redraw();
}

void INBOX_Tick10ms(void)
{
    if (sOpen && (sView == V_TO || sView == V_TEXT)) {
        const uint8_t cursor = TE_Cursor();
        TE_Tick10ms();
        if (TE_Cursor() != cursor)
            APP_Redraw();
    }
}

// ---------------------------------------------------------------- keys

void INBOX_HandleKey(KeyEvent_t ev)
{
    if (sView == V_TO || sView == V_TEXT) {
        ComposeKey(ev);
        return;
    }
    const bool press  = ev.type == KEV_PRESS;
    const bool repeat = ev.type == KEV_LONG || ev.type == KEV_REPEAT;
    const uint8_t rows = (uint8_t)(MSG_Count() + 1);
    const AprsMessage *m = Selected();

    switch (ev.key) {
    case KEY_UP:
    case KEY_DOWN:
        if (sView != V_LIST || (!press && !repeat)) return;
        if (ev.key == KEY_DOWN && sSel + 1 < rows) sSel++;
        if (ev.key == KEY_UP && sSel > 0)          sSel--;
        break;
    case KEY_MENU:
        if (!press) return;
        if (sView == V_LIST && sSel == 0) {
            ComposeTo(NULL);
        } else if (sView == V_LIST && m) {
            sView = V_READ;
            MSG_MarkRead((uint8_t)(sSel - 1));
        } else if (sView == V_READ && m) {
            if (m->state == MSG_FAILED || m->state == MSG_REJECTED) {   // send it again
                strncpy(sTo, m->call, MSG_CALL_MAX);
                strcpy(sText, m->text);
                Send();
            } else {
                ComposeTo(m->call);                                     // reply
            }
        }
        break;
    case KEY_EXIT:
        if (press) { sExitDown = true; return; }
        if (!sExitDown || (ev.type != KEV_LONG && ev.type != KEV_RELEASE)) return;
        sExitDown = false;
        if (ev.type == KEV_LONG && sView == V_READ && m) {             // delete this message
            MSG_Delete((uint8_t)(sSel - 1));
            if (sSel > MSG_Count()) sSel--;
            sView = V_LIST;
            APP_ShowMessage("DELETED", false);
        } else if (ev.type == KEV_RELEASE) {
            if (sView == V_READ) sView = V_LIST;
            else                 sOpen = false;
        }
        break;
    default:
        return;
    }
    APP_Redraw();
}

// ---------------------------------------------------------------- drawing

static void FormatAge(uint32_t s, char *out)     // "12s", "5m", "3h", "2d"
{
    char unit = 's';
    if (s >= 86400)     { s /= 86400; unit = 'd'; }
    else if (s >= 3600) { s /= 3600;  unit = 'h'; }
    else if (s >= 60)   { s /= 60;    unit = 'm'; }
    uint8_t k = 0;
    if (s >= 10) out[k++] = (char)('0' + s / 10 % 10);
    out[k++] = (char)('0' + s % 10);
    out[k++] = unit;
    out[k]   = '\0';
}

// List mark: * unread, > sending, + delivered, x no ack / rejected.
static char Mark(const AprsMessage *m)
{
    switch (m->state) {
    case MSG_IN:        return m->unread ? '*' : ' ';
    case MSG_SENDING:   return '>';
    case MSG_DELIVERED: return '+';
    default:            return 'x';
    }
}

static void RenderList(void)
{
    const uint8_t rows = (uint8_t)(MSG_Count() + 1);
    if (sSel >= rows)          sSel = (uint8_t)(rows - 1);
    if (sSel < sTop)           sTop = sSel;
    if (sSel >= sTop + ROWS)   sTop = (uint8_t)(sSel - ROWS + 1);
    char line[COLS + 1];
    for (uint8_t r = 0; r < ROWS && sTop + r < rows; r++) {
        const uint8_t row = (uint8_t)(sTop + r), page = (uint8_t)(1 + r);
        if (row == 0) {
            strcpy(line, " New message");
        } else {
            // "*VE6HUE hello the": mark, the other station, the start of the text
            const AprsMessage *m = MSG_Get((uint8_t)(row - 1));
            uint8_t k = 0;
            line[k++] = Mark(m);
            for (const char *p = m->call; *p && k < COLS; p++) line[k++] = *p;
            if (k < COLS) line[k++] = ' ';
            for (const char *p = m->text; *p && k < COLS; p++) line[k++] = *p;
            line[k] = '\0';
        }
        GFX_Text(1, page, line);
        if (row == sSel)
            GFX_InvertPage(page, 0, LCD_WIDTH - 1);
    }
}

static void RenderMessage(const AprsMessage *m)
{
    char head[COLS + 1], age[8];
    const bool out = m->state != MSG_IN;
    strcpy(head, out ? "To " : "");
    strcat(head, m->call);
    if (m->old) strcpy(age, "old");                 // from before power-off
    else        FormatAge(APRS_NowSeconds() - m->time_s, age);
    GFX_Text(1, 1, head);
    GFX_TextRight(LCD_WIDTH - 1, 1, age);
    GFX_InvertPage(1, 0, LCD_WIDTH - 1);

    char line[COLS + 1];
    const char *t = m->text;
    for (uint8_t page = 2; page <= 6 && *t; page++) {
        uint8_t len = (uint8_t)strlen(t);   // wrap at a space where possible
        if (len > COLS) {
            len = COLS;
            for (uint8_t i = COLS; i > COLS / 2; i--)
                if (t[i] == ' ') { len = i; break; }
        }
        memcpy(line, t, len);
        line[len] = '\0';
        GFX_Text(1, page, line);
        t += len;
        while (*t == ' ') t++;
    }

    const char *status;
    char sending[16];
    switch (m->state) {
    case MSG_IN:        status = GFX_KEY_MENU " REPLY"; break;
    case MSG_SENDING:
        strcpy(sending, "SENDING ");
        sending[8]  = (char)('0' + m->tries);
        sending[9]  = '/';
        sending[10] = (char)('0' + MSG_TRIES);
        sending[11] = '\0';
        status = sending;
        break;
    case MSG_DELIVERED: status = "DELIVERED"; break;
    case MSG_REJECTED:  status = "REJECTED " GFX_KEY_MENU " SEND"; break;
    default:            status = "NO ACK " GFX_KEY_MENU " RESEND"; break;
    }
    GFX_Text(1, 7, status);
}

static void RenderTo(void)
{
    char field[MSG_CALL_MAX + 1];
    TE_FormatFixed(field);
    GFX_Text(1, 2, "Send to:");
    const uint8_t x = 1 + 9 * CELL_W;
    GFX_Text(x, 2, field);
    const uint8_t cx = (uint8_t)(x + TE_Cursor() * CELL_W);
    GFX_InvertPage(2, cx - 1, (uint8_t)(cx + GFX_CHAR_W - 1));
    GFX_Text(1, 4, "CALL-SSID, 1 = -");
    GFX_Text(1, 6, GFX_KEY_MENU " NEXT");
    GFX_Text(1, 7, "hold " GFX_KEY_EXIT " CANCEL");
}

void INBOX_Render(void)
{
    const AprsMessage *m = Selected();
    if (sView == V_TEXT) {
        char label[COLS + 1] = "To ";
        strcat(label, sTo);
        TE_RenderText(label);
    } else if (sView == V_TO) {
        RenderTo();
    } else if (sView == V_READ && m) {
        RenderMessage(m);
    } else {
        RenderList();
    }
    GFX_ShiftDown1(1);                            // clear of the status bar's divider
}
