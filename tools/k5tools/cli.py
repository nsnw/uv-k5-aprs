"""k5: command-line tools for the Quansheng UV-K5 and the APRSK5 firmware."""

from __future__ import annotations

import os
import sys
import threading
import time
from pathlib import Path

import click
import serial
from serial.tools import list_ports

from . import eeprom as eeprom_mod
from . import flash as flash_mod
from .console import K5Console
from .protocol import Link

REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_IMAGE = REPO_ROOT / "build" / "aprsk5.bin"


# ---------------------------------------------------------------- port handling

def _candidate_ports() -> list:
    """USB serial adapters (the programming cable), excluding modems/Bluetooth."""
    out = []
    for p in list_ports.comports():
        name = p.device
        if sys.platform == "darwin" and not name.startswith("/dev/cu."):
            continue
        if "usbserial" in name or "ttyUSB" in name or "wchusbserial" in name or (
                p.vid is not None and "usbmodem" not in name):
            out.append(p)
    return out


def resolve_port(port: str | None) -> str:
    if port:
        return port
    found = _candidate_ports()
    if len(found) == 1:
        return found[0].device
    if not found:
        raise click.UsageError("no USB serial adapter found; is the programming cable plugged in? "
                               "(see `k5 ports`, or pass --port)")
    names = ", ".join(p.device for p in found)
    raise click.UsageError(f"several serial adapters found ({names}); pass --port or set K5_PORT")


def _port(ctx: click.Context) -> str:
    return resolve_port(ctx.obj["port"])


def _parse_hex(value: str, limit: int, what: str) -> int:
    try:
        v = int(value, 16)
    except ValueError:
        raise click.BadParameter(f"{value!r} is not a hex number", param_hint=what)
    if not 0 <= v <= limit:
        raise click.BadParameter(f"must be 0..{limit:X}", param_hint=what)
    return v


def _mhz_to_10hz(text: str) -> int:
    """'144.39' -> 14439000. Exact decimal parsing, no float rounding."""
    whole, _, frac = text.strip().partition(".")
    if not whole.isdigit() or (frac and not frac.isdigit()) or len(frac) > 5:
        raise click.BadParameter(f"{text!r}: give MHz like 144.390 (at most 5 decimals)",
                                 param_hint="FREQ")
    return int(whole) * 100000 + int(frac.ljust(5, "0") or 0)


def _console(ctx: click.Context, raw_log: str | None = None) -> K5Console:
    port = _port(ctx)
    try:
        return K5Console(port, raw_log=raw_log)
    except Exception as e:          # serial.SerialException and friends
        raise click.ClickException(f"can't open {port}: {e}")


# ---------------------------------------------------------------- root

@click.group(context_settings={"help_option_names": ["-h", "--help"]})
@click.option("-p", "--port", envvar="K5_PORT", metavar="DEVICE",
              help="Serial device of the programming cable. Default: $K5_PORT, or "
                   "auto-detect if exactly one USB serial adapter is present.")
@click.pass_context
def cli(ctx: click.Context, port: str | None) -> None:
    """Tools for the Quansheng UV-K5 and the APRSK5 firmware.

    \b
    Flashing and EEPROM backup talk to the radio's bootloader / stock firmware.
    console, version, reg, tune, aprs, send, beacon, config and kiss need APRSK5 running.
    """
    ctx.obj = {"port": port}


@cli.command()
def ports() -> None:
    """List serial ports and show which looks like the programming cable."""
    likely = {p.device for p in _candidate_ports()}
    for p in sorted(list_ports.comports(), key=lambda p: p.device):
        if sys.platform == "darwin" and not p.device.startswith("/dev/cu."):
            continue
        mark = "*" if p.device in likely else " "
        desc = p.description if p.description and p.description != "n/a" else ""
        click.echo(f"{mark} {p.device}  {desc}")
    if not likely:
        click.echo("no USB serial adapter found", err=True)


# ---------------------------------------------------------------- bootloader / stock firmware

@cli.command()
@click.argument("image", required=False, type=click.Path(exists=True, dir_okay=False, path_type=Path))
@click.option("--retries", default=5, show_default=True, help="Attempts per 256-byte block.")
@click.option("--version-string", default="*.01.23", show_default=True,
              help="Version sent to the bootloader.")
