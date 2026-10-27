#!/usr/bin/env python3
"""last_turn.py IN.json OUT.json: an online save's blob with GWSystem.turn (byte 4) set to max_turn (byte 5)."""
import json, sys
d = json.load(open(sys.argv[1]))
b = bytearray.fromhex(d["blob"])
b[4] = b[5]
d["blob"] = b.hex()
d["turn"] = b[4]
json.dump(d, open(sys.argv[2], "w"))
