#include <string.h>

#include "app.h"
#include "aprs.h"
#include "driver/st7565.h"
#include "gfx.h"
#include "inbox.h"
#include "msg.h"
#include "textedit.h"

#define HEAD    APP_TOP_PAGE            // the header; the body under it (gfx.h GFX_Divide)
#define BODY    (HEAD + 1)
#define ROWS    (GFX_LAST_BODY_PAGE - HEAD) // list rows
#define TEXT_ROWS ROWS                  // a message's text
#define CELL_W  (GFX_CHAR_W + 1)
#define COLS    (LCD_WIDTH / CELL_W)

_Static_assert(TEXT_ROWS * COLS >= MSG_TEXT_MAX, "a message must fit, wrapped anywhere");

typedef enum {
    V_LIST,                             // row 0 "New message", then the messages
    V_READ,                             // one message
    V_TO,                               // composing: the callsign
    V_TEXT,                             // composing: the text
} View;

static bool    sOpen;
static View    sView;
static uint8_t sSel, sTop;              // list row: the message, newest first
static bool    sExitDown;               // EXIT acts on release, so a hold can delete
static char    sTo[MSG_CALL_MAX + 1];   // compose: fixed width, ' ' padded while editing
static char    sText[MSG_TEXT_MAX + 1];

static const AprsMessage *Selected(void) { return MSG_Get(sSel); }

void INBOX_Open(void)
{
    sOpen     = true;
    sView     = V_LIST;
    sSel      = 0;                      // newest first: an unread one is at the top
    sExitDown = false;
    APP_Redraw();
}

bool INBOX_IsOpen(void) { return sOpen; }
void INBOX_Close(void)  { sOpen = false; }
bool INBOX_IsComposing(void) { return sOpen && (sView == V_TO || sView == V_TEXT); }

// ---------------------------------------------------------------- compose

// Trims the editor's padding, then checks it as an addressee (msg.h).
static bool ValidCall(char *c)
{
    uint8_t n = MSG_CALL_MAX;
    while (n && c[n - 1] == ' ') n--;
    c[n] = '\0';
    return MSG_ValidAddressee(c);
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
    sSel  = 0;                                       // the new message, at the top
}

void INBOX_Compose(void)
{
    INBOX_ComposeTo(NULL);
}

void INBOX_ComposeTo(const char *call)
{
    if (sOpen && sView != V_TO && sView != V_TEXT) {
        ComposeTo(call);
        APP_Redraw();
    }
}

static void EditTo(void)                // back to the callsign, padded for the editor
{
    for (uint8_t i = (uint8_t)strlen(sTo); i < MSG_CALL_MAX; i++) sTo[i] = ' ';
    sTo[MSG_CALL_MAX] = '\0';
    TE_Begin(sTo, MSG_CALL_MAX, TE_NAME, false);
    sView = V_TO;
}

