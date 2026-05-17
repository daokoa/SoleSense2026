#!/usr/bin/env python3
"""
SoleSense mock server -- screenshot-ready local stand-in for the firmware.

Run from the repo root or this folder:
    python3 software/frontend/mock-server.py
    python3 software/frontend/mock-server.py --mode claim       # first-time setup view
    python3 software/frontend/mock-server.py --mode login       # default; sign-in view
    python3 software/frontend/mock-server.py --flags none       # report shows "Looks clean"
    python3 software/frontend/mock-server.py --flags low        # 1 yellow flag
    python3 software/frontend/mock-server.py --flags multi      # 2 flags + 1 diagnostic

Open http://localhost:8080/ in any browser. Auth is permissive (any
username / PIN works) since this is for screenshots, not security.

Implements every endpoint the SPA hits, including the auth flow,
/api/run-state polling during a recording, and a fully populated
/api/run-report so the Report screen renders with realistic numbers
and the foot heatmap colors visibly.
"""

from __future__ import annotations

import argparse
import http.server
import json
import math
import os
import random
import secrets
import socketserver
import sys
import time
from urllib.parse import urlparse

PORT = 8080
THIS_DIR = os.path.dirname(os.path.abspath(__file__))
UI = "solesense-v2"
SERVE_DIR = os.path.join(THIS_DIR, UI)

if not os.path.isdir(SERVE_DIR):
    print(f"error: no UI folder at {SERVE_DIR}", file=sys.stderr)
    sys.exit(1)


# -- Configuration -----------------------------------------------------------

config = {
    "mode": "login",     # "claim" or "login"
    "flags": "low",      # "none" / "low" / "multi"
}


# -- Server state ------------------------------------------------------------

state = {
    "owner_exists": True,            # flipped by --mode
    "session_active": False,
    "session_token": "",
    "username": "",
    "body_kg": 70.0,
    "height_cm": 175.0,
    "user_count": 1,
    "recording": False,
    "run_started_at": 0.0,
    "run_stopped_at": 0.0,
    "started_at_mono": time.monotonic(),
    "has_report": False,
}


# -- Sample synthesis (FSR + IMU) --------------------------------------------

def mock_sensor():
    """One realistic-ish sample. Stride ~ 2.85 Hz (171 spm)."""
    t = time.monotonic() - state["started_at_mono"]
    stride_hz = 2.85
    phase = (t * stride_hz) % 1.0

    def pulse(p, center, width, peak):
        d = abs(p - center)
        return int(peak * (1 - d / width)) if d < width else 0

    # 3-zone medial/lateral layout — heel-dominant strike for visible heatmap.
    # ch0 heel-medial, ch1 heel-lateral, ch2 mid-medial, ch3 mid-lateral,
    # ch4 fore-medial, ch5 fore-lateral.
    fsr = [
        pulse(phase, 0.07, 0.10, 900) + random.randint(0, 30),   # heel-med
        pulse(phase, 0.07, 0.10, 820) + random.randint(0, 30),   # heel-lat
        pulse(phase, 0.13, 0.10, 520) + random.randint(0, 30),   # mid-med
        pulse(phase, 0.13, 0.10, 480) + random.randint(0, 30),   # mid-lat
        pulse(phase, 0.27, 0.10, 720) + random.randint(0, 30),   # fore-med
        pulse(phase, 0.27, 0.10, 650) + random.randint(0, 30),   # fore-lat
    ]

    bounce = 4.0 * math.sin(2 * math.pi * stride_hz * t)
    ax = 0.30 * math.sin(2 * math.pi * 2 * stride_hz * t) + random.uniform(-0.05, 0.05)
    ay = 0.50 * math.cos(2 * math.pi * stride_hz * t) + random.uniform(-0.05, 0.05)
    az = 9.81 + bounce + random.uniform(-0.10, 0.10)

    gx = 80 * math.sin(2 * math.pi * stride_hz * t) + random.uniform(-3, 3)
    gy = 30 * math.cos(2 * math.pi * stride_hz * t) + random.uniform(-3, 3)
    gz =  5 * math.sin(2 * math.pi * 2 * stride_hz * t) + random.uniform(-3, 3)

    return {
        "ax": round(ax, 3), "ay": round(ay, 3), "az": round(az, 3),
        "gx": round(gx, 3), "gy": round(gy, 3), "gz": round(gz, 3),
        "fsr":     fsr,
        "fsrEma":  [int(v * 0.9) for v in fsr],
    }


# -- Report payload (the heavy hitter) ---------------------------------------

