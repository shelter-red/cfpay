import type { Env } from "./types";

export type TurnstileEnv = Pick<Env, "TURNSTILE_SITE_KEY" | "TURNSTILE_SECRET_KEY">;

export interface TurnstileConfig {
  enabled: boolean;
  siteKey: string | null;
  secretKey: string | null;
}

export interface TurnstileVerification {
  success: boolean;
  errorCodes: string[];
}

const SITEVERIFY_URL = "https://challenges.cloudflare.com/turnstile/v0/siteverify";
const VERIFY_TIMEOUT_MS = 8_000;

interface SiteVerifyResponse {
  success?: boolean;
  "error-codes"?: unknown;
}

/**
 * 与 cffk 的 lib/turnstile-config.ts 保持一致：站点 Key 与 Secret Key 必须同时配置才启用，
 * 只配一个视为未启用，避免前端弹了验证框而后端不校验（或反之）造成登录直接失败。
 */
export function getTurnstileConfig(env: TurnstileEnv): TurnstileConfig {
  const siteKey = typeof env.TURNSTILE_SITE_KEY === "string" ? env.TURNSTILE_SITE_KEY.trim() : "";
  const secretKey = typeof env.TURNSTILE_SECRET_KEY === "string" ? env.TURNSTILE_SECRET_KEY.trim() : "";
  return {
    enabled: Boolean(siteKey && secretKey),
    siteKey: siteKey || null,
    secretKey: secretKey || null,
  };
}

/**
 * 校验登录携带的 Turnstile 令牌。未启用时直接放行；启用后任何一步不通过都拒绝登录。
 * 失败原因用 Cloudflare 的 error-codes 原样返回，便于在后台定位是令牌过期还是站点不匹配。
 */
export async function verifyTurnstileToken(
  env: TurnstileEnv,
  token: string | null,
  remoteIp: string | null,
): Promise<TurnstileVerification> {
  const config = getTurnstileConfig(env);
  if (!config.enabled) return { success: true, errorCodes: [] };
  if (!token) return { success: false, errorCodes: ["missing-input-response"] };

  const body = new URLSearchParams({ secret: config.secretKey ?? "", response: token });
  if (remoteIp) body.set("remoteip", remoteIp);

  let response: Response;
  try {
    response = await fetch(SITEVERIFY_URL, {
      method: "POST",
      headers: { "content-type": "application/x-www-form-urlencoded" },
      body,
      signal: AbortSignal.timeout(VERIFY_TIMEOUT_MS),
    });
  } catch (error) {
    const timeout = error instanceof Error && (error.name === "TimeoutError" || error.name === "AbortError");
    return { success: false, errorCodes: [timeout ? "verify-timeout" : "verify-network-error"] };
  }

  if (!response.ok) return { success: false, errorCodes: [`http-${response.status}`] };
  const data = (await response.json().catch(() => null)) as SiteVerifyResponse | null;
  if (!data) return { success: false, errorCodes: ["invalid-verify-response"] };
  const errorCodes = Array.isArray(data["error-codes"])
    ? data["error-codes"].filter((code): code is string => typeof code === "string")
    : [];
  return { success: data.success === true, errorCodes };
}