@click.option("--wait", default=30, show_default=True, help="Seconds to wait for the bootloader.")
@click.pass_context
def flash(ctx: click.Context, image: Path | None, retries: int, version_string: str, wait: int) -> None:
    """Flash a raw firmware IMAGE (default: build/aprsk5.bin).

    Put the radio in bootloader mode first: hold PTT while switching it on
    (the flashlight LED lights). Use the raw .bin, not .packed.bin.
    """
    image = image or DEFAULT_IMAGE
    if not image.exists():
        raise click.UsageError(f"{image} not found; build first or pass an IMAGE")
    data = image.read_bytes()
    try:
        flash_mod.check_image(data)
    except flash_mod.FlashError as e:
        raise click.ClickException(str(e))

    port = _port(ctx)
    click.echo(f"{image.name}: {len(data)} bytes, port {port}")
    with Link(port) as link:
        click.echo("waiting for bootloader... (hold PTT while switching the radio on)")
        try:
            click.echo(f"bootloader {flash_mod.wait_for_bootloader(link, wait)}")
            with click.progressbar(length=len(data), label="flashing", show_pos=True) as bar:
                done = [0]

                def progress(n: int, total: int, attempt: int) -> None:
                    if attempt > 1:
                        click.echo(f"\n  block at 0x{n - 1 & ~0xFF:04X} needed {attempt} attempts")
                    bar.update(n - done[0])
                    done[0] = n

                flash_mod.flash(link, data, version_string, retries, progress)
        except flash_mod.FlashError as e:
            raise click.ClickException(str(e))
        except serial.SerialException as e:
            raise click.ClickException(
                f"lost the serial connection ({e}). The radio stays in bootloader mode if "
                "flashing was interrupted; check the cable, then run `k5 flash` again.")
    click.echo("done. Switch the radio off and on to start the new firmware.")


@cli.command()
@click.argument("output", type=click.Path(dir_okay=False, writable=True, path_type=Path))
@click.option("--size", default="0x2000", show_default=True, help="Bytes to read (hex or decimal).")
@click.option("--force", is_flag=True, help="Overwrite OUTPUT if it exists.")
@click.pass_context
def backup(ctx: click.Context, output: Path, size: str, force: bool) -> None:
    """Back up the EEPROM to OUTPUT (radio running stock/F4HWN firmware).

    Read-only. Doesn't work while the radio runs APRSK5.
    """
    if output.exists() and not force:
        raise click.UsageError(f"{output} exists; pass --force to overwrite")
    nbytes = int(size, 0)
    port = _port(ctx)
    with Link(port) as link:
        try:
            with click.progressbar(length=nbytes, label="reading", show_pos=True) as bar:
                done = [0]

                def progress(n: int, total: int) -> None:
                    bar.update(n - done[0])
                    done[0] = n

                version, data = eeprom_mod.read(link, nbytes, progress)
        except TimeoutError as e:
            raise click.ClickException(f"{e}. Is the radio on and running stock/F4HWN firmware?")
    output.write_bytes(data)
    click.echo(f"radio firmware {version}; wrote {len(data)} bytes to {output}")


# ---------------------------------------------------------------- APRSK5 console

@cli.command()
@click.pass_context
def console(ctx: click.Context) -> None:
    """Interactive APRSK5 debug console.

    \b
    Commands (hex unless noted):
      v                version
      r RR             read BK4819 register
      w RR VVVV        write BK4819 register
      t FREQ           tune, decimal 10 Hz units (14439000 = 144.390 MHz)
      d N              read N words from the FSK FIFO
      p N              words to auto-read per FIFO-almost-full interrupt
      k [FREQ]         switch to KISS mode (leaves this console; see `k5 kiss`)
    Events: "I ssss" interrupt status, "D ..." FIFO words, "A ..." APRS packets.
    Ctrl-D or Ctrl-C to quit.
    """
    k = _console(ctx)
    click.echo(f"connected to {k.ser.port}; type commands, Ctrl-D to quit", err=True)

    def printer() -> None:
        while True:
            ts, line = k.lines.get()
            click.echo(f"{time.strftime('%H:%M:%S', time.localtime(ts))} {line}")

    threading.Thread(target=printer, daemon=True).start()
    try:
        for line in sys.stdin:
            if line.strip():
                k.send(line.strip())
    except KeyboardInterrupt:
        pass
    finally:
        time.sleep(0.2)           # let the last replies print
        k.close()


