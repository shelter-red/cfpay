import { getSettings } from "./db";
import { createEpaySign, constantTimeEqual } from "./lib/crypto";
import { formatFen, parseMoneyToFen } from "./lib/money";
import { validatePublicHttpUrl } from "./lib/http";
import { getQrCode } from "./qr-codes";
import { runtimeAll, runtimeFirst, runtimeInvalidate, runtimePut } from "./runtime-data";
import type { Env, PaymentOrder, Provider, SystemSetting } from "./types";

export interface CreateOrderInput {
  merchantOrderNo: string;
  provider: Provider;
  subject: string;
  amount: string;
  notifyUrl?: string | null;
  returnUrl?: string | null;
  attach?: string;
  clientIp?: string | null;
  device?: string | null;
}

export interface CreateOrderResult {
  order: PaymentOrder;
  setting: SystemSetting;
  reused: boolean;
}

export interface OrderListInput {
  page: number;
  pageSize: number;
  query: string;
  provider: Provider | "";
  status: "" | "PENDING" | "PAID" | "EXPIRED" | "CLOSED";
  notifyStatus: "" | "NONE" | "PENDING" | "RETRYING" | "SUCCESS" | "FAILED" | "HANDLED" | "ACTIVE";
}

export async function cacheOrder(env: Env, order: PaymentOrder): Promise<void> {
  await Promise.all([
    runtimePut(env, `order:id:${order.id}`, order, { ttlMs: null }),
    runtimePut(env, `order:merchant:${order.merchant_order_no}`, order, { ttlMs: null }),
  ]);
}

export async function invalidateOrderViews(env: Env, orderId?: string): Promise<void> {
  await runtimeInvalidate(env, {
    prefixes: [
      "view:orders:",
      "view:dashboard:",
      "view:payment-events:",
      "view:quick-pay:",
      "view:alipay:",
      ...(orderId ? [`callback:order:${orderId}`, `attempts:order:${orderId}`] : []),
    ],
  });
}

/** 分页读取订单并在查询时计算有效过期状态，避免为展示状态额外写 D1。 */
export async function listOrders(env: Env, input: OrderListInput): Promise<Record<string, unknown>> {
  const now = Date.now();
  const offset = (input.page - 1) * input.pageSize;
  const filters: string[] = [];
  const params: unknown[] = [];
  if (input.query) {
    filters.push("(merchant_order_no LIKE ? OR id LIKE ?)");
    const search = `%${input.query}%`;
    params.push(search, search);
  }
  if (input.provider) {
    filters.push("provider = ?");
    params.push(input.provider);
  }
  if (input.status) {
    filters.push("effective_status = ?");
    params.push(input.status);
  }
  if (input.notifyStatus === "ACTIVE") {
    filters.push("notify_status IN ('PENDING', 'RETRYING')");
  } else if (input.notifyStatus === "FAILED") {
    filters.push("notify_status = 'FAILED' AND callback_handled_at IS NULL");
  } else if (input.notifyStatus === "HANDLED") {
    filters.push("notify_status = 'FAILED' AND callback_handled_at IS NOT NULL");
  } else if (input.notifyStatus) {
    filters.push("notify_status = ?");
    params.push(input.notifyStatus);
  }
  const where = filters.length ? `WHERE ${filters.join(" AND ")}` : "";
  const key = `view:orders:${input.page}:${input.pageSize}:${input.query}:${input.provider}:${input.status}:${input.notifyStatus}`;
  const source = `SELECT o.id, o.merchant_order_no, o.provider, o.subject, o.amount_fen, o.payable_amount_fen,
                         CASE WHEN o.status = 'PENDING' AND o.expires_at <= ? THEN 'EXPIRED' ELSE o.status END AS effective_status,
                         o.notify_status, o.paid_source, o.manual_paid_by, o.paid_at, o.expires_at, o.created_at, o.device,
                         j.handled_at AS callback_handled_at, j.handled_reason AS callback_handled_reason,
                         j.handled_by AS callback_handled_by
                  FROM payment_order o LEFT JOIN callback_job j ON j.order_id = o.id`;
  const [items, count] = await Promise.all([
    runtimeAll<Record<string, unknown>>(
      env,
      key,
      `SELECT id, merchant_order_no, provider, subject, amount_fen, payable_amount_fen,
              effective_status AS status, notify_status, paid_source, manual_paid_by, paid_at, expires_at, created_at, device,
              callback_handled_at, callback_handled_reason, callback_handled_by
       FROM (${source}) ${where} ORDER BY created_at DESC LIMIT ? OFFSET ?`,
      [now, ...params, input.pageSize, offset],
      { ttlMs: 30_000 },
    ),
    runtimeFirst<{ total: number }>(
      env,
      `${key}:count`,
      `SELECT COUNT(*) AS total FROM (${source}) ${where}`,
      [now, ...params],
      { ttlMs: 30_000 },
    ),
  ]);
  return { items, total: count?.total ?? 0, page: input.page, pageSize: input.pageSize };
}

