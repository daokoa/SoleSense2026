#!/usr/bin/env python3
"""
identify-fsr.py -- figure out which FSR channel has a real sensor connected.

Polls the device's /api/sensor endpoint for ~3 seconds while you press your
FSR a few times. Reports min/max/range per channel and identifies the most
likely connected one.

Usage (laptop must be on the SoleSense WiFi):
    python3 software/scripts/identify-fsr.py

Optional flags:
    --duration 5        watch for 5 seconds instead of 3
    --rate 10           poll at 10 Hz instead of 5
    --url http://...    override device URL (default: http://192.168.4.1)
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
    p.add_argument("--duration", type=float, default=3.0, help="seconds to watch (default 3)")
    p.add_argument("--rate", type=float, default=5.0, help="poll rate in Hz (default 5)")
    p.add_argument("--url", default=DEFAULT_URL, help="device base URL")
    args = p.parse_args()

    # Sanity check the connection first.
    try:
        read_sensor(args.url)
    except Exception as e:
        print(f"Could not reach {args.url}/api/sensor: {e}", file=sys.stderr)
        print("Are you connected to the SoleSense WiFi (password 'solesense')?", file=sys.stderr)
        sys.exit(1)

    print(f"Press and release your FSR firmly several times over the next {args.duration:.0f} seconds.")
    print("Recording...")
    print()

    mn = [99999] * 6
    mx = [-99999] * 6
    n  = 0

    end_at = time.monotonic() + args.duration
    period = 1.0 / args.rate
    while time.monotonic() < end_at:
        try:
            d = read_sensor(args.url)
            for i, v in enumerate(d["fsr"]):
                if v < mn[i]: mn[i] = v
                if v > mx[i]: mx[i] = v
            n += 1
        except Exception as e:
            print(f"  read error: {e}")
        time.sleep(period)

    print(f"Done -- {n} samples taken.")
    print()

    ranges = [mx[i] - mn[i] for i in range(6)]
    sorted_r = sorted(ranges, reverse=True)
    best = ranges.index(sorted_r[0])
    runner_up = sorted_r[1] if len(sorted_r) > 1 else 0

    # Header.
    print(f"  {'ch':>2}  {'zone':<10}  {'min':>6}  {'max':>6}  {'range':>6}")
    print("  " + "-" * 38)
    for i in range(6):
        marker = "  <-- likely your FSR" if i == best else ""
        print(f"  {i:>2}  {ZONE_NAMES[i]:<10}  {mn[i]:>6}  {mx[i]:>6}  {ranges[i]:>6}{marker}")

    print()

    # Heuristic verdict.
    if sorted_r[0] < 100:
        print("WARNING:  No channel had a meaningful range. Did you press hard enough?")
        print("    Possible causes: FSR not wired, no pull-down resistor, wrong analog pin,")
        print("    or the FSR is dead/shorted. Check the wiring against the README pin map.")
        sys.exit(2)

    if sorted_r[0] < 1.5 * runner_up:
        print(f"WARNING:  Channel {best} ({ZONE_NAMES[best]}) is the largest, but other channels")
        print(f"    are within ~1.5x of it. That's likely floating-pin noise -- your FSR")
        print(f"    might not be wired in, or its pull-down resistor is missing.")
        sys.exit(3)

    print(f"[x]  Your FSR is on channel {best} ({ZONE_NAMES[best]}).")
    print(f"   Wiring: this is " +
          ("Set 1 ADC " + "ABC"[best]      + f" -- power pin GPIO5, analog pin GPIO{2+best}." if best < 3 else
           "Set 2 ADC " + "ABC"[best - 3]  + f" -- power pin GPIO10, analog pin GPIO{2+best-3}."))


if __name__ == "__main__":
    main()