@cli.command()
@click.pass_context
def version(ctx: click.Context) -> None:
    """Show the APRSK5 firmware version."""
    with _console(ctx) as k:
        click.echo(k.version())


@cli.group()
def reg() -> None:
    """Read or write BK4819 registers (APRSK5)."""


@reg.command("read")
@click.argument("registers", nargs=-1, required=True)
@click.pass_context
def reg_read(ctx: click.Context, registers: tuple[str, ...]) -> None:
    """Read one or more REGISTERS (hex), e.g. `k5 reg read 58 59 5c`."""
    regs = [_parse_hex(r, 0x7F, "REGISTER") for r in registers]
    with _console(ctx) as k:
        for r in regs:
            click.echo(f"{r:02X} = {k.read_reg(r):04X}")


@reg.command("write")
@click.argument("register")
@click.argument("value")
@click.pass_context
def reg_write(ctx: click.Context, register: str, value: str) -> None:
    """Write VALUE (hex) to REGISTER (hex), then read it back."""
    r = _parse_hex(register, 0x7F, "REGISTER")
    v = _parse_hex(value, 0xFFFF, "VALUE")
    with _console(ctx) as k:
        k.write_reg(r, v)
        click.echo(f"{r:02X} = {k.read_reg(r):04X}")


@cli.command()
@click.argument("freq")
@click.pass_context
def tune(ctx: click.Context, freq: str) -> None:
    """Tune to FREQ in MHz (APRSK5; 6 m, 2 m, 1.25 m and 70 cm only).

    Example: `k5 tune 144.390`
    """
    f = _mhz_to_10hz(freq)
    with _console(ctx) as k:
        try:
            k.tune(f)
        except RuntimeError as e:
            raise click.ClickException(str(e))
    click.echo(f"tuned to {f // 100000}.{f % 100000:05d} MHz")


@cli.command()
@click.option("--log", "log_path", type=click.Path(dir_okay=False, path_type=Path),
              help="Also append packets (with UTC timestamps) to this file.")
@click.pass_context
def aprs(ctx: click.Context, log_path: Path | None) -> None:
    """Print APRS packets decoded by the radio, live.

    Needs the text console (not KISS mode; see `k5 kiss off`). Ctrl-C to stop.
    """
    k = _console(ctx)
    log = open(log_path, "a") if log_path else None
    click.echo(f"listening on {k.ser.port}; Ctrl-C to stop", err=True)
    count = 0
    try:
        while True:
            for ts, line in k.events(timeout=1.0):
                if not line.startswith("A "):
                    continue
                count += 1
                local = time.strftime("%H:%M:%S", time.localtime(ts))
                click.echo(f"{local} {line[2:]}")
                if log:
                    utc = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime(ts))
                    log.write(f"{utc} {line[2:]}\n")
                    log.flush()
    except KeyboardInterrupt:
        click.echo(f"\n{count} packets", err=True)
    finally:
        k.close()
        if log:
            log.close()




# ---------------------------------------------------------------- beacon

def _read_beacon(k: K5Console):
    from .beacon import Beacon
    data = k.command("c", "C").split()[1]
    return None if data == "-" else Beacon.unpack(bytes.fromhex(data))


def _show_beacon(b) -> None:
    click.echo(f"call     {b.source}")
    click.echo(f"position {b.lat:.5f}, {b.lon:.5f}")
    click.echo(f"symbol   {b.symbol}")
    click.echo(f"path     {b.tnc2().split(':')[0].split(',', 1)[1] if b.path_hops else '(none)'}")
    click.echo(f"comment  {b.comment!r}")
    click.echo(f"packet   {b.tnc2()}")


@cli.group()
def beacon() -> None:
    """APRS position beacon stored in the radio (APRSK5).

    Send it from the radio: hold F (#); it goes out on the current frequency.
    """


@beacon.command("show")
@click.pass_context
def beacon_show(ctx: click.Context) -> None:
    """Show the beacon configuration stored in the radio."""
    with _console(ctx) as k:
        b = _read_beacon(k)
    if b is None:
        click.echo("no beacon configured (use `k5 beacon set`)")
    else:
        _show_beacon(b)


