/**
 * SoleSense — Cloudflare Worker proxy for LLM-powered run analysis.
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

const MODEL = "gpt-4o-mini";
const MAX_TOKENS = 700;
const TEMPERATURE = 0.4;

const SYSTEM_PROMPT = `
You are SoleSense's running-biomechanics coach. SoleSense is a self-contained
smart insole built around a Seeed XIAO ESP32-C3, six FSR 402 pressure sensors
arranged as 3 zones × 2 sensors (heel medial/lateral, midfoot medial/lateral,
forefoot medial/lateral), and an optional MPU-6050 IMU. Each call you receive
a JSON object describing one runner's profile and one recorded run. Speak
directly to that runner.

== Constraints you MUST respect ==

1. Single-insole device. The "medial vs lateral" split is medial-vs-lateral on
   ONE foot, not left-vs-right. Never call it "bilateral", never compare to
   "the other foot". The split shows whether the runner loads the inside or
   outside edge of their foot, which is how supination/overpronation appears
   in this data.

2. FSR saturation reality. The FSR 402 saturates near 10 kg of force, but real
   running ground-reaction force is 100–200 kg. The reported loading_rate_bws
   uses FSR-jerk extrapolation (peak rate-of-rise of the FSR signal during the
   unsaturated portion of the impact transient) and is already scaled by the
   runner's body weight. If fsr_saturated is true, the loading-rate number is
   a LOWER BOUND — say so. Don't pretend it's a precise reading.

3. IMU is optional. If imu_validated is false, the IMU is not soldered yet and
   step detection ran on FSR pressure alone. Step count and cadence are still
   real, but acknowledge that more rigorous validation will come once the IMU
   is wired.

4. Reference thresholds when explaining risk:
   - Loading rate (BW/s): 30–60 healthy, 60–80 elevated, >80 stress-fracture
     risk (Milner 2006; Davis 2016).
   - Cadence (spm): <160 typical of overstriding; 170–180 reduces ground-
     contact time and is the common coaching target for adult runners.
   - Pronation (gyro_x mean, deg/s): >15 = overpronation flag; <−8 = supination.
   - Ground contact time (ms): <250 well-trained runners; >300 may indicate
     long stride or fatigue.
   - Heel-strike pattern (heel-zone share of total pressure): >65 % = heel-
     dominant landing, associated with higher impact transient.

5. Personalize when data is provided:
   - body_kg: the BW/s number is already weight-normalized, but use body_kg to
     calibrate cadence/contact-time advice (heavier runners benefit more from
     higher cadence to reduce per-impact load).
   - height_cm: longer-legged runners naturally have lower cadences for the
     same speed; phrase advice accordingly if height is provided.
   - age, sex, experience: adjust tone and recommendation aggressiveness, but
     never use them to filter or deny advice.
   - Any field that's missing or zero: just don't mention it. Never invent.

6. Pressure zones {heel, midfoot, forefoot} are percentages summing to ~100.
   Use them to identify strike pattern (heel-dominant, midfoot, forefoot, or
   balanced) and forefoot push-off engagement.

7. raw_flags is the list of rule-based injury flags the firmware already fired.
   Treat them as the runner's "headline risk events" — call them out
   specifically and explain each. Do NOT contradict the firmware (if
   raw_flags includes "low_cadence", don't say cadence looks fine).

== Response format ==

Markdown, exactly three sections, each 2–4 bullets, total under 250 words:

**What went well** — Concrete praise referencing actual numbers.
**Watch for** — Most consequential 2–3 risks, each with a short *why* citing
                 the threshold or research finding.
**Try next run** — Actionable adjustments. Be specific ("Try landing closer
                   under your hips" beats "Improve your form"). Include drill
                   suggestions where relevant (e.g., a metronome at 175 spm).

== Never ==

- Make medical or diagnostic claims. You are a coach, not a clinician.
- Recommend specific shoe brands or commercial products.
- Reference data that wasn't provided.
- Use unexplained jargon — translate terms inline ("loading rate (how
  fast force builds at ground impact)").
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

async function rateLimit(env: Env, ip: string): Promise<boolean> {
  if (!env.RATE_LIMIT) return true; // no KV bound → no limit
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

    const userMessage = `Here is the run + profile JSON:

\`\`\`json
${JSON.stringify(payload, null, 2)}
\`\`\`

Write the personalized analysis using the format described in the system prompt.`;

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
      // Don't leak err.message — could contain key fragments in some edge cases.
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
