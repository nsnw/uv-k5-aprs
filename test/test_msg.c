// Host test for msg.c: addressee matching, message numbers, reply-acks,
// acks to us, duplicates (retries) and the inbox.
//   cc -DMSG_HOST_TEST -I src -o build/test_msg test/test_msg.c src/msg.c && build/test_msg

#include <stdio.h>
#include <string.h>

#include "msg.h"

// Transmitter stub: records the last frame's text; can be told to fail.
static char sTxTo[16], sTxText[96];
static int  sTxCount;
static bool sTxFail;
bool MSG_TestTransmit(const char *to, const char *text)
{
    if (sTxFail) return false;
    strcpy(sTxTo, to); strcpy(sTxText, text); sTxCount++;
    return true;
}

static int sPass, sFail;
#define CHECK(c, ...) do { if (c) sPass++; else { sFail++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static bool Feed(const char *from, const char *info, uint32_t t, char *ack)
{
    return MSG_HandleInfo(from, info, (uint16_t)strlen(info), "VE6CXZ-1", t, ack);
}

int main(void)
{
    char ack[6];

    CHECK(Feed("VE6HUE-7", ":VE6CXZ-1 :hello there{42", 100, ack), "message stored");
    CHECK(strcmp(ack, "42") == 0, "ack id 42 (got '%s')", ack);
    CHECK(MSG_Count() == 1 && MSG_Unread() == 1, "one unread");
    CHECK(strcmp(MSG_Get(0)->call, "VE6HUE-7") == 0 && strcmp(MSG_Get(0)->text, "hello there") == 0,
          "from / text (got '%s' / '%s')", MSG_Get(0)->call, MSG_Get(0)->text);
    CHECK(MSG_TakeNew() && !MSG_TakeNew(), "new flag once");

    // the sender retries: ack again, don't store again
    CHECK(!Feed("VE6HUE-7", ":VE6CXZ-1 :hello there{42", 130, ack) && strcmp(ack, "42") == 0, "retry re-acked");
    CHECK(MSG_Count() == 1, "retry not stored twice");
    CHECK(!Feed("VE6HUE-7", ":VE6CXZ-1 :hello there{42", 132, ack) && ack[0] == 0, "digipeated copy 2 s later: no second ack");

    // not for us / for us without SSID
    CHECK(!Feed("VE6HUE-7", ":VE6CXZ   :hi{43", 140, ack) && ack[0] == 0, "other SSID ignored, no ack");
    CHECK(!Feed("VE6HUE-7", ":VE6CXZ-11:hi{44", 140, ack) && ack[0] == 0, "VE6CXZ-11 ignored");
    // lower-case addressee still matches
    CHECK(Feed("W1AW", ":ve6cxz-1 :case{45", 150, ack) && strcmp(ack, "45") == 0, "lower-case addressee");

    // no message number: stored, nothing to ack; repeat of the same text is a dupe
    CHECK(Feed("W1AW", ":VE6CXZ-1 :no id here", 160, ack) && ack[0] == 0, "no id: stored, no ack");
    CHECK(!Feed("W1AW", ":VE6CXZ-1 :no id here", 170, ack), "no-id repeat ignored");

    // reply-ack: "{MM}AA" acks MM
    CHECK(Feed("K1ABC", ":VE6CXZ-1 :replyack{AB}CD", 180, ack) && strcmp(ack, "AB") == 0, "reply-ack id (got '%s')", ack);
    CHECK(strcmp(MSG_Get(0)->text, "replyack") == 0, "reply-ack text");

    // acks / rejects to us aren't messages
    CHECK(!Feed("VE6HUE-7", ":VE6CXZ-1 :ack12", 190, ack) && ack[0] == 0, "ack not stored");
    CHECK(!Feed("VE6HUE-7", ":VE6CXZ-1 :rej12", 190, ack) && ack[0] == 0, "rej not stored");

    // not a message at all
    CHECK(!Feed("VE6HUE-7", "!5102.34N/11404.86W[", 200, ack), "position ignored");
    CHECK(!Feed("VE6HUE-7", ":VE6CXZ-1", 200, ack), "too short ignored");

    // a retry after the dupe window counts as new
    CHECK(Feed("VE6HUE-7", ":VE6CXZ-1 :hello there{42", 100 + 31 * 60, ack), "old id after 30 min is new");

    // long text is cut at 67, control characters become '.'
    char longmsg[120] = ":VE6CXZ-1 :";
    for (int i = 0; i < 80; i++) strcat(longmsg, "x");
    CHECK(Feed("N0CALL", longmsg, 3000, ack) && strlen(MSG_Get(0)->text) == MSG_TEXT_MAX, "cut to 67");
    CHECK(Feed("N0CALL", ":VE6CXZ-1 :a\tb{9", 3010, ack) && strcmp(MSG_Get(0)->text, "a.b") == 0, "control char");

    // inbox: 8 max, newest first; delete and mark read
    for (int i = 0; i < 10; i++) {
        char m[40];
        snprintf(m, sizeof m, ":VE6CXZ-1 :fill %d{F%d", i, i);
        Feed("N0CALL", m, 4000 + (uint32_t)i, ack);
    }
    CHECK(MSG_Count() == MSG_INBOX && strcmp(MSG_Get(0)->text, "fill 9") == 0, "inbox capped, newest first");
    MSG_MarkRead(0);
    CHECK(!MSG_Get(0)->unread && MSG_Unread() == MSG_INBOX - 1, "mark read");
    MSG_Delete(0);
    CHECK(MSG_Count() == MSG_INBOX - 1 && strcmp(MSG_Get(0)->text, "fill 8") == 0, "delete");

    // ------------------------------------------------ sending
    while (MSG_Count()) MSG_Delete(0);
    sTxCount = 0;
    CHECK(MSG_Send("VE6HUE-7", "hi there", 5000), "send queued");
    CHECK(sTxCount == 1 && !strcmp(sTxTo, "VE6HUE-7") && !strcmp(sTxText, "hi there{1"), "first copy '%s'", sTxText);
    CHECK(MSG_Get(0)->state == MSG_SENDING && MSG_Get(0)->tries == 1 && !MSG_Get(0)->unread, "sending");
    MSG_Poll1s(5000 + MSG_RETRY_S - 1);
    CHECK(sTxCount == 1, "no retry before %ds", MSG_RETRY_S);
    MSG_Poll1s(5000 + MSG_RETRY_S);
    CHECK(sTxCount == 2 && MSG_Get(0)->tries == 2 && MSG_TakeChanged(), "retry");
    // an ack from someone else doesn't count; from them it does
    Feed("W1AW", ":VE6CXZ-1 :ack1", 5040, ack);
    CHECK(MSG_Get(0)->state == MSG_SENDING, "ack from another station ignored");
    Feed("VE6HUE-7", ":VE6CXZ-1 :ack1", 5045, ack);
    CHECK(MSG_Get(0)->state == MSG_DELIVERED && ack[0] == 0, "delivered");
    MSG_Poll1s(5000 + 10 * MSG_RETRY_S);
    CHECK(sTxCount == 2, "no more retries once delivered");

    // no ack: fails after MSG_TRIES copies
    CHECK(MSG_Send("K1ABC", "anyone?", 6000), "second message");
    CHECK(!strcmp(sTxText, "anyone?{2"), "ids count up ('%s')", sTxText);
    for (uint32_t t = 6000; t < 6000 + (MSG_TRIES + 2) * MSG_RETRY_S; t++) MSG_Poll1s(t);
    CHECK(MSG_Get(0)->state == MSG_FAILED && MSG_Get(0)->tries == MSG_TRIES, "failed after %d tries (%d)", MSG_TRIES, MSG_Get(0)->tries);

    // reject, and a reply-ack that acks our message
    MSG_Send("N0CALL", "nope", 7000);
    Feed("N0CALL", ":VE6CXZ-1 :rej3", 7005, ack);
    CHECK(MSG_Get(0)->state == MSG_REJECTED, "rejected");
    MSG_Send("N0CALL", "again", 7100);
    CHECK(Feed("N0CALL", ":VE6CXZ-1 :sure{77}4", 7110, ack) && !strcmp(ack, "77"), "reply-ack message stored, acked 77");
    CHECK(MSG_Get(1)->state == MSG_DELIVERED, "reply-ack delivered our 4");

    // can't transmit: nothing stored
    sTxFail = true;
    const uint8_t before = MSG_Count();
    CHECK(!MSG_Send("N0CALL", "x", 7200) && MSG_Count() == before, "send refused, not stored");
    sTxFail = false;

    printf("%d passed, %d failed\n", sPass, sFail);
    return sFail != 0;
}
