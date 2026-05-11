#!/usr/bin/env python3
"""
SoleSense mock server -- simulates the firmware backend so you can see
the frontend live in a browser without flashing the XIAO.

Run from the repo root or from this folder:
    python3 software/frontend/mock-server.py

Then open http://localhost:8080/ in any browser.

Implements the same HTTP API as the real firmware (see SOLESENSE.md 10):
    GET  /                  serves index.html
    GET  /api/device        device info JSON
    GET  /api/sensor        live FSR + IMU snapshot (the frontend polls this at 5 Hz)
    POST /api/start         begin "recording" (state-only, no file is written)
    POST /api/stop          end recording
    POST /api/calibrate/zero  fake FSR zero
    POST /api/calibrate/imu   fake IMU offsets
    POST /api/settings      accept thresholds (no validation, no persistence)
    POST /api/data/clear    clear "data"
    POST /api/sleep         logs and exits (mock)
    GET  /data.csv          returns a small synthetic CSV

Sensor data is generated to look plausibly like a runner's stride:
  - heel + lateral mid + medial mid pulse together (heel strike)
  - ball + toe pulse a moment later (toe-off)
  - accel/gyro show a low-amplitude periodic motion
  - random small noise on every channel
"""

import http.server
import json
import math
import os
import random
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

# Server "state" (mock -- not persisted, not enforced)
state = {
    "recording": False,
    "has_data": True,
    "started_at": time.monotonic(),
    "thresholds": {
        "hlr": 100,
        "proneMax": 15,
        "proneMin": -8,
        "gct": 300,
        "cadenceMin": 160,
    },
}


def mock_sensor():
    """
    Synthesize one sample of FSR + IMU data that looks like a runner's
    stride at roughly 170 spm (~=2.83 Hz).
    """
    t = time.monotonic() - state["started_at"]
    stride_hz = 2.83
    phase = (t * stride_hz) % 1.0  # 0..1 within one stride

    # Heel strike spike during phase 0.0-0.15, toe-off spike during 0.20-0.35
    def pulse(p, center, width, peak):
        d = abs(p - center)
        if d > width:
            return 0
        return int(peak * (1 - d / width))

    heel = pulse(phase, 0.07, 0.10, 850) + random.randint(0, 40)
    lat_mid = pulse(phase, 0.10, 0.10, 600) + random.randint(0, 40)
    med_mid = pulse(phase, 0.12, 0.10, 500) + random.randint(0, 40)
    ball_lat = pulse(phase, 0.25, 0.10, 700) + random.randint(0, 40)
    ball_med = pulse(phase, 0.27, 0.10, 750) + random.randint(0, 40)
    toe = pulse(phase, 0.30, 0.08, 650) + random.randint(0, 40)

    # Accel: gravity on Z plus a periodic vertical bounce
    bounce = 4.0 * math.sin(2 * math.pi * stride_hz * t)
    ax = 0.3 * math.sin(2 * math.pi * 2 * stride_hz * t) + random.uniform(-0.05, 0.05)
    ay = 0.5 * math.cos(2 * math.pi * stride_hz * t) + random.uniform(-0.05, 0.05)
    az = 9.81 + bounce + random.uniform(-0.1, 0.1)

    # Gyro: foot rotates around X (pronation axis) during stance
    gx = 80 * math.sin(2 * math.pi * stride_hz * t) + random.uniform(-3, 3)
    gy = 30 * math.cos(2 * math.pi * stride_hz * t) + random.uniform(-3, 3)
    gz = 5 * math.sin(2 * math.pi * 2 * stride_hz * t) + random.uniform(-3, 3)

    return {
        "ax": round(ax, 3), "ay": round(ay, 3), "az": round(az, 3),
        "gx": round(gx, 3), "gy": round(gy, 3), "gz": round(gz, 3),
        "fsr": [heel, lat_mid, med_mid, ball_lat, ball_med, toe],
    }


