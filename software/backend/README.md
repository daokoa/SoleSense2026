# Backend

Server-side helpers that complement the offline-first SoleSense device. The XIAO ESP32-C3 hosts its own WiFi AP and never reaches the internet, so anything that needs an external API (LLM analysis, future cloud sync) lives here instead.

## Contents

| Folder | Purpose |
|---|---|
| [`analyze-worker/`](analyze-worker/) | Cloudflare Worker that proxies LLM-powered run analysis. Holds the OpenAI API key as an encrypted secret. The frontend POSTs a run report and user profile, and gets back personalized injury-risk markdown. |

## Adding new services

Each service should:

- Read its API keys exclusively from environment variables or secret-manager bindings. **Never** check secrets into git.
- Be deployable independently (no shared build pipeline).
- Document `local dev` and `deploy` commands in its own README.
- Follow the same JSON contract shape: `{ ok: boolean, ... }` for success, `{ ok: false, error: string }` for failure.
