import { cacheOrder, invalidateOrderViews } from "./orders";
import { runtimeAll, runtimeFirst, runtimeInvalidate, runtimePut } from "./runtime-data";
import type { Env, PaymentOrder, Provider } from "./types";

type PaymentEventStatus = "UNMATCHED" | "MATCHED" | "IGNORED";
type PaymentEventAction = "AUTO_MATCH" | "MANUAL_MATCH" | "IGNORE" | "RESTORE";

export interface PaymentEventRecord {
  id: string;
  device_id: string;
  provider: Provider;
  amount_fen: number;
  paid_at: number;
  status: PaymentEventStatus;
  matched_order_id: string | null;
  raw_excerpt: string | null;
  created_at: number;
  updated_at: number;
}

export interface PaymentEventListInput {
  scope: "pending" | "resolved";
  page: number;
  pageSize: number;
  overdueOnly: boolean;
}

export interface PaymentEventCandidateInput {
  eventId: string;
  query: string;
  page: number;
  pageSize: number;
}

interface PaymentEventActionRow {
  id: number;
  event_id: string;
  action: PaymentEventAction;
  order_id: string | null;
  reason: string | null;
  actor: string;
  created_at: number;
}

/** 把页码和每页条数限制在管理端允许的范围内。 */
export function normalizePage(value: unknown, fallback: number, maximum: number): number {
  const parsed = Number(value);
  if (!Number.isInteger(parsed) || parsed < 1) return fallback;
  return Math.min(parsed, maximum);
}

/** 读取分页后的收款事件，待处理和处理记录使用独立缓存键。 */
export async function listPaymentEvents(env: Env, input: PaymentEventListInput): Promise<Record<string, unknown>> {
  const now = Date.now();
  const offset = (input.page - 1) * input.pageSize;
  const filters = input.scope === "pending" ? ["e.status = 'UNMATCHED'"] : ["e.status <> 'UNMATCHED'"];
  const params: unknown[] = [];
  if (input.overdueOnly && input.scope === "pending") {
    filters.push("e.created_at <= ?");
    params.push(now - 24 * 60 * 60_000);
  }
  const where = filters.join(" AND ");
  const cacheKey = `view:payment-events:${input.scope}:${input.overdueOnly ? 1 : 0}:${input.page}:${input.pageSize}`;
  const [items, count] = await Promise.all([
    runtimeAll<Record<string, unknown>>(
      env,
      cacheKey,
      `SELECT e.id, e.device_id, e.provider, e.amount_fen, e.paid_at, e.status, e.matched_order_id,
              e.created_at, e.updated_at, length(e.raw_excerpt) AS raw_length,
              a.action AS latest_action, a.reason AS latest_reason, a.actor AS latest_actor,
              a.created_at AS latest_action_at, o.merchant_order_no AS matched_merchant_order_no
       FROM payment_event e
       LEFT JOIN payment_event_action a ON a.id = (
         SELECT id FROM payment_event_action WHERE event_id = e.id ORDER BY id DESC LIMIT 1
       )
       LEFT JOIN payment_order o ON o.id = e.matched_order_id
       WHERE ${where}
       ORDER BY e.created_at DESC
       LIMIT ? OFFSET ?`,
      [...params, input.pageSize, offset],
      { ttlMs: 30_000 },
    ),
    runtimeFirst<{ total: number }>(
      env,
      `${cacheKey}:count`,
      `SELECT COUNT(*) AS total FROM payment_event e WHERE ${where}`,
      params,
      { ttlMs: 30_000 },
    ),
  ]);
  return { items, total: count?.total ?? 0, page: input.page, pageSize: input.pageSize, now };
}

/** 读取一个收款事件及其全部人工处理历史。 */
export async function getPaymentEventDetail(env: Env, eventId: string): Promise<Record<string, unknown> | null> {
  const event = await runtimeFirst<PaymentEventRecord>(
    env,
    `event:id:${eventId}`,
    `SELECT id, device_id, provider, amount_fen, paid_at, status, matched_order_id, raw_excerpt, created_at, updated_at
     FROM payment_event WHERE id = ?`,
    [eventId],
    { ttlMs: null },
  );
  if (!event) return null;
  const actions = await runtimeAll<PaymentEventActionRow>(
    env,
    `event:actions:${eventId}`,
    `SELECT id, event_id, action, order_id, reason, actor, created_at
     FROM payment_event_action WHERE event_id = ? ORDER BY id DESC`,
    [eventId],
    { ttlMs: null },
  );
  let detail: Record<string, unknown> | null = null;
  if (event.raw_excerpt) {
    try {
      const parsed: unknown = JSON.parse(event.raw_excerpt);
      if (parsed && typeof parsed === "object") detail = parsed as Record<string, unknown>;
    } catch {
      detail = null;
    }
  }
  const order = event.matched_order_id
    ? await runtimeFirst<Record<string, unknown>>(
        env,
        `order:id:${event.matched_order_id}`,
        "SELECT * FROM payment_order WHERE id = ?",
        [event.matched_order_id],
        { ttlMs: null },
      )
    : null;
  return { event: { ...event, raw_excerpt: undefined }, detail, raw: event.raw_excerpt, order, actions };
}

