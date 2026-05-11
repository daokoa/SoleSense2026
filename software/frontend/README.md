# SoleSense Frontend

Two parallel browser UIs, one per firmware generation. Both connect to the firmware HTTP API; the v2 SPA is the canonical one going forward.

```
software/frontend/
├── README.md             ← this file
├── mock-server.py        ← Python http.server that simulates the firmware
├── solesense-v1/
│   └── index.html        ← v0.1-compatible UI (parses /data.csv, JS analysis)
└── solesense-v2/
    └── index.html        ← v0.2-compatible UI (polls /api/run-report, MCU does the math)
```

| Frontend | Pairs with | Recording-screen visual | Where the analysis runs |
|---|---|---|---|
| `solesense-v1/` | v0.1 firmware | Foot diagram, live FSR fill | Browser, after `/data.csv` download |
| `solesense-v2/` | v0.2 firmware | Foot diagram, live FSR fill, "Paused" badge, AI Coach | MCU; browser fetches `/api/run-report` |

Both are self-contained single-file SPAs using system fonts.

The `solesense-v2/` foot diagram shows the 3-zone × medial/lateral sensor layout (2 circles per zone) drawn on a cut-out CAD render of the actual insole. Right foot is the same image; both have the live FSR fill so you can verify wiring. Auth screen lives at the front of the SPA — sign in or create an account before reaching the recording flow.

## Which UI is currently on the device

```bash
curl --noproxy '*' -s http://192.168.4.1/api/device | python3 -m json.tool | head -3
```

If `firmware` shows `SoleSense v0.1`, the LittleFS data is `solesense-v1/`. If it shows `v0.2-dev`, it's `solesense-v2/`. (Strictly the LittleFS `index.html` is a copy made at flash time — but in practice you flash the matching pair so this works as a check.)

## Switch the active UI

Pick a frontend, sync it into the matching firmware's `data/` folder, re-flash LittleFS:

```bash
# Demo path — v0.1 firmware + v1 UI
cp software/frontend/solesense-v1/index.html firmware/SoleSense/data/index.html
bash firmware/SoleSense/flash-littlefs.sh

# Active development — v0.2 firmware + v2 UI
cp software/frontend/solesense-v2/index.html firmware/SoleSenseV2/data/index.html
bash firmware/SoleSenseV2/flash-littlefs.sh
```

The flash script auto-detects mklittlefs/esptool/USB port. Close any open Serial Monitor first (it locks the USB port).

## Preview locally without flashing

`mock-server.py` simulates the firmware backend with synthetic stride-shaped sensor data:

```bash
python3 software/frontend/mock-server.py                  # serves solesense-v2/ (default)
python3 software/frontend/mock-server.py solesense-v1     # serves solesense-v1/
```

Open <http://localhost:8080/>. The mock implements the v0.1-style HTTP API surface. Ctrl+C to stop.

