# k5 command-line tools

One `k5` command for flashing, EEPROM backup and talking to APRSK5's
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
| `k5 flash [IMAGE]` | bootloader (hold PTT at power-on) | flash a raw `.bin`, default `build/aprsk5.bin` |
| `k5 backup FILE` | stock / F4HWN | read the 8 KiB EEPROM (read-only) |
| `k5 version` | APRSK5 | firmware version |
| `k5 reg read 58 59` / `k5 reg write 58 33C3` | APRSK5 | BK4819 registers (hex) |
| `k5 tune 144.390` | APRSK5 | tune (MHz) |
| `k5 aprs [--log FILE]` | APRSK5 | print decoded packets live |
| `k5 beacon show` / `k5 beacon set CALL-SSID --lat .. --lon ..` | APRSK5 | position beacon: callsign, symbol, position, path, comment |
| `k5 send --from CALL ...` | APRSK5 | transmit test frames (needs a licence) |
| `k5 config show` / `k5 config set --digi on --digi-hops 2 ...` | APRSK5 | TX delay/tail, digipeater, auto-beacon, message and TX beeps (stored on the radio) |
| `k5 kiss on/off [--freq]` | APRSK5 | switch the cable between the text console and KISS (or hold * on the radio) |
| `k5 kiss monitor` | KISS mode | print frames the radio passes to the host |
| `k5 kiss send --from CALL ...` | KISS mode | transmit one frame through the TNC (CSMA) |
| `k5 kiss serve [--tcp-port 8001]` | KISS mode | KISS-over-TCP server for APRS programs |
| `k5 console` | APRSK5 | interactive console (`k5 console --help` lists commands) |

Only one program can hold the serial port at a time.

## Library

The modules are usable from Python too: `k5tools.protocol.Link` (stock
framing), `k5tools.flash`, `k5tools.eeprom`, `k5tools.console.K5Console`,
`k5tools.ax25raw` (AX.25 recovery from raw FSK bits).
