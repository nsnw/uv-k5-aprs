# k5 command-line tools

One `k5` command for flashing, EEPROM backup and talking to K5-APRS's
debug console over the programming cable.

## Install

```sh
python3 -m venv .venv          # from the repo root
.venv/bin/pip install -e tools
.venv/bin/k5 --help
```

The serial port is auto-detected when exactly one USB serial adapter is
plugged in. Otherwise pass `--port /dev/cu.usbserial-XXXX` (before the
subcommand) or set `K5_PORT`. `k5 ports` shows what's connected.

## Commands

| Command | Radio must be running | What it does |
|---|---|---|
| `k5 ports` | – | list serial ports, `*` marks the likely cable |
| `k5 flash [IMAGE]` | bootloader (hold PTT at power-on) | flash a raw `.bin`, default `build/k5-aprs.bin` |
| `k5 backup FILE` | stock / F4HWN | read the 8 KiB EEPROM (read-only) |
| `k5 version` | K5-APRS | firmware version |
| `k5 reg read 58 59` / `k5 reg write 58 33C3` | K5-APRS | BK4819 registers (hex) |
| `k5 tune 144.390` | K5-APRS | tune (MHz) |
| `k5 aprs [--log FILE]` | K5-APRS | print decoded packets live |
| `k5 beacon show` / `k5 beacon set --call CALL-SSID [--lat .. --lon ..] [--type status --status TEXT] ...` | K5-APRS | beacon: callsign, symbol, position, path, comment, type (position / status / both), status text; shows what a beacon sends |
| `k5 send --from CALL ...` | K5-APRS | transmit test frames (needs a licence) |
| `k5 config show` / `k5 config set --digi on --digi-hops 2 ...` | K5-APRS | TX delay/tail, digipeater, auto-beacon, message / TX / RX beeps (stored on the radio) |
| `k5 kiss on/off [--freq]` | K5-APRS | switch the cable between the text console and KISS (or hold * on the radio) |
| `k5 kiss monitor` | KISS mode | print frames the radio passes to the host |
| `k5 kiss send --from CALL ...` | KISS mode | transmit one frame through the TNC (CSMA) |
| `k5 kiss serve [--tcp-port 8001]` | KISS mode | KISS-over-TCP server for APRS programs |
| `k5 console` | K5-APRS | interactive console (`k5 console --help` lists commands) |

Only one program can hold the serial port at a time.

## Library

The modules are usable from Python too: `k5tools.protocol.Link` (stock
framing), `k5tools.flash`, `k5tools.eeprom`, `k5tools.console.K5Console`,
`k5tools.ax25raw` (AX.25 recovery from raw FSK bits).
