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

The `dao-v2/` foot diagram shows the 3-zone × medial/lateral sensor layout (2 circles per zone) drawn on an anatomically-shaped foot SVG (toes are ellipses anchored to the foot body, asymmetric medial/lateral edges, arch indent on the medial side). Right foot is mirrored via SVG transform and greyed out — there's no second insole wired in v0.2.

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

Dao's report shows zone bars with percentages of total foot pressure during the run, not raw ADC counts. The bars sum to 100%.

- `dao/` (v0.1): four zones — Heel, Midfoot, Ball, Toe (1 heel + 2 midfoot + 2 ball + 1 toe sensor layout).
- `dao-v2/` (v0.2): three zones — Heel, Midfoot, Forefoot — matching the new 3-zone × medial/lateral sensor layout (2 sensors per zone).

## Medial / Lateral, not Left / Right

Both Dao UIs label the L/R-style split as **Medial / Lateral** because the system has *one* insole. We can measure inside-of-foot vs outside-of-foot pressure on a single foot; we cannot measure left foot vs right foot without a second insole. The injury flag is named `medial_lateral_asym`. (When a second insole gets added, true L/R becomes possible and the wording can change.)

## Thresholds are hardcoded

The injury-flag thresholds (cadence < 160 spm, pronation > 15°, supination < −8°, asymmetry > 10%, loading > 80 BW/s) come from peer-reviewed biomechanics research (sources in [`../../SOLESENSE.md`](../../SOLESENSE.md) §13) and aren't user-tunable. The firmware has a `/api/settings` endpoint and NVS-backed thresholds struct for future tooling, but no UI is wired to it.

## Known limitations and gaps

- **Loading rate** now reports a real BW/s value via FSR-jerk extrapolation (peak `d(heel_ADC)/dt` × conversion factor). The conversion currently assumes a 70 kg body weight and that the FSR + voltage divider hits 10 kg of force at full-scale ADC=4095; for users outside that envelope the displayed BW/s is a constant-factor scaling of the truth. User-configurable body weight and per-FSR saturation calibration are the next steps.
- **Ground contact time** now works — averaged across heel-strike-to-toe-off intervals from the time-domain step detector. Drops anything <50 ms (debounce) or >800 ms (lean, not a step).
- **Pressure-zone bars** show 3 zones now: Heel / Midfoot / Forefoot. The old Ball + Toe split is gone — the v0.2 sensor layout has 2 forefoot sensors averaged into one zone.
- **Andony's recording is client-side only** at 5 Hz. The firmware's 500 Hz on-device recording isn't wired into Andony's UI.
- **dao and andony cover different feature sets.** Pick one as canonical for v0.2 and fold the missing pieces in.
- **Browser side state** — dao still keeps the parsed CSV in memory after Stop. dao-v2 fixes this entirely (browser holds nothing; reconnect re-fetches from MCU).
- **Browser cache pitfall** — when the LittleFS gets a frontend update (different `zoneAvg` keys, etc.), iPhone Safari and desktop browsers will keep serving the cached old `index.html` and you'll see numbers like "26500%" because the JS reads keys that no longer exist. Force-refresh (Cmd+Shift+R on desktop, "Request New Page" or quit-and-reopen Safari on iOS) after every LittleFS reflash.

## When to use which UI

- **Live demo today:** `dao/` (v0.1) — works end-to-end, real numbers from JS analysis.
- **v0.2 architecture path:** `dao-v2/` — pair with v0.2 firmware. All headline metrics (steps, cadence, contactMs, loadingRate BW/s, pressure zones, medial/lateral split, injury flags) are real numbers from the MCU.
- **Slide screenshots / standalone preview:** `andony/` (open in any browser) or `mock-server.py` against `dao/`.
