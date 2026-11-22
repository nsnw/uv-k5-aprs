// Screenshots: renders the K5-APRS screens on the host. app.c (included, so
// the harness can set its view state), menu.c, inbox.c, gfx.c and font.c run
// unchanged against stubbed hardware, with example stations and messages
// loaded; each screen's framebuffer (8 pages x 128 bytes, LSB at the top) is
// written to <out>/<name>.fb. test/screens.sh builds and runs it, and
// test/screens_png.py turns the dumps into PNGs that look like the LCD.

#include <stdio.h>
#include <stdlib.h>
#include "app.c"

// ------------------------------------------------------------ hardware stubs
uint8_t gFrameBuffer[LCD_PAGES][LCD_WIDTH];
static uint8_t sShown[LCD_PAGES][LCD_WIDTH];
void ST7565_Blit(void) { memcpy(sShown, gFrameBuffer, sizeof sShown); }
void SYSTICK_DelayUs(uint32_t us) { (void)us; }
void BOARD_SetBacklight(uint8_t l) { (void)l; }
void BK4819_WriteRegister(BK4819_REGISTER_t r, uint16_t v) { (void)r; (void)v; }
void BK4819_ToggleGpioOut(BK4819_GPIO_PIN_t p, bool on) { (void)p; (void)on; }
bool RADIO_IsAudioOpen(void) { return false; }
static uint8_t sRom[8192];
void EEPROM_ReadBuffer(uint32_t a, void *p, uint32_t n) { memcpy(p, sRom + a, n); }
void EEPROM_WriteBuffer(uint32_t a, const void *p) { memcpy(sRom + a, p, 8); }

Settings_t gSettings = { .frequency = 14439000, .backlight = 255 };
void SETTINGS_Save(void) {}
bool RADIO_IsTunable(uint32_t f) { return f >= 14400000 && f <= 14800000; }
bool RADIO_IsValidFrequency(uint32_t f) { return RADIO_IsTunable(f); }
void RADIO_Apply(void) {}
void RADIO_Poll(void) {}
void RADIO_SetMonitor(bool on) { (void)on; }
bool RADIO_IsMonitoring(void) { return false; }
int16_t RADIO_GetRSSI_dBm(void) { return sRssi_dBm; }
static bool sRx;
bool APRS_IsReceiving(void) { return sRx; }
static uint32_t sNow = 100000;
uint32_t APRS_NowSeconds(void) { return sNow; }
void APRS_Enter(void) {}
void APRS_Tick10ms(void) {}
bool APRS_TakeNewPacket(void) { return false; }
AprsRxKind APRS_LastRxKind(void) { return APRS_RX_PACKET; }
static StationConfig sS = { .magic = STATION_MAGIC, .txdelay_flags = 40, .txtail_flags = 3,
                            .flags = STATION_DIGI, .digi_max_hops = 2, .beacon_interval_min = 30,
                            .rx_beep = STATION_RX_BEEP_NEW };
const StationConfig *STATION_Get(void) { return &sS; }
bool STATION_Set(const StationConfig *c) { sS = *c; return true; }
bool STATION_Toggle(uint8_t f) { sS.flags ^= f; return sS.flags & f; }
uint16_t BATTERY_Get10mV(void) { return 792; }
static uint16_t sDecoded;
uint16_t APRS_TotalDecoded(void) { return sDecoded; }
static const char *sLastLine = "";
static uint32_t sLastHeard_s;
const char *APRS_LastPacket(uint32_t *heard_s) { *heard_s = sLastHeard_s; return sLastLine; }
bool CHANNEL_IsBusy(void) { return sRx; }
TxStats gTxStats = { .attempts = 4 };
uint32_t BEACON_SecondsToNext(uint32_t now_s) { (void)now_s; return (sS.flags & STATION_AUTOBEACON) ? 754 : 0; }
void BATTERY_Sample(void) {}
void CHANNEL_ObserveRssi(int16_t d) { (void)d; }
static bool sKiss;
bool KISS_IsActive(void) { return sKiss; }
void KISS_Enter(void) { sKiss = true; }
void KISS_Exit(void) { sKiss = false; }
void KISS_Poll10ms(void) {}
void CONSOLE_Poll(void) {}
KeyEvent_t INPUT_Poll10ms(void) { return (KeyEvent_t){ KEV_NONE, 0 }; }
void TXQ_Poll10ms(void) {}
uint8_t TXQ_Count(void) { return 0; }
bool TXQ_TakeLocalSent(void) { return false; }
bool TX_IsAllowedFrequency(uint32_t f) { return f >= 14400000 && f <= 14800000; }
void BEEP_Message(void) {}
void BEEP_Sent(void) {}
void BEEP_Received(void) {}
void STORE_Poll1s(uint32_t s) { (void)s; }
void STORE_Poll10ms(bool b) { (void)b; }
void BEACON_Poll1s(uint32_t s) { (void)s; }
void BEACON_RestartTimer(void) {}
bool MSG_TestTransmit(const char *to, const char *text) { (void)to; (void)text; return true; }

