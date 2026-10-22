import { getAlipayCredentialStatus } from "./alipay-config";
import { activateAlipayPolling, getAlipayPollerStatus } from "./alipay";
import { getSettings } from "./db";
import { constantTimeEqual, hmacSha256Hex } from "./lib/crypto";
import { getClientIp } from "./lib/http";
import { getMonitorSnapshot } from "./monitor-state";
import { createOrder, getOrder, getOrderByMerchantNo, normalizeProvider, verifyEpayRequest } from "./orders";
import { getQrCode } from "./qr-codes";
import { runtimeAll, runtimeFirst } from "./runtime-data";
import type { Env, PaymentOrder } from "./types";

interface CreateQuickPayInput {
  provider?: unknown;
  amount?: unknown;
  subject?: unknown;
}

interface QuickPayCallbackRow {
  status: string;
  attempt_count: number;
  max_attempts: number;
  next_attempt_at: number | null;
  last_error: string | null;
  handled_at: number | null;
  handled_reason: string | null;
  handled_by: string | null;
  updated_at: number;
}

interface QuickPayEventRow {
  id: string;
  device_id: string;
  provider: string;
  amount_fen: number;
  paid_at: number;
  status: string;
  matched_order_id: string | null;
  raw_excerpt: string | null;
  created_at: number;
}

type DiagnosticStatus = "complete" | "waiting" | "failed";

/** 生成快捷支付内置回调令牌的签名原文。 */
function quickPayTokenInput(merchantOrderNo: string): string {
  return `cfpay:quick-pay:${merchantOrderNo}`;
}

/** 使用会话密钥为快捷支付订单生成不可伪造的内置回调令牌。 */
async function quickPayToken(env: Env, merchantOrderNo: string): Promise<string> {
  return hmacSha256Hex(env.SESSION_SECRET, quickPayTokenInput(merchantOrderNo));
}

/** 生成普通快捷支付商户订单号，不再包含测试订单标记。 */
function quickPayMerchantOrderNo(): string {
  return `CFPAY${Date.now()}${crypto.randomUUID().replaceAll("-", "").slice(0, 8)}`;
}

/** 校验并规范化商品名称，空值使用管理端默认商品名。 */
function quickPaySubject(value: unknown): string {
  if (typeof value !== "string") return "CFPay 快捷支付";
  const normalized = value.trim();
  if (!normalized) return "CFPay 快捷支付";
  if (normalized.length > 200) throw new Error("QUICK_PAY_SUBJECT_INVALID");
  return normalized;
}

/** 安全解析收款事件原始 JSON，解析失败时仍保留原文。 */
function parsePaymentRaw(raw: string | null): Record<string, unknown> | null {
  if (!raw) return null;
  try {
    const parsed: unknown = JSON.parse(raw);
    return parsed && typeof parsed === "object" && !Array.isArray(parsed) ? parsed as Record<string, unknown> : null;
  } catch {
    return null;
  }
}

/** 构造一个带实际数据的诊断阶段。 */
function diagnosticStage(
  key: string,
  title: string,
  status: DiagnosticStatus,
  summary: string,
  data: Record<string, unknown>,
): Record<string, unknown> {
  return { key, title, status, summary, data };
}

/** 创建完全按普通订单处理的快捷支付订单，内置回调只是普通 notify_url。 */
export async function createQuickPayOrder(
  env: Env,
  request: Request,
  input: CreateQuickPayInput,
): Promise<{ order: PaymentOrder; payUrl: string }> {
  const provider = normalizeProvider(typeof input.provider === "string" ? input.provider : "");
  const amount = typeof input.amount === "string" ? input.amount.trim() : "";
  const merchantOrderNo = quickPayMerchantOrderNo();
  const token = await quickPayToken(env, merchantOrderNo);
  const origin = new URL(request.url).origin;
  const result = await createOrder(env, {
    merchantOrderNo,
    provider,
    subject: quickPaySubject(input.subject),
    amount,
    notifyUrl: `${origin}/api/quick-pay/callback/${token}`,
    returnUrl: null,
    attach: "",
    clientIp: getClientIp(request.headers),
    device: null,
  });
  if (provider === "alipay") await activateAlipayPolling(env);
  return { order: result.order, payUrl: `${origin}/pay/${result.order.id}` };
}

