# APRSK5

APRS station firmware for the Quansheng UV-K5: receive, beacon, digipeat,
send and receive messages and run as a KISS TNC, all from a $30 handheld.

This is APRSK5 0.9.0. It grew out of **[uv-k5-minimal](https://github.com/nsnw/uv-k5-minimal)**, a small
from-scratch firmware for the same radio. The low-level drivers come from
the open UV-K5 firmware projects (see [Credits](#credits)); everything above
them is new.

> [!WARNING]
> - **Back up your EEPROM first** (`k5 backup eeprom.bin` while the stock firmware
>   is running). APRSK5 keeps its settings at `0x0000`–`0x00FF`, over the start of
>   the stock channel memory. Factory calibration (`0x1E00`–`0x1FFF`) is only read.
> - **It transmits.** You need an amateur radio licence. It only transmits on the
>   6 m, 2 m, 1.25 m and 70 cm amateur bands, at the radio's calibrated low power.
> - Tested on one UV-K5 (DP32G030, BK4819, bootloader 2.00.06). Other variants
>   haven't been tried.

## Features

- **Receive APRS** (1200 baud AFSK) on any frequency in the bands above. The
  default is 144.390 MHz (North America), and the last frequency is remembered.
- **Station list**: the 16 most recently heard stations, with position, speed,
  course, altitude, path and comment.
- **Position beacon**: on demand, or automatically every 1–1440 minutes.
- **Digipeater**: WIDEn-N (New-N paradigm) up to a set number of hops, or
  fill-in only (WIDE1-1), with duplicate suppression.
- **Messages**: received messages are acked automatically, including
  reply-acks and messages relayed by IGates. Send and reply from the keypad;
  unacknowledged messages are retried every 30 s, up to 5 times. Inbox of 8.
- **KISS TNC** on the programming cable for APRS programs (Xastir, YAAC,
  APRSIS32, …); `k5 kiss serve` turns it into a KISS-over-TCP server.
- **10 memories** with 6-character names, and a memory mode that shows the
  name instead of the frequency.
- **On-radio settings**: callsign, SSID, position, comment, path, beacon,
  digipeater and TX timing, all from the keypad.
- **A careful transmitter**: amateur bands only, only where the radio has a
  factory power calibration, calibrated low power, at most 3 s per frame,
  and only once the channel is clear (p-persistent CSMA).

## The screen

The status bar along the top shows:

- your callsign (`KISS` while the TNC is on, `NO CALL` until you set one)
- **D | B**: digipeater and auto-beacon. A filled cell is on; it blinks if
  it's on but the current frequency can't transmit.
- a speaker: sound waves while receiving or monitoring, crossed out when idle,
  inverted while transmitting
- an envelope while there are unread messages
- the battery

Below it are the frequency (or the memory's name in memory mode), a signal
meter and the stations heard. Key hints show `MENU` and `EXIT` as small
inverted **M** and **E** icons.

## Keys

### Main screen

| Key | Action |
|---|---|
| `0`–`9` | Type a frequency. The first 3 digits are MHz (`1 4 4 3 9` = 144.390); `*` places the point yourself, `MENU` confirms, `EXIT` deletes (hold: cancel). In memory mode a digit goes to that memory instead. |
| hold `0`–`9` | Go to that memory |
| hold `EXIT` | Switch between frequency and memory mode |
| `UP` / `DOWN` | Select a station |
| `MENU` | Station details (`EXIT` goes back) |
| hold `MENU` | Settings |
| `F` (`#`) | Messages |
| hold `F` | Send your position beacon now |
| `SIDE1` | Digipeater on / off |
| `SIDE2` | Auto-beacon on / off |
| hold `*` | KISS TNC on / off |
| PTT | Monitor: opens the speaker while held |

PTT never transmits: everything APRSK5 sends is an APRS packet.

### Messages (`F`)

| Key | Action |
|---|---|
| `UP` / `DOWN` | Select. The top row is **New message**. |
| `MENU` | Read the message, or write a new one |
| `MENU` while reading | Reply, or send again if it wasn't delivered |
| hold `EXIT` while reading | Delete the message |
| `EXIT` | Back |

In the list, `*` marks unread messages. Sent messages show `>` while waiting
for an ack, `+` once delivered and `x` if there was no ack or it was rejected.

### Settings (hold `MENU`)

Memories, Callsign, SSID, Lat, Lon, Comment, Path, Auto beacon, Beacon every,
Digipeater, Digi hops, TX delay, TX tail.

`UP`/`DOWN` move and change values, `MENU` edits and saves, `EXIT` cancels or
closes. In **Memories**, `MENU` tunes to a memory, hold `MENU` stores the
current frequency and names it, and hold `EXIT` clears it.

### Typing text

Callsigns, names, the comment and messages use phone-style entry: `2` is
A B C 2, and pressing it again within a second moves to the next letter.

| Key | Action |
|---|---|
| `0`–`9` | Letters and digits (`0` = space, `1` = punctuation) |
| `UP` / `DOWN` | Step through every character at the cursor |
| `*` / `F` | Cursor left / right |
| `SIDE1` | Upper / lower case |
| `EXIT` | Delete (hold: cancel) |
| `MENU` | Done |

## Getting started

1. Back up the EEPROM while the stock firmware is still running (see [Tools](#tools)).
2. Build and flash APRSK5.
3. Hold `MENU` and set your callsign, SSID and position.
4. Tune your local APRS frequency. Hold `F` to beacon, press `SIDE2` to beacon
   automatically and `SIDE1` to digipeat.

## Building

Needs Docker (Colima, Rancher Desktop or Docker Desktop all work):

```sh
./build.sh                         # uses the current docker context
DOCKER_CONTEXT=colima ./build.sh   # or pick one
./build.sh clean
```

Output goes to `build/`:

- `aprsk5.packed.bin`: for any UV-K5 flasher (web flashers, `k5prog`, `uvtools`)
- `aprsk5.bin`: the raw image, for `k5 flash`
- `aprsk5.elf` and `.map`: for debugging

With a native `arm-none-eabi-gcc` and `python3 -m pip install crcmod`, plain
`make` works too. `./build.sh ENABLE_TX=0` builds a receive-only firmware that
never keys the transmitter. The version number is set in the `Makefile`.

The host tests (decoder/encoder loopback, digipeater, KISS, messages, menu,
beacon) run with the system C compiler: `test/run.sh`.

## Tools

The `k5` command-line tool flashes the radio, backs up the EEPROM, configures
the station, shows decoded packets and runs the KISS TNC. See
[tools/README.md](tools/README.md).

```sh
python3 -m venv .venv && .venv/bin/pip install -e tools
.venv/bin/k5 backup eeprom.bin   # with the stock firmware running
.venv/bin/k5 flash               # bootloader mode: hold PTT while switching on
.venv/bin/k5 aprs                # decoded packets, live
```

## How it works

- **Receiving.** The UV-K5 has no audio path back to the MCU, so the BK4819's
  own FSK receiver is used as a raw Bell 202 bit slicer: its sync word is set
  to the NRZI image of the APRS flag preamble, and the raw bits that follow
  are decoded in software (NRZI, bit unstuffing, FCS) in `src/ax25.c`.
- **Transmitting.** The BK4819's tone generator is switched between 1200 and
  2200 Hz at 1200 baud, timed from SysTick (`src/tx.c`, `src/hdlc_tx.c`).
- **Everything else** (parsing, digipeater, beacon, messages, KISS, the UI)
  lives in `src/`, one module per job; each header starts with a description.

### EEPROM layout

| Address | Contents |
|---|---|
| `0x0000`–`0x000F` | Settings: frequency, memory mode, squelch, backlight |
| `0x0010`–`0x004F` | Beacon: callsign, SSID, symbol, position, path, comment |
| `0x0050`–`0x005F` | Station: TX delay and tail, digipeater, beacon interval |
| `0x0060`–`0x00FF` | 10 memories |
| `0x1E00`–`0x1FFF` | Factory calibration (read only) |

## Notes

- Packets go to the destination `APZK5M`. `APZ` is APRS's experimental range;
  a registered tocall would come from [aprs-deviceid](https://github.com/aprsorg/aprs-deviceid).
- Messages are kept in RAM, so they're lost when the radio is switched off.

## Credits

APRSK5 stands on the work of the UV-K5 firmware community. The drivers,
start-up code, register definitions and firmware packer come from these
Apache-2.0 projects; [NOTICE](NOTICE) lists exactly what was taken.

- **DualTachyon**: the open re-implementation of the UV-K5 firmware that made
  all of this possible ([uv-k5-firmware](https://github.com/DualTachyon/uv-k5-firmware))
- **OneOfEleven**, **egzumer** and **armel (F4HWN)**: the custom firmware forks
  ([OneOfEleven](https://github.com/OneOfEleven/uv-k5-firmware-custom),
  [egzumer](https://github.com/egzumer/uv-k5-firmware-custom),
  [armel](https://github.com/armel/uv-k5-firmware-custom))
- **sq5bpf**: [k5prog](https://github.com/sq5bpf/k5prog), which documented the
  bootloader and EEPROM protocol the `k5` tool speaks
- **ARM**: CMSIS 5 headers (Apache-2.0), in `external/CMSIS/`

## References

- [APRS Protocol Reference 1.0.1](http://www.aprs.org/doc/APRS101.PDF): positions, messages, acks
- [APRS 1.1 reply-acks](http://www.aprs.org/aprs11/replyacks.txt)
- [The New-N paradigm](http://www.aprs.org/fix14439.html): WIDEn-N digipeating
- [AX.25 Link Access Protocol 2.2](https://www.tapr.org/pdf/AX25.2.2.pdf) (TAPR)
- [KISS](http://www.ka9q.net/papers/kiss.html) (Chepponis and Karn)
- [aprs-deviceid](https://github.com/aprsorg/aprs-deviceid): destination callsigns (tocalls)
- [Dire Wolf](https://github.com/wb2osz/direwolf): the reference software TNC, handy for testing

## Licence

Apache License 2.0: see [LICENSE](LICENSE) and [NOTICE](NOTICE).
Copyright 2026 Andy Smith, VE6LY.