static void ComposeKey(KeyEvent_t ev)
{
    // EXIT with no text yet: back up to the callsign (a reply starts in the text)
    if (sView == V_TEXT && !sText[0] && ev.key == KEY_EXIT && ev.type == KEV_PRESS) {
        EditTo();
        APP_Redraw();
        return;
    }
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
        } else {                                // stay on it, padded again for the editor
            for (uint8_t i = (uint8_t)strlen(sTo); i < MSG_CALL_MAX; i++) sTo[i] = ' ';
            APP_ShowMessage("BAD ADDRESSEE", false);
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
    const uint8_t rows = MSG_Count();
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
        if (sView == V_LIST && m) {
            sView = V_READ;
            MSG_MarkRead(sSel);
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
            MSG_Delete(sSel);
            if (sSel && sSel >= MSG_Count()) sSel--;
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
    GFX_TextBold(1, HEAD, "Messages");
    GFX_TextRightBold(LCD_WIDTH - 1, HEAD, GFX_KEY_F " NEW");
    const uint8_t rows = MSG_Count();
    if (!rows) {
        GFX_Text(19, BODY + 1, "no messages");
        return;
    }
    if (sSel >= rows)          sSel = (uint8_t)(rows - 1);
    if (sSel < sTop)           sTop = sSel;
    if (sSel >= sTop + ROWS)   sTop = (uint8_t)(sSel - ROWS + 1);
    char line[COLS + 1];
    for (uint8_t r = 0; r < ROWS && sTop + r < rows; r++) {
        const uint8_t row = (uint8_t)(sTop + r), page = (uint8_t)(BODY + r);
        // "*VE6HUE hello the": mark, the other station (bold), the start of the text
        const AprsMessage *m = MSG_Get(row);
        const char mark[2] = { Mark(m), '\0' };
        uint8_t x = GFX_Text(1, page, mark);
        x = GFX_TextBold(x, page, m->call);
        const uint8_t room = (uint8_t)((LCD_WIDTH - x) / CELL_W);
        uint8_t k = 0;
        line[k++] = ' ';
        for (const char *p = m->text; *p && k < room && k < COLS; p++) line[k++] = *p;
        line[k] = '\0';
        GFX_Text(x, page, line);
        if (row == sSel)
            GFX_InvertPage(page, 0, LCD_WIDTH - 1);
    }
}

// Length of the next line of `t`: up to COLS characters, ending at a space
// if `at_spaces` and there is one in the second half.
static uint8_t WrapLen(const char *t, bool at_spaces)
{
    uint8_t len = (uint8_t)strlen(t);
    if (len > COLS) {
        len = COLS;
        if (at_spaces)
            for (uint8_t i = COLS; i > COLS / 2; i--)
                if (t[i] == ' ') { len = i; break; }
    }
    return len;
}

static uint8_t WrapLines(const char *t, bool at_spaces)
{
    uint8_t n = 0;
    while (*t) {
        t += WrapLen(t, at_spaces);
        while (*t == ' ') t++;
        n++;
    }
    return n;
}

// One message (divided, gfx.h). Header: the other station (with the list's
// mark if we sent it) and what MENU does. Body: the text, then on the last
// row, if the text leaves it free, the status and the age.
static void RenderMessage(const AprsMessage *m)
{
    char head[COLS + 1], age[8];
    uint8_t k = 0;
    if (m->state != MSG_IN)
        head[k++] = Mark(m);                        // > sending, + delivered, x not
    strcpy(head + k, m->call);
    GFX_TextBold(1, HEAD, head);
    const bool retry = m->state == MSG_FAILED || m->state == MSG_REJECTED;
    GFX_TextRightBold(LCD_WIDTH - 1, HEAD, retry ? GFX_KEY_MENU " RETRY" : GFX_KEY_MENU " REPLY");

    // Wrapped at spaces where possible; if that takes more than TEXT_ROWS
    // lines, wrapped anywhere (TEXT_ROWS x COLS holds MSG_TEXT_MAX).
    const bool at_spaces = WrapLines(m->text, true) <= TEXT_ROWS;
    const uint8_t lines = WrapLines(m->text, at_spaces);
    char line[COLS + 1];
    const char *t = m->text;
    for (uint8_t page = BODY; page < BODY + TEXT_ROWS && *t; page++) {
        const uint8_t len = WrapLen(t, at_spaces);
        memcpy(line, t, len);
        line[len] = '\0';
        GFX_Text(1, page, line);
        t += len;
        while (*t == ' ') t++;
    }
    if (lines >= TEXT_ROWS)
        return;

    const char *status;
    char sending[16];
    switch (m->state) {
    case MSG_IN:        status = "received"; break;
    case MSG_SENDING:
        strcpy(sending, "SENDING ");
        sending[8]  = (char)('0' + m->tries);
        sending[9]  = '/';
        sending[10] = (char)('0' + MSG_TRIES);
        sending[11] = '\0';
        status = sending;
        break;
    case MSG_DELIVERED: status = "DELIVERED"; break;
    case MSG_REJECTED:  status = "REJECTED"; break;
    default:            status = "NO ACK"; break;
    }
    if (m->old) strcpy(age, "old");                 // from before power-off
    else        FormatAge(APRS_NowSeconds() - m->time_s, age);
    GFX_Text(1, GFX_LAST_BODY_PAGE, status);
    GFX_TextRight(LCD_WIDTH - 1, GFX_LAST_BODY_PAGE, age);
}

// Writing a message, one screen: "To" and the callsign (CALL-SSID, one
// field) with the length on page 1, the text on pages 2-5, hints on 6-7.
// The cursor is in whichever is being edited.
static void RenderCompose(void)
{
    char buf[MSG_CALL_MAX + 1];
    const uint8_t x = 1 + 3 * CELL_W;
    GFX_Text(1, 1, "To");
    if (sView == V_TO) {
        TE_FormatFixed(buf);
        GFX_Text(x, 1, buf);
        const uint8_t cx = (uint8_t)(x + TE_Cursor() * CELL_W);
        GFX_InvertPage(1, cx - 1, (uint8_t)(cx + GFX_CHAR_W - 1));
        TE_DrawWrapped(sText, 2, 4);
        TE_DrawHints(6, "CALL-SSID  1 = -", "NEXT");
    } else {
        char count[8];
        GFX_Text(x, 1, sTo);
        TE_FormatCount(count);
        GFX_TextRight(LCD_WIDTH - 1, 1, count);
        TE_DrawText(2, 4);
        TE_DrawHints(6, NULL, "SEND");
    }
}

void INBOX_Render(void)
{
    const AprsMessage *m = Selected();
    if (sView == V_TO || sView == V_TEXT) {
        RenderCompose();
    } else if (sView == V_READ && m) {
        RenderMessage(m);
        GFX_Divide(HEAD);
        return;
    } else {
        RenderList();
        GFX_Divide(HEAD);
        return;
    }
    GFX_ShiftDown1(1);                            // the editors: clear of the status bar's divider
}
