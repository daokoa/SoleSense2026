---
name: Bug report
about: Something on the firmware, frontend, or hardware isn't working as expected.
labels: bug
---

## Summary

A one-sentence description of the bug.

## Where it shows up

- [ ] Firmware (`firmware/SoleSenseV2/`)
- [ ] Frontend SPA (`software/frontend/solesense-v2/`)
- [ ] Cloudflare Worker / AI Coach (`software/backend/analyze-worker/`)
- [ ] Hardware (FSRs, MPU-6050, wiring)
- [ ] Build / flash workflow

## Steps to reproduce

1. ...
2. ...
3. ...

## Expected vs actual

**Expected:** ...
**Actual:** ...

## Environment

- Firmware version (`curl http://192.168.4.1/api/device | jq .firmware`):
- Phone / browser (e.g. iPhone 14, iOS 18, Safari):
- XIAO board revision (if known):