(For `solesense-v2`, the mock currently doesn't expose v0.2-only endpoints like `/api/run-state`, `/api/run-report`, or the auth endpoints. Use real firmware to validate the v2 SPA end-to-end.)

## Edit each UI

Edit the source file in `solesense-v1/` or `solesense-v2/` directly. **Don't edit `firmware/{SoleSense,SoleSenseV2}/data/index.html`** — those are deployment artifacts that get overwritten by the `cp` step above.

## Feature matrix

| Feature | `solesense-v1/` (v0.1) | `solesense-v2/` (v0.2) |
|---|---|---|
| Home / Recording / Report / Settings screens | ✓ | ✓ |
| Auth (login + self-signup) | — | ✓ |
| `POST /api/start` / `/api/stop` | ✓ | ✓ |
| Live FSR readout during recording (foot diagram) | ✓ | ✓ |
| Pause-on-disconnect "Paused" badge | — | ✓ |
| "Disconnected" status indicator | — | ✓ |
| Recording timer source | JS `setInterval` | `/api/run-state.elapsed_ms` (MCU truth) |
| Report from `/data.csv` parse + JS analysis | ✓ | — |
| Report from `/api/run-report` (MCU computes) | — | ✓ |
| AI Coach (LLM-personalised feedback) | — | ✓ |
| `POST /api/calibrate/zero` & `/api/calibrate/imu` UI | ✓ | ✓ |
| `POST /api/data/clear` UI | ✓ | ✓ |
| Pressure distribution by zone | ✓ (4 zones) | ✓ (3 zones × med/lat) |
| Two-foot diagram with anatomical sensor positions | ✓ | ✓ |

The full HTTP API surface is documented in the root [`README.md`](../../README.md#api).

## Pressure distribution math

Both UIs show zone bars with percentages of total foot pressure during the run, not raw ADC counts. The bars sum to 100 %.

- `solesense-v1/`: four zones — Heel, Midfoot, Ball, Toe (legacy 1 heel + 2 midfoot + 2 ball + 1 toe sensor layout).
- `solesense-v2/`: three zones — Heel, Midfoot, Forefoot — matching the v0.2 3-zone × medial/lateral sensor layout (2 sensors per zone).

## Medial / Lateral, not Left / Right

Both UIs label the L/R-style split as **Medial / Lateral** because the system has *one* insole. We can measure inside-of-foot vs outside-of-foot pressure on a single foot; we cannot measure left foot vs right foot without a second insole. The injury flag is named `medial_lateral_asym`. (When a second insole gets added, true L/R becomes possible and the wording can change.)

## Thresholds are hardcoded

The injury-flag thresholds (cadence < 160 spm, pronation > 15°, supination < −8°, asymmetry > 10%, impact rate > 80 BW/s) come from peer-reviewed biomechanics research (sources in [`../../SOLESENSE.md`](../../SOLESENSE.md) §13) and aren't user-tunable. The firmware has a `/api/settings` endpoint and NVS-backed thresholds struct for future tooling, but no UI is wired to it.

## Auth (solesense-v2 only)

The v2 SPA requires login. On first load it polls `/api/auth/state`:

- If no owner exists, the user lands in **claim mode** — sets a username + PIN + body weight, becoming the device owner.
- Otherwise, a **Sign in** form. A toggle at the bottom flips to a **Create account** form for any subsequent user on the AP (anti-spam caps live server-side: max 20 accounts, 3 signups per 60 s window).
- Wrong PIN three times in 60 s locks the username for 30 s.
- Token is stored in `localStorage` under `solesense_token`. `body_kg` and `username` are cached too so Settings can show "Signed in as …" without a round-trip.
- An `authFetch()` helper attaches `Authorization: Bearer <token>` to every protected call. Any 401 wipes localStorage and bounces back to login.
- Logout: clear the session via `POST /api/auth/logout` and back to the auth screen.

To factory-reset the user database, send `factory_reset\n` over USB serial — see firmware/SoleSenseV2/README.md.

## Known limitations and gaps

- **Loading rate / Impact rate** now reports a real BW/s value via FSR-jerk extrapolation, with the UI showing a Healthy / Elevated / High category for runners who don't speak biomech. The conversion uses the logged-in user's body weight from their profile.
- **Ground contact time** now works — averaged across heel-strike-to-toe-off intervals from the Kalman-filtered time-domain step detector.
- **Pressure-zone bars** show 3 zones now: Heel / Midfoot / Forefoot.
- **`solesense-v1/`** is kept as a working fallback for the live demo path; new features land in `solesense-v2/` only.
- **Browser cache pitfall** — when LittleFS gets a frontend update (new auth fields, new keys), iPhone Safari and desktop browsers will keep serving the cached old `index.html` and you'll see undefined values render as `NaN` or `26500 %`. Force-refresh after every LittleFS reflash.

## When to use which UI

- **Live demo today:** `solesense-v1/` — works end-to-end, real numbers from JS analysis.
- **v0.2 architecture path:** `solesense-v2/` — pair with v0.2 firmware. Auth, AI Coach, IMU fusion, Kalman step detector, all the v2-only metrics.
- **Slide screenshots / standalone preview:** `mock-server.py` against `solesense-v1` (the mock implements the v0.1 API surface; v2-only endpoints need real firmware).
