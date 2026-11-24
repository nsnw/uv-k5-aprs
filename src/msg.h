#ifndef MSG_H
#define MSG_H

#include <stdbool.h>
#include <stdint.h>

// APRS messages addressed to MYCALL (beacon.h): kept in a small inbox in RAM
// and acknowledged automatically, so the sender's radio shows "delivered".
// Messages we send go in the same list; each is retried every MSG_RETRY_S
// until the other station acks (or rejects) it, up to MSG_TRIES times.
// Repeats of a message the sender retries (it hasn't heard our ack yet) are
// acked again but not stored twice. Messages relayed from APRS-IS by an IGate
// (third-party "}" packets) are handled the same way.
//
// Directed queries (APRS101 ch. 15) sent as messages are answered instead of
// stored: ?APRSP our position (if one is set), ?APRSS our status report
// (beacon.h), ?APRST / ?PING? the path the query took, ?APRSD the stations we
// hear direct. The inbox is kept in EEPROM (store.h).

#define MSG_INBOX     8
#define MSG_TEXT_MAX  67           // APRS limit
#define MSG_CALL_MAX  9            // "VE6CXZ-15"
#define MSG_RETRY_S   30
#define MSG_TRIES     5

typedef enum {
    MSG_IN,                        // received
    MSG_SENDING,                   // sent, waiting for the ack
    MSG_DELIVERED,
    MSG_REJECTED,
    MSG_FAILED,                    // no ack after MSG_TRIES
} MsgState;

typedef struct {
    char     call[MSG_CALL_MAX + 1];   // the other station: sender, or who we sent it to
    char     text[MSG_TEXT_MAX + 1];
    char     id[6];                // our message number (sent messages)
    uint32_t time_s;               // APRS_NowSeconds(): heard / first sent
    uint32_t next_s;               // next retry
    uint8_t  state;                // MsgState
    uint8_t  tries;
    bool     unread;
    bool     old;                  // restored at boot: heard / sent before power-off (age unknown)
} AprsMessage;

typedef enum {
    MSG_QUERY_NONE,
    MSG_QUERY_POSITION,            // ?APRSP
    MSG_QUERY_STATUS,              // ?APRSS
    MSG_QUERY_TRACE,               // ?APRST, ?PING?
    MSG_QUERY_DIRECT,              // ?APRSD
} MsgQuery;

#define MSG_QUERY_GAP_S  10        // answer at most one query this often

// From the APRS receiver, for every decoded frame (FCS stripped).
void MSG_OnFrame(const uint8_t *frame, uint16_t len, uint32_t now_s);

uint8_t MSG_Count(void);
uint8_t MSG_Unread(void);
const AprsMessage *MSG_Get(uint8_t i);   // 0 = newest; NULL past the end
void MSG_MarkRead(uint8_t i);
void MSG_Delete(uint8_t i);
bool MSG_TakeNew(void);                  // true once per newly stored message

// Who a message can go to: 1-9 of A-Z, 0-9 and '-', not starting or
// ending with '-'. A station ("VE6HUE-7") or a service without a digit or
// a numeric SSID ("RFDATA", "ANSRVR", "EMAIL-2", "WHO-IS").
bool MSG_ValidAddressee(const char *to);

// Sends `text` to `to` (queued for the transmitter; retried until acked).
// False if there's nowhere to send from: no callsign, not a TX frequency or
// the TX queue is full.
bool MSG_Send(const char *to, const char *text, uint32_t now_s);
void MSG_Poll1s(uint32_t now_s);         // retries
bool MSG_TakeChanged(void);              // a sent message changed state (redraw)

// The info-field logic, exposed for the host test. `info` is the APRS
// information field (after any third-party header); returns true if it was
// a new message for `mycall` (stored). *ack_id gets the message number to
// acknowledge ("" if none: nothing to ack). Acks and rejects of our messages
// update their state here.
bool MSG_HandleInfo(const char *from, const char *info, uint16_t n, const char *mycall,
                    uint32_t now_s, char ack_id[6]);

// The query in the last message MSG_HandleInfo saw (MSG_QUERY_NONE if it
// wasn't one, was a retry, or came within MSG_QUERY_GAP_S of the last answer).
MsgQuery MSG_TakeQuery(void);

// For store.h: the inbox table (newest first) and its count, and the fix-up
// for an entry read back from EEPROM (false: drop it).
AprsMessage *MSG_Table(uint8_t **count);
bool MSG_RestoreMessage(AprsMessage *m);

#endif
