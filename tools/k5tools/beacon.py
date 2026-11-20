"""BeaconConfig (src/beacon.h): the 64-byte APRS beacon configuration stored
in the radio's EEPROM at 0x0010."""

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

    def tnc2(self) -> str:
        path = PATHS[self.path_hops]
        return f"{self.source}>APZK5M{',' + path if path else ''}:{self.info()}"
