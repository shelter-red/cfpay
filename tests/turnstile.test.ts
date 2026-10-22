import { afterEach, describe, expect, it, vi } from "vitest";
import { getTurnstileConfig, verifyTurnstileToken } from "../src/turnstile";
import type { Env } from "../src/types";

const bothKeys = { TURNSTILE_SITE_KEY: " site-key ", TURNSTILE_SECRET_KEY: " secret-key " } as Env;

function stubFetch(implementation: typeof fetch) {
  vi.stubGlobal("fetch", implementation);
}

afterEach(() => {
  vi.unstubAllGlobals();
});

describe("turnstile configuration", () => {
  it("enables Turnstile only when both keys are configured", () => {
    expect(getTurnstileConfig(bothKeys)).toEqual({
      enabled: true,
      siteKey: "site-key",
      secretKey: "secret-key",
    });
  });

  it("stays disabled when either key is missing, blank or not a string", () => {
    expect(getTurnstileConfig({ TURNSTILE_SITE_KEY: "site" } as Env).enabled).toBe(false);
    expect(getTurnstileConfig({ TURNSTILE_SECRET_KEY: "secret" } as Env).enabled).toBe(false);
    expect(getTurnstileConfig({ TURNSTILE_SITE_KEY: "  ", TURNSTILE_SECRET_KEY: "secret" } as Env).enabled).toBe(false);
    expect(getTurnstileConfig({ TURNSTILE_SITE_KEY: 42, TURNSTILE_SECRET_KEY: "secret" } as unknown as Env).enabled).toBe(
      false,
    );
    expect(getTurnstileConfig({} as Env).enabled).toBe(false);
  });
});

describe("turnstile verification", () => {
  it("passes through without calling Cloudflare when Turnstile is disabled", async () => {
    const fetchMock = vi.fn();
    stubFetch(fetchMock as unknown as typeof fetch);

    await expect(verifyTurnstileToken({} as Env, null, null)).resolves.toEqual({ success: true, errorCodes: [] });
    expect(fetchMock).not.toHaveBeenCalled();
  });

  it("rejects a missing token before calling Cloudflare", async () => {
    const fetchMock = vi.fn();
    stubFetch(fetchMock as unknown as typeof fetch);

    await expect(verifyTurnstileToken(bothKeys, "", "1.2.3.4")).resolves.toEqual({
      success: false,
      errorCodes: ["missing-input-response"],
    });
    expect(fetchMock).not.toHaveBeenCalled();
  });

  it("posts the secret, token and client ip to siteverify", async () => {
    let seenUrl = "";
    let seenBody = "";
    stubFetch((async (input: RequestInfo | URL, init?: RequestInit) => {
      seenUrl = String(input);
      seenBody = String(init?.body);
      return new Response(JSON.stringify({ success: true, "error-codes": [] }), { status: 200 });
    }) as unknown as typeof fetch);

    await expect(verifyTurnstileToken(bothKeys, "token-value", "203.0.113.7")).resolves.toEqual({
      success: true,
      errorCodes: [],
    });
    expect(seenUrl).toBe("https://challenges.cloudflare.com/turnstile/v0/siteverify");
    expect(seenBody).toContain("secret=secret-key");
    expect(seenBody).toContain("response=token-value");
    expect(seenBody).toContain("remoteip=203.0.113.7");
  });

  it("returns Cloudflare error codes when the challenge fails", async () => {
    stubFetch((async () =>
      new Response(JSON.stringify({ success: false, "error-codes": ["invalid-input-response", "bad-request"] }), {
        status: 200,
      })) as unknown as typeof fetch);

    await expect(verifyTurnstileToken(bothKeys, "token", null)).resolves.toEqual({
      success: false,
      errorCodes: ["invalid-input-response", "bad-request"],
    });
  });

  it("classifies transport failures so the UI can tell a retry from a bad token", async () => {
    stubFetch((async () => new Response("nope", { status: 502 })) as unknown as typeof fetch);
    await expect(verifyTurnstileToken(bothKeys, "token", null)).resolves.toEqual({
      success: false,
      errorCodes: ["http-502"],
    });

    stubFetch((async () => new Response("not json", { status: 200 })) as unknown as typeof fetch);
    await expect(verifyTurnstileToken(bothKeys, "token", null)).resolves.toEqual({
      success: false,
      errorCodes: ["invalid-verify-response"],
    });

    stubFetch((async () => {
      throw new Error("socket hang up");
    }) as unknown as typeof fetch);
    await expect(verifyTurnstileToken(bothKeys, "token", null)).resolves.toEqual({
      success: false,
      errorCodes: ["verify-network-error"],
    });

    const timeout = new Error("aborted");
    timeout.name = "TimeoutError";
    stubFetch((async () => {
      throw timeout;
    }) as unknown as typeof fetch);
    await expect(verifyTurnstileToken(bothKeys, "token", null)).resolves.toEqual({
      success: false,
      errorCodes: ["verify-timeout"],
    });
  });

  it("ignores malformed error codes from Cloudflare", async () => {
    stubFetch((async () =>
      new Response(JSON.stringify({ success: false, "error-codes": ["real", 42, null] }), {
        status: 200,
      })) as unknown as typeof fetch);

    await expect(verifyTurnstileToken(bothKeys, "token", null)).resolves.toEqual({
      success: false,
      errorCodes: ["real"],
    });
  });
});
