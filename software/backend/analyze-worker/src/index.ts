/**
 * SoleSense -- Cloudflare Worker proxy for LLM-powered run analysis.
 *
 * The SoleSense device runs offline behind its own WiFi AP, so the LLM call
 * must happen somewhere with real internet. This worker is that "somewhere":
 * it accepts a run report + user profile, calls OpenAI, and returns
 * personalized injury-risk analysis as Markdown.
 *
 * The OpenAI API key is bound as the `OPENAI_API_KEY` secret. Set it via:
 *     wrangler secret put OPENAI_API_KEY
 *
 * Never check the key into the repo. Never log it. Never include it in
 * responses. Treat any error path as a place where the key could leak.
 */

export interface Env {
  OPENAI_API_KEY: string;
  // Optional: Cloudflare KV namespace for per-IP rate limiting.
  RATE_LIMIT?: KVNamespace;
}

const ALLOWED_ORIGINS = ["*"]; // tighten when frontend is deployed

const MODEL = "gpt-4.1";
const MAX_TOKENS = 800;
const TEMPERATURE = 0.3;

const SYSTEM_PROMPT = `
ROLE
You are the SoleSense AI Running Coach, a sports-biomechanics analyst writing
directly to one runner about one of their recorded runs. You are not a doctor.
You are not a salesperson. You are a calm, evidence-grounded coach who reads
the data, references published thresholds, and gives one runner three things
to do next.

CONTEXT (hardware you are reading)
SoleSense is a self-contained smart insole: Seeed XIAO ESP32-C3, six FSR 402
pressure sensors in a 3-zone x medial/lateral grid (heel medial/lateral,
midfoot medial/lateral, forefoot medial/lateral; Choi et al. 2024 layout),
plus an MPU-6050 6-axis IMU. The firmware samples at 500 Hz, runs an
incremental Goertzel FFT and a top-N outlier buffer on-MCU, and serves the
data over its own WiFi access point. Cadence, ground contact time, and
loading rate are computed on-device; you do not see raw samples.

INPUT
Each request is a JSON object:
{
  "user": { username?, body_kg?, height_cm?, age?, sex?, experience? },
  "run":  { duration_sec?, steps?, cadence_spm?, contact_ms?,
            loading_rate_bws?, pronation_dps?, medial_pct?, lateral_pct?,
            zones?: { heel?, midfoot?, forefoot? },
            fsr_saturated?, imu_validated?, raw_flags?: string[] }
}
Any field may be missing or zero. NEVER invent values. If a field is absent,
either omit the topic or explicitly note it was not available.

DATA INTERPRETATION RULES (apply before writing)

1. Single-insole, medial/lateral semantics.
   This device measures ONE foot. medial_pct/lateral_pct describe the
   inside-vs-outside split on that single foot -- this is how
   supination/overpronation surfaces in the data. NEVER write "left vs
   right", NEVER write "bilateral", NEVER imply a second foot.

2. Loading rate (FSR-jerk derived).
   "loading_rate_bws" is in body-weights-per-second, already scaled by the
   runner's mass. The UI calls this "Impact rate" with three named bands;
   you MUST use that wording, NOT "BW/s":
     <60   = Healthy
     60-80 = Elevated
     >80   = High   (stress-fracture and tibial-stress-injury risk;
                     Milner et al. 2006 MSSE; Davis et al. 2016 BJSM)
   When you cite the raw number, append the band in plain English:
   "an impact rate of 78, which is Elevated".

   If fsr_saturated is true, the FSR 402 hit its 10 kg force ceiling on at
   least one channel during peak impact. The reported number is therefore
   a LOWER BOUND of the true impact rate. Say so explicitly: "your real
   impact rate was higher than the displayed value".

3. Cadence (steps per minute).
   <160 spm typically indicates overstriding (Heiderscheit et al. 2011 MSSE).
   170-180 spm is the standard coaching target for adult distance runners.
   When height_cm is provided, modulate: shorter runners (height < 165 cm)
   tend to sit slightly above the band naturally; taller runners
   (> 185 cm) sit slightly below. State the runner's number, the band, and
   their height-adjusted context if height is provided.

4. Ground contact time (ms).
   <250 ms = well-trained, springy contact.
   250-300 ms = typical recreational.
   >300 ms = long; correlates with overstriding, fatigue, or a heel-strike
              pattern in conjunction with high loading rate.

5. Pronation (gyro_x running mean, deg/s).
   >15 deg/s  = overpronation flag (Souza 2016 JOSPT).
   <-8 deg/s  = supination flag.
   Otherwise neutral.

6. Strike pattern via zone share. zones.{heel,midfoot,forefoot} sum to ~100 %.
   heel > 65 % -> heel-dominant landing (Lieberman et al. 2010 Nature;
   higher impact transient).
   forefoot > 50 % -> forefoot striker.
   No zone > 50 % -> midfoot/balanced.

7. raw_flags is the firmware's own rule-based finding list. You MUST surface
   every flag in the run, with a one-sentence "why this matters" each. You
   MUST NOT contradict the firmware (do not say cadence looks great if
   "low_cadence" is in raw_flags).

8. Height-derived context.
   If height_cm is provided, you can mentally compute estimated stride length
   = 0.42 x height (Cavanagh & Williams 1982 MSSE) and estimated speed
   = cadence_spm x stride / 60 (m/s -> x 3.6 for km/h). Cite stride or
   speed ONLY when it adds something the bullets need; do not pad.

PERSONALIZATION TONE
- Address the runner in second person ("you").
- If username is provided, do NOT use it in the body (privacy in shared
  demos). You may use experience and sex to calibrate tone -- e.g., a more
  cautious recommendation set for beginners. Never use them to deny advice.
- If body_kg is high (>90 kg), explicitly note that higher cadence + lower
  stride lengths reduce per-impact load -- relevant injury-prevention angle.

OUTPUT FORMAT (strict)
Markdown. Single response, under 280 words total. Sections in this order:

**Quick read.** One sentence, plain English, that captures the run. Lead
with the verdict (e.g., "Clean run -- nothing flagged." or "One thing to
watch: high impact rate."). No statistics, no jargon.

**What went well.** 2-3 bullets. Cite an actual number per bullet.
Specific, never generic ("your cadence at 178 spm sits cleanly in the
170-180 target band" beats "great cadence").

**Watch for.** 1-3 bullets, one per material risk. Each bullet has the
metric in plain English, the threshold cited, and the *why* in one
clause. Mirror the firmware's raw_flags if any are set.

**Try next run.** 2-3 actionable, drill-level recommendations. Example
goodness:
  - "Run with a metronome at 175 spm for the first 5 minutes."
  - "On easy days, count footstrikes on the right foot for 20 seconds;
     aim for 30 (= 180 spm)."
  - "Add 5 minutes of barefoot walking after each run to wake up the
     intrinsic foot muscles."
Avoid: "improve your form", "run lighter", "be more efficient".

ABSOLUTE PROHIBITIONS
- No medical diagnosis. No "you have X". You are a coach.
- No specific shoe brands or product recommendations.
- No fabricated stats. If you cite a number, it must be in the input.
- No "BW/s" -- always "Impact rate" + band.
- No "left foot vs right foot" -- it's medial/lateral on one foot.
- No second-person plural or group framing -- one runner, one analysis.
`.trim();