static BeaconConfig sB = { .magic = BEACON_MAGIC, .call = "VE6CXZ", .ssid = 1, .path_hops = 2,
                           .symbol_table = '/', .symbol = '#',
                           .lat = 51 * 6000 + 234, .lon = -(114 * 6000 + 486),
                           .comment = "K5-APRS digi" };
static bool sConfigured = true;
bool BEACON_IsConfigured(void) { return sConfigured; }
bool BEACON_HasPosition(void) { return sB.lat || sB.lon; }
const BeaconConfig *BEACON_Get(void) { return &sB; }
bool BEACON_Set(const BeaconConfig *c) { sB = *c; return true; }
static BeaconStatus sBs = { .magic = BEACON_STATUS_MAGIC, .type = BEACON_TYPE_POSITION, .text = "Digi on the hill, 2m" };
const BeaconStatus *BEACON_GetStatus(void) { return &sBs; }
bool BEACON_SetStatus(const BeaconStatus *b) { sBs = *b; return true; }
BeaconResult BEACON_Send(void) { return BEACON_QUEUED; }

// ------------------------------------------------------------ sample data
static const char *sOut;

static void Save(const char *name)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s.fb", sOut, name);
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); exit(1); }
    fwrite(sShown, 1, sizeof sShown, f);
    fclose(f);
}

static void Shot(const char *name) { Render(); Save(name); }

static void Splashshot(const char *name)
{
    Splash();
    Save(name);
}

#define DM(d, m) ((d) * 6000 + (m))   // degrees, hundredths of a minute

static void Station(const char *call, const char *via, uint32_t ago, bool direct, bool old,
                    bool pos, int32_t lat, int32_t lon, char tab, char sym,
                    int16_t kmh, int16_t course, int32_t alt, const char *comment)
{
    uint8_t *count;
    AprsStation *t = APRS_StationTable(&count);
    AprsStation *st = &t[(*count)++];
    memset(st, 0, sizeof *st);
    strcpy(st->source, call);
    strcpy(st->via, via);
    st->heard_s = sNow - ago;
    st->flags = (uint8_t)((direct ? APRS_ST_DIRECT : 0) | (old ? APRS_ST_OLD : 0));
    st->packets = 3;
    st->info = (AprsInfo){ pos, lat, lon, tab, sym, kmh, course, alt, pos ? 'P' : 0 };
    strcpy(st->comment, comment);
}

static void Message(const char *call, const char *text, uint32_t ago, MsgState state, bool unread)
{
    uint8_t *count;
    AprsMessage *t = MSG_Table(&count);
    AprsMessage *m = &t[(*count)++];
    memset(m, 0, sizeof *m);
    strcpy(m->call, call);
    strcpy(m->text, text);
    m->time_s = sNow - ago;
    m->state = (uint8_t)state;
    m->unread = unread;
}

static void Press(KEY_Code_t k)   { HandleKey((KeyEvent_t){ KEV_PRESS, k }); HandleKey((KeyEvent_t){ KEV_RELEASE, k }); }
static void Settle(void)          { for (int i = 0; i < 200; i++) { MENU_Tick10ms(); INBOX_Tick10ms(); } }