def mock_device():
    return {
        "firmware": "SoleSense v0.1 (mock-server)",
        "version": "v0.1",
        "board": "mock-server.py",
        "sampleRateHz": 50,
        "heap_free": 200000,
        "state": "recording" if state["recording"] else "idle",
        "fs": {"totalBytes": 1441792, "usedBytes": 8192},
        "hasData": state["has_data"],
        "thresholds": state["thresholds"],
    }


def mock_csv():
    """Return a tiny synthetic CSV that matches the firmware's 13-column schema."""
    lines = ["timestamp_ms,fsr1,fsr2,fsr3,fsr4,fsr5,fsr6,ax,ay,az,gx,gy,gz"]
    base_ms = 0
    for i in range(50):
        s = mock_sensor()
        lines.append(
            f"{base_ms + i*20},"
            f"{s['fsr'][0]},{s['fsr'][1]},{s['fsr'][2]},"
            f"{s['fsr'][3]},{s['fsr'][4]},{s['fsr'][5]},"
            f"{s['ax']:.2f},{s['ay']:.2f},{s['az']:.2f},"
            f"{s['gx']:.2f},{s['gy']:.2f},{s['gz']:.2f}"
        )
    return "\n".join(lines) + "\n"


class Handler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=SERVE_DIR, **kwargs)

    def log_message(self, fmt, *args):
        # Quieter than the default -- one line per request, no timestamps
        sys.stderr.write(f"  {self.command} {self.path} -> {args[1]}\n")

    def _send_json(self, status, payload):
        body = json.dumps(payload).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def _send_text(self, status, content_type, body):
        body_b = body.encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body_b)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body_b)

    def do_GET(self):
        path = urlparse(self.path).path
        if path == "/api/device":
            return self._send_json(200, mock_device())
        if path == "/api/sensor":
            return self._send_json(200, mock_sensor())
        if path == "/data.csv":
            if not state["has_data"]:
                return self._send_text(404, "text/plain", "no data")
            return self._send_text(200, "text/csv", mock_csv())
        if path in ("/", ""):
            self.path = "/index.html"
        return super().do_GET()

    def do_POST(self):
        path = urlparse(self.path).path
        # Drain body so the connection stays clean
        length = int(self.headers.get("Content-Length", "0") or "0")
        if length:
            self.rfile.read(length)

        if path == "/api/start":
            if state["recording"]:
                return self._send_json(409, {"ok": False, "error": "already recording"})
            state["recording"] = True
            state["has_data"] = True
            return self._send_json(200, {"ok": True})

        if path == "/api/stop":
            if not state["recording"]:
                return self._send_json(409, {"ok": False, "error": "not recording"})
            state["recording"] = False
            return self._send_json(200, {"ok": True})

        if path == "/api/calibrate/zero":
            return self._send_json(200, {"ok": True, "fsrZero": [random.randint(0, 80) for _ in range(6)]})

        if path == "/api/calibrate/imu":
            return self._send_json(200, {
                "ok": True,
                "accel": [round(random.uniform(-0.3, 0.3), 4) for _ in range(3)],
                "gyro":  [round(random.uniform(-1.0, 1.0), 4) for _ in range(3)],
            })

        if path == "/api/settings":
            return self._send_json(200, {"ok": True})

        if path == "/api/data/clear":
            if state["recording"]:
                return self._send_json(409, {"ok": False, "error": "recording"})
            state["has_data"] = False
            return self._send_json(200, {"ok": True})

        if path == "/api/sleep":
            self._send_json(200, {"ok": True})
            print("[mock] /api/sleep called -- exiting")
            sys.exit(0)

        return self._send_text(404, "text/plain", "not found")


def main():
    print(f"SoleSense mock server")
    print(f"  UI: {UI}")
    print(f"  Serving frontend from: {SERVE_DIR}")
    print(f"  Open in browser: http://localhost:{PORT}/")
    print(f"  Ctrl+C to stop.\n")
    socketserver.TCPServer.allow_reuse_address = True
    with socketserver.TCPServer(("", PORT), Handler) as httpd:
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            print("\nStopped.")


if __name__ == "__main__":
    main()