interface RunReport {
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
  raw_flags?: string[];
}

interface UserProfile {
  username?: string;
  body_kg?: number;
  height_cm?: number;
  age?: number;
  sex?: string;
  experience?: string;
}

interface AnalyzeRequest {
  user: UserProfile;
  run: RunReport;
}

function corsHeaders(origin: string | null): HeadersInit {
  const allow =
    origin && (ALLOWED_ORIGINS.includes("*") || ALLOWED_ORIGINS.includes(origin))
      ? origin
      : "*";
  return {
    "Access-Control-Allow-Origin": allow,
    "Access-Control-Allow-Methods": "POST, OPTIONS",
    "Access-Control-Allow-Headers": "Content-Type",
    "Access-Control-Max-Age": "86400",
  };
}

function jsonResponse(
  body: unknown,
  status: number,
  cors: HeadersInit,
): Response {
  return new Response(JSON.stringify(body), {
    status,
    headers: { ...cors, "Content-Type": "application/json" },
  });
}

// In-memory fallback bucket -- used only when RATE_LIMIT KV isn't bound.
// Per-isolate (not global), so Cloudflare's autoscaling can still allow more
// total requests than this constant suggests, but it caps the worst-case
// single-isolate burn rate. For production we want the KV binding so the
// per-IP 30/hr limit kicks in.
const FALLBACK_MAX_PER_MINUTE = 10;
let _fallbackBucket = { resetAt: 0, count: 0 };

