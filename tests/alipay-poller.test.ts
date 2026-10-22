import { afterEach, describe, expect, it, vi } from "vitest";
import { pendingAlipayOrders } from "../src/alipay-active-orders";
import type { Env } from "../src/types";

afterEach(() => vi.useRealTimers());

/** 构造只实现运行缓存读写协议的测试环境。 */
function runtimeEnv(rows: Array<{ id: string; expires_at: number } | null>) {
  let readIndex = 0;
  const fetch = vi.fn(async (input: RequestInfo | URL) => {
    const pathname = new URL(String(input)).pathname;
    if (pathname === "/read") {
      const value = rows[Math.min(readIndex, rows.length - 1)] ?? null;
      readIndex += 1;
      return Response.json({ value });
    }
    if (pathname === "/invalidate") return Response.json({ ok: true, removed: 1 });
    return new Response("Not Found", { status: 404 });
  });
  const env = {
    RUNTIME_DATA: {
      idFromName: vi.fn(() => "runtime-id"),
      get: vi.fn(() => ({ fetch })),
    },
  } as unknown as Env;
  return { env, fetch };
}

describe("Alipay active order polling", () => {
  it("rechecks D1 after the cached earliest order expires", async () => {
    vi.useFakeTimers();
    vi.setSystemTime(new Date("2026-10-08T12:00:00Z"));
    const now = Date.now();
    const { env, fetch } = runtimeEnv([
      { id: "expired", expires_at: now - 1 },
      { id: "still-active", expires_at: now + 60_000 },
    ]);

    await expect(pendingAlipayOrders(env)).resolves.toBe(1);
    expect(fetch).toHaveBeenCalledTimes(3);
  });

  it("uses one cached read while the earliest order remains active", async () => {
    vi.useFakeTimers();
    vi.setSystemTime(new Date("2026-10-08T12:00:00Z"));
    const { env, fetch } = runtimeEnv([{ id: "active", expires_at: Date.now() + 60_000 }]);

    await expect(pendingAlipayOrders(env)).resolves.toBe(1);
    expect(fetch).toHaveBeenCalledTimes(1);
  });
});
