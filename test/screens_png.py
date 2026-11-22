"""Turns test/screens.c's framebuffer dumps into PNGs that look like the LCD,
plus an index.html to page through them. Standard library only.

Usage: screens_png.py DIR   (reads DIR/*.fb, writes DIR/*.png and DIR/index.html)

Each LCD pixel is drawn SX x SY (slightly taller than wide, as on the radio)
with a faint gap between pixels.
"""
import glob
import html
import os
import struct
import sys
import zlib

SX, SY, GAP, BORDER = 5, 6, 1, 12
BG, ON, OFF = (0xc4, 0xc6, 0xc4), (0x1c, 0x1d, 0x1e), (0xbe, 0xc0, 0xbe)
ON_GAP, OFF_GAP = (0x2e, 0x2f, 0x30), (0xb4, 0xb6, 0xb4)
W, H = 128 * SX + 2 * BORDER, 64 * SY + 2 * BORDER


def write_png(path, rows):
    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d))
    raw = b"".join(b"\0" + bytes(r) for r in rows)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n"
                + chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def render(fb):
    rows = []
    for Y in range(H):
        row = bytearray()
        y = Y - BORDER
        for X in range(W):
            x = X - BORDER
            if 0 <= x < 128 * SX and 0 <= y < 64 * SY:
                px, py = x // SX, y // SY
                lit = fb[(py // 8) * 128 + px] >> (py % 8) & 1
                gap = x % SX >= SX - GAP or y % SY >= SY - GAP
                row += bytes((ON_GAP if gap else ON) if lit else (OFF_GAP if gap else OFF))
            else:
                row += bytes(BG)
        rows.append(row)
    return rows


def main():
    out = sys.argv[1]
    names = []
    for path in sorted(glob.glob(os.path.join(out, "*.fb"))):
        with open(path, "rb") as f:
            fb = f.read()
        write_png(path[:-3] + ".png", render(fb))
        names.append(os.path.basename(path)[:-3])
    figs = "\n".join(
        f'<figure><img src="{html.escape(n)}.png" width="{W}" height="{H}" alt="{html.escape(n)}">'
        f"<figcaption>{html.escape(n)}</figcaption></figure>" for n in names)
    with open(os.path.join(out, "index.html"), "w") as f:
        f.write("<!doctype html><meta charset=utf-8><title>K5-APRS screens</title>"
                "<style>body{font:14px system-ui;background:#222;color:#ddd;margin:16px}"
                "main{display:grid;grid-template-columns:repeat(auto-fill,minmax(340px,1fr));gap:20px}"
                "figure{margin:0}img{width:100%;height:auto;image-rendering:pixelated}</style>"
                f"<main>{figs}</main>\n")
    print(f"{len(names)} screens in {out}")


if __name__ == "__main__":
    main()
