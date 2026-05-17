---
name: Bug report
about: Something in the firmware, frontend, or hardware isn't working right.
labels: bug
---

## What's broken

One sentence on the bug.

## Where it happens

- [ ] Firmware (`firmware/SoleSenseV2/`)
- [ ] Frontend SPA (`software/frontend/solesense-v2/`)
- [ ] Cloudflare Worker / AI Coach (`software/backend/analyze-worker/`)
- [ ] Hardware (FSRs, MPU-6050, wiring)
- [ ] Build / flash workflow

## How to reproduce

1. ...
2. ...
3. ...

## What you saw

**Expected:** ...
**Actual:** ...

## Setup

- Firmware version (`curl http://192.168.4.1/api/device | jq .firmware`):
- Phone / browser (e.g. iPhone 14, iOS 18, Safari):
- XIAO board revision (if you know it):
