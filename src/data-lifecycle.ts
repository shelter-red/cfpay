import { invalidateOrderViews } from "./orders";
import { runtimeInvalidate } from "./runtime-data";
import type { Env, PaymentOrder } from "./types";

export type ExportFormat = "json" | "csv";

export interface ExportFile {
  body: string;
  contentType: string;
  filename: string;
}

interface OrderExportRecord {
  order: PaymentOrder;
  paymentEvent: Record<string, unknown> | null;
  paymentActions: Record<string, unknown>[];
  callback: Record<string, unknown> | null;
  callbackAttempts: Record<string, unknown>[];
}

interface PaymentEventExportRecord {
  event: Record<string, unknown>;
  actions: Record<string, unknown>[];
}

/** 校验管理端提交的批量标识，限制单次操作规模并去重。 */
export function normalizeExportIds(value: unknown, maximum = 100): string[] {
  if (!Array.isArray(value)) throw new Error("EXPORT_IDS_INVALID");
  const ids = [...new Set(value.filter((item): item is string => typeof item === "string").map((item) => item.trim()).filter(Boolean))];
  if (ids.length < 1 || ids.length > maximum || ids.some((id) => id.length > 160)) throw new Error("EXPORT_IDS_INVALID");
  return ids;
}

/** 规范化导出格式，只允许完整 JSON 或电子表格友好的 CSV。 */
export function normalizeExportFormat(value: unknown): ExportFormat {
  if (value === "json" || value === "csv") return value;
  throw new Error("EXPORT_FORMAT_INVALID");
}

/** 生成 SQL IN 子句占位符。 */
function placeholders(length: number): string {
  return Array.from({ length }, () => "?").join(",");
}

/** 将可能被电子表格误识别的长数字或公式文本转成安全文本单元格。 */
function csvText(value: unknown): string {
  let text = value === null || value === undefined
    ? ""
    : typeof value === "object"
      ? JSON.stringify(value)
      : String(value);
  if (/^[=+\-@]/.test(text) || /^\d{12,}$/.test(text)) text = `\t${text}`;
  return `"${text.replaceAll('"', '""')}"`;
}

/** 把对象数组编码成带 UTF-8 BOM 的 CSV。 */
function recordsToCsv(records: Array<Record<string, unknown>>): string {
  const keys = [...new Set(records.flatMap((record) => Object.keys(record)))];
  return `\uFEFF${keys.map(csvText).join(",")}\r\n${records.map((record) => keys.map((key) => csvText(record[key])).join(",")).join("\r\n")}`;
}

/** 为导出文件生成稳定且不含本地化字符的时间戳。 */
function exportStamp(now = Date.now()): string {
  return new Date(now).toISOString().replaceAll(":", "-").replace(/\.\d{3}Z$/, "Z");
}

/** 从 D1 读取订单及其完整收款、处理和回调审计数据。 */
async function loadOrderExportRecords(env: Env, ids: string[]): Promise<OrderExportRecord[]> {
  const marks = placeholders(ids.length);
  const [orders, events, callbacks, attempts] = await Promise.all([
    env.DB.prepare(`SELECT * FROM payment_order WHERE id IN (${marks}) ORDER BY created_at DESC`).bind(...ids).all<PaymentOrder>(),
    env.DB.prepare(`SELECT * FROM payment_event WHERE matched_order_id IN (${marks})`).bind(...ids).all<Record<string, unknown>>(),
    env.DB.prepare(`SELECT * FROM callback_job WHERE order_id IN (${marks})`).bind(...ids).all<Record<string, unknown>>(),
    env.DB.prepare(
      `SELECT a.*, j.order_id FROM callback_attempt a JOIN callback_job j ON j.id = a.job_id
       WHERE j.order_id IN (${marks}) ORDER BY j.order_id, a.attempt_no`,
    ).bind(...ids).all<Record<string, unknown>>(),
  ]);
  if (orders.results.length !== ids.length) throw new Error("ORDER_NOT_FOUND");
  const eventIds = events.results.map((event) => String(event.id));
  const actions = eventIds.length
    ? await env.DB.prepare(`SELECT * FROM payment_event_action WHERE event_id IN (${placeholders(eventIds.length)}) ORDER BY id`).bind(...eventIds).all<Record<string, unknown>>()
    : { results: [] as Record<string, unknown>[] };
  return orders.results.map((order) => {
    const paymentEvent = events.results.find((event) => event.matched_order_id === order.id) ?? null;
    const callback = callbacks.results.find((item) => item.order_id === order.id) ?? null;
    return {
      order,
      paymentEvent,
      paymentActions: paymentEvent ? actions.results.filter((action) => action.event_id === paymentEvent.id) : [],
      callback,
      callbackAttempts: attempts.results.filter((attempt) => attempt.order_id === order.id),
    };
  });
}