// 把易支付的 type 参数（wxpay/wechat/weixin/1、alipay/zfb/2）归一成内部渠道枚举，无法识别抛 UNSUPPORTED_PROVIDER。
export function normalizeProvider(value: string): Provider {
  const normalized = value.trim().toLowerCase();
  if (["wxpay", "wechat", "weixin", "1"].includes(normalized)) return "wxpay";
  if (["alipay", "zfb", "2"].includes(normalized)) return "alipay";
  throw new Error("UNSUPPORTED_PROVIDER");
}

// 校验商户请求签名：pid 必须等于配置的商户 ID，sign_type 只能是 MD5，签名用定长比较，避免时序侧信道。
export function verifyEpayRequest(params: Record<string, string>, setting: SystemSetting): boolean {
  if (!setting.merchant_key || params.pid !== setting.merchant_id || !params.sign) return false;
  if (params.sign_type && params.sign_type.toUpperCase() !== "MD5") return false;
  return constantTimeEqual(params.sign.toLowerCase(), createEpaySign(params, setting.merchant_key));
}

// 创建订单并分配尾差。
// 同一商户单号已存在时，只有渠道与金额完全一致才复用（幂等），否则报 MERCHANT_ORDER_CONFLICT。
// 尾差用于区分同金额并发订单：按配置方向在 amount_fen 上逐档加减 1 分，
// 靠 (provider, payable_amount_fen) 的部分唯一索引抢槽位，抢不到就试下一档，全部占满报 AMOUNT_SLOTS_EXHAUSTED。
export async function createOrder(env: Env, input: CreateOrderInput): Promise<CreateOrderResult> {
  const database = env.DB;
  const setting = await getSettings(env);
  if (!setting.merchant_key) throw new Error("MERCHANT_NOT_CONFIGURED");
  if (!(await getQrCode(env, input.provider))) throw new Error("PAYMENT_QR_NOT_CONFIGURED");

  const amountFen = parseMoneyToFen(input.amount);
  const existing = await getOrderByMerchantNo(env, input.merchantOrderNo);
  if (existing) {
    if (existing.provider !== input.provider || existing.amount_fen !== amountFen) {
      throw new Error("MERCHANT_ORDER_CONFLICT");
    }
    return { order: existing, setting, reused: true };
  }

  const now = Date.now();
  await database
    .prepare("UPDATE payment_order SET status = 'EXPIRED', updated_at = ? WHERE provider = ? AND status = 'PENDING' AND expires_at <= ?")
    .bind(now, input.provider, now)
    .run();
  const expiresAt = now + setting.order_ttl_minutes * 60_000;
  const notifyUrl = validatePublicHttpUrl(input.notifyUrl);
  const returnUrl = validatePublicHttpUrl(input.returnUrl);
  const direction = setting.amount_direction === "DOWN" ? -1 : 1;

  for (let adjustment = 0; adjustment <= setting.max_amount_adjustments; adjustment += 1) {
    const payableAmountFen = amountFen + direction * adjustment;
    if (payableAmountFen <= 0) break;
    const id = crypto.randomUUID().replaceAll("-", "");
    try {
      const order = await database
        .prepare(
          `INSERT INTO payment_order (
            id, merchant_order_no, merchant_id, provider, subject, amount_fen, payable_amount_fen,
            status, notify_status, notify_url, return_url, attach, client_ip, device,
            expires_at, created_at, updated_at
          ) VALUES (?, ?, ?, ?, ?, ?, ?, 'PENDING', 'NONE', ?, ?, ?, ?, ?, ?, ?, ?)
          RETURNING *`,
        )
        .bind(
          id,
          input.merchantOrderNo,
          setting.merchant_id,
          input.provider,
          input.subject.slice(0, 200),
          amountFen,
          payableAmountFen,
          notifyUrl,
          returnUrl,
          (input.attach ?? "").slice(0, 500),
          input.clientIp,
          input.device?.slice(0, 50) ?? null,
          expiresAt,
          now,
          now,
        )
        .first<PaymentOrder>();
      if (!order) throw new Error("ORDER_CREATE_FAILED");
      await Promise.all([cacheOrder(env, order), invalidateOrderViews(env, order.id)]);
      return { order, setting, reused: false };
    } catch (error) {
      const message = error instanceof Error ? error.message : String(error);
      if (message.includes("merchant_order_no")) {
        throw new Error("MERCHANT_ORDER_CONFLICT");
      }
      if (!message.includes("UNIQUE constraint failed") && !message.includes("payment_order_active_amount_unique")) {
        throw error;
      }
    }
  }

  throw new Error("AMOUNT_SLOTS_EXHAUSTED");
}

