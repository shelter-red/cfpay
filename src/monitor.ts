import { getSettings } from "./db";
import { matchPaymentEventToOrder, type PaymentEventRecord } from "./payment-events";
import { runtimeFirst, runtimeInvalidate, runtimePut } from "./runtime-data";
import { constantTimeEqual, hmacSha256Hex, sha256Hex } from "./lib/crypto";
import { truncate } from "./lib/http";
import { parseMoneyToFen } from "./lib/money";
import { normalizeProvider } from "./orders";
import type { Env, PaymentOrder } from "./types";

const MAX_CLOCK_SKEW_SECONDS = 300;
const COMPONENT_STATUS_MAX_AGE_MS = 10 * 60_000;

const MONITOR_COMPONENTS = new Set(["wechat_hook", "alipay_ledger"]);
const MONITOR_COMPONENT_STATUSES = new Set(["STARTING", "HEALTHY", "DEGRADED", "ERROR", "STOPPED", "UNSUPPORTED"]);

// 校验监控端请求的签名与时间窗口。nonce 的一次性校验交给单例 MonitorState，
// 避免每 5 秒把 nonce 和心跳写进 D1。
export interface VerifiedMonitorRequest {
  deviceId: string;
  nonce: string;
  timestampSeconds: number;
}

export async function verifyMonitorSignature(
  env: Env,
  request: Request,
  rawBody: string,
): Promise<VerifiedMonitorRequest> {
  const setting = await getSettings(env);
  if (!setting.monitor_secret) throw new Error("MONITOR_NOT_CONFIGURED");

  const deviceId = request.headers.get("x-monitor-id")?.trim();
  const timestamp = request.headers.get("x-timestamp")?.trim();
  const nonce = request.headers.get("x-nonce")?.trim();
  const signature = request.headers.get("x-signature")?.trim().toLowerCase();
  if (!deviceId || !timestamp || !nonce || !signature) throw new Error("MONITOR_AUTH_REQUIRED");
  if (!/^[a-zA-Z0-9._-]{1,80}$/.test(deviceId) || !/^[a-zA-Z0-9_-]{16,128}$/.test(nonce)) {
    throw new Error("INVALID_MONITOR_HEADERS");
  }

  const timestampSeconds = Number(timestamp);
  const nowSeconds = Math.floor(Date.now() / 1000);
  if (!Number.isInteger(timestampSeconds) || Math.abs(nowSeconds - timestampSeconds) > MAX_CLOCK_SKEW_SECONDS) {
    throw new Error("MONITOR_TIMESTAMP_EXPIRED");
  }

  const bodyHash = await sha256Hex(rawBody);
  const expected = await hmacSha256Hex(setting.monitor_secret, `${timestamp}\n${nonce}\n${bodyHash}`);
  if (!constantTimeEqual(signature, expected)) throw new Error("INVALID_MONITOR_SIGNATURE");
  return { deviceId, nonce, timestampSeconds };
}

export type WechatChannelReason = "OK" | "NO_MONITOR" | "STALE" | "UNHEALTHY";

export interface WechatChannelStatus {
  available: boolean;
  reason: WechatChannelReason;
  componentStatus: string | null;
  detailCode: string | null;
  deviceId: string | null;
  reportedAt: number | null;
  loginStatus: string | null;
}

interface ComponentStatusPayload {
  // 0.9.1 起同一个请求同时携带设备信息，服务端据此刷新设备在线状态。
  name?: unknown;
  platform?: unknown;
  component?: unknown;
  status?: unknown;
  version?: unknown;
  targetVersion?: unknown;
  processId?: unknown;
  hookOffset?: unknown;
  detailCode?: unknown;
  message?: unknown;
  lastEventAt?: unknown;
  observedAt?: unknown;
}

export interface NormalizedComponentStatus {
  component: string;
  status: "STARTING" | "HEALTHY" | "DEGRADED" | "ERROR" | "STOPPED" | "UNSUPPORTED";
  componentVersion: string | null;
  targetVersion: string | null;
  processId: number | null;
  hookOffset: string | null;
  detailCode: string | null;
  detailMessage: string | null;
  lastEventAt: number | null;
  reportedAt: number;
}

