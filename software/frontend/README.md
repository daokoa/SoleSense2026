# SoleSense Frontend

Three parallel browser UIs. All connect to the firmware HTTP API; visual design and target firmware version differ.

```
software/frontend/
├── README.md           ← this file
├── mock-server.py      ← Python http.server that simulates the firmware
├── dao/
│   └── index.html      ← Dao's UI for v0.1 firmware (currently flashed)
├── dao-v2/
│   └── index.html      ← Dao's UI rewired for v0.2 firmware
└── andony/
    └── index.html      ← Andony's dark-themed alternative SPA
```

| Frontend | Pairs with | Recording-screen visual | Where the analysis runs |
|---|---|---|---|
| `dao/` | v0.1 firmware | Foot diagram, live FSR fill | Browser, after `/data.csv` download |
| `dao-v2/` | v0.2 firmware | Foot diagram, live FSR fill, "Paused" badge | MCU; browser fetches `/api/run-report` |
| `andony/` | either | Animated dark-theme readout | Browser; polls `/api/sensor` at 5 Hz |

All three are self-contained single-file SPAs. `dao/` and `dao-v2/` use system fonts. `andony/` pulls Google Fonts at runtime (fails on the SoleSense AP since there's no internet — falls back to system fonts).

## Which UI is currently on the device

```bash
curl --noproxy '*' -s http://192.168.4.1/api/device | python3 -m json.tool | head -3
```

If `firmware` shows `SoleSense v0.1`, the LittleFS data is `dao/`. If it shows `v0.2-dev`, it's `dao-v2/`. (Strictly the LittleFS `index.html` is a copy made at flash time — but in practice you flash the matching pair so this works as a check.)

## Switch the active UI

Pick a frontend, sync it into the matching firmware's `data/` folder, re-flash LittleFS:

```bash
# Demo path — v0.1 firmware + dao UI
cp software/frontend/dao/index.html firmware/SoleSense/data/index.html
bash firmware/SoleSense/flash-littlefs.sh

# Post-demo path — v0.2 firmware + dao-v2 UI
cp software/frontend/dao-v2/index.html firmware/SoleSenseV2/data/index.html
bash firmware/SoleSenseV2/flash-littlefs.sh

# Andony's alt UI on top of v0.1 firmware
cp software/frontend/andony/index.html firmware/SoleSense/data/index.html
bash firmware/SoleSense/flash-littlefs.sh
```

The flash script auto-detects mklittlefs/esptool/USB port. Close any open Serial Monitor first (it locks the USB port).

## Preview locally without flashing

`mock-server.py` simulates the firmware backend with synthetic stride-shaped sensor data:

```bash
python3 software/frontend/mock-server.py            # serves dao/  (default)
python3 software/frontend/mock-server.py andony     # serves andony/
```

Open <http://localhost:8080/>. The mock implements the same HTTP API surface as the real firmware. Ctrl+C to stop.

(For dao-v2, point the mock at it manually if you need to iterate offline — the mock currently doesn't expose v0.2-only endpoints like `/api/run-state` or `/api/run-report`. That's a TODO; for now use real firmware to validate dao-v2.)

## Edit each UI

Edit the source file in `dao/`, `dao-v2/`, or `andony/` directly. **Don't edit `firmware/{SoleSense,SoleSenseV2}/data/index.html`** — those are deployment artifacts that get overwritten by the `cp` step above.

## Feature matrix

| Feature | `dao/` (v0.1) | `dao-v2/` (v0.2) | `andony/` |
|---|---|---|---|
| Home / Recording / Report / Settings screens | ✓ | ✓ | ✓ (no Settings) |
| `POST /api/start` / `/api/stop` | ✓ | ✓ | (records client-side) |
| Live FSR readout during recording (foot diagram) | ✓ | ✓ | — |
| Live FSR readout (alt animated viz) | — | — | ✓ |
| Pause-on-disconnect "Paused" badge | — | ✓ | — |
| Recording timer source | JS `setInterval` | `/api/run-state.elapsed_ms` (MCU truth) | JS `setInterval` |
| Report from `/data.csv` parse + JS analysis | ✓ | — | — |
| Report from `/api/run-report` (MCU computes) | — | ✓ | — |
| Report (in-browser only, dummy data) | — | — | ✓ |
| `POST /api/calibrate/zero` & `/api/calibrate/imu` UI | ✓ | ✓ | — |
| `POST /api/data/clear` UI | ✓ | ✓ | — |
| Pressure distribution by zone | ✓ | ✓ (firmware computes) | — |
| Two-foot diagram with anatomical sensor positions | ✓ | ✓ | — |

The full HTTP API surface is documented in the root [`README.md`](../../README.md#api).

## Pressure distribution math

Dao's report shows four zone bars — Heel, Midfoot, Ball, Toe — with a percentage. Each value is the share of total foot pressure that zone carried during the run, not raw ADC counts. The four percentages sum to 100% (e.g., Heel 38%, Midfoot 19%, Ball 28%, Toe 15%). Lets you see "this runner heel-strikes hard" or "forefoot-dominant" at a glance.

## Medial / Lateral, not Left / Right

Both Dao UIs label the L/R-style split as **Medial / Lateral** because the system has *one* insole. We can measure inside-of-foot vs outside-of-foot pressure on a single foot; we cannot measure left foot vs right foot without a second insole. The injury flag is named `medial_lateral_asym`. (When a second insole gets added, true L/R becomes possible and the wording can change.)

## Thresholds are hardcoded

The injury-flag thresholds (cadence < 160 spm, pronation > 15°, supination < −8°, asymmetry > 10%, loading > 80 BW/s) come from peer-reviewed biomechanics research (sources in [`../../SOLESENSE.md`](../../SOLESENSE.md) §13) and aren't user-tunable. The firmware has a `/api/settings` endpoint and NVS-backed thresholds struct for future tooling, but no UI is wired to it.

## Known limitations and gaps

- **Loading rate currently shows "—"** in dao-v2 because the v0.2 firmware doesn't have a working loading-rate calculation yet. Plan: FSR-jerk extrapolation (track the rate-of-rise of the FSR signal during the brief unsaturated portion of impact, since the FSR caps at 10 kg but a runner's impact is 100–200 kg). Until that lands, `loadingRate = 0` in the firmware response and the chip renders "—". Honest.
- **Ground contact time** also "—" in dao-v2 — needs time-domain step detection in the firmware.
- **Andony's recording is client-side only** at 5 Hz. The firmware's 50 Hz on-device recording isn't wired into Andony's UI.
- **dao and andony cover different feature sets.** Pick one as canonical for v0.2 and fold the missing pieces in.
- **Browser side state** — dao still keeps the parsed CSV in memory after Stop. dao-v2 fixes this entirely (browser holds nothing; reconnect re-fetches from MCU).

## When to use which UI

- **Live demo today:** `dao/` (v0.1) — works end-to-end, real numbers from JS analysis.
- **Future / showing the v0.2 architecture:** `dao-v2/` — but pair with v0.2 firmware. Some metrics will read "—" until the open firmware tasks land (loading rate, GCT).
- **Slide screenshots / standalone preview:** `andony/` (open in any browser) or `mock-server.py` against `dao/`.
