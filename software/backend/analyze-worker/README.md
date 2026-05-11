# SoleSense Analyze Worker

Cloudflare Worker that proxies LLM-powered run analysis. The SoleSense device is offline (its own AP, no internet), so this worker is the only thing that ever talks to OpenAI. The frontend POSTs a run-report + user profile here and gets back personalized injury-risk markdown.

## Why a worker (not direct from the phone)

The OpenAI API key would otherwise sit in the phone's localStorage or in the LittleFS-served HTML — both trivially extractable. With the worker:

```
phone (browser)  ──POST run+profile──▶  Cloudflare Worker  ──Bearer KEY──▶  OpenAI
       ▲                                         │
       └─────────────analysis markdown───────────┘
```

The key lives only as a Wrangler secret. It's never in git, never in client code, never in logs.

## One-time setup

```bash
# Cloudflare account: https://dash.cloudflare.com/sign-up (free tier is enough)
npm install -g wrangler
wrangler login            # browser-based auth
cd software/backend/analyze-worker
npm install
```

## Set the OpenAI key (NEVER commit it)

```bash
wrangler secret put OPENAI_API_KEY
# paste the key when prompted; wrangler encrypts it server-side
```

You can verify it exists with `wrangler secret list` (shows the name only, not the value).

To rotate: just run `wrangler secret put OPENAI_API_KEY` again.

## Run locally

```bash
npm run dev
# Worker now serving on http://localhost:8787

# Smoke test:
curl -X POST http://localhost:8787 \
  -H 'Content-Type: application/json' \
  -d '{
    "user": { "username": "demo", "body_kg": 72.0, "height_cm": 178, "age": 28, "sex": "M" },
    "run":  { "duration_sec": 1820, "steps": 3050, "cadence_spm": 168,
              "contact_ms": 240, "loading_rate_bws": 78,
              "pronation_dps": 12.4, "medial_pct": 58, "lateral_pct": 42,
              "zones": { "heel": 38, "midfoot": 24, "forefoot": 38 },
              "fsr_saturated": false, "imu_validated": true,
              "raw_flags": ["heel_strike", "low_cadence"] }
  }'
```

You should get back:

```json
{
  "ok": true,
  "analysis": "**What went well**\n- ...",
  "usage": { "total_tokens": 412 },
  "model": "gpt-4o-mini"
}
```

## Deploy

```bash
npm run deploy
# Wrangler prints something like:
#   Deployed solesense-analyze (1.32 sec)
#   https://solesense-analyze.<your-account>.workers.dev
```

That URL is what the frontend will call. Free-tier Workers give 100k requests/day — plenty for this.

## Tail live logs while testing

```bash
npm run tail
```

Useful when debugging upstream errors. Logs never include the API key — the code is careful to never echo `env.OPENAI_API_KEY` into log statements or response bodies.

## Optional: per-IP rate limit

Uncomment the `[[kv_namespaces]]` block in `wrangler.toml` and:

```bash
wrangler kv namespace create RATE_LIMIT
# paste the printed id into wrangler.toml
npm run deploy
```

Default policy: 30 calls/hour/IP. Tune in `src/index.ts`.

## Request / response contract

POST `/`

Body (JSON):

```ts
{
  user: {
    username?: string;
    body_kg?: number;        // strongly recommended; otherwise the LLM
                              // can't calibrate loading-rate guidance
    height_cm?: number;
    age?: number;
    sex?: string;            // free-form; LLM treats sensitively
    experience?: string;     // "beginner" | "intermediate" | "advanced"
  };
  run: {
    duration_sec?: number;
    steps?: number;
    cadence_spm?: number;
    contact_ms?: number;
    loading_rate_bws?: number;
    pronation_dps?: number;
    medial_pct?: number;
    lateral_pct?: number;
    zones?: { heel?: number; midfoot?: number; forefoot?: number };
    fsr_saturated?: boolean;
    imu_validated?: boolean;
    raw_flags?: string[];    // current rule-based flag IDs
  };
}
```

Success response:

```ts
{ ok: true; analysis: string; usage: { total_tokens: number }; model: string; }
```

Error response:

```ts
{ ok: false; error: string; }
```

Status codes: `400` bad payload, `405` wrong method, `429` rate-limited, `502` upstream OpenAI failure.

## Cost guardrails

- `MAX_TOKENS = 700` and `gpt-4o-mini` keep each call ≪ 1¢.
- Set a hard monthly cap on the OpenAI dashboard (`Settings → Billing → Usage limits`).
- Add the rate-limit KV binding above before exposing the worker URL publicly.

## Frontend integration

When the analysis feature lands in `software/frontend/solesense-v2/index.html`, the report screen will get an "Analyze with AI" button that:

1. Reads the run-report JSON from the report state (already cached after `/api/run-report`).
2. Reads `body_kg` and the rest of the profile from `localStorage` (cached at login).
3. POSTs the combined object to the worker URL.
4. Renders the returned markdown.
5. Falls back to the existing static rule-based summary when offline / worker unreachable.

The worker URL is configured in the frontend at build time — the firmware's LittleFS-served `index.html` uses a constant `ANALYZE_WORKER_URL` that points at your deployed worker.