// 校验并归一化组件状态上报：组件名与状态必须在白名单内，Hook 偏移必须是合法十六进制，
// 观察时间不能是未来或超过 10 分钟，字符串字段统一限长。任何一项不符都抛错而不是静默丢弃。
export function normalizeComponentStatusPayload(
  payload: ComponentStatusPayload,
  now = Date.now(),
): NormalizedComponentStatus {
  const component = typeof payload.component === "string" ? payload.component.trim().toLowerCase() : "";
  const status = typeof payload.status === "string" ? payload.status.trim().toUpperCase() : "";
  if (!MONITOR_COMPONENTS.has(component)) throw new Error("INVALID_MONITOR_COMPONENT");
  if (!MONITOR_COMPONENT_STATUSES.has(status)) throw new Error("INVALID_MONITOR_COMPONENT_STATUS");

  const processId = optionalInteger(payload.processId, 1, 0xffff_ffff);
  const hookOffset = optionalString(payload.hookOffset, 32);
  if (hookOffset && !/^0x[0-9a-f]{1,16}$/i.test(hookOffset)) throw new Error("INVALID_HOOK_OFFSET");

  const detailCode = optionalString(payload.detailCode, 80)?.toUpperCase() ?? null;
  if (detailCode && !/^[A-Z0-9._-]+$/.test(detailCode)) throw new Error("INVALID_MONITOR_DETAIL_CODE");

  return {
    component,
    status: status as NormalizedComponentStatus["status"],
    componentVersion: optionalString(payload.version, 50),
    targetVersion: optionalString(payload.targetVersion, 80),
    processId,
    hookOffset: hookOffset?.toLowerCase() ?? null,
    detailCode,
    detailMessage: typeof payload.message === "string" ? truncate(payload.message.trim(), 500) || null : null,
    lastEventAt: optionalTimestamp(payload.lastEventAt, now),
    reportedAt: parseComponentObservedAt(payload.observedAt, now),
  };
}

interface PaymentPayload {
  eventId?: unknown;
  provider?: unknown;
  amount?: unknown;
  paidAt?: unknown;
  raw?: unknown;
}

// 处理监控端上报的收款事件：先校验事件标识、渠道、金额与支付时间，再交给 recordTrustedPaymentEvent 落库与匹配。
// 这一层负责把不可信输入挡在外面，金额解析全程整数分，不走浮点。
export async function recordPaymentEvent(
  env: Env,
  deviceId: string,
  payload: PaymentPayload,
): Promise<{ duplicate: boolean; matched: boolean; orderId: string | null }> {
  if (typeof payload.eventId !== "string" || !/^[a-zA-Z0-9:._-]{8,160}$/.test(payload.eventId)) {
    throw new Error("INVALID_EVENT_ID");
  }
  if (typeof payload.provider !== "string" || typeof payload.amount !== "string") {
    throw new Error("INVALID_PAYMENT_EVENT");
  }

  const provider = normalizeProvider(payload.provider);
  const amountFen = parseMoneyToFen(payload.amount);
  const paidAt = parsePaidAt(payload.paidAt);
  return recordTrustedPaymentEvent(env, {
    eventId: payload.eventId,
    deviceId,
    provider,
    amountFen,
    paidAt,
    rawExcerpt: typeof payload.raw === "string" ? truncate(payload.raw, 8000) : null,
  });
}

export interface TrustedPaymentEvent {
  eventId: string;
  deviceId: string;
  provider: "wxpay" | "alipay";
  amountFen: number;
  paidAt: number;
  rawExcerpt?: string | null;
}

// 把已校验的收款事件写入 payment_event 并尝试匹配订单。
// 事件标识天然唯一（微信为 wechat:sha256(transid)），插入冲突即视为重复通知，直接返回既有匹配结果；
// 匹配条件是同渠道、同支付金额、订单处于 PENDING/EXPIRED，且支付时间落在订单的创建与过期时间之间。
export async function recordTrustedPaymentEvent(
  env: Env,
  input: TrustedPaymentEvent,
): Promise<{ duplicate: boolean; matched: boolean; orderId: string | null }> {
  const now = Date.now();
  const unmatchedEvent: PaymentEventRecord = {
    id: input.eventId,
    device_id: input.deviceId,
    provider: input.provider,
    amount_fen: input.amountFen,
    paid_at: input.paidAt,
    status: "UNMATCHED",
    matched_order_id: null,
    raw_excerpt: input.rawExcerpt ?? null,
    created_at: now,
    updated_at: now,
  };

  try {
    await env.DB.prepare(
      `INSERT INTO payment_event (
        id, device_id, provider, amount_fen, paid_at, status, raw_excerpt, created_at, updated_at
      ) VALUES (?, ?, ?, ?, ?, 'UNMATCHED', ?, ?, ?)`,
    )
      .bind(
        input.eventId,
        input.deviceId,
        input.provider,
        input.amountFen,
        input.paidAt,
        input.rawExcerpt ?? null,
        now,
        now,
      )
      .run();
    await Promise.all([
      runtimePut(env, `event:id:${input.eventId}`, unmatchedEvent, { ttlMs: null }),
      runtimeInvalidate(env, { prefixes: ["view:payment-events:", "view:dashboard:"] }),
    ]);
  } catch {
    await runtimeInvalidate(env, { keys: [`event:id:${input.eventId}`] });
    const existing = await runtimeFirst<{ matched_order_id: string | null }>(
      env,
      `event:id:${input.eventId}`,
      "SELECT matched_order_id FROM payment_event WHERE id = ?",
      [input.eventId],
      { ttlMs: null },
    );
    if (!existing) throw new Error("PAYMENT_EVENT_CREATE_FAILED");
    return { duplicate: true, matched: Boolean(existing.matched_order_id), orderId: existing.matched_order_id };
  }

  const order = await env.DB.prepare(
    `SELECT * FROM payment_order
     WHERE provider = ?
       AND payable_amount_fen = ?
       AND status IN ('PENDING', 'EXPIRED')
       AND created_at <= ?
       AND expires_at >= ?
     ORDER BY created_at DESC
     LIMIT 1`,
  )
    .bind(input.provider, input.amountFen, input.paidAt, input.paidAt)
    .first<PaymentOrder>();

  if (!order) return { duplicate: false, matched: false, orderId: null };

  const changed = await matchPaymentEventToOrder(env, unmatchedEvent, order, "AUTO_MATCH", "系统");
  return changed
    ? { duplicate: false, matched: true, orderId: order.id }
    : { duplicate: false, matched: false, orderId: null };
}

