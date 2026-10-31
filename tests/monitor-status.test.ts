import { describe, expect, it } from "vitest";
import { normalizeComponentStatusPayload } from "../src/monitor";

const now = Date.parse("2026-09-27T21:30:00+08:00");

describe("monitor component status", () => {
  it("normalizes a healthy Weixin hook report", () => {
    expect(
      normalizeComponentStatusPayload(
        {
          component: "wechat_hook",
          status: "healthy",
          version: "0.1.0",
          targetVersion: "Weixin 4.2.0.11",
          processId: 4308,
          hookOffset: "0x10b0230",
          detailCode: "hook_active",
          message: "hardware breakpoint installed",
          lastEventAt: now - 60_000,
          observedAt: now,
        },
        now,
      ),
    ).toEqual({
      component: "wechat_hook",
      status: "HEALTHY",
      componentVersion: "0.1.0",
      targetVersion: "Weixin 4.2.0.11",
      processId: 4308,
      hookOffset: "0x10b0230",
      detailCode: "HOOK_ACTIVE",
      detailMessage: "hardware breakpoint installed",
      lastEventAt: now - 60_000,
      reportedAt: now,
    });
  });

  it("rejects unsupported components and invalid offsets", () => {
    expect(() => normalizeComponentStatusPayload({ component: "network_hook", status: "HEALTHY" }, now)).toThrow(
      "INVALID_MONITOR_COMPONENT",
    );
    expect(() =>
      normalizeComponentStatusPayload({ component: "wechat_hook", status: "HEALTHY", hookOffset: "53c9350" }, now),
    ).toThrow("INVALID_HOOK_OFFSET");
  });

  it("rejects stale component reports", () => {
    expect(() =>
      normalizeComponentStatusPayload(
        { component: "wechat_hook", status: "ERROR", observedAt: now - 10 * 60_000 - 1 },
        now,
      ),
    ).toThrow("MONITOR_COMPONENT_TIMESTAMP_EXPIRED");
  });
});
