import { describe, expect, it } from "vitest";
import { buildOrderTrend, dayKeyOf, dayStartMs, getDashboardData } from "../src/dashboard";
import type { Env } from "../src/types";

describe("dashboard date aggregation", () => {
  it("uses Asia/Shanghai calendar days", () => {
    expect(dayKeyOf(Date.parse("2026-09-28T15:59:59Z"))).toBe("2026-09-28");
    expect(dayKeyOf(Date.parse("2026-09-28T16:00:00Z"))).toBe("2026-09-29");
    expect(dayStartMs("2026-09-29")).toBe(Date.parse("2026-09-28T16:00:00Z"));
  });

  it("fills empty days and counts paid orders", () => {
    const now = Date.parse("2026-09-29T04:00:00Z");
    const points = buildOrderTrend(
      [
        { created_at: Date.parse("2026-09-27T18:00:00Z"), status: "PAID" },
        { created_at: Date.parse("2026-09-28T02:00:00Z"), status: "PENDING" },
      ],
      now,
      7,
    ).slice(-3);

    expect(points.map((point) => point.date)).toEqual(["2026-09-27", "2026-09-28", "2026-09-29"]);
    expect(points.map(({ createdOrders, paidOrders }) => ({ createdOrders, paidOrders }))).toEqual([
      { createdOrders: 0, paidOrders: 0 },
      { createdOrders: 2, paidOrders: 1 },
      { createdOrders: 0, paidOrders: 0 },
    ]);
  });
});

describe("dashboard queries", () => {
  it("binds the today boundary for both aggregate placeholders", async () => {
    const now = Date.parse("2026-09-30T08:30:00+08:00");
    const todayStart = dayStartMs("2026-09-30");
    let aggregateBindings: unknown[] = [];

    const database = {
      prepare(sql: string) {
        if (sql.includes("AS today_paid_fen")) {
          return {
            bind(...values: unknown[]) {
              aggregateBindings = values;
              return {
                first: async () => ({
                  total_orders: 3,
                  paid_orders: 2,
                  paid_fen: 300,
                  pending_notify: 1,
                  failed_notify: 1,
                  today_orders: 2,
                  today_paid_fen: 200,
                }),
              };
            },
          };
        }
        if (sql.includes("COUNT(*) AS unmatched")) {
          return { bind: () => ({ first: async () => ({ unmatched: 2, stale_unmatched: 1 }) }) };
        }
        if (sql.includes("SELECT created_at, status")) {
          return { bind: () => ({ all: async () => ({ results: [] }) }) };
        }
        return { first: async () => null };
      },
    } as unknown as D1Database;

    const result = await getDashboardData({ DB: database } as Env, now);

    expect(aggregateBindings).toEqual([todayStart, todayStart]);
    expect(result.metrics).toMatchObject({
      todayOrders: 2,
      todayPaidFen: 200,
      totalPaidFen: 300,
      pendingNotify: 1,
      failedNotify: 1,
      unmatched: 2,
      staleUnmatched: 1,
    });
  });
});