// 解析支付时间：接受毫秒或秒级时间戳，并限制在合理区间内，防止用未来或远古时间绕过订单时间窗匹配。
function parsePaidAt(value: unknown): number {
  const now = Date.now();
  let timestamp: number;
  if (typeof value === "number") timestamp = value;
  else if (typeof value === "string" && /^\d+$/.test(value)) timestamp = Number(value);
  else if (typeof value === "string") timestamp = Date.parse(value);
  else timestamp = now;

  if (!Number.isFinite(timestamp)) throw new Error("INVALID_PAID_AT");
  if (timestamp < now - 24 * 60 * 60_000 || timestamp > now + 5 * 60_000) {
    throw new Error("PAID_AT_OUT_OF_RANGE");
  }
  return Math.trunc(timestamp);
}

// 可选字符串字段：空值归一为 null，非字符串直接判为非法上报，长度超限则截断。
function optionalString(value: unknown, maximumLength: number): string | null {
  if (value === undefined || value === null || value === "") return null;
  if (typeof value !== "string") throw new Error("INVALID_MONITOR_COMPONENT_STATUS");
  return truncate(value.trim(), maximumLength) || null;
}

// 可选整数字段：空值归一为 null，超出允许区间判为非法上报。
function optionalInteger(value: unknown, minimum: number, maximum: number): number | null {
  if (value === undefined || value === null || value === "") return null;
  const number = typeof value === "number" ? value : Number(value);
  if (!Number.isInteger(number) || number < minimum || number > maximum) {
    throw new Error("INVALID_MONITOR_COMPONENT_STATUS");
  }
  return number;
}

// 解析组件状态的观察时间：缺省取当前时间，未来时间与过旧时间都判为非法，避免用陈旧状态伪装成实时上报。
function parseComponentObservedAt(value: unknown, now: number): number {
  const timestamp = value === undefined || value === null ? now : parseTimestamp(value);
  if (timestamp < now - COMPONENT_STATUS_MAX_AGE_MS || timestamp > now + 5 * 60_000) {
    throw new Error("MONITOR_COMPONENT_TIMESTAMP_EXPIRED");
  }
  return timestamp;
}

// 可选时间戳字段：空值归一为 null，否则按 parseTimestamp 校验。
function optionalTimestamp(value: unknown, now: number): number | null {
  if (value === undefined || value === null || value === "") return null;
  const timestamp = parseTimestamp(value);
  if (timestamp < 0 || timestamp > now + 5 * 60_000) throw new Error("INVALID_MONITOR_COMPONENT_STATUS");
  return timestamp;
}

// 把秒或毫秒时间戳统一成毫秒，并拒绝非有限值。
function parseTimestamp(value: unknown): number {
  let timestamp: number;
  if (typeof value === "number") timestamp = value;
  else if (typeof value === "string" && /^\d+$/.test(value)) timestamp = Number(value);
  else if (typeof value === "string") timestamp = Date.parse(value);
  else timestamp = Number.NaN;
  if (!Number.isFinite(timestamp)) throw new Error("INVALID_MONITOR_COMPONENT_STATUS");
  return Math.trunc(timestamp);
}
