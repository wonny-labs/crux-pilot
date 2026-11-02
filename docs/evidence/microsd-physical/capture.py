#!/usr/bin/env python3
"""Read a serial port at a fixed rate for N seconds; the speed is set on the open handle."""
import os, select, sys, termios, time

path, rate, seconds, out = sys.argv[1], int(sys.argv[2]), float(sys.argv[3]), sys.argv[4]
fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
attrs = termios.tcgetattr(fd)
attrs[0] = 0; attrs[1] = 0; attrs[3] = 0
attrs[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
attrs[4] = attrs[5] = getattr(termios, f'B{rate}')
attrs[6][termios.VMIN] = 0; attrs[6][termios.VTIME] = 0
termios.tcsetattr(fd, termios.TCSANOW, attrs)
print('speed', termios.tcgetattr(fd)[4], flush=True)
end = time.time() + seconds
with open(out, 'wb') as f:
    while time.time() < end:
        if select.select([fd], [], [], 0.2)[0]:
            data = os.read(fd, 4096)
            if data:
                f.write(data); f.flush()
os.close(fd)
