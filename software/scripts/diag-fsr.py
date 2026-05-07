#!/usr/bin/env python3
"""
diag-fsr.py — full diagnostic of all 6 FSR analog inputs.

Three phases:
  1. baseline (2s, don't touch anything)
  2. active (3s, press your FSR(s) repeatedly)
  3. release (1s settle)

Reports per-channel: baseline mean, baseline stddev, active range, verdict.

Verdicts:
  GROUNDED     baseline near 0 with low noise — pulldown is working, no FSR pressed
  FLOATING     baseline 200-800 with high noise — no pulldown, ADC pin floating
  RESPONSIVE   went from grounded to high during press phase — FSR is wired & working
  STUCK-HIGH   reads near 4095 — likely shorted to 3.3V

Pairs that share an analog pin should mirror each other in pressed:
  ch0 + ch3 share A0
  ch1 + ch4 share A1
  ch2 + ch5 share A2

Usage from laptop on SoleSense AP:
    python3 software/scripts/diag-fsr.py            # full 3-phase test
    python3 software/scripts/diag-fsr.py --baseline # baseline only, no press
"""

import argparse
import json
import statistics
import sys
import time
import urllib.request

DEFAULT_URL = "http://192.168.4.1"
ZONE_NAMES  = ["heel", "lat-mid", "med-mid", "ball-lat", "ball-med", "toe"]
ANALOG_PINS = ["A0", "A1", "A2", "A0", "A1", "A2"]
POWER_SETS  = [1,    1,    1,    2,    2,    2]


def read_sensor(base_url):
    with urllib.request.urlopen(f"{base_url}/api/sensor", timeout=2) as r:
        return json.loads(r.read())


def collect_for(base_url, seconds, rate_hz):
    """Return list of 6 lists, one per channel, of samples taken over `seconds`."""
    samples = [[] for _ in range(6)]
    end_at = time.monotonic() + seconds
    period = 1.0 / rate_hz
    while time.monotonic() < end_at:
        try:
            d = read_sensor(base_url)
            for i in range(6):
                samples[i].append(int(d["fsr"][i]))
        except Exception:
            pass
        time.sleep(period)
    return samples


def stats(samples):
    if not samples:
        return (0.0, 0.0, 0, 0)
    mean = statistics.mean(samples)
    std  = statistics.stdev(samples) if len(samples) > 1 else 0.0
    return (mean, std, min(samples), max(samples))


def classify_baseline(mean, std):
    """Verdict from just the baseline pass."""
    if mean > 3500:
        return "STUCK-HIGH"
    if abs(mean) < 50 and std < 30:
        return "GROUNDED  "
    return "FLOATING  "


def classify_full(b_mean, b_std, a_min, a_max):
    """Verdict from baseline + active passes."""
    base = classify_baseline(b_mean, b_std).strip()
    active_range = a_max - a_min
    # If grounded and pressed swing > 5× baseline noise → RESPONSIVE
    if base == "GROUNDED" and active_range > max(150, 5 * b_std):
        return "RESPONSIVE"
    # If floating, can't really detect a press over the noise — keep it as FLOATING
    return base + " " * (10 - len(base))


def pretty_print(baselines, actives=None):
    print()
    if actives is None:
        print(f"  {'ch':>2}  {'zone':<10}  {'pin':<3}  {'set':<3}  "
              f"{'base μ':>7}  {'base σ':>7}  verdict")
    else:
        print(f"  {'ch':>2}  {'zone':<10}  {'pin':<3}  {'set':<3}  "
              f"{'base μ':>7}  {'base σ':>7}  {'press min':>9}  {'press max':>9}  {'press Δ':>8}  verdict")
    print("  " + "-" * (60 if actives is None else 96))
    for i in range(6):
        b_mean, b_std, b_min, b_max = stats(baselines[i])
        if actives is None:
            verdict = classify_baseline(b_mean, b_std)
            print(f"  {i:>2}  {ZONE_NAMES[i]:<10}  {ANALOG_PINS[i]:<3}  {POWER_SETS[i]:<3}  "
                  f"{b_mean:>7.1f}  {b_std:>7.1f}  {verdict}")
        else:
            a_mean, a_std, a_min, a_max = stats(actives[i])
            verdict = classify_full(b_mean, b_std, a_min, a_max)
            delta = a_max - a_min
            print(f"  {i:>2}  {ZONE_NAMES[i]:<10}  {ANALOG_PINS[i]:<3}  {POWER_SETS[i]:<3}  "
                  f"{b_mean:>7.1f}  {b_std:>7.1f}  {a_min:>9}  {a_max:>9}  {delta:>8}  {verdict}")
    print()


