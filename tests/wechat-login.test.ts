import { describe, expect, it } from "vitest";
import { normalizeWechatLoginPayload } from "../src/wechat-login";

const now = Date.parse("2026-09-28T05:30:00+08:00");

describe("WeChat login session", () => {
  it("accepts only generic login states and short-lived QR content", () => {
    expect(normalizeWechatLoginPayload({ status: "qr_ready", qrPayload: "https://weixin.qq.com/x/test", observedAt: now }, now))
      .toMatchObject({ status: "QR_READY", qrPayload: "https://weixin.qq.com/x/test", observedAt: now });
    expect(normalizeWechatLoginPayload({ status: "LOGGED_IN", observedAt: now }, now)).toMatchObject({
      status: "LOGGED_IN",
      qrPayload: null,
    });
    expect(() => normalizeWechatLoginPayload({ status: "FAILED", reason: "button missing" }, now)).toThrow(
      "INVALID_WECHAT_LOGIN_PAYLOAD",
    );
    expect(() => normalizeWechatLoginPayload({ status: "QR_READY", qrPayload: "plain text" }, now)).toThrow(
      "INVALID_WECHAT_LOGIN_QR",
    );
  });

  it("rejects stale or future observations", () => {
    expect(() => normalizeWechatLoginPayload({ status: "LOGGED_IN", observedAt: now - 5 * 60_000 - 1 }, now)).toThrow(
      "WECHAT_LOGIN_TIMESTAMP_EXPIRED",
    );
    expect(() => normalizeWechatLoginPayload({ status: "LOGGED_IN", observedAt: now + 5 * 60_000 + 1 }, now)).toThrow(
      "WECHAT_LOGIN_TIMESTAMP_EXPIRED",
    );
  });
});
