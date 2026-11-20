"""Flash a raw firmware image through the UV-K5 bootloader.

Protocol as implemented by sq5bpf's k5prog: wait for the bootloader's 0x0518
announcement, send a 0x0530 version message, then 0x0519 blocks of 256 bytes,
each acknowledged by a 0x051A carrying the block offset. Unlike k5prog this
resyncs on frame markers instead of waiting for silence, ignores
announcements, and retries each block.
"""

from __future__ import annotations

import struct
import time
from typing import Callable

from .protocol import Link

BLOCK = 0x100
MAX_FLASH = 0xF000          # 60 KiB application area
MSG_ANNOUNCE = 0x0518
MSG_VERSION = 0x0530
MSG_WRITE = 0x0519
MSG_WRITE_ACK = 0x051A


class FlashError(Exception):
    pass


def check_image(image: bytes) -> None:
    if len(image) > MAX_FLASH:
        raise FlashError(f"image is {len(image)} bytes, larger than the {MAX_FLASH}-byte flash")
    if len(image) < BLOCK or struct.unpack_from("<I", image)[0] & 0xFFF00000 != 0x20000000:
        raise FlashError("image doesn't start with a RAM stack pointer; is this a raw .bin "
                         "(not .packed.bin)?")


def wait_for_bootloader(link: Link, timeout: float = 30) -> str:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        m = link.receive(1.0)
        if m and m[0] == MSG_ANNOUNCE:
            return m[1][0x10:0x20].split(b"\0")[0].decode(errors="replace")
    raise FlashError(f"no bootloader announcement within {timeout:.0f} s; is the radio in "
                     "bootloader mode (hold PTT while switching on)?")


def flash(link: Link, image: bytes, version_string: str = "*.01.23", retries: int = 5,
          progress: Callable[[int, int, int], None] | None = None) -> None:
    """Write `image`. progress(bytes_done, total, attempt) is called after each block."""
    check_image(image)
    end_block = (len(image) + BLOCK - 1) // BLOCK * BLOCK

    link.send(MSG_VERSION, version_string.encode()[:16].ljust(16, b"\0"))

    for offset in range(0, len(image), BLOCK):
        chunk = image[offset:offset + BLOCK]
        body = (bytes([0x8A, 0x8D, 0x9F, 0x1D])
                + bytes([offset >> 8, offset & 0xFF, end_block >> 8, 0])
                + struct.pack("<H", len(chunk)) + b"\0\0"
                + chunk.ljust(BLOCK, b"\0"))
        for attempt in range(1, retries + 1):
            link.send(MSG_WRITE, body)
            if _wait_ack(link, offset):
                if progress:
                    progress(offset + len(chunk), len(image), attempt)
                break
        else:
            raise FlashError(f"no acknowledgement for block 0x{offset:04X} after {retries} "
                             "attempts. The radio stays in bootloader mode; power-cycle into "
                             "bootloader mode and flash again.")


def _wait_ack(link: Link, offset: int, timeout: float = 3.0) -> bool:
    deadline = time.monotonic() + timeout
    while True:
        m = link.receive(max(0.0, deadline - time.monotonic()))
        if m is None:
            return False
        msg_id, body = m
        if msg_id == MSG_WRITE_ACK and len(body) >= 6 and body[4] == offset >> 8 and body[5] == offset & 0xFF:
            return True
        # announcements and anything else: keep waiting
