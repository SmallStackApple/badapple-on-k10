import sys
import time

import serial

port = sys.argv[1]
duration = float(sys.argv[2])
path = sys.argv[3]

s = serial.Serial(port, 115200, timeout=0.2)
with open(path, "a", encoding="utf-8", errors="replace") as f:
    t0 = time.time()
    while time.time() - t0 < duration:
        data = s.read(4096)
        if data:
            f.write(data.decode("utf-8", "replace"))
            f.flush()
s.close()
