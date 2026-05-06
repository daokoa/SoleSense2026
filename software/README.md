# Software

Web-side code for SoleSense. The MCU code lives in [`../firmware/`](../firmware/); this folder is for everything that runs in the user's browser or on a host computer.

## Contents

| Folder | Purpose |
|---|---|
| [`frontend/`](frontend/) | The single-page web app served from the XIAO ESP32-C3 over its WiFi access point. This is the canonical, editable copy. |

## Future additions

When the team adds host-side analysis tooling, training pipelines for the v1.0 CNN/LSTM model, or a mobile app wrapper, those go here too — likely as `software/analysis/`, `software/training/`, `software/mobile/`.
