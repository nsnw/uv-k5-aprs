"""Feed beacon configurations to build/test_beacon and compare its frames with
k5tools.beacon.Beacon.tnc2(). Run from the repo root."""
import subprocess
import sys

sys.path.insert(0, "tools")
from k5tools.beacon import Beacon  # noqa: E402

H = Beacon.degrees_to_hmin
cases = [
    Beacon("VE6HUE", 1, 2, "/[", H(51.039), H(-114.081), "k5-minimal test"),
    Beacon("VK2ABC", 0, 0, "/>", H(-33.8688), H(151.2093), ""),          # S/E, no SSID, no path
    Beacon("G4XYZ", 15, 1, "\\-", H(0.0001), H(-0.0001), "x" * 39),     # near 0, overlay table, max comment
    Beacon("N0CALL", 9, 2, "/k", H(89.99), H(179.99), "edge"),
]
blobs = [c.pack().hex() for c in cases]
# invalid configs must be rejected: bad magic, lowercase call
bad = bytearray(cases[0].pack()); bad[0] ^= 1
bad2 = bytearray(cases[0].pack()); bad2[4] = ord("v")
blobs += [bad.hex(), bad2.hex()]

out = subprocess.run(["build/test_beacon"], input="\n".join(blobs) + "\n",
                     capture_output=True, text=True, check=True).stdout.splitlines()
print(out[0])
ok = out[0].startswith("blank")
for c, got in zip(cases, out[1:]):
    match = got == c.tnc2()
    ok &= match
    print(("ok   " if match else "DIFF ") + got + ("" if match else f"\n     want {c.tnc2()}"))
for got in out[1 + len(cases):]:
    ok &= got == "REJECTED"
    print(("ok   " if got == "REJECTED" else "DIFF ") + f"invalid config -> {got}")
sys.exit(0 if ok else 1)