@beacon.command("set")
@click.option("--call", "call", required=True, metavar="CALL[-SSID]", help="Your callsign, e.g. VE6HUE-1.")
@click.option("--lat", type=click.FloatRange(-90, 90), required=True, help="Latitude, decimal degrees (+N).")
@click.option("--lon", type=click.FloatRange(-180, 180), required=True, help="Longitude, decimal degrees (+E).")
@click.option("--symbol", default="/[", show_default=True,
              help="APRS symbol table+code: /[ person, /> car, /- house, /b bike, /k truck.")
@click.option("--path", type=click.Choice(["none", "1", "2"]), default="2", show_default=True,
              help="Digipeater hops: none, 1 (WIDE1-1) or 2 (WIDE1-1,WIDE2-1).")
@click.option("--comment", default="", help="Up to 39 characters.")
@click.pass_context
def beacon_set(ctx: click.Context, call: str, lat: float, lon: float, symbol: str,
               path: str, comment: str) -> None:
    """Store the beacon configuration in the radio's EEPROM."""
    from .beacon import Beacon

    name, _, ssid = call.upper().partition("-")
    if ssid and not ssid.isdigit():
        raise click.BadParameter(f"{call!r}", param_hint="--call")
    b = Beacon(name, int(ssid or 0), 0 if path == "none" else int(path), symbol,
               Beacon.degrees_to_hmin(lat), Beacon.degrees_to_hmin(lon), comment)
    try:
        blob = b.pack()
    except ValueError as e:
        raise click.UsageError(str(e))
    with _console(ctx) as k:
        try:
            k.command("c " + blob.hex().upper(), "C")
        except RuntimeError as e:
            raise click.ClickException(str(e))
        stored = _read_beacon(k)
    if stored != b:
        raise click.ClickException("read-back differs from what was written")
    click.echo("stored (verified by read-back):")
    _show_beacon(stored)


@cli.command()
@click.option("--from", "source", required=True, metavar="CALL-SSID",
              help="Your callsign, e.g. VE6HUE-1.")
@click.option("--to", "dest", default="APZK5M", show_default=True,
              help="Destination (APZ* = experimental software).")
@click.option("--path", default="", show_default=True,
              help="Digipeater path, e.g. WIDE1-1,WIDE2-1. Empty = not digipeated.")
@click.option("--info", default=">APRSK5 AFSK test", show_default=True,
              help="APRS information field.")
@click.option("--freq", default="144.900", show_default=True,
              help="MHz; the firmware transmits only on 50-54, 144-148, 222-225 and 430-450 MHz.")
@click.option("--count", default=1, show_default=True, help="Number of frames to send.")
@click.option("--interval", default=5.0, show_default=True, help="Seconds between frames.")
@click.option("--gain", type=click.IntRange(0, 127), help="AFSK tone level 0-127 (deviation).")
@click.option("--bias", type=click.IntRange(0, 255),
              help="PA bias 0-255; the firmware caps it at the calibrated low-power value.")
@click.option("--flags", type=click.IntRange(1, 200), help="Preamble flags (6.67 ms each).")
@click.option("--raw-log", type=click.Path(dir_okay=False),
              help="Record every byte received from the radio (timestamped) to this file.")
