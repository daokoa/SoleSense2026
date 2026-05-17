# Software

Web-side and host-side code for SoleSense. The MCU firmware lives in [`../firmware/`](../firmware/); this folder holds everything that runs in the user's browser or on a host computer connected over USB or WiFi.

## Contents

| Folder | Purpose |
|---|---|
| [`frontend/`](frontend/) | The single-page web app served from the XIAO ESP32-C3 over its WiFi access point. One canonical SPA at `solesense-v2/` (auth and AI Coach). See [`frontend/README.md`](frontend/README.md). |
| [`backend/`](backend/) | Server-side helpers that need real internet. Hosts the Cloudflare Worker proxy that runs LLM-powered run analysis; the OpenAI key lives only there, as an encrypted secret. |
| [`scripts/`](scripts/) | Host-side Python utilities for FSR bring-up and debugging. Run from a laptop while the device serves its AP. |

## scripts/

Quick references. All run against `http://192.168.4.1` while connected to the SoleSense AP:

| Script | What it does |
|---|---|
| `identify-fsr.py` | Walks each FSR channel and prompts you to press a sensor, labelling which physical FSR maps to which `(set, ADC)` matrix index. Use during initial wiring. |
| `watch-fsr.py` | Streams live FSR readings to the terminal at ~5 Hz. Useful for sanity-checking that all 6 channels move when pressed. |
| `diag-fsr.py` | Cycles power sets and ADC pins independently to diagnose stuck-low, stuck-high, or floating pins. The first thing to run when channels look wrong. |

Run with:
```bash
python3 software/scripts/diag-fsr.py
```

## Future additions

When the team adds host-side analysis tooling, training pipelines for a CNN/LSTM model, or a mobile app wrapper, those go here too -- likely as `software/analysis/`, `software/training/`, or `software/mobile/`.
