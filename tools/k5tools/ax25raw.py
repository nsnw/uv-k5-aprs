"""Recover AX.25 (APRS) frames from a raw demodulated bit stream.

The BK4819 FSK receiver hands us 16-bit FIFO words. If it is demodulating
Bell 202 tones without understanding HDLC, those words hold the raw "tone"
bit stream: NRZI-encoded, bit-stuffed, with 0x7E flags. We don't know the
chip's conventions, so we try every combination of:

  - byte order within each 16-bit word
  - bit order within each byte
  - tone polarity (irrelevant after NRZI, but kept for completeness)

and report only frames whose FCS (CRC-16/X.25) is correct, so a hit is
unambiguous.
"""

from __future__ import annotations

from dataclasses import dataclass
from itertools import product

FLAG_BITS = [0, 1, 1, 1, 1, 1, 1, 0]  # 0x7E, LSB first


def crc_x25(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0x8408 if crc & 1 else crc >> 1
    return crc ^ 0xFFFF


# ---------------------------------------------------------------- unpacking

def words_to_bits(words: list[int], swap_bytes: bool, lsb_first: bool) -> list[int]:
    bits: list[int] = []
    for w in words:
        hi, lo = (w >> 8) & 0xFF, w & 0xFF
        for byte in ((lo, hi) if swap_bytes else (hi, lo)):
            order = range(8) if lsb_first else range(7, -1, -1)
            bits.extend((byte >> i) & 1 for i in order)
    return bits


def nrzi_decode(tones: list[int]) -> list[int]:
    """1 = no tone change, 0 = tone change (AX.25 convention)."""
    return [1 if a == b else 0 for a, b in zip(tones, tones[1:])]


# ---------------------------------------------------------------- HDLC

def hdlc_frames(bits: list[int]) -> list[bytes]:
    """Split a decoded bit stream on flags, unstuff, return candidate frames."""
    frames: list[bytes] = []
    i, n = 0, len(bits)
    in_frame = False
    cur: list[int] = []
    ones = 0
    while i < n:
        if bits[i:i + 8] == FLAG_BITS:
            if in_frame and len(cur) >= 8 * 17:  # addresses + ctl + pid + fcs minimum
                usable = len(cur) - len(cur) % 8
                frames.append(bytes(
                    sum(cur[k + j] << j for j in range(8)) for k in range(0, usable, 8)
                ))
            in_frame, cur, ones = True, [], 0
            i += 8
            continue
        if not in_frame:
            i += 1
            continue
        b = bits[i]
        if b:
            ones += 1
            if ones > 6:          # abort / noise
                in_frame, cur, ones = False, [], 0
                i += 1
                continue
            cur.append(1)
        else:
            if ones == 5:         # stuffed zero
                ones = 0
                i += 1
                continue
            ones = 0
            cur.append(0)
        i += 1
    return frames


def check_fcs(frame: bytes) -> bool:
    if len(frame) < 17:
        return False
    body, fcs = frame[:-2], frame[-2] | (frame[-1] << 8)
    return crc_x25(body) == fcs


# ---------------------------------------------------------------- formatting

def _call(field: bytes) -> str:
    call = "".join(chr(b >> 1) for b in field[:6]).strip()
    ssid = (field[6] >> 1) & 0x0F
    return f"{call}-{ssid}" if ssid else call


def describe(frame: bytes) -> str:
    """TNC2-style 'SRC>DST,PATH:info' for a frame that passed FCS."""
    body = frame[:-2]
    addrs = []
    pos = 0
    while pos + 7 <= len(body):
        addrs.append(body[pos:pos + 7])
        pos += 7
        if addrs[-1][6] & 1:
            break
    if len(addrs) < 2:
        return body.hex()
    dst, src, *path = addrs
    path_s = ",".join(_call(a) + ("*" if a[6] & 0x80 else "") for a in path)
    info = body[pos + 2:]  # skip control + PID
    text = info.decode("latin-1").rstrip("\r\n")
    return f"{_call(src)}>{_call(dst)}{',' + path_s if path_s else ''}:{text}"


# ---------------------------------------------------------------- top level

@dataclass
class Hit:
    variant: str
    frame: bytes

    def __str__(self) -> str:
        return f"[{self.variant}] {describe(self.frame)}"


def decode_words(words: list[int]) -> list[Hit]:
    hits: list[Hit] = []
    seen: set[bytes] = set()
    for swap, lsb, invert in product((False, True), repeat=3):
        tones = words_to_bits(words, swap, lsb)
        if invert:
            tones = [1 - t for t in tones]
        for frame in hdlc_frames(nrzi_decode(tones)):
            if check_fcs(frame) and frame not in seen:
                seen.add(frame)
                variant = f"{'swap' if swap else 'noswap'},{'lsb' if lsb else 'msb'}"
                hits.append(Hit(variant, frame))
    return hits


# ---------------------------------------------------------------- encoder (tests)

def _encode_call(call: str, last: bool) -> bytes:
    name, _, ssid = call.partition("-")
    b = bytes((ord(c) << 1) for c in name.ljust(6)[:6])
    return b + bytes([0x60 | (int(ssid or 0) << 1) | (1 if last else 0)])


def build_frame(src: str, dst: str, path: list[str], info: str) -> bytes:
    calls = [dst, src, *path]
    addr = b"".join(_encode_call(c, i == len(calls) - 1) for i, c in enumerate(calls))
    body = addr + b"\x03\xF0" + info.encode("latin-1")
    fcs = crc_x25(body)
    return body + bytes([fcs & 0xFF, fcs >> 8])


def encode_tones(frame: bytes, flags_before: int = 20, flags_after: int = 3) -> list[int]:
    data_bits: list[int] = []
    ones = 0
    for byte in frame:
        for j in range(8):
            b = (byte >> j) & 1
            data_bits.append(b)
            if b:
                ones += 1
                if ones == 5:
                    data_bits.append(0)
                    ones = 0
            else:
                ones = 0
    bits = FLAG_BITS * flags_before + data_bits + FLAG_BITS * flags_after
    tones, t = [], 0
    for b in bits:
        if b == 0:
            t ^= 1
        tones.append(t)
    return tones


def pack_words(tones: list[int], swap_bytes: bool, lsb_first: bool) -> list[int]:
    tones = tones + [0] * (-len(tones) % 16)
    out = []
    for k in range(0, len(tones), 16):
        bs = []
        for half in (tones[k:k + 8], tones[k + 8:k + 16]):
            order = range(8) if lsb_first else range(7, -1, -1)
            bs.append(sum(bit << pos for bit, pos in zip(half, order)))
        hi, lo = (bs[1], bs[0]) if swap_bytes else (bs[0], bs[1])
        out.append((hi << 8) | lo)
    return out