@click.pass_context
def send(ctx: click.Context, source: str, dest: str, path: str, info: str, freq: str,
         count: int, interval: float, gain: int | None, bias: int | None,
         flags: int | None, raw_log: str | None) -> None:
    """Transmit APRS frames from the radio (APRSK5).

    \b
    This keys the transmitter: you need an amateur licence, and SOURCE must be
    your callsign. The radio is tuned to --freq first. Defaults are for
    testing: 144.900 MHz, no digipeater path, a short status message.
    """
    from .ax25raw import build_frame

    calls = [c.strip().upper() for c in path.split(",") if c.strip()]
    for c in [source.upper(), dest.upper(), *calls]:
        name, _, ssid = c.partition("-")
        if not (1 <= len(name) <= 6 and name.isalnum()) or (ssid and not (ssid.isdigit() and int(ssid) <= 15)):
            raise click.BadParameter(f"{c!r} is not a valid callsign[-SSID]")
    frame = build_frame(source.upper(), dest.upper(), calls, info)[:-2]   # firmware adds the FCS
    if len(frame) > 330:
        raise click.BadParameter("frame longer than 330 bytes", param_hint="--info")

    with _console(ctx, raw_log) as k:
        try:
            k.tune(_mhz_to_10hz(freq))
            if gain is not None:
                k.command(f"g {gain:02X}", "G")
            if bias is not None:
                reply = k.command(f"b {bias:02X}", "B").split()
                click.echo(f"PA bias {int(reply[1], 16)} (cap {int(reply[2], 16)})")
            if flags is not None:
                k.command(f"f {flags:02X}", "F")
        except RuntimeError as e:
            raise click.ClickException(str(e))

    # RF from the radio's own transmission can wedge the cable's receive side
    # until the port is reopened, so each frame is sent on one connection and
    # confirmed on a fresh one using the radio's TX counter ("s").
    TX_RESULTS = {0: None, 1: "outside the TX bands (6 m, 2 m, 1.25 m, 70 cm)", 2: "bad frame length",
                  3: "would exceed the TX time limit", 4: "no power calibration"}
    via = f" via {','.join(calls)}" if calls else ""
    on_air_s = (50 + 35 + (flags or 40) * 8 / 1.2 + len(frame) * 8 * 1.25 / 1.2) / 1000
    sent = 0
    for n in range(1, count + 1):
        if n > 1:
            time.sleep(interval)
        with _console(ctx, raw_log) as k:
            before = int(k.command("s", "S").split()[1])
            k.send("x " + frame.hex().upper())
            sent_at = time.time()
            time.sleep(on_air_s + 0.5)
        with _console(ctx, raw_log) as k:
            _, count_s, result_s, ms_s = k.command("s", "S", timeout=2.0).split()
        if int(count_s) != before + 1:
            status = "NOT RECEIVED by the radio"
        elif TX_RESULTS.get(int(result_s)):
            status = f"REFUSED: {TX_RESULTS[int(result_s)]}"
        else:
            status = f"{ms_s} ms on air"
            sent += 1
        click.echo(f"[{n}/{count}] {time.strftime('%H:%M:%S', time.localtime(sent_at))} "
                   f"{freq} MHz {source.upper()}>{dest.upper()}{via}: {info}  ({status})")
    if sent != count:
        raise click.ClickException(f"only {sent} of {count} frames confirmed sent")


# ---------------------------------------------------------------- station config

def _read_station(k: K5Console):
    from .station import Station
    return Station.unpack(bytes.fromhex(k.command("o", "O").split()[1]))


def _show_station(st) -> None:
    from .station import FLAG_MS
    click.echo(f"TX delay     {st.txdelay_flags} flags ({st.txdelay_flags * FLAG_MS:.0f} ms)")
    click.echo(f"TX tail      {st.txtail_flags} flags ({st.txtail_flags * FLAG_MS:.0f} ms)")
    hops = "fill-in, WIDE1-1 only" if st.digi_max_hops == 1 else f"WIDEn-N up to n = {st.digi_max_hops}"
    click.echo(f"digipeater   {'on' if st.digi else 'off'} ({hops})")
    click.echo(f"auto-beacon  {'on' if st.autobeacon else 'off'} (every {st.beacon_interval_min} min)")


@cli.group()
def config() -> None:
    """Station options: TX timing, digipeater, auto-beacon (stored on the radio).

    \b
    The digipeater and auto-beacon use the callsign from `k5 beacon set`.
    On the radio: SIDE1 toggles the digipeater, SIDE2 the auto-beacon;
    the status line shows D / B (inverted = on and on a TX frequency).
    """


@config.command("show")
@click.pass_context
def config_show(ctx: click.Context) -> None:
    """Show the station options."""
    with _console(ctx) as k:
        try:
            _show_station(_read_station(k))
        except (RuntimeError, TimeoutError) as e:
            raise click.ClickException(str(e))


_ON_OFF = click.Choice(["on", "off"])


@config.command("set")
@click.option("--txdelay", type=click.FloatRange(8, 1667), metavar="MS",
              help="Flags before each frame, in ms (rounded to 6.67 ms flags).")
@click.option("--txtail", type=click.FloatRange(8, 417), metavar="MS", help="Flags after each frame, in ms.")
@click.option("--digi", type=_ON_OFF, help="Digipeater.")
@click.option("--digi-hops", type=click.IntRange(1, 7),
              help="Largest WIDEn-N to relay (1 = fill-in, WIDE1-1 only).")
