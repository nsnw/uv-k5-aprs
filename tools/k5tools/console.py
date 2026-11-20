"""Talk to the APRSK5 debug console over the programming cable.

As a library:
    with K5Console("/dev/cu.usbserial-XXXX") as k:
        k.write_reg(0x58, 0x37C3)
        print(hex(k.read_reg(0x58)))
        for ev in k.events(timeout=5): ...

"""

from __future__ import annotations

import queue
import threading
import time

import serial


class K5Console:
    def __init__(self, port: str, baud: int = 38400, raw_log: str | None = None):
        self.ser = serial.Serial(port, baud, timeout=0.05)
        # optional: every received byte with a timestamp, for debugging the link
        self._raw = open(raw_log, "a") if raw_log else None
        self.lines: queue.Queue[tuple[float, str]] = queue.Queue()
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._reader, daemon=True)
        self._thread.start()

    # ------------------------------------------------------------ plumbing

    def _reader(self) -> None:
        buf = b""
        while not self._stop.is_set():
            chunk = self.ser.read(256)
            if chunk and self._raw:
                self._raw.write(f"{time.time():.3f} {chunk.hex(' ')}  {chunk!r}\n")
                self._raw.flush()
            buf += chunk
            while b"\n" in buf:
                raw, buf = buf.split(b"\n", 1)
                line = raw.strip(b"\r").decode("ascii", "replace")
                if line:
                    self.lines.put((time.time(), line))

    def close(self) -> None:
        self._stop.set()
        self._thread.join(timeout=1)
        self.ser.close()
        if self._raw:
            self._raw.close()

    def __enter__(self) -> "K5Console":
        return self

    def __exit__(self, *exc) -> None:
        self.close()

    def send(self, line: str) -> None:
        self.ser.write(line.encode("ascii") + b"\n")

    def command(self, line: str, reply_prefix: str, timeout: float = 1.0) -> str:
        """Send a command and wait for its reply; events seen meanwhile are kept."""
        self.send(line)
        deadline = time.time() + timeout
        held: list[tuple[float, str]] = []
        try:
            while time.time() < deadline:
                try:
                    ts, l = self.lines.get(timeout=0.05)
                except queue.Empty:
                    continue
                if l.startswith(reply_prefix + " ") or l == reply_prefix:
                    return l
                if l.startswith("E "):
                    raise RuntimeError(f"{line!r}: {l}")
                held.append((ts, l))
            raise TimeoutError(f"no {reply_prefix!r} reply to {line!r}")
        finally:
            for item in held:
                self.lines.put(item)

    # ------------------------------------------------------------ helpers

    def read_reg(self, reg: int) -> int:
        return int(self.command(f"r {reg:02X}", "R").split()[2], 16)

    def write_reg(self, reg: int, value: int) -> None:
        self.command(f"w {reg:02X} {value:04X}", "W")

    def tune(self, freq_10hz: int) -> None:
        self.command(f"t {freq_10hz}", "T")

    def set_autoread(self, words: int) -> None:
        self.command(f"p {words:X}", "P")

    def read_fifo(self, words: int) -> list[int]:
        return [int(w, 16) for w in self.command(f"d {words:X}", "D").split()[1:]]

    def version(self) -> str:
        return self.command("v", "V")[2:]

    def events(self, timeout: float):
        """Yield (timestamp, line) for asynchronous output until timeout."""
        deadline = time.time() + timeout
        while time.time() < deadline:
            try:
                yield self.lines.get(timeout=min(0.1, max(0.0, deadline - time.time())))
            except queue.Empty:
                continue
