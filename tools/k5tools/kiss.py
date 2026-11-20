"""KISS framing (host side of src/kiss.c) and a serial KISS link to the radio."""

from __future__ import annotations

import time

import serial

FEND, FESC, TFEND, TFESC = 0xC0, 0xDB, 0xDC, 0xDD
CMD_DATA, CMD_TXDELAY, CMD_P, CMD_SLOTTIME, CMD_RETURN = 0x00, 0x01, 0x02, 0x03, 0xFF
RETURN = bytes([FEND, CMD_RETURN, FEND])


def encode(payload: bytes, cmd: int = CMD_DATA) -> bytes:
    """One KISS frame: FEND, command byte, escaped payload, FEND."""
    body = bytes([cmd]) + payload
    body = body.replace(bytes([FESC]), bytes([FESC, TFESC])).replace(bytes([FEND]), bytes([FESC, TFEND]))
    return bytes([FEND]) + body + bytes([FEND])


class Decoder:
    """Feed it bytes; yields (command, payload) for every complete frame."""

    def __init__(self) -> None:
        self._buf: bytearray | None = None
        self._esc = False

    def feed(self, data: bytes):
        for b in data:
            if b == FEND:
                if self._buf:
                    yield self._buf[0], bytes(self._buf[1:])
                self._buf = bytearray()
                self._esc = False
            elif self._buf is None:
                continue                    # text before the first FEND
            elif self._esc:
                self._esc = False
                if b in (TFEND, TFESC):
                    self._buf.append(FEND if b == TFEND else FESC)
                else:
                    self._buf = None        # protocol error: drop the frame
            elif b == FESC:
                self._esc = True
            else:
                self._buf.append(b)


def describe(frame: bytes) -> str:
    """TNC2 text for a KISS data frame (AX.25 without FCS)."""
    from .ax25raw import describe as describe_with_fcs
    return describe_with_fcs(frame + b"\0\0")


def enter(ser: serial.Serial, freq_10hz: int | None = None, timeout: float = 2.0) -> int | None:
    """Switch the radio's console to KISS ("k" command). Returns the frequency
    it reports, or None if there was no reply (e.g. it was already in KISS)."""
    ser.reset_input_buffer()
    ser.write(f"k {freq_10hz}\n".encode() if freq_10hz else b"k\n")
    deadline = time.time() + timeout
    while time.time() < deadline:
        raw = ser.readline()
        if bytes([FEND]) in raw:
            return None                     # KISS traffic: it already was in KISS mode
        line = raw.strip().decode("ascii", "replace")
        if line.startswith("K "):
            return int(line.split()[1])
        if line.startswith("E "):
            raise RuntimeError(line)
    return None
