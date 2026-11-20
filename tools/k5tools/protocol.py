"""Framing shared by the UV-K5 bootloader and the stock/F4HWN programming
protocol: 0xABCD, length, XOR-obfuscated payload + CRC-16/XMODEM, 0xDCBA.
"""

from __future__ import annotations

import struct
import time

import serial

OBFUSCATION = bytes([
    0x16, 0x6C, 0x14, 0xE6, 0x2E, 0x91, 0x0D, 0x40,
    0x21, 0x35, 0xD5, 0x40, 0x13, 0x03, 0xE9, 0x80,
])
BAUD = 38400


def xor(data: bytes) -> bytes:
    return bytes(b ^ OBFUSCATION[i % 16] for i, b in enumerate(data))


def crc16_xmodem(data: bytes) -> int:
    crc = 0
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) if crc & 0x8000 else (crc << 1)
            crc &= 0xFFFF
    return crc


def encode(msg_id: int, body: bytes) -> bytes:
    msg = struct.pack("<HH", msg_id, len(body)) + body
    payload = xor(msg + struct.pack("<H", crc16_xmodem(msg)))
    return b"\xAB\xCD" + struct.pack("<H", len(msg)) + payload + b"\xDC\xBA"


class Link:
    """A serial link speaking the framed protocol. Receiving resyncs on frame
    markers, so it copes with joining a stream mid-frame (the bootloader
    announces itself continuously). Radio replies often carry no valid CRC,
    so it isn't checked."""

    def __init__(self, port: str, timeout: float = 0.05):
        self.ser = serial.Serial(port, BAUD, timeout=timeout)
        self.rx = bytearray()

    def close(self) -> None:
        self.ser.close()

    def __enter__(self) -> "Link":
        return self

    def __exit__(self, *exc) -> None:
        self.close()

    def send(self, msg_id: int, body: bytes) -> None:
        self.ser.write(encode(msg_id, body))
        self.ser.flush()

    def receive(self, timeout: float) -> tuple[int, bytes] | None:
        """Next well-framed message as (id, body), or None on timeout."""
        deadline = time.monotonic() + timeout
        while True:
            msg = self._parse_one()
            if msg is not None:
                return msg
            if time.monotonic() >= deadline:
                return None
            self.rx += self.ser.read(self.ser.in_waiting or 1)

    def receive_id(self, want: int, timeout: float) -> bytes:
        """Body of the next message with id `want`, skipping others."""
        deadline = time.monotonic() + timeout
        while True:
            m = self.receive(max(0.0, deadline - time.monotonic()))
            if m is None:
                raise TimeoutError(f"no 0x{want:04X} reply from radio")
            if m[0] == want:
                return m[1]

    def _parse_one(self) -> tuple[int, bytes] | None:
        while True:
            start = self.rx.find(b"\xAB\xCD")
            if start < 0:
                del self.rx[:max(0, len(self.rx) - 1)]
                return None
            del self.rx[:start]
            if len(self.rx) < 4:
                return None
            (size,) = struct.unpack_from("<H", self.rx, 2)
            if size > 0x200:                 # garbage length: drop this marker
                del self.rx[:2]
                continue
            end = 4 + size + 4               # header, payload, 2 CRC/footer bytes, DC BA
            if len(self.rx) < end:
                return None
            frame = bytes(self.rx[:end])
            if frame[-2:] != b"\xDC\xBA":
                del self.rx[:2]              # false marker, resync
                continue
            del self.rx[:end]
            msg = xor(frame[4:4 + size])
            if len(msg) < 4:
                continue
            msg_id, body_len = struct.unpack_from("<HH", msg)
            return msg_id, msg[4:4 + body_len]