def mock_run_report():
    """Realistic completed-run JSON. Tuned so the heatmap pads colour visibly
    and at least one finding card renders (per --flags setting). Per-foot
    `feet.left` / `feet.right` carry asymmetric dummy zone data so the
    report-screen heatmap shows visibly different patterns on each foot."""
    # Pull values from real-looking ranges. These match the firmware schema.
    cadence    = 172          # spm — in the healthy 170-180 band
    contactMs  = 245.0        # ms — Quick
    pronation  = 6.4          # deg/s — Neutral
    medialPct  = 56.0         # mild medial bias (overall)
    lateralPct = 44.0
    # Heel-dominant strike (overall).
    zone_heel     = 4200.0
    zone_midfoot  = 1600.0
    zone_forefoot = 2400.0

    # Asymmetric per-foot data: ~10-15% imbalance (realistic running
    # asymmetry, see Zifchock 2006). Left foot is the "overpronating
    # heel-striker" -- heavier on heel, more medial weight transfer.
    # Right foot is closer to neutral with a slightly forward bias.
    left_foot = {
        "zoneAvg":    {"heel": 4900.0, "midfoot": 1450.0, "forefoot": 2100.0},
        "medialPct":  60.0,
        "lateralPct": 40.0,
    }
    right_foot = {
        "zoneAvg":    {"heel": 3500.0, "midfoot": 1750.0, "forefoot": 2700.0},
        "medialPct":  52.0,
        "lateralPct": 48.0,
    }

    # Flag set per --flags.
    flags = []
    loadingRate = 48.0   # Healthy by default
    if config["flags"] in ("low", "multi"):
        # Heel-strike-dominant -> heel_strike + high_loading flags
        loadingRate = 72.0  # Elevated
        flags.append({
            "key": "high_loading",
            "val": f"{int(loadingRate)} BW/s",
        })
    if config["flags"] == "multi":
        flags.append({
            "key": "heel_strike",
            "val": f"{int(zone_heel / (zone_heel + zone_midfoot + zone_forefoot) * 100)}% heel load",
        })
        flags.append({
            "key": "fsr_saturated",
            "val": "loading rate may be underreported",
        })

    durationMs   = max(1000, int((state["run_stopped_at"] - state["run_started_at"]) * 1000)) \
                   if state["run_stopped_at"] > 0 else 60_000
    samples      = durationMs // 2  # 500 Hz
    steps        = int(cadence / 60 * durationMs / 1000)

    return {
        "steps":           steps,
        "cadence":         cadence,
        "contactMs":       contactMs,
        "loadingRate":     loadingRate,
        "pronate":         pronation,
        "medialPct":       medialPct,
        "lateralPct":      lateralPct,
        "zoneAvg": {
            "heel":     zone_heel,
            "midfoot":  zone_midfoot,
            "forefoot": zone_forefoot,
        },
        # Per-foot breakdown for the asymmetric heatmap. Real firmware on
        # a single-insole v0.2 device will omit this; the frontend falls
        # back to top-level zoneAvg for both feet when `feet` is absent.
        "feet": {
            "left":  left_foot,
            "right": right_foot,
        },
        "flags":           flags,
        "durationMs":      durationMs,
        "samples":         samples,
        "outliers":        14,
        "profile": {
            "body_kg":     state["body_kg"],
            "height_cm":   state["height_cm"],
        },
        "imuConnected":    True,
        "imuImpacts":      steps,   # one impact per step in the mock
        "maxTotalPressure": 6200,
    }


# -- Other endpoint payloads -------------------------------------------------

def mock_device():
    # Simulate a slowly draining battery (starts at 78%, drifts down over time).
    elapsed_min = (time.monotonic() - state["started_at_mono"]) / 60
    battery_pct = max(5, round(78 - elapsed_min * 0.5))
    return {
        "firmware":      "SoleSense v0.2 (mock)",
        "version":       "v0.2",
        "board":         "mock-server.py",
        "sampleRateHz":  500,
        "heap_free":     200_000,
        "state":         "recording" if state["recording"] else "idle",
        "fs":            {"totalBytes": 1_441_792, "usedBytes": 184_320},
        "hasData":       state["has_report"],
        "battery_pct":   battery_pct,
    }

def mock_auth_state():
    return {
        "ownerExists":   state["owner_exists"],
        "sessionActive": state["session_active"],
        "username":      state["username"] if state["session_active"] else "",
        "userCount":     state["user_count"],
        "maxUsers":      50,
    }