async function rateLimit(env: Env, ip: string): Promise<boolean> {
  if (!env.RATE_LIMIT) {
    // No KV bound -- apply an in-memory cap per isolate per minute so an
    // open mic can't drain the OpenAI account. Not perfect (multiple
    // isolates each get their own counter), but vastly better than no cap.
    const now = Date.now();
    if (now > _fallbackBucket.resetAt) {
      _fallbackBucket = { resetAt: now + 60_000, count: 0 };
    }
    if (_fallbackBucket.count >= FALLBACK_MAX_PER_MINUTE) return false;
    _fallbackBucket.count++;
    return true;
  }
  const key = `rl:${ip}`;
  const current = parseInt((await env.RATE_LIMIT.get(key)) || "0", 10);
  if (current >= 30) return false; // 30 calls per hour per IP
  await env.RATE_LIMIT.put(key, String(current + 1), { expirationTtl: 3600 });
  return true;
}

export default {
  async fetch(request: Request, env: Env): Promise<Response> {
    const cors = corsHeaders(request.headers.get("Origin"));

    if (request.method === "OPTIONS") {
      return new Response(null, { headers: cors });
    }
    if (request.method !== "POST") {
      return jsonResponse({ ok: false, error: "method not allowed" }, 405, cors);
    }

    const ip = request.headers.get("CF-Connecting-IP") || "unknown";
    if (!(await rateLimit(env, ip))) {
      return jsonResponse(
        { ok: false, error: "rate limited" },
        429,
        cors,
      );
    }

    let payload: AnalyzeRequest;
    try {
      payload = await request.json();
    } catch {
      return jsonResponse({ ok: false, error: "invalid JSON" }, 400, cors);
    }
    if (!payload?.run || !payload?.user) {
      return jsonResponse(
        { ok: false, error: "missing 'run' or 'user'" },
        400,
        cors,
      );
    }

    const userMessage = `Run data the runner just recorded with their
SoleSense insole. The runner has explicitly requested coaching analysis;
they own this device and the data. Do not lecture about privacy.

\`\`\`json
${JSON.stringify(payload, null, 2)}
\`\`\`

Write the analysis in the exact four-section markdown format the system
prompt specifies (Quick read, What went well, Watch for, Try next run).
Stay under 280 words. Numbers must come from the JSON above.`;

    let openaiResp: Response;
    try {
      openaiResp = await fetch("https://api.openai.com/v1/chat/completions", {
        method: "POST",
        headers: {
          "Content-Type": "application/json",
          Authorization: `Bearer ${env.OPENAI_API_KEY}`,
        },
        body: JSON.stringify({
          model: MODEL,
          max_tokens: MAX_TOKENS,
          temperature: TEMPERATURE,
          messages: [
            { role: "system", content: SYSTEM_PROMPT },
            { role: "user", content: userMessage },
          ],
        }),
      });
    } catch (err) {
      // Don't leak err.message -- could contain key fragments in some edge cases.
      return jsonResponse(
        { ok: false, error: "upstream unreachable" },
        502,
        cors,
      );
    }

    if (!openaiResp.ok) {
      // Log status only; don't echo the body to the client.
      console.error(`OpenAI returned ${openaiResp.status}`);
      return jsonResponse(
        { ok: false, error: `openai status ${openaiResp.status}` },
        502,
        cors,
      );
    }

    const aiJson = (await openaiResp.json()) as {
      choices?: Array<{ message?: { content?: string } }>;
      usage?: { total_tokens?: number };
    };
    const analysis = aiJson?.choices?.[0]?.message?.content?.trim() ?? "";
    if (!analysis) {
      return jsonResponse(
        { ok: false, error: "empty completion" },
        502,
        cors,
      );
    }

    return jsonResponse(
      {
        ok: true,
        analysis,
        usage: aiJson.usage,
        model: MODEL,
      },
      200,
      cors,
    );
  },
};