/** 返回人工匹配允许选择的同渠道、同实付金额普通订单。 */
export async function listPaymentEventCandidates(env: Env, input: PaymentEventCandidateInput): Promise<Record<string, unknown>> {
  const event = await runtimeFirst<PaymentEventRecord>(
    env,
    `event:id:${input.eventId}`,
    "SELECT * FROM payment_event WHERE id = ?",
    [input.eventId],
    { ttlMs: null },
  );
  if (!event || event.status !== "UNMATCHED") throw new Error("PAYMENT_EVENT_NOT_PENDING");
  const now = Date.now();
  const offset = (input.page - 1) * input.pageSize;
  const search = input.query ? `%${input.query}%` : null;
  const searchClause = search ? "AND (merchant_order_no LIKE ? OR id LIKE ?)" : "";
  const baseParams: unknown[] = [event.provider, event.amount_fen];
  if (search) baseParams.push(search, search);
  const statusExpression = "CASE WHEN status = 'PENDING' AND expires_at <= ? THEN 'EXPIRED' ELSE status END";
  const rows = await env.DB.prepare(
    `SELECT id, merchant_order_no, provider, subject, amount_fen, payable_amount_fen,
            ${statusExpression} AS status, notify_status, paid_at, expires_at, created_at, device
     FROM payment_order
     WHERE provider = ? AND payable_amount_fen = ? AND payment_event_id IS NULL
       AND status IN ('PENDING', 'EXPIRED') ${searchClause}
     ORDER BY created_at DESC LIMIT ? OFFSET ?`,
  ).bind(now, ...baseParams, input.pageSize, offset).all<Record<string, unknown>>();
  const count = await env.DB.prepare(
     `SELECT COUNT(*) AS total FROM payment_order
     WHERE provider = ? AND payable_amount_fen = ? AND payment_event_id IS NULL
       AND status IN ('PENDING', 'EXPIRED') ${searchClause}`,
  ).bind(...baseParams).first<{ total: number }>();
  return { items: rows.results, total: count?.total ?? 0, page: input.page, pageSize: input.pageSize };
}

/** 将已落库的收款事件原子关联到订单，并创建商户回调任务。 */
export async function matchPaymentEventToOrder(
  env: Env,
  event: PaymentEventRecord,
  order: PaymentOrder,
  action: "AUTO_MATCH" | "MANUAL_MATCH",
  actor: string,
): Promise<boolean> {
  const now = Date.now();
  const results = await env.DB.batch([
    env.DB.prepare(
      `UPDATE payment_order
       SET status = 'PAID',
           notify_status = CASE WHEN notify_url IS NULL OR notify_url = '' THEN 'NONE' ELSE 'PENDING' END,
           payment_event_id = ?, paid_source = 'EVENT', manual_paid_by = NULL,
           paid_at = ?, updated_at = ?
       WHERE id = ? AND provider = ? AND payable_amount_fen = ?
         AND status IN ('PENDING', 'EXPIRED') AND payment_event_id IS NULL`,
    ).bind(event.id, event.paid_at, now, order.id, event.provider, event.amount_fen),
    env.DB.prepare(
      `UPDATE payment_event SET status = 'MATCHED', matched_order_id = ?, updated_at = ?
       WHERE id = ? AND status = 'UNMATCHED'
         AND EXISTS (SELECT 1 FROM payment_order WHERE id = ? AND payment_event_id = ?)`,
    ).bind(order.id, now, event.id, order.id, event.id),
    env.DB.prepare(
      `INSERT OR IGNORE INTO callback_job (
        order_id, status, attempt_count, max_attempts, next_attempt_at, created_at, updated_at
      ) SELECT id, 'PENDING', 0, 6, ?, ?, ? FROM payment_order
        WHERE id = ? AND payment_event_id = ? AND notify_url IS NOT NULL AND notify_url <> ''`,
    ).bind(now, now, now, order.id, event.id),
    env.DB.prepare(
      `INSERT INTO payment_event_action (event_id, action, order_id, reason, actor, created_at)
       SELECT ?, ?, ?, NULL, ?, ?
       WHERE EXISTS (SELECT 1 FROM payment_event WHERE id = ? AND status = 'MATCHED' AND matched_order_id = ?)`,
    ).bind(event.id, action, order.id, actor, now, event.id, order.id),
  ]);
  if ((results[0]?.meta.changes ?? 0) === 0 || (results[1]?.meta.changes ?? 0) === 0) return false;
  const paidOrder: PaymentOrder = {
    ...order,
    status: "PAID",
    notify_status: order.notify_url ? "PENDING" : "NONE",
    payment_event_id: event.id,
    paid_source: "EVENT",
    manual_paid_by: null,
    paid_at: event.paid_at,
    updated_at: now,
  };
  await Promise.all([
    cacheOrder(env, paidOrder),
    runtimePut(env, `event:id:${event.id}`, { ...event, status: "MATCHED", matched_order_id: order.id, updated_at: now }, { ttlMs: null }),
    runtimeInvalidate(env, { prefixes: [`event:actions:${event.id}`, "view:payment-events:"] }),
    invalidateOrderViews(env, order.id),
  ]);
  return true;
}