def pair_check(actives):
    """Channels that share an analog pin should track each other when pressed."""
    pairs = [(0, 3, "A0"), (1, 4, "A1"), (2, 5, "A2")]
    print("  pair check (channels that share an analog pin should track each other):")
    for a, b, pin in pairs:
        a_max = max(actives[a]) if actives[a] else 0
        b_max = max(actives[b]) if actives[b] else 0
        if max(a_max, b_max) < 100:
            note = "no press detected on this pin"
        elif abs(a_max - b_max) < 100:
            note = f"OK — both peaked at ~{(a_max + b_max) // 2}"
        else:
            note = f"MISMATCH — ch{a} peaked at {a_max}, ch{b} peaked at {b_max}"
        print(f"    ch{a} ↔ ch{b}  ({pin}): {note}")
    print()


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--baseline", action="store_true", help="run baseline phase only (no press required)")
    p.add_argument("--rate", type=float, default=10.0, help="poll rate in Hz (default 10)")
    p.add_argument("--url", default=DEFAULT_URL, help="device base URL")
    args = p.parse_args()

    # Sanity check.
    try:
        read_sensor(args.url)
    except Exception as e:
        print(f"Could not reach {args.url}/api/sensor: {e}", file=sys.stderr)
        print("Are you connected to the SoleSense WiFi (password 'solesense')?", file=sys.stderr)
        sys.exit(1)

    print()
    print("=" * 60)
    print(" SoleSense FSR diagnostic")
    print("=" * 60)

    print()
    print("PHASE 1 — baseline (2 sec). Don't touch any sensors.")
    time.sleep(0.3)
    baselines = collect_for(args.url, 2.0, args.rate)

    if args.baseline:
        pretty_print(baselines)
        return

    pretty_print(baselines)

    print("PHASE 2 — active (3 sec). Press your FSR(s) firmly several times now!")
    for i in range(3, 0, -1):
        print(f"  starting in {i}...", end="\r")
        time.sleep(1)
    print("  GO!                                ")
    actives = collect_for(args.url, 3.0, args.rate)

    print()
    print("PHASE 3 — release. Don't touch anything.")
    time.sleep(1.0)

    pretty_print(baselines, actives)
    pair_check(actives)

    # Heuristic summary
    print("  summary:")
    responsive = [i for i in range(6) if "RESPONSIVE" in classify_full(*stats(baselines[i])[:2], *stats(actives[i])[2:])]
    grounded   = [i for i in range(6) if classify_baseline(*stats(baselines[i])[:2]).strip() == "GROUNDED" and i not in responsive]
    floating   = [i for i in range(6) if classify_baseline(*stats(baselines[i])[:2]).strip() == "FLOATING"]
    if responsive:
        print(f"    ✓  {len(responsive)} channel(s) responded to press: {responsive}")
        if 0 in responsive and 3 in responsive:
            print("       ch0 and ch3 are both A0 — your FSR(s) are on A0 (likely 3.3V-powered).")
        if 1 in responsive and 4 in responsive:
            print("       ch1 and ch4 are both A1.")
        if 2 in responsive and 5 in responsive:
            print("       ch2 and ch5 are both A2.")
    else:
        print("    ✗  no channel responded to press. Check FSR wiring and pull-down resistor.")
    if grounded:
        print(f"    ◦  {len(grounded)} channel(s) wired but quiet (pulldown OK, no FSR or no press): {grounded}")
    if floating:
        print(f"    ◦  {len(floating)} channel(s) floating (pin not pulled to GND, no resistor): {floating}")
    print()


if __name__ == "__main__":
    main()
