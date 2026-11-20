"""Emit AX.25 frames (hex, no FCS) for test_aprs_parse: spec examples plus
any frames decoded from an aprs_probe capture log given as argv[1]."""
import json
import sys

sys.path.insert(0, "tools")
from k5tools.ax25raw import build_frame, decode_words  # noqa: E402

SPEC = [
    "!4903.50N/07201.75W-Test 001234",            # plain, no extension
    "!4903.50N/07201.75W>088/036",                # course 88, 36 kn
    "=4903.50N/07201.75W>088/036/A=001234Hello",  # + altitude 1234 ft
    "@092345z4903.50N/07201.75W>Timestamped",     # timestamp before position
    "=/5L!!<*e7>7P[",                             # compressed: 49 30N 72 45W, crs 88, 36.2 kn
    ">Status only, no position",
]
for info in SPEC:
    print(build_frame("N0CALL", "APRS", ["WIDE1-1"], info)[:-2].hex())

if len(sys.argv) > 1:
    seen = set()
    for line in open(sys.argv[1]):
        j = json.loads(line)
        if "raw" in j:
            for hit in decode_words(j["raw"]):
                if hit.frame not in seen:
                    seen.add(hit.frame)
                    print(hit.frame[:-2].hex())
