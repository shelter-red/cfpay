const SESSION_TTL_MS = 3 * 60_000;
const QR_TTL_MS = 2 * 60_000;
const MAX_OBSERVATION_AGE_MS = 5 * 60_000;
const MAX_FUTURE_SKEW_MS = 5 * 60_000;
const LOGIN_STATUSES = new Set(["LOGIN_REQUIRED", "LOGIN_CLICKED", "QR_READY", "LOGGED_IN", "RESTARTING"]);
const PAYLOAD_KEYS = new Set(["status", "qrPayload", "observedAt"]);

type WechatLoginStatus =
  | "LOGIN_REQUIRED"
  | "LOGIN_CLICKED"
  | "QR_READY"
  | "LOGGED_IN"
  | "RESTARTING";

export interface NormalizedWechatLoginState {
  status: WechatLoginStatus;
  qrPayload: string | null;
  observedAt: number;
  expiresAt: number;
}

export interface WechatLoginSessionView {
  deviceId: string;
  status: WechatLoginStatus;
  observedAt: number;
  expiresAt: number;
  updatedAt: number;
  qrAvailable: boolean;
}

export function normalizeWechatLoginPayload(
  payload: Record<string, unknown>,
  now = Date.now(),
): NormalizedWechatLoginState {
  if (!payload || Array.isArray(payload)) throw new Error("INVALID_WECHAT_LOGIN_PAYLOAD");
  if (Object.keys(payload).some((key) => !PAYLOAD_KEYS.has(key))) throw new Error("INVALID_WECHAT_LOGIN_PAYLOAD");

  const status = typeof payload.status === "string" ? payload.status.trim().toUpperCase() : "";
  if (!LOGIN_STATUSES.has(status)) throw new Error("INVALID_WECHAT_LOGIN_STATUS");
  const observedAt = parseObservedAt(payload.observedAt, now);
  const qrPayload = status === "QR_READY" ? validateLoginQrPayload(payload.qrPayload) : null;
  if (status !== "QR_READY" && payload.qrPayload !== undefined && payload.qrPayload !== null && payload.qrPayload !== "") {
    throw new Error("INVALID_WECHAT_LOGIN_PAYLOAD");
  }
  return {
    status: status as WechatLoginStatus,
    qrPayload,
    observedAt,
    expiresAt: now + (status === "QR_READY" ? QR_TTL_MS : SESSION_TTL_MS),
  };
}

function validateLoginQrPayload(value: unknown): string {
  if (typeof value !== "string") throw new Error("WECHAT_LOGIN_QR_REQUIRED");
  const payload = value.trim();
  if (payload.length < 8 || payload.length > 2048 || /[\u0000-\u001f\u007f]/.test(payload)) {
    throw new Error("INVALID_WECHAT_LOGIN_QR");
  }
  try {
    const url = new URL(payload);
    if (!new Set(["http:", "https:", "weixin:", "wxp:"]).has(url.protocol) || url.username || url.password) {
      throw new Error("INVALID_WECHAT_LOGIN_QR");
    }
  } catch {
    throw new Error("INVALID_WECHAT_LOGIN_QR");
  }
  return payload;
}

function parseObservedAt(value: unknown, now: number): number {
  const observedAt = value === undefined || value === null ? now : Number(value);
  if (
    !Number.isInteger(observedAt) ||
    observedAt < now - MAX_OBSERVATION_AGE_MS ||
    observedAt > now + MAX_FUTURE_SKEW_MS
  ) {
    throw new Error("WECHAT_LOGIN_TIMESTAMP_EXPIRED");
  }
  return observedAt;
}
