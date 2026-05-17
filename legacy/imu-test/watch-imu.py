#!/usr/bin/env python3
"""
watch-imu.py -- live watch of MPU-6050 accel + gyro over serial.

Reads the stream produced by firmware/imu-test/imu-test.ino (115200 baud,
columns: ax ay az gx gy gz T) and renders a fixed-position dashboard with
min/max tracking and a centered bar per axis.

Usage:
    python3 software/scripts/watch-imu.py
    python3 software/scripts/watch-imu.py --port /dev/cu.usbmodem2101
    python3 software/scripts/watch-imu.py --reset       # zero out min/max as it runs

Press Ctrl+C to stop.  Press the device with the board face-down / face-up /
on each edge in turn -- you should see exactly one accel axis read ~+/-1 g
while the other two stay near zero.
"""

import argparse
import os
import subprocess
import sys
import time

DEFAULT_PORT = "/dev/cu.usbmodem2101"
ACCEL_RANGE_G   = 2.0    # MPU-6050 default full-scale: +/- 2 g
GYRO_RANGE_DPS  = 250.0  # MPU-6050 default full-scale: +/- 250 dps
BAR_WIDTH       = 31     # odd, so there is a true center cell


def configure_tty(port):
    subprocess.run(
        ["stty", "-f", port, "115200", "cs8", "-cstopb", "-parenb", "raw", "-echo"],
        check=True,
    )


def bar(value, full_scale, width=BAR_WIDTH):
    """Centered bar: '|' walls, '.' filler, '#' marker, '+' center tick."""
    half = width // 2
    v = max(-full_scale, min(full_scale, value))
    pos = int(round(v / full_scale * half)) + half
    cells = ["."] * width
    cells[half] = "+"
    cells[pos] = "#"
    return "|" + "".join(cells) + "|"


def parse_row(line):
    parts = line.strip().split("\t")
    if len(parts) != 7:
        return None
    try:
        return [float(x) for x in parts]
    except ValueError:
        return None


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--port", default=DEFAULT_PORT, help="serial device path")
    p.add_argument("--reset", action="store_true",
                   help="reset min/max every 2 seconds (useful while tuning)")
    args = p.parse_args()

    if not os.path.exists(args.port):
        print(f"No such device: {args.port}", file=sys.stderr)
        print("Is the XIAO connected and the imu-test sketch flashed?", file=sys.stderr)
        sys.exit(1)

    try:
        configure_tty(args.port)
    except subprocess.CalledProcessError:
        print(f"Could not configure {args.port}. Is something else holding it?",
              file=sys.stderr)
        print("Try:  lsof " + args.port, file=sys.stderr)
        sys.exit(1)

    axes = ["ax", "ay", "az", "gx", "gy", "gz"]
    units = ["g", "g", "g", "dps", "dps", "dps"]
    scales = [ACCEL_RANGE_G] * 3 + [GYRO_RANGE_DPS] * 3
    mn = [float("+inf")] * 6
    mx = [float("-inf")] * 6

    rate_window = []
    last_reset = time.time()

    print("\033[2J", end="")  # clear screen once

    try:
        with open(args.port, "rb", buffering=0) as f:
            buf = b""
            while True:
                chunk = f.read(256)
                if not chunk:
                    continue
                buf += chunk
                while b"\n" in buf:
                    raw, buf = buf.split(b"\n", 1)
                    row = parse_row(raw.decode("ascii", errors="ignore"))
                    if not row:
                        continue
                    vals = row[:6]
                    temp = row[6]
                    for i in range(6):
                        if vals[i] < mn[i]: mn[i] = vals[i]
                        if vals[i] > mx[i]: mx[i] = vals[i]

                    now = time.time()
                    rate_window.append(now)
                    rate_window = [t for t in rate_window if now - t < 1.0]
                    rate = len(rate_window)

                    if args.reset and (now - last_reset) > 2.0:
                        mn = [float("+inf")] * 6
                        mx = [float("-inf")] * 6
                        last_reset = now

                    # -- render ---------------------------------------------
                    out = ["\033[H"]
                    out.append("SoleSense IMU live watch    (Ctrl+C to stop)")
                    out.append(f"port: {args.port}    rate: {rate:>3} Hz    "
                               f"T = {temp:5.1f} C\033[K")
                    out.append("")
                    out.append(f"  {'axis':<4}  {'live':>8}  {'min':>8}  {'max':>8}  "
                               f"{'range':>7}  bar (full-scale: +/-2 g / +/-250 dps)\033[K")
                    out.append("  " + "-" * 78 + "\033[K")
                    for i in range(6):
                        rng = mx[i] - mn[i] if mx[i] > mn[i] else 0.0
                        out.append(
                            f"  {axes[i]:<4}  {vals[i]:+8.3f}  {mn[i]:+8.3f}  "
                            f"{mx[i]:+8.3f}  {rng:7.3f}  {bar(vals[i], scales[i])}"
                            f"  {units[i]}\033[K"
                        )
                    out.append("\033[K")
                    a_mag = (vals[0]**2 + vals[1]**2 + vals[2]**2) ** 0.5
                    g_mag = (vals[3]**2 + vals[4]**2 + vals[5]**2) ** 0.5
                    out.append(f"  |a| = {a_mag:5.3f} g   "
                               f"(expect ~1.000 at rest)\033[K")
                    out.append(f"  |g| = {g_mag:5.2f} dps "
                               f"(expect ~0 at rest; non-zero = bias to subtract)\033[K")
                    out.append("\033[J")  # clear from cursor to end of screen
                    sys.stdout.write("\n".join(out))
                    sys.stdout.flush()
    except KeyboardInterrupt:
        print("\n\nStopped.")


if __name__ == "__main__":
    main()
