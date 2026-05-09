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
You are SoleSense's running-biomechanics coach. You receive a JSON object
describing a single recorded run and the runner's profile. You write a
personalized analysis in clear, friendly English that:

1. Acknowledges what went well in this run.
2. Highlights the most important biomechanical risk(s) you see.
3. Tailors recommendations to the runner's body weight, height, age, and
   sex if provided.
4. Explains *why* each recommendation matters in plain terms (e.g.
   "loading rate above 80 BW/s correlates with elevated stress-fracture
   risk per Milner 2006").
5. Avoids medical claims; uses biomechanics-coach phrasing, not doctor
   phrasing.

Output Markdown with three sections:
**What went well**, **Watch for**, **Try next run**. Keep each section
to 2–4 short bullets. Total response under 250 words.
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
