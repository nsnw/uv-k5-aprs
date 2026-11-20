"""Print raw FIFO captures from an aprs_probe log, one capture per line."""
import json
import sys

for line in open(sys.argv[1]):
    j = json.loads(line)
    if "raw" in j:
        print(" ".join(f"{w:04X}" for w in j["raw"]))