// 按系统订单号读取订单。
export async function getOrder(env: Env, id: string): Promise<PaymentOrder | null> {
  const order = await runtimeFirst<PaymentOrder>(
    env,
    `order:id:${id}`,
    "SELECT * FROM payment_order WHERE id = ?",
    [id],
    { ttlMs: null },
  );
  if (!order) return null;
  const effective = effectiveOrder(order);
  await cacheOrder(env, effective);
  return effective;
}

// 按商户订单号读取订单，供易支付查询接口使用。
export async function getOrderByMerchantNo(env: Env, merchantOrderNo: string): Promise<PaymentOrder | null> {
  const order = await runtimeFirst<PaymentOrder>(
    env,
    `order:merchant:${merchantOrderNo}`,
    "SELECT * FROM payment_order WHERE merchant_order_no = ?",
    [merchantOrderNo],
    { ttlMs: null },
  );
  if (!order) return null;
  const effective = effectiveOrder(order);
  await cacheOrder(env, effective);
  return effective;
}

function effectiveOrder(order: PaymentOrder, now = Date.now()): PaymentOrder {
  return order.status === "PENDING" && order.expires_at <= now ? { ...order, status: "EXPIRED" } : order;
}

// 构造易支付风格的回调/跳转参数并签名。type 取订单当前渠道，因此渠道变化会如实反映给商户。
export function buildEpayCallbackParams(order: PaymentOrder, merchantKey: string): Record<string, string> {
  const params: Record<string, string> = {
    pid: order.merchant_id,
    trade_no: order.id,
    out_trade_no: order.merchant_order_no,
    type: order.provider,
    name: order.subject,
    money: formatFen(order.amount_fen),
    trade_status: "TRADE_SUCCESS",
    param: order.attach,
    sign_type: "MD5",
  };
  params.sign = createEpaySign(params, merchantKey);
  return params;
}

// 把参数追加到 URL 查询串，保留原有查询参数并做百分号编码。
export function appendParamsToUrl(baseUrl: string, params: Record<string, string>): string {
  const url = new URL(baseUrl);
  for (const [key, value] of Object.entries(params)) url.searchParams.set(key, value);
  return url.toString();
}