/** 汇总普通订单的六阶段诊断数据，诊断读取不改变订单业务流程。 */
export async function getQuickPayDiagnostics(env: Env, request: Request, orderId: string): Promise<Record<string, unknown>> {
  const order = await getOrder(env, orderId);
  if (!order) throw new Error("ORDER_NOT_FOUND");
  const event = order.payment_event_id
    ? await runtimeFirst<QuickPayEventRow>(
        env,
        `event:id:${order.payment_event_id}`,
        `SELECT id, device_id, provider, amount_fen, paid_at, status, matched_order_id, raw_excerpt, created_at
         FROM payment_event WHERE id = ?`,
        [order.payment_event_id],
        { ttlMs: null },
      )
    : null;
  const callback = await runtimeFirst<QuickPayCallbackRow>(
    env,
    `callback:order:${order.id}`,
    `SELECT status, attempt_count, max_attempts, next_attempt_at, last_error,
            handled_at, handled_reason, handled_by, updated_at
     FROM callback_job WHERE order_id = ?`,
    [order.id],
    { ttlMs: null },
  );
  const attempts = callback
    ? await runtimeAll<Record<string, unknown>>(
        env,
        `attempts:order:${order.id}:asc`,
        `SELECT a.attempt_no, a.response_status, a.response_excerpt, a.error, a.created_at
         FROM callback_attempt a JOIN callback_job j ON j.id = a.job_id
         WHERE j.order_id = ? ORDER BY a.attempt_no ASC`,
        [order.id],
        { ttlMs: null },
      )
    : [];
  const qrConfigured = Boolean(await getQrCode(env, order.provider));
  let channelData: Record<string, unknown>;
  if (order.provider === "wxpay") {
    const monitor = await getMonitorSnapshot(env);
    channelData = { qrConfigured, availability: monitor.wechatChannel, monitor };
  } else {
    channelData = {
      qrConfigured,
      credentials: await getAlipayCredentialStatus(env),
      poller: await getAlipayPollerStatus(env),
    };
  }
  const channelReady = order.provider === "wxpay"
    ? qrConfigured && Boolean((channelData as { availability?: { available?: boolean } }).availability?.available)
    : qrConfigured && Boolean((channelData as { credentials?: { configured?: boolean } }).credentials?.configured);
  const matched = Boolean(event && event.status === "MATCHED" && event.matched_order_id === order.id && order.status === "PAID");
  const callbackFailed = callback?.status === "FAILED";
  const callbackSucceeded = callback?.status === "SUCCESS" && order.notify_status === "SUCCESS";
  const payUrl = `${new URL(request.url).origin}/pay/${order.id}`;
  const paymentRaw = event?.raw_excerpt ?? null;
  const stages = [
    diagnosticStage("order", "商品与订单", "complete", "商品与普通支付订单已创建。", {
      subject: order.subject,
      provider: order.provider,
      amountFen: order.amount_fen,
      payableAmountFen: order.payable_amount_fen,
      merchantOrderNo: order.merchant_order_no,
      orderId: order.id,
      createdAt: order.created_at,
      expiresAt: order.expires_at,
    }),
    diagnosticStage("channel", "通道与收银台", channelReady ? "complete" : "waiting", channelReady ? "支付通道和收银台已就绪。" : "支付通道尚未完全就绪，请检查阶段数据。", {
      payUrl,
      ...channelData,
    }),
    diagnosticStage("capture", "真实收款采集", event ? "complete" : "waiting", event ? "已经收到收款事件。" : "等待微信 Hook 或支付宝账务查询上报真实收款。", {
      event: event ? { ...event, raw_excerpt: undefined } : null,
      parsed: parsePaymentRaw(paymentRaw),
      raw: paymentRaw,
    }),
    diagnosticStage("match", "匹配与确认", matched ? "complete" : "waiting", matched ? "收款事件已匹配并确认订单。" : "等待收款金额和渠道与订单匹配。", {
      orderStatus: order.status,
      orderProvider: order.provider,
      orderPayableAmountFen: order.payable_amount_fen,
      eventProvider: event?.provider ?? null,
      eventAmountFen: event?.amount_fen ?? null,
      eventStatus: event?.status ?? null,
      matchedOrderId: event?.matched_order_id ?? null,
      paidAt: order.paid_at,
    }),
    diagnosticStage("callback", "回调任务", callbackFailed ? "failed" : callback ? "complete" : "waiting", callbackFailed ? "商户回调任务执行失败。" : callback ? "商户回调任务已创建。" : "订单支付确认后创建商户回调任务。", {
      notifyUrl: order.notify_url,
      notifyStatus: order.notify_status,
      callback,
    }),
    diagnosticStage("response", "回调响应", callbackFailed ? "failed" : callbackSucceeded ? "complete" : "waiting", callbackSucceeded ? "内置回调已验签并返回 success。" : callbackFailed ? "回调已失败，请查看每次响应数据。" : "等待商户回调响应。", {
      attempts,
      finalStatus: callback?.status ?? null,
      verified: callbackSucceeded,
    }),
  ];
  return {
    order,
    paymentEvent: event ? { ...event, raw_excerpt: undefined } : null,
    paymentDetail: parsePaymentRaw(paymentRaw),
    paymentRaw,
    callback,
    callbackAttempts: attempts,
    stages,
    completed: order.status === "PAID" && matched && callbackSucceeded,
    payUrl,
  };
}

/** 接收快捷支付的内置易支付回调并验证令牌、签名和订单参数。 */
export async function receiveQuickPayCallback(
  env: Env,
  request: Request,
  providedToken: string,
): Promise<Response> {
  if (!/^[a-f0-9]{64}$/.test(providedToken)) return new Response("回调令牌无效", { status: 404 });
  const params = Object.fromEntries(new URL(request.url).searchParams.entries());
  const merchantOrderNo = params.out_trade_no ?? "";
  const order = merchantOrderNo ? await getOrderByMerchantNo(env, merchantOrderNo) : null;
  if (!order) return new Response("订单不存在", { status: 404 });
  const expectedToken = await quickPayToken(env, order.merchant_order_no);
  if (!constantTimeEqual(providedToken, expectedToken)) {
    return new Response("回调令牌校验失败", { status: 403 });
  }
  const setting = await getSettings(env);
  if (!verifyEpayRequest(params, setting)) return new Response("回调签名校验失败", { status: 400 });
  if (params.trade_status !== "TRADE_SUCCESS" || params.trade_no !== order.id) {
    return new Response("回调订单参数不匹配", { status: 400 });
  }
  return new Response("success", { status: 200, headers: { "content-type": "text/plain; charset=utf-8" } });
}
