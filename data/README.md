# `/data/index.html` — duplicate of the canonical frontend

This folder contains a copy of the SoleSense frontend SPA that **does not get flashed to the device**. It exists because Andony originally placed his frontend file at `data/index.html` (repo root) before the firmware was wired up.

The **canonical frontend** that actually gets served from the XIAO ESP32-C3 lives at:

> [`firmware/SoleSense/data/index.html`](../firmware/SoleSense/data/index.html)

The Arduino IDE LittleFS upload plugin uploads from the **sketch-relative** `data/` folder (i.e., `firmware/SoleSense/data/`), not from the repo root.

## When you edit the frontend

Edit `firmware/SoleSense/data/index.html`, not this copy. Then run the LittleFS upload from Arduino IDE.

If you want to keep this root copy in sync as a reference, run from the repo root:
```bash
cp firmware/SoleSense/data/index.html data/index.html
```

We're keeping both copies for now to preserve git history, but expect to consolidate to a single location in v0.2.