def mock_run_state():
    """Live timer + sample count for the recording screen."""
    if state["recording"]:
        elapsed_ms = int((time.monotonic() - state["run_started_at_mono"]) * 1000)
    elif state["run_stopped_at"] > 0:
        elapsed_ms = int((state["run_stopped_at"] - state["run_started_at"]) * 1000)
    else:
        elapsed_ms = 0
    return {
        "recording":        state["recording"],
        "run_active":       state["recording"],
        "elapsed_ms":       elapsed_ms,
        "sample_count":     elapsed_ms // 2,   # 500 Hz
        "clients_connected": 1,
        "outlier_count":    int(elapsed_ms / 7_000),   # one outlier per ~7 sec
    }


# -- Mock auth helpers -------------------------------------------------------

def issue_token():
    state["session_token"]  = secrets.token_hex(32)
    state["session_active"] = True
    return state["session_token"]


def auth_check(headers) -> bool:
    """Permissive: any non-empty Bearer token works. It's a screenshot mock."""
    h = headers.get("Authorization", "")
    return h.startswith("Bearer ") and len(h) > len("Bearer ")


# -- HTTP handler ------------------------------------------------------------

class Handler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=SERVE_DIR, **kwargs)

    def log_message(self, fmt, *args):
        sys.stderr.write(f"  {self.command:5} {self.path:35} -> {args[1]}\n")

    def end_headers(self):
        # Mock-server is for live iteration on the SPA -- always serve fresh.
        # Otherwise browsers cache index.html and edits don't show up without
        # a manual hard-refresh.
        self.send_header("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0")
        self.send_header("Pragma",        "no-cache")
        self.send_header("Expires",       "0")
        super().end_headers()

    def _send_json(self, status, payload):
        body = json.dumps(payload).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type",   "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control",  "no-store")
        self.end_headers()
        self.wfile.write(body)

    def _send_text(self, status, content_type, body):
        body_b = body.encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type",   content_type)
        self.send_header("Content-Length", str(len(body_b)))
        self.send_header("Cache-Control",  "no-store")
        self.end_headers()
        self.wfile.write(body_b)

    def _read_body(self) -> dict:
        n = int(self.headers.get("Content-Length", "0") or "0")
        raw = self.rfile.read(n).decode("utf-8") if n else ""
        # Form-urlencoded for auth + calibrate; JSON for nothing right now.
        out: dict = {}
        for pair in raw.split("&"):
            if "=" in pair:
                k, v = pair.split("=", 1)
                out[k] = v
        return out

    # ---- GET routes ----
    def do_GET(self):
        path = urlparse(self.path).path

        # Public
        if path == "/api/device":      return self._send_json(200, mock_device())
        if path == "/api/sensor":      return self._send_json(200, mock_sensor())
        if path == "/api/auth/state":  return self._send_json(200, mock_auth_state())
        if path == "/api/run-state":   return self._send_json(200, mock_run_state())
        if path == "/api/run-report":  return self._send_json(200, mock_run_report())
        if path == "/api/run-spectrum":
            # Trivial spectrum stub — frontend doesn't render it on the demo screen.
            return self._send_json(200, {"binsHz": [0.5*i for i in range(18)], "channels": []})
        if path == "/api/run-outliers":
            return self._send_json(200, {"outliers": [
                {"ts": 5_000, "channel": 0, "value": 4080, "sigma": 4.8},
                {"ts": 12_400, "channel": 1, "value": 3970, "sigma": 4.2},
            ]})
        if path == "/api/storage-state":
            return self._send_json(200, {"slots": 10, "bytesPerSlot": 10_240, "filled": 0})

        # Protected
        if path == "/api/auth/profile":
            if not auth_check(self.headers): return self._send_json(401, {"ok": False})
            return self._send_json(200, {
                "username":  state["username"] or "demo",
                "body_kg":   state["body_kg"],
                "height_cm": state["height_cm"],
            })

        # Default: serve static files (index.html, solesense.png, etc.)
        if path in ("/", ""):
            self.path = "/index.html"
        return super().do_GET()

    # ---- POST routes ----
    def do_POST(self):
        path = urlparse(self.path).path
        body = self._read_body()

        if path == "/api/auth/register":
            state["username"]    = body.get("username", "demo")
            state["body_kg"]     = float(body.get("body_kg",   "70"))
            state["height_cm"]   = float(body.get("height_cm", "175"))
            state["owner_exists"] = True
            state["user_count"]  = max(state["user_count"], 1)
            tok = issue_token()
            return self._send_json(200, {
                "ok": True,
                "token":     tok,
                "username":  state["username"],
                "body_kg":   state["body_kg"],
                "height_cm": state["height_cm"],
            })

        if path == "/api/auth/login":
            state["username"]    = body.get("username", "demo")
            state["owner_exists"] = True
            tok = issue_token()
            return self._send_json(200, {
                "ok": True,
                "token":     tok,
                "username":  state["username"],
                "body_kg":   state["body_kg"],
                "height_cm": state["height_cm"],
            })

        if path == "/api/auth/logout":
            if not auth_check(self.headers): return self._send_json(401, {"ok": False})
            state["session_active"] = False
            state["session_token"]  = ""
            return self._send_json(200, {"ok": True})

        if path == "/api/auth/profile":
            if not auth_check(self.headers): return self._send_json(401, {"ok": False})
            if "body_kg"   in body: state["body_kg"]   = float(body["body_kg"])
            if "height_cm" in body: state["height_cm"] = float(body["height_cm"])
            return self._send_json(200, {
                "ok": True,
                "username":  state["username"] or "demo",
                "body_kg":   state["body_kg"],
                "height_cm": state["height_cm"],
            })

        if path == "/api/start":
            if not auth_check(self.headers): return self._send_json(401, {"ok": False})
            if state["recording"]:
                return self._send_json(409, {"ok": False, "error": "already recording"})
            state["recording"]          = True
            state["run_started_at"]     = time.time()
            state["run_started_at_mono"] = time.monotonic()
            return self._send_json(200, {"ok": True})

        if path == "/api/stop":
            if not auth_check(self.headers): return self._send_json(401, {"ok": False})
            if not state["recording"]:
                return self._send_json(409, {"ok": False, "error": "not recording"})
            state["recording"]      = False
            state["run_stopped_at"] = time.time()
            state["has_report"]     = True
            return self._send_json(200, {"ok": True})

        if path == "/api/calibrate/zero":
            if not auth_check(self.headers): return self._send_json(401, {"ok": False})
            return self._send_json(200, {"ok": True, "fsrZero": [random.randint(0, 80) for _ in range(6)]})

        if path == "/api/calibrate/imu":
            if not auth_check(self.headers): return self._send_json(401, {"ok": False})
            return self._send_json(200, {
                "ok": True,
                "accel": [round(random.uniform(-0.3, 0.3), 4) for _ in range(3)],
                "gyro":  [round(random.uniform(-1.0, 1.0), 4) for _ in range(3)],
            })

        if path == "/api/data/clear":
            if not auth_check(self.headers): return self._send_json(401, {"ok": False})
            if state["recording"]:
                return self._send_json(409, {"ok": False, "error": "recording"})
            state["has_report"] = False
            return self._send_json(200, {"ok": True})

        if path == "/api/sleep":
            if not auth_check(self.headers): return self._send_json(401, {"ok": False})
            self._send_json(200, {"ok": True})
            print("\n[mock] /api/sleep called -- exiting")
            sys.exit(0)

        if path in ("/api/storage-selftest", "/api/fft-selftest"):
            return self._send_json(200, {"ok": True, "note": "mock — see firmware for the real check"})

        return self._send_text(404, "text/plain", "not found")


# -- main --------------------------------------------------------------------

def main():
    p = argparse.ArgumentParser(description="SoleSense screenshot-ready mock server")
    p.add_argument("--mode", choices=["claim", "login"], default="login",
                   help="claim: first-time setup view (no owner). login: sign-in view (owner exists). Default: login")
    p.add_argument("--flags", choices=["none", "low", "multi"], default="low",
                   help="Report flag set. none: 'Looks clean'. low: 1 amber flag. multi: 2 + diagnostic. Default: low")
    p.add_argument("--port", type=int, default=PORT)
    args = p.parse_args()

    config["mode"]  = args.mode
    config["flags"] = args.flags
    state["owner_exists"] = (args.mode == "login")

    print("SoleSense mock server")
    print(f"  mode:   {args.mode}    (start screen: {'sign-in' if args.mode == 'login' else 'claim-this-device'})")
    print(f"  flags:  {args.flags}")
    print(f"  ui dir: {SERVE_DIR}")
    print(f"  open:   http://localhost:{args.port}/")
    print("  Ctrl+C to stop.\n")

    socketserver.TCPServer.allow_reuse_address = True
    with socketserver.TCPServer(("", args.port), Handler) as httpd:
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            print("\nStopped.")


if __name__ == "__main__":
    main()