/** 导出选中订单；JSON 保留层级，CSV 每笔订单一行并用 JSON 文本保存明细数组。 */
export async function exportOrders(env: Env, ids: string[], format: ExportFormat): Promise<ExportFile> {
  const records = await loadOrderExportRecords(env, ids);
  const stamp = exportStamp();
  if (format === "json") {
    return {
      body: JSON.stringify({ schemaVersion: 1, exportedAt: new Date().toISOString(), records }, null, 2),
      contentType: "application/json; charset=utf-8",
      filename: `cfpay-orders-${stamp}.json`,
    };
  }
  const rows = records.map(({ order, paymentEvent, paymentActions, callback, callbackAttempts }) => ({
    ...order,
    payment_event: paymentEvent,
    payment_actions: paymentActions,
    callback_job: callback,
    callback_attempts: callbackAttempts,
  }));
  return { body: recordsToCsv(rows), contentType: "text/csv; charset=utf-8", filename: `cfpay-orders-${stamp}.csv` };
}

/** 永久删除选中订单及其关联收款、处理记录、回调任务和回调尝试。 */
export async function deleteOrders(env: Env, ids: string[]): Promise<number> {
  const marks = placeholders(ids.length);
  const orders = await env.DB.prepare(`SELECT id, merchant_order_no FROM payment_order WHERE id IN (${marks})`).bind(...ids).all<{ id: string; merchant_order_no: string }>();
  if (orders.results.length !== ids.length) throw new Error("ORDER_NOT_FOUND");
  await env.DB.batch([
    env.DB.prepare(`DELETE FROM payment_event WHERE matched_order_id IN (${marks})`).bind(...ids),
    env.DB.prepare(`DELETE FROM payment_order WHERE id IN (${marks})`).bind(...ids),
  ]);
  await Promise.all([
    ...orders.results.flatMap((order) => [
      runtimeInvalidate(env, { keys: [`order:id:${order.id}`, `order:merchant:${order.merchant_order_no}`] }),
      invalidateOrderViews(env, order.id),
    ]),
    runtimeInvalidate(env, { prefixes: ["event:id:", "event:actions:", "view:payment-events:", "view:quick-pay:"] }),
  ]);
  return orders.results.length;
}

/** 从 D1 读取已忽略收款及其处理记录，其他状态禁止进入删除导出链路。 */
async function loadIgnoredEventExportRecords(env: Env, ids: string[]): Promise<PaymentEventExportRecord[]> {
  const marks = placeholders(ids.length);
  const events = await env.DB.prepare(`SELECT * FROM payment_event WHERE id IN (${marks}) AND status = 'IGNORED'`).bind(...ids).all<Record<string, unknown>>();
  if (events.results.length !== ids.length) throw new Error("PAYMENT_EVENT_DELETE_FORBIDDEN");
  const actions = await env.DB.prepare(`SELECT * FROM payment_event_action WHERE event_id IN (${marks}) ORDER BY id`).bind(...ids).all<Record<string, unknown>>();
  return events.results.map((event) => ({ event, actions: actions.results.filter((action) => action.event_id === event.id) }));
}

/** 导出选中的已忽略收款及其完整处理记录。 */
export async function exportIgnoredPaymentEvents(env: Env, ids: string[], format: ExportFormat): Promise<ExportFile> {
  const records = await loadIgnoredEventExportRecords(env, ids);
  const stamp = exportStamp();
  if (format === "json") {
    return {
      body: JSON.stringify({ schemaVersion: 1, exportedAt: new Date().toISOString(), records }, null, 2),
      contentType: "application/json; charset=utf-8",
      filename: `cfpay-ignored-payments-${stamp}.json`,
    };
  }
  const rows = records.map(({ event, actions }) => ({ ...event, actions }));
  return { body: recordsToCsv(rows), contentType: "text/csv; charset=utf-8", filename: `cfpay-ignored-payments-${stamp}.csv` };
}

/** 永久删除选中的已忽略收款；未匹配和已匹配记录均拒绝删除。 */
export async function deleteIgnoredPaymentEvents(env: Env, ids: string[]): Promise<number> {
  await loadIgnoredEventExportRecords(env, ids);
  const marks = placeholders(ids.length);
  const result = await env.DB.prepare(`DELETE FROM payment_event WHERE id IN (${marks}) AND status = 'IGNORED'`).bind(...ids).run();
  await runtimeInvalidate(env, { prefixes: ["event:id:", "event:actions:", "view:payment-events:", "view:dashboard:"] });
  return result.meta.changes ?? 0;
}