@click.option("--auto-beacon", type=_ON_OFF, help="Beacon automatically.")
@click.option("--beacon-interval", type=click.IntRange(1, 1440), metavar="MIN", help="Minutes between beacons.")
@click.pass_context
def config_set(ctx: click.Context, txdelay: float | None, txtail: float | None, digi: str | None,
               digi_hops: int | None, auto_beacon: str | None, beacon_interval: int | None) -> None:
    """Change station options; anything not given stays as it is."""
    from .station import Station
    with _console(ctx) as k:
        try:
            st = _read_station(k)
            if txdelay is not None:
                st.txdelay_flags = Station.ms_to_flags(txdelay)
            if txtail is not None:
                st.txtail_flags = Station.ms_to_flags(txtail)
            if digi is not None:
                st.digi = digi == "on"
            if digi_hops is not None:
                st.digi_max_hops = digi_hops
            if auto_beacon is not None:
                st.autobeacon = auto_beacon == "on"
            if beacon_interval is not None:
                st.beacon_interval_min = beacon_interval
            data = st.pack()
            reply = bytes.fromhex(k.command("o " + data.hex().upper(), "O").split()[1])
            if reply != data:   # a stale reply (e.g. delayed by a transmission): read again
                reply = bytes.fromhex(k.command("o", "O", timeout=3.0).split()[1])
                if reply != data:
                    raise click.ClickException("the radio did not store the new settings")
            _show_station(Station.unpack(reply))
        except ValueError as e:
            raise click.BadParameter(str(e))
        except (RuntimeError, TimeoutError) as e:
            raise click.ClickException(str(e))


# ---------------------------------------------------------------- KISS

@cli.group()
def kiss() -> None:
    """KISS TNC mode: the radio as a 1200-baud APRS modem for host software.

    \b
    In KISS mode the cable carries KISS instead of the text console: every
    decoded frame goes to the host, and frames from the host are transmitted
    (same frequency/power/time limits as `k5 send`). On the radio, hold * to
    toggle KISS; the header shows KISS instead of APRS.
    """


def _kiss_serial(ctx: click.Context, freq: str | None) -> serial.Serial:
    from . import kiss as kiss_mod
    ser = serial.Serial(_port(ctx), 38400, timeout=0.1)
    try:
        f = kiss_mod.enter(ser, _mhz_to_10hz(freq) if freq else None)
    except RuntimeError as e:
        ser.close()
        raise click.ClickException(str(e))
    if f is not None:
        click.echo(f"radio in KISS mode on {f / 100000:.3f} MHz", err=True)
    elif freq:
        click.echo("no reply to `k` (already in KISS mode?); frequency unchanged", err=True)
    return ser


_FREQ_OPTION = click.option("--freq", metavar="MHZ",
                            help="Retune first (only when switching from console mode).")


@kiss.command("on")
@_FREQ_OPTION
@click.pass_context
def kiss_on(ctx: click.Context, freq: str | None) -> None:
    """Switch the radio to KISS mode."""
    _kiss_serial(ctx, freq).close()


@kiss.command("off")
@click.pass_context
def kiss_off(ctx: click.Context) -> None:
    """Return the radio to the text console (sends C0 FF C0)."""
    from . import kiss as kiss_mod
    with serial.Serial(_port(ctx), 38400, timeout=0.5) as ser:
        ser.write(kiss_mod.RETURN)
        ser.flush()
        time.sleep(0.1)
        ser.reset_input_buffer()
        ser.write(b"v\n")
        reply = ser.readline().strip().decode("ascii", "replace")
    click.echo(f"console mode: {reply}" if reply.startswith("V ") else "no console reply", err=True)


@kiss.command("monitor")
@_FREQ_OPTION
@click.pass_context
def kiss_monitor(ctx: click.Context, freq: str | None) -> None:
    """Print frames the radio sends over KISS. Ctrl-C to stop."""
    from . import kiss as kiss_mod
    ser = _kiss_serial(ctx, freq)
    dec = kiss_mod.Decoder()
    try:
        while True:
            for cmd, payload in dec.feed(ser.read(512)):
                if cmd == kiss_mod.CMD_DATA:
                    click.echo(f"{time.strftime('%H:%M:%S')} {kiss_mod.describe(payload)}")
    except KeyboardInterrupt:
        pass
    finally:
        ser.close()


