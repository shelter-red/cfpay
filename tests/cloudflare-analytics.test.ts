import { afterEach, describe, expect, it, vi } from "vitest";
import { getD1Usage } from "../src/cloudflare-analytics";
import type { Env } from "../src/types";

afterEach(() => vi.unstubAllGlobals());

describe("Cloudflare D1 analytics", () => {
  it("aggregates hourly rows into UTC daily quota usage", async () => {
    const fetchMock = vi.fn().mockResolvedValue(Response.json({
      data: {
        viewer: {
          accounts: [{
            d1AnalyticsAdaptiveGroups: [
              { count: 7, dimensions: { date: "2026-10-05" }, sum: { rowsRead: 4_000_000, rowsWritten: 95_000 } },
            ],
            d1StorageAdaptiveGroups: [
              { dimensions: { databaseId: "database-a", datetimeHour: "2026-10-05T01:00:00Z" }, max: { databaseSizeBytes: 1_500_000_000 } },
              { dimensions: { databaseId: "database-a", datetimeHour: "2026-10-05T02:00:00Z" }, max: { databaseSizeBytes: 1_600_000_000 } },
              { dimensions: { databaseId: "database-b", datetimeHour: "2026-10-05T01:00:00Z" }, max: { databaseSizeBytes: 2_000_000_000 } },
            ],
          }],
        },
      },
      errors: null,
    }));
    vi.stubGlobal("fetch", fetchMock);

    const usage = await getD1Usage({
      CLOUDFLARE_ACCOUNT_ID: "account-a",
      CLOUDFLARE_D1_PLAN: "free",
      CLOUDFLARE_ANALYTICS_TOKEN: "secret-token",
    } as Env, new Date("2026-10-05T02:00:00Z"));

    expect(usage.today).toMatchObject({ date: "2026-10-05", queries: 7 });
    expect(usage.today.rowsRead).toMatchObject({ used: 4_000_000, limit: 5_000_000, level: "warning" });
    expect(usage.today.rowsWritten).toMatchObject({ used: 95_000, limit: 100_000, level: "danger" });
    expect(usage.storage).toMatchObject({ used: 3_600_000_000, limit: 5_000_000_000, level: "warning" });
    expect(usage.resetAt).toBe("2026-10-06T00:00:00.000Z");
    expect(usage.trend).toHaveLength(30);
    expect(usage.trend.at(-1)).toEqual({ date: "2026-10-05", rowsRead: 4_000_000, rowsWritten: 95_000, queries: 7 });
    expect(fetchMock).toHaveBeenCalledTimes(1);
    const query = JSON.parse(String(fetchMock.mock.calls[0]?.[1]?.body));
    expect(query.variables).toMatchObject({
      startDate: "2026-09-06",
      endDate: "2026-10-05",
    });
    expect(query.query).not.toContain("databaseId: $databaseId");
    expect(query.variables).not.toHaveProperty("databaseId");
  });

  it("reports Cloudflare time-range limits with a stable error code", async () => {
    vi.stubGlobal("fetch", vi.fn().mockResolvedValue(Response.json({
      data: null,
      errors: [{ message: 'account "example" cannot request a time range wider than 4w4d' }],
    })));

    await expect(getD1Usage({
      CLOUDFLARE_ACCOUNT_ID: "account-b",
      CLOUDFLARE_D1_PLAN: "free",
      CLOUDFLARE_ANALYTICS_TOKEN: "secret-token",
    } as Env, new Date("2026-10-05T02:00:00Z"))).rejects.toThrow("CLOUDFLARE_ANALYTICS_RANGE_TOO_WIDE");
  });

  it("bypasses the five-minute cache only for an explicit refresh", async () => {
    const response = (rowsRead: number) => Response.json({
      data: {
        viewer: {
          accounts: [{
            d1AnalyticsAdaptiveGroups: [
              { count: 1, dimensions: { date: "2026-10-08" }, sum: { rowsRead, rowsWritten: 1 } },
            ],
            d1StorageAdaptiveGroups: [],
          }],
        },
      },
      errors: null,
    });
    const fetchMock = vi.fn()
      .mockResolvedValueOnce(response(100))
      .mockResolvedValueOnce(response(200));
    vi.stubGlobal("fetch", fetchMock);
    const env = {
      CLOUDFLARE_ACCOUNT_ID: "account-refresh",
      CLOUDFLARE_D1_PLAN: "free",
      CLOUDFLARE_ANALYTICS_TOKEN: "secret-token",
    } as Env;
    const now = new Date("2026-10-08T02:00:00Z");

    expect((await getD1Usage(env, now)).today.rowsRead.used).toBe(100);
    expect((await getD1Usage(env, now)).today.rowsRead.used).toBe(100);
    expect((await getD1Usage(env, now, true)).today.rowsRead.used).toBe(200);
    expect(fetchMock).toHaveBeenCalledTimes(2);
  });
});
