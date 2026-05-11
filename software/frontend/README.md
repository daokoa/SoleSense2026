# SoleSense Frontend

Single-page web app served from the XIAO ESP32-C3 over its WiFi AP. One canonical SPA, no internet dependency.

```
software/frontend/
|-- README.md
|-- mock-server.py        <- Python http.server that simulates the firmware
`-- solesense-v2/
    `-- index.html        <- the SPA (auth, recording, report, AI Coach)
```

The SPA is a single self-contained file using system fonts -- no build step, no external resources. The recording screen shows a 3-zone x medial/lateral foot diagram (the actual insole CAD render, cut out with a transparent background) with live FSR fill on the six visible sensor pads.

## Verify what's flashed

```bash
curl --noproxy '*' -s http://192.168.4.1/api/device | python3 -m json.tool | head -3
```

`firmware` should read `SoleSense v0.2-dev` and `sampleRateHz: 500`. The LittleFS `index.html` is a copy of `solesense-v2/index.html` made at flash time.

## Switch the UI on the device

If you've changed `solesense-v2/index.html` and want the new version on the board:

```bash
cp software/frontend/solesense-v2/index.html firmware/SoleSenseV2/data/index.html
bash firmware/SoleSenseV2/flash-littlefs.sh
```

The flash script auto-detects mklittlefs / esptool / the USB port. Close any open Serial Monitor first -- it locks the USB port and the upload fails with `exit status 2`.

## Preview locally without flashing

`mock-server.py` simulates the firmware backend with synthetic stride-shaped sensor data:

```bash
python3 software/frontend/mock-server.py
```

Open <http://localhost:8080/>. The mock implements a subset of the HTTP API (live `/api/sensor` polls, `/api/start`, `/api/stop`, `/api/calibrate/*`). The run-state, run-report, and `/api/auth/*` endpoints aren't mocked -- use real firmware to validate those.

## Auth flow

The SPA loads the auth screen first. It polls `/api/auth/state` to decide what to show:

- **No owner yet** -- claim mode. User picks a username + PIN + body weight, becomes the device owner.
- **Owner exists** -- sign-in form, with a "Create one" toggle that flips to self-signup (subject to the firmware-side caps: max 20 accounts, 3 signups per 60 s).

Token is cached in `localStorage` under `solesense_token`; `body_kg` and `username` are cached alongside so Settings can render "Signed in as ..." without a round-trip.

An `authFetch()` helper attaches `Authorization: Bearer <token>` to every protected call. Any 401 wipes localStorage and bounces the user back to login.

To factory-reset the user database, send `factory_reset\n` over USB serial; details in [`../../firmware/SoleSenseV2/README.md`](../../firmware/SoleSenseV2/README.md).

## Pressure-distribution math

Zone bars on the report screen render percentages of total foot pressure during the run, not raw ADC counts. The bars sum to 100 %. Three zones -- Heel, Midfoot, Forefoot -- match the 3-zone x medial/lateral sensor layout (two sensors per zone, averaged).

## Medial / Lateral, not Left / Right

The SPA labels the L/R-style split as **Medial / Lateral** because the system has one insole. We measure inside-of-foot vs outside-of-foot pressure on a single foot; we cannot measure left foot vs right foot without a second insole. The injury flag is named `medial_lateral_asym`.

## Injury-flag thresholds (hardcoded)

Cadence < 160 spm, pronation > 15 deg/s, supination < -8 deg/s, asymmetry > 10 %, impact rate > 80 BW/s. All come from peer-reviewed biomechanics research (sources in [`../../SOLESENSE.md`](../../SOLESENSE.md) section 13). Not user-tunable in the UI today; the firmware has a `/api/settings` endpoint and an NVS-backed thresholds struct for future tooling.

## Known limitations and gaps

- **Loading rate / Impact rate** reports a real BW/s value via FSR-jerk extrapolation, with the UI showing a Healthy / Elevated / High category. The conversion uses the logged-in user's body weight from their profile; FSR saturation point is still hardcoded at full-scale ADC = 10 kg.
- **Browser cache pitfall** -- when LittleFS gets a frontend update (new auth fields, renamed keys), iOS Safari and desktop browsers keep serving the cached old `index.html` and you'll see undefined values render as `NaN` or `26500 %`. Force-refresh after every LittleFS reflash.
- **Mock server is incomplete** -- doesn't simulate `/api/run-state`, `/api/run-report`, or the auth endpoints, so those screens need real firmware to exercise.