int main(int argc, char **argv)
{
    sOut = argc > 1 ? argv[1] : ".";
    memset(sRom, 0xFF, sizeof sRom);
    MEM_Load();
    sRssi_dBm = -97;

    Splashshot("01-splash");

    // stations, most recently heard first
    Station("VE6LY-7",   "",       12,   true,  false, true, DM(51, 214), -DM(114, 1032), '/', '>',
            48, 271, 1068, "TH-D74 mobile");
    Station("VE6WRT",    "",       64,   true,  false, true, DM(51, 1190), -DM(114, 1610), '/', '#',
            APRS_UNKNOWN_I16, APRS_UNKNOWN_I16, 1290, "W2 digi, Calgary west");
    Station("VE6HUE-1",  "",       190,  true,  false, false, 0, 0, 0, 0,
            APRS_UNKNOWN_I16, APRS_UNKNOWN_I16, INT32_MIN, "K5-APRS 0.9.1");
    Station("VA6ABC-9",  "VE6WRT", 260,  false, false, true, DM(50, 4790), -DM(114, 220), '/', '>',
            96, 172, INT32_MIN, "");
    Station("VE6XYZ-5",  "VE6WRT", 1390, false, false, true, DM(51, 1960), -DM(113, 4410), '/', '-',
            APRS_UNKNOWN_I16, APRS_UNKNOWN_I16, INT32_MIN, "home QTH");
    Station("VE6CAL",    "",       0,    false, true,  true, DM(51, 480), -DM(114, 640), '/', '_',
            APRS_UNKNOWN_I16, APRS_UNKNOWN_I16, INT32_MIN, "WX station");

    sDecoded = 137;
    sLastLine = "VE6LY-7>APK004,VE6WRT*,WIDE2-1:=5102.14N/11410.32W>271/026/A=003504TH-D74 mobile";
    sLastHeard_s = sNow - 12;

    // ---- stations page (home)
    Shot("02-stations");
    sRx = true;
    Shot("03-stations-receiving");
    sRx = false;
    gSettings.list_by_distance = 1;
    Shot("04-stations-nearest");
    gSettings.list_by_distance = 0;
    Press(KEY_DOWN);
    Shot("05-stations-selected");
    Press(KEY_UP);
    strcpy(sAprsSel, "VE6LY-7");
    Press(KEY_MENU);
    Shot("06-detail-position");
    strcpy(sAprsSel, "VA6ABC-9");
    Shot("07-detail-via");
    strcpy(sAprsSel, "VE6HUE-1");
    Shot("08-detail-status");
    Press(KEY_F);                                // F in the details: a message to VE6HUE-1
    Shot("08a-detail-message");
    HandleKey((KeyEvent_t){ KEV_PRESS, KEY_EXIT }); HandleKey((KeyEvent_t){ KEV_LONG, KEY_EXIT });
    HandleKey((KeyEvent_t){ KEV_RELEASE, KEY_EXIT });   // hold EXIT: cancel, back to the details
    if (sPage != PAGE_STATIONS || !sAprsDetail) { fprintf(stderr, "not back in the details\n"); return 1; }
    Press(KEY_EXIT);
    sAprsSel[0] = '\0';

    // ---- messages page (SIDE2 from the station list), one unread
    Message("VE6LY-7", "Hi Andy, heard your beacon via the W2 digi. 73!", 40, MSG_IN, true);
    Message("VE6HUE-1", "test from radio 1", 900, MSG_DELIVERED, false);
    Message("VE6LY",    "?APRSS", 1800, MSG_FAILED, false);
    sNow |= 1;                                   // the envelope blinks: catch it on
    ShowMessageFor("MSG VE6LY-7", NEW_MSG_TICKS);
    Shot("09-new-message");
    sMessage = NULL;
    Press(KEY_SIDE2);
    Shot("10-messages");
    Press(KEY_MENU);
    Shot("11-message");
    Press(KEY_MENU);                             // reply: straight to the text
    Press(KEY_4); Press(KEY_4); Settle();        // H
    Press(KEY_4); Press(KEY_4); Press(KEY_4); Settle();   // I
    Press(KEY_0); Settle();                      // space
    Press(KEY_7); Press(KEY_7); Press(KEY_7); Press(KEY_7); Press(KEY_7); Settle();   // 7
    Press(KEY_3); Press(KEY_3); Press(KEY_3); Press(KEY_3); Settle();                 // 3
    Shot("11c-reply");
    HandleKey((KeyEvent_t){ KEV_PRESS, KEY_EXIT }); HandleKey((KeyEvent_t){ KEV_LONG, KEY_EXIT });
    HandleKey((KeyEvent_t){ KEV_RELEASE, KEY_EXIT });   // hold EXIT: cancel, back to the list
    Press(KEY_DOWN); Press(KEY_DOWN);            // the one that wasn't delivered
    Press(KEY_MENU);
    Shot("11b-message-no-ack");
    Press(KEY_EXIT);
    Press(KEY_UP); Press(KEY_UP);
    Press(KEY_F);                                // a new message: the callsign first
    Shot("11a-new-message");
    HandleKey((KeyEvent_t){ KEV_PRESS, KEY_EXIT }); HandleKey((KeyEvent_t){ KEV_LONG, KEY_EXIT });
    HandleKey((KeyEvent_t){ KEV_RELEASE, KEY_EXIT });   // hold EXIT: cancel
    sNow &= ~1u;

    // ---- radio page
    Press(KEY_SIDE2);
    Shot("12-radio");
    sS.flags |= STATION_AUTOBEACON;
    sRx = true;
    Shot("13-radio-receiving");
    sRx = false;
    Press(KEY_1); Press(KEY_4); Press(KEY_4); Press(KEY_8);
    Shot("14-frequency-entry");
    ClearEntry();

    // ---- packet page
    Press(KEY_SIDE2);
    Shot("15-packet");
    Press(KEY_EXIT);                             // back to the station list

    // ---- hold F: beacon
    ShowMessage("BEACON QUEUED");
    Shot("16-beacon-queued");
    ShowMessage("STATUS QUEUED");
    Shot("17-status-queued");
    sMessage = NULL;

    // ---- settings
    MENU_Open();
    Shot("18-menu");
    for (int i = 0; i < 5; i++) MENU_HandleKey((KeyEvent_t){ KEV_PRESS, KEY_DOWN }), MENU_HandleKey((KeyEvent_t){ KEV_RELEASE, KEY_DOWN });
    Shot("19-menu-symbol");
    for (int i = 0; i < 3; i++) MENU_HandleKey((KeyEvent_t){ KEV_PRESS, KEY_DOWN }), MENU_HandleKey((KeyEvent_t){ KEV_RELEASE, KEY_DOWN });
    Shot("20-menu-beacon-type");
    for (int i = 0; i < 6; i++) MENU_HandleKey((KeyEvent_t){ KEV_PRESS, KEY_DOWN }), MENU_HandleKey((KeyEvent_t){ KEV_RELEASE, KEY_DOWN });
    Shot("21-menu-beeps");
    for (int i = 0; i < 30; i++) MENU_HandleKey((KeyEvent_t){ KEV_PRESS, KEY_DOWN }), MENU_HandleKey((KeyEvent_t){ KEV_RELEASE, KEY_DOWN });
    Shot("21a-menu-about");
    MENU_HandleKey((KeyEvent_t){ KEV_PRESS, KEY_MENU }); MENU_HandleKey((KeyEvent_t){ KEV_RELEASE, KEY_MENU });
    Shot("21b-about");
    MENU_HandleKey((KeyEvent_t){ KEV_PRESS, KEY_EXIT }); MENU_HandleKey((KeyEvent_t){ KEV_RELEASE, KEY_EXIT });
    Settle();
    while (MENU_IsOpen()) { MENU_HandleKey((KeyEvent_t){ KEV_PRESS, KEY_EXIT }); MENU_HandleKey((KeyEvent_t){ KEV_RELEASE, KEY_EXIT }); MENU_HandleKey((KeyEvent_t){ KEV_LONG, KEY_EXIT }); }

    // ---- memory mode: stored as APRS in slot 0
    MEM_Set(0, 14439000, "APRS  ");
    Shot("22-stations-memory-name");
    gSettings.memory = 1;
    Shot("23-stations-memory-mode");
    gSettings.memory = 0;

    sKiss = true;
    Shot("24-kiss");
    sKiss = false;

    // ---- empty: just switched on
    uint8_t *count;
    APRS_StationTable(&count);
    *count = 0;
    sLastLine = "";
    Shot("25-stations-empty");
    uint8_t *mcount;
    MSG_Table(&mcount);
    *mcount = 0;
    Press(KEY_SIDE2);
    Shot("25a-messages-empty");
    Press(KEY_SIDE1);
    Press(KEY_SIDE1);
    Shot("26-packet-empty");
    Press(KEY_EXIT);

    // ---- a 9-character callsign, digipeater and beacon on
    sB.ssid = 15;
    sS.flags = STATION_DIGI | STATION_AUTOBEACON;
    Shot("27-long-callsign");
    sB.ssid = 1;

    sS.flags = 0;
    sConfigured = false;
    Splashshot("28-splash-unset");
    return 0;
}
