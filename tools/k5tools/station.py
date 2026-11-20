"""StationConfig (src/station.h): 16 bytes at EEPROM 0x0050."""

from __future__ import annotations

import struct
from dataclasses import dataclass

MAGIC = 0x3153354B          # "K5S1"
FORMAT = "<IBBBBHB5x"
assert struct.calcsize(FORMAT) == 16
DIGI, AUTOBEACON, MSG_QUIET, TX_QUIET = 0x01, 0x02, 0x04, 0x08
RX_BEEP = ["off", "new", "all"]
FLAG_MS = 8 / 1.2           # one 0x7E flag at 1200 baud


@dataclass
class Station:
    txdelay_flags: int = 40
    txtail_flags: int = 3
    digi: bool = False
    autobeacon: bool = False
    digi_max_hops: int = 2
    beacon_interval_min: int = 30
    msg_beep: bool = True           # beep for new messages (stored inverted: MSG_QUIET)
    tx_beep: bool = True            # beep after our own transmissions (stored inverted: TX_QUIET)
    rx_beep: str = "off"            # beep for received packets: off, new (stations), all

    def validate(self) -> None:
        if not 1 <= self.txdelay_flags <= 200:
            raise ValueError("TX delay must be 1-200 flags (8-1667 ms)")
        if not 1 <= self.txtail_flags <= 50:
            raise ValueError("TX tail must be 1-50 flags (8-417 ms)")
        if not 1 <= self.digi_max_hops <= 7:
            raise ValueError("digipeater hops must be 1-7")
        if not 1 <= self.beacon_interval_min <= 1440:
            raise ValueError("beacon interval must be 1-1440 minutes")
        if self.rx_beep not in RX_BEEP:
            raise ValueError("RX beep must be off, new or all")

    def pack(self) -> bytes:
        self.validate()
        flags = ((DIGI if self.digi else 0) | (AUTOBEACON if self.autobeacon else 0)
                 | (0 if self.msg_beep else MSG_QUIET) | (0 if self.tx_beep else TX_QUIET))
        return struct.pack(FORMAT, MAGIC, self.txdelay_flags, self.txtail_flags, flags,
                           self.digi_max_hops, self.beacon_interval_min, RX_BEEP.index(self.rx_beep))

    @classmethod
    def unpack(cls, data: bytes) -> "Station":
        magic, delay, tail, flags, hops, interval, rx = struct.unpack(FORMAT, data)
        if magic != MAGIC:
            raise ValueError("not a station configuration")
        return cls(delay, tail, bool(flags & DIGI), bool(flags & AUTOBEACON), hops, interval,
                   not flags & MSG_QUIET, not flags & TX_QUIET, RX_BEEP[rx] if rx < len(RX_BEEP) else "off")

    @staticmethod
    def ms_to_flags(ms: float) -> int:
        return max(1, round(ms / FLAG_MS))
