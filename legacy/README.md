# SoleSense — legacy iterations

Snapshot of every notable iteration the codebase passed through before settling
on the current `firmware/SoleSenseV2/` + `software/frontend/solesense-v2/`
layout. Restored from git history so they're browsable without checking out
old commits.

These folders are **read-only references**. Nothing here is wired into the
current build; the live firmware and frontend live elsewhere in the repo.

## Contents

| Folder | What it is | Restored from |
|---|---|---|
| `platformio-andony/` | Andony's PlatformIO-based firmware skeleton (pre-Arduino-IDE migration). `platformio.ini` at the folder root + a full `firmware-platformio/` project tree (`src/main.cpp`, `include/`, `lib/`, `test/`). | commit at the time the Arduino-IDE sketch took over |
| `frontend-dao/` | First Dao SPA — single `index.html`, ~920 lines. Original "dao vs andony" side-by-side era. | `b00a97e` (*Tidy dao UI + sync READMEs to current state*) |
| `frontend-dao-v2/` | Dao SPA v2 — single `index.html`, ~1.6k lines. Thin viewer rewired for the v0.2 firmware contract. | `7c9bde3` (*Drop captive-portal auto-popup; keep only mDNS*) |
| `frontend-dark-spa/` | Standalone white/blue test panel that briefly served as the active frontend before being replaced by the full SPA. | `9ac473d` (*Restore white/blue test panel as active frontend*) |
| `docs-v0.1/` | v0.1 firmware implementation plan — the planning doc that drove the original firmware before the v0.2 rewrite scrubbed v0.1/v1.0 references. | `c9136a5~1` (parent of *Scrub v0.1/v1.0 + Andony references*) |
| `imu-test/` | Standalone MPU-6050 smoke-test: `imu-test.ino` (Arduino sketch) + `watch-imu.py` (terminal live-watcher with min/max tracking). | `ab06435` (*Add IMU smoke-test sketch + serial live watcher*) |
| `tinkercad-fsr-demo/` | Tinkercad 6-FSR matrix-scan demo on an Arduino Uno — reproduces the production 2-power-sets × 3-shared-ADCs architecture. Includes the `.ino` sketch, the `.brd` board file, and a screenshot. | `4e97b06` (*add TinkerCAD 6-FSR matrix-scan demo*) |

## Why this branch exists

Git history is the authoritative record, but jumping between commits to look
at old UIs or compare the PlatformIO project layout against the current
Arduino IDE one is friction. This branch flattens the most interesting
iterations side-by-side so they can be opened in a file tree like any other
code.

If you need the *full* history of any file (not just the snapshot), use
`git log --all -- <path>` and check out the commit you want.