/** 使用管理员选择的订单完成人工匹配。 */
export async function manuallyMatchPaymentEvent(env: Env, eventId: string, orderId: string, actor: string): Promise<string> {
  const [event, order] = await Promise.all([
    runtimeFirst<PaymentEventRecord>(env, `event:id:${eventId}`, "SELECT * FROM payment_event WHERE id = ?", [eventId], { ttlMs: null }),
    runtimeFirst<PaymentOrder>(env, `order:id:${orderId}`, "SELECT * FROM payment_order WHERE id = ?", [orderId], { ttlMs: null }),
  ]);
  if (!event) throw new Error("PAYMENT_EVENT_NOT_FOUND");
  if (event.status !== "UNMATCHED") throw new Error("PAYMENT_EVENT_NOT_PENDING");
  if (!order) throw new Error("ORDER_NOT_FOUND");
  if (order.provider !== event.provider || order.payable_amount_fen !== event.amount_fen) throw new Error("PAYMENT_EVENT_ORDER_MISMATCH");
  if (!(["PENDING", "EXPIRED"] as string[]).includes(order.status) || order.payment_event_id) throw new Error("ORDER_NOT_MATCHABLE");
  if (!(await matchPaymentEventToOrder(env, event, order, "MANUAL_MATCH", actor))) throw new Error("PAYMENT_EVENT_MATCH_CONFLICT");
  return order.id;
}

/** 标记一个待处理收款事件为已忽略并记录必填原因。 */
export async function ignorePaymentEvent(env: Env, eventId: string, reason: string, actor: string): Promise<void> {
  const normalized = reason.trim();
  if (normalized.length < 1 || normalized.length > 200) throw new Error("PAYMENT_EVENT_IGNORE_REASON_INVALID");
  const now = Date.now();
  const results = await env.DB.batch([
    env.DB.prepare("UPDATE payment_event SET status = 'IGNORED', updated_at = ? WHERE id = ? AND status = 'UNMATCHED'").bind(now, eventId),
    env.DB.prepare(
      `INSERT INTO payment_event_action (event_id, action, order_id, reason, actor, created_at)
       SELECT ?, 'IGNORE', NULL, ?, ?, ? WHERE EXISTS (
         SELECT 1 FROM payment_event WHERE id = ? AND status = 'IGNORED'
       )`,
    ).bind(eventId, normalized, actor, now, eventId),
  ]);
  if ((results[0]?.meta.changes ?? 0) === 0) throw new Error("PAYMENT_EVENT_NOT_PENDING");
  await Promise.all([
    runtimeInvalidate(env, { keys: [`event:id:${eventId}`], prefixes: [`event:actions:${eventId}`, "view:payment-events:", "view:dashboard:"] }),
  ]);
}

/** 将误忽略的收款事件恢复为待处理并保留完整操作历史。 */
export async function restorePaymentEvent(env: Env, eventId: string, actor: string): Promise<void> {
  const now = Date.now();
  const results = await env.DB.batch([
    env.DB.prepare("UPDATE payment_event SET status = 'UNMATCHED', updated_at = ? WHERE id = ? AND status = 'IGNORED'").bind(now, eventId),
    env.DB.prepare(
      `INSERT INTO payment_event_action (event_id, action, order_id, reason, actor, created_at)
       SELECT ?, 'RESTORE', NULL, NULL, ?, ? WHERE EXISTS (
         SELECT 1 FROM payment_event WHERE id = ? AND status = 'UNMATCHED'
       )`,
    ).bind(eventId, actor, now, eventId),
  ]);
  if ((results[0]?.meta.changes ?? 0) === 0) throw new Error("PAYMENT_EVENT_NOT_IGNORED");
  await runtimeInvalidate(env, { keys: [`event:id:${eventId}`], prefixes: [`event:actions:${eventId}`, "view:payment-events:", "view:dashboard:"] });
}