@kiss.command("send")
@click.option("--from", "source", required=True, metavar="CALL-SSID", help="Your callsign.")
@click.option("--to", "dest", default="APZK5M", show_default=True, metavar="CALL")
@click.option("--path", default="", help="Digipeaters, e.g. WIDE1-1,WIDE2-1.")
@click.option("--info", default=">APRSK5 KISS test", show_default=True)
@click.option("--txdelay", type=click.IntRange(1, 255), help="KISS TXDELAY, 10 ms units.")
@_FREQ_OPTION
@click.pass_context
def kiss_send(ctx: click.Context, source: str, dest: str, path: str, info: str,
              txdelay: int | None, freq: str | None) -> None:
    """Send one frame through KISS (the radio picks the moment: CSMA).

    This keys the transmitter: SOURCE must be your callsign.
    """
    from . import kiss as kiss_mod
    from .ax25raw import build_frame
    calls = [c.strip().upper() for c in path.split(",") if c.strip()]
    frame = build_frame(source.upper(), dest.upper(), calls, info)[:-2]
    if len(frame) > 330:
        raise click.BadParameter("frame longer than 330 bytes", param_hint="--info")
    with _kiss_serial(ctx, freq) as ser:
        if txdelay:
            ser.write(kiss_mod.encode(bytes([txdelay]), kiss_mod.CMD_TXDELAY))
        ser.write(kiss_mod.encode(frame))
        ser.flush()
    click.echo(f"queued {kiss_mod.describe(frame)}")


@kiss.command("serve")
@click.option("--bind", default="127.0.0.1", show_default=True, help="Address to listen on.")
@click.option("--tcp-port", default=8001, show_default=True, help="TCP port (8001 = Dire Wolf's).")
@_FREQ_OPTION
@click.pass_context
def kiss_serve(ctx: click.Context, bind: str, tcp_port: int, freq: str | None) -> None:
    """KISS over TCP: share the radio with APRS programs (YAAC, Xastir, ...).

    Frames from the radio go to every connected client; frames from any
    client go to the radio. Received frames are also printed. Ctrl-C to stop.
    """
    import selectors
    import socket
    from . import kiss as kiss_mod

    ser = _kiss_serial(ctx, freq)
    srv = socket.create_server((bind, tcp_port), reuse_port=False)
    srv.setblocking(False)
    sel = selectors.DefaultSelector()
    sel.register(srv, selectors.EVENT_READ, "accept")
    clients: dict[socket.socket, kiss_mod.Decoder] = {}
    radio = kiss_mod.Decoder()
    click.echo(f"KISS over TCP on {bind}:{tcp_port}; Ctrl-C to stop", err=True)

    def drop(c: socket.socket) -> None:
        sel.unregister(c)
        clients.pop(c, None)
        c.close()
        click.echo(f"client disconnected ({len(clients)} left)", err=True)

    try:
        while True:
            for key, _ in sel.select(timeout=0.05):
                if key.data == "accept":
                    c, addr = srv.accept()
                    c.setblocking(False)
                    sel.register(c, selectors.EVENT_READ, "client")
                    clients[c] = kiss_mod.Decoder()
                    click.echo(f"client {addr[0]}:{addr[1]} connected", err=True)
                    continue
                c = key.fileobj
                try:
                    data = c.recv(4096)
                except OSError:
                    data = b""
                if not data:
                    drop(c)
                    continue
                for cmd, payload in clients[c].feed(data):
                    if cmd == kiss_mod.CMD_RETURN:
                        continue                 # keep the radio in KISS for the others
                    ser.write(kiss_mod.encode(payload, cmd))
                    if cmd & 0x0F == kiss_mod.CMD_DATA:
                        click.echo(f"{time.strftime('%H:%M:%S')} TX {kiss_mod.describe(payload)}")
            data = ser.read(ser.in_waiting or 1) if ser.in_waiting else b""
            for cmd, payload in radio.feed(data):
                if cmd != kiss_mod.CMD_DATA:
                    continue
                click.echo(f"{time.strftime('%H:%M:%S')} RX {kiss_mod.describe(payload)}")
                out = kiss_mod.encode(payload)
                for c in list(clients):
                    try:
                        c.sendall(out)
                    except OSError:
                        drop(c)
    except KeyboardInterrupt:
        pass
    finally:
        for c in list(clients):
            c.close()
        srv.close()
        ser.close()


def main() -> None:
    cli(prog_name=os.path.basename(sys.argv[0]) or "k5")


if __name__ == "__main__":
    main()
