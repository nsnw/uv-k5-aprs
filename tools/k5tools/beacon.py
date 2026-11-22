"""BeaconConfig and BeaconStatus (src/beacon.h): the APRS beacon configuration
stored in the radio's EEPROM at 0x0010 (64 bytes) and the beacon type and
status text at 0x0C00 (72 bytes)."""

from __future__ import annotations

import struct
from dataclasses import dataclass

MAGIC = 0x3142354B          # "K5B1"
COMMENT_MAX = 39            # + NUL = 40 bytes
FORMAT = "<I6sBBcc2xii40s"  # must match BeaconConfig
assert struct.calcsize(FORMAT) == 64

PATHS = {0: "", 1: "WIDE1-1", 2: "WIDE1-1,WIDE2-1"}


@dataclass
class Beacon:
    call: str               # "VE6HUE"
    ssid: int               # 0..15
    path_hops: int          # 0, 1, 2
    symbol: str             # two chars: table + code, e.g. "/["
    lat_hmin: int           # hundredths of a minute, + = N
    lon_hmin: int           # hundredths of a minute, + = E
    comment: str

    # -------------------------------------------------------- conversions

    @staticmethod
    def degrees_to_hmin(deg: float) -> int:
        return round(deg * 6000)

    @property
    def lat(self) -> float:
        return self.lat_hmin / 6000

    @property
    def lon(self) -> float:
        return self.lon_hmin / 6000

    def validate(self) -> None:
        if not (3 <= len(self.call) <= 6 and self.call.isalnum() and self.call == self.call.upper()):
            raise ValueError(f"callsign {self.call!r}: 3-6 letters/digits")
        if not 0 <= self.ssid <= 15:
            raise ValueError("SSID must be 0-15")
        if self.path_hops not in PATHS:
            raise ValueError("path must be 0, 1 or 2 hops")
        if len(self.symbol) != 2 or not all("!" <= c <= "~" for c in self.symbol):
            raise ValueError("symbol must be two printable characters, e.g. /[")
        if abs(self.lat_hmin) > 90 * 6000 or abs(self.lon_hmin) > 180 * 6000:
            raise ValueError("position out of range")
        if len(self.comment) > COMMENT_MAX or not all(" " <= c <= "~" for c in self.comment):
            raise ValueError(f"comment: at most {COMMENT_MAX} printable ASCII characters")

    def pack(self) -> bytes:
        self.validate()
        return struct.pack(FORMAT, MAGIC, self.call.encode().ljust(6, b"\0"), self.ssid,
                           self.path_hops, self.symbol[0].encode(), self.symbol[1].encode(),
                           self.lat_hmin, self.lon_hmin,
                           self.comment.encode().ljust(40, b"\0"))

    @classmethod
    def unpack(cls, data: bytes) -> "Beacon":
        magic, call, ssid, hops, table, code, lat, lon, comment = struct.unpack(FORMAT, data)
        if magic != MAGIC:
            raise ValueError("not a beacon configuration")
        return cls(call.split(b"\0")[0].decode().strip(), ssid, hops,
                   (table + code).decode(), lat, lon, comment.split(b"\0")[0].decode())

    # -------------------------------------------------------- display

    @property
    def source(self) -> str:
        return f"{self.call}-{self.ssid}" if self.ssid else self.call

    @staticmethod
    def _coord(v: int, digits: int, pos: str, neg: str) -> str:
        hemi = neg if v < 0 else pos
        v = abs(v)
        return f"{v // 6000:0{digits}d}{(v % 6000) // 100:02d}.{v % 100:02d}{hemi}"

    def info(self) -> str:
        """The APRS information field the firmware sends."""
        return ("!" + self._coord(self.lat_hmin, 2, "N", "S") + self.symbol[0]
                + self._coord(self.lon_hmin, 3, "E", "W") + self.symbol[1] + self.comment)

    @property
    def has_position(self) -> bool:
        return not (self.lat_hmin == 0 and self.lon_hmin == 0)

    def header(self) -> str:
        path = PATHS[self.path_hops]
        return f"{self.source}>APZK5M{',' + path if path else ''}"

    def tnc2(self) -> str:
        return f"{self.header()}:{self.info()}"


STATUS_MAGIC = 0x3154354B   # "K5T1"
STATUS_MAX = 62
STATUS_FORMAT = "<IB3x64s"  # must match BeaconStatus
assert struct.calcsize(STATUS_FORMAT) == 72
TYPES = ["position", "status", "both"]


@dataclass
class Status:
    type: str = "position"  # position (status if no position is set), status, both
    text: str = ""          # "" = "K5-APRS <version>"

    def validate(self) -> None:
        if self.type not in TYPES:
            raise ValueError("beacon type must be position, status or both")
        if (len(self.text) > STATUS_MAX or not all(" " <= c <= "~" for c in self.text)
                or "|" in self.text or "~" in self.text):
            raise ValueError(f"status: at most {STATUS_MAX} printable ASCII characters, no | or ~")

    def pack(self) -> bytes:
        self.validate()
        return struct.pack(STATUS_FORMAT, STATUS_MAGIC, TYPES.index(self.type),
                           self.text.encode().ljust(64, b"\0"))

    @classmethod
    def unpack(cls, data: bytes) -> "Status":
        magic, kind, text = struct.unpack(STATUS_FORMAT, data)
        if magic != STATUS_MAGIC or kind >= len(TYPES):
            return cls()             # never set: the firmware's defaults
        return cls(TYPES[kind], text.split(b"\0")[0].decode())

    def frames(self, b: Beacon, version: str = "K5-APRS") -> list[str]:
        """What the radio sends for a beacon (hold F / auto-beacon), as TNC2 lines."""
        pos = b.has_position and self.type != "status"
        out = [b.tnc2()] if pos else []
        if not pos or self.type == "both":
            out.append(f"{b.header()}:>{self.text or version}")
        return out
