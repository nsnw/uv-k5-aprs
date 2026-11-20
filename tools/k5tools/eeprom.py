"""Read the UV-K5 EEPROM over the stock/F4HWN programming protocol (the one
CHIRP uses). Read-only. The radio must be running stock-compatible firmware,
not APRSK5.
"""

from __future__ import annotations

import struct
import time
from typing import Callable

from .protocol import Link

MSG_HELLO, MSG_HELLO_REPLY = 0x0514, 0x0515
MSG_READ, MSG_READ_REPLY = 0x051B, 0x051C
CHUNK = 0x80


def hello(link: Link) -> tuple[str, int]:
    """Open a session; returns (firmware version, session timestamp)."""
    timestamp = int(time.time()) & 0xFFFFFFFF
    link.ser.reset_input_buffer()
    link.send(MSG_HELLO, struct.pack("<I", timestamp))
    body = link.receive_id(MSG_HELLO_REPLY, 2.0)
    return body[:16].split(b"\0")[0].decode(errors="replace"), timestamp


def read(link: Link, size: int = 0x2000,
         progress: Callable[[int, int], None] | None = None) -> tuple[str, bytes]:
    """Returns (firmware version, EEPROM contents)."""
    version, timestamp = hello(link)
    data = bytearray()
    for offset in range(0, size, CHUNK):
        link.send(MSG_READ, struct.pack("<HBBI", offset, CHUNK, 0, timestamp))
        body = link.receive_id(MSG_READ_REPLY, 2.0)
        r_offset, r_size = struct.unpack_from("<HB", body)
        if r_offset != offset or r_size != CHUNK:
            raise IOError(f"reply for 0x{r_offset:04X}/{r_size} when reading 0x{offset:04X}")
        data += body[4:4 + CHUNK]
        if progress:
            progress(offset + CHUNK, size)
    return version, bytes(data)
