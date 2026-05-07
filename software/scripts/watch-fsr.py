#!/usr/bin/env python3
"""
watch-fsr.py — live watch of all 6 FSR channels with min/max tracking.

Polls the device's /api/sensor endpoint at 5 Hz and updates a fixed-position
display so you can see live values and the press-vs-rest swing per channel.

Usage (laptop must be on the SoleSense WiFi):
    python3 software/scripts/watch-fsr.py

Optional flags:
    --rate 10           poll at 10 Hz instead of 5
    --url http://...    override device URL (default: http://192.168.4.1)

Press Ctrl+C to stop.
"""

import argparse
import json
import sys
import time
import urllib.request

DEFAULT_URL = "http://192.168.4.1"
ZONE_NAMES  = ["heel", "lat-mid", "med-mid", "ball-lat", "ball-med", "toe"]


def read_sensor(base_url):
    with urllib.request.urlopen(f"{base_url}/api/sensor", timeout=2) as r:
        return json.loads(r.read())


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--rate", type=float, default=5.0, help="poll rate in Hz (default 5)")
    p.add_argument("--url", default=DEFAULT_URL, help="device base URL")
    args = p.parse_args()

    try:
        read_sensor(args.url)
    except Exception as e:
        print(f"Could not reach {args.url}/api/sensor: {e}", file=sys.stderr)
        print("Are you connected to the SoleSense WiFi (password 'solesense')?", file=sys.stderr)
        sys.exit(1)

    mn = [99999] * 6
    mx = [-99999] * 6

    print("\033[2J", end="")  # clear screen once
    period = 1.0 / args.rate

    try:
        while True:
            try:
                d = read_sensor(args.url)
                f = d["fsr"]
                for i in range(6):
                    if f[i] < mn[i]: mn[i] = f[i]
                    if f[i] > mx[i]: mx[i] = f[i]
            except Exception as e:
                print(f"\033[H read error: {e}")
                time.sleep(period)
                continue

            print("\033[H", end="")  # cursor home
            print(f"  {'ch':>2}  {'zone':<10}  {'live':>6}  {'min':>6}  {'max':>6}  {'range':>6}")
            print("  " + "-" * 50)
            for i in range(6):
                rng = mx[i] - mn[i]
                hot = "  <-- biggest swing" if rng == max(mx[j] - mn[j] for j in range(6)) and rng > 50 else ""
                print(f"  {i:>2}  {ZONE_NAMES[i]:<10}  {f[i]:>6}  {mn[i]:>6}  {mx[i]:>6}  {rng:>6}{hot}")
            print()
            print(f"  ch0/ch3 diff: {f[0]-f[3]:+d}    (should stay within ~10 if both reading the same A0)")
            print()
            print("  Ctrl+C to stop.   Press your FSR(s) to see the live and max columns move.")
            time.sleep(period)
    except KeyboardInterrupt:
        print()
        print("Stopped.")


if __name__ == "__main__":
    main()
