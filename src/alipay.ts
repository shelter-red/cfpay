import { DurableObject } from "cloudflare:workers";
import { resolveAlipayConfig, type AlipayRuntimeConfig } from "./alipay-config";
import { pendingAlipayOrders } from "./alipay-active-orders";
import { buildAlipayLedgerRequest } from "./alipay-request";
import { processCallbackForOrder } from "./callbacks";
import { parseMoneyToFen } from "./lib/money";
import { recordTrustedPaymentEvent } from "./monitor";
import type { Env } from "./types";

const POLLER_NAME = "single-merchant-alipay-ledger";

interface AlipayLedgerDetail {
  account_log_id?: string;
  trans_dt?: string;
  trans_amount?: string;
  direction?: string;
  alipay_order_no?: string;
  merchant_order_no?: string;
  trans_memo?: string;
}

interface AlipayLedgerResponse {
  page_no?: string | number;
  page_size?: string | number;
  total_size?: string | number;
  detail_list?: AlipayLedgerDetail[];
}

export interface AlipayQueryResult {
  traceId: string;
  traceSource: "alipay" | "cfpay_request";
  totalSize: number;
  details: AlipayLedgerDetail[];
}

let cachedPublicKey: { source: string; key: CryptoKey } | null = null;

// 判断支付宝账务 API 是否可用（D1 凭据或 Worker Secrets 任一配好即可）。
export async function isAlipayApiConfigured(env: Env): Promise<boolean> {
  return Boolean(await resolveAlipayConfig(env));
}

// 唤醒 Durable Object 并让它自行决定是否需要排下一次闹钟。
export async function activateAlipayPolling(env: Env): Promise<void> {
  if (!(await isAlipayApiConfigured(env))) return;
  const stub = env.ALIPAY_POLLER.get(env.ALIPAY_POLLER.idFromName(POLLER_NAME));
  await stub.fetch("https://alipay-poller/activate", { method: "POST" });
}

// 向 Durable Object 查询轮询状态（是否已配置、当前闹钟、上次运行结果），供后台展示。
export async function getAlipayPollerStatus(env: Env): Promise<Record<string, unknown>> {
  const stub = env.ALIPAY_POLLER.get(env.ALIPAY_POLLER.idFromName(POLLER_NAME));
  const response = await stub.fetch("https://alipay-poller/status");
  return response.json<Record<string, unknown>>();
}

// 查询支付宝账务明细（alipay.data.bill.accountlog.query）。
// 请求按 V3 证书模式签名，响应必须验签通过才采信，验签失败一律抛错而不是当作“没有账单”。
export async function queryAlipayLedger(env: Env, lookbackMinutes = 5): Promise<AlipayQueryResult> {
  const config = await requireAlipayConfig(env);
  const signedRequest = await buildAlipayLedgerRequest(config, lookbackMinutes);
  const response = await fetch(signedRequest.url, signedRequest.init);
  const body = await response.text();
  const alipayTraceId = response.headers.get("alipay-trace-id")?.trim();
  const traceId = alipayTraceId || signedRequest.requestId;
  const traceSource = alipayTraceId ? "alipay" : "cfpay_request";
  if (!response.ok) {
    throw new Error(`ALIPAY_HTTP_${response.status}:${safeAlipayError(body)}`);
  }
  await verifyAlipayResponse(config, response.headers, body);
  const parsed = JSON.parse(body) as AlipayLedgerResponse;
  return {
    traceId,
    traceSource,
    totalSize: Number(parsed.total_size ?? 0),
    details: Array.isArray(parsed.detail_list) ? parsed.detail_list : [],
  };
}

export class AlipayLedgerPoller extends DurableObject<Env> {
  // 保存 DO 状态与绑定；轮询进度存放在 storage 里，实例重建后仍能续上。
  constructor(state: DurableObjectState, env: Env) {
    super(state, env);
  }

  // DO 的内部接口：/status 返回轮询状态，/activate 触发一次立即检查，其它路径 404。不对外暴露。
  async fetch(request: Request): Promise<Response> {
    const pathname = new URL(request.url).pathname;
    if (request.method === "POST" && pathname === "/activate") {
      const currentAlarm = await this.ctx.storage.getAlarm();
      if (currentAlarm === null || currentAlarm > Date.now() + 1_000) {
        await this.ctx.storage.setAlarm(Date.now() + 250);
      }
      return Response.json({ ok: true });
    }
    if (request.method === "GET" && pathname === "/status") {
      const lastRun = (await this.ctx.storage.get<Record<string, unknown>>("lastRun")) ?? null;
      return Response.json({ configured: await isAlipayApiConfigured(this.env), alarm: await this.ctx.storage.getAlarm(), lastRun });
    }
    return new Response("Not Found", { status: 404 });
  }

  // 轮询主体：统计待支付支付宝订单，逐条查询账务并把收入落库匹配，然后决定下一次闹钟。
  // 没有待支付订单时停止轮询（不再排闹钟），避免无意义的 API 调用；
  // 出错时按不小于 30 秒的间隔重试，防止把支付宝接口打爆。
  async alarm(): Promise<void> {
    const pendingBefore = await pendingAlipayOrders(this.env);
    if (pendingBefore === 0 || !(await isAlipayApiConfigured(this.env))) {
      await this.ctx.storage.put("lastRun", { at: Date.now(), pending: pendingBefore, stopped: true });
      return;
    }

    let matched = 0;
    let processed = 0;
    let error: string | null = null;
    let traceId: string | null = null;
    try {
      const result = await queryAlipayLedger(this.env, 5);
      traceId = result.traceId;
      for (const detail of result.details) {
        const normalized = normalizeLedgerIncome(detail);
        if (!normalized) continue;
        processed += 1;
        const settlement = await recordTrustedPaymentEvent(this.env, normalized);
        if (settlement.matched && settlement.orderId) {
          matched += 1;
          await processCallbackForOrder(this.env, settlement.orderId);
        }
      }
    } catch (cause) {
      error = cause instanceof Error ? cause.message : String(cause);
      console.error("alipay ledger polling failed", cause);
    }

    const pendingAfter = await pendingAlipayOrders(this.env);
    await this.ctx.storage.put("lastRun", {
      at: Date.now(),
      pending: pendingAfter,
      processed,
      matched,
      traceId,
      error,
    });
    if (pendingAfter > 0) {
      const seconds = normalizePollSeconds(this.env.ALIPAY_ACTIVE_POLL_SECONDS);
      await this.ctx.storage.setAlarm(Date.now() + (error ? Math.max(seconds, 30) : seconds) * 1_000);
    }
  }
}

// 把账务明细归一成内部收款事件：只接受 direction 为“收入”、金额与时间都能解析的记录。
// 事件标识为 alipay: + 账务流水号，天然幂等；同时保存完整账务字段供后台人工对账。
function normalizeLedgerIncome(detail: AlipayLedgerDetail) {
  if (!detail.account_log_id || detail.direction !== "收入" || !detail.trans_amount || !detail.trans_dt) return null;
  let amountFen: number;
  try {
    amountFen = parseMoneyToFen(detail.trans_amount);
  } catch {
    return null;
  }
  const paidAt = Date.parse(`${detail.trans_dt.replace(" ", "T")}+08:00`);
  if (!Number.isFinite(paidAt)) return null;
  return {
    eventId: `alipay:${detail.account_log_id}`,
    deviceId: "alipay-openapi",
    provider: "alipay" as const,
    amountFen,
    paidAt,
    // 保存账务接口返回的完整明细字段，后台订单详情才能用于人工对账；
    // source 与微信侧的 paymsg 对齐，便于前端判断该用哪套字段名。
    rawExcerpt: JSON.stringify({
      source: "alipay-accountlog",
      accountLogId: detail.account_log_id,
      alipayOrderNo: detail.alipay_order_no ?? null,
      merchantOrderNo: detail.merchant_order_no ?? null,
      transAmount: detail.trans_amount ?? null,
      transDt: detail.trans_dt ?? null,
      direction: detail.direction ?? null,
      memo: detail.trans_memo ?? null,
    }),
  };
}

// 取凭据，未配置时抛 ALIPAY_API_NOT_CONFIGURED。
async function requireAlipayConfig(env: Env): Promise<AlipayRuntimeConfig> {
  const config = await resolveAlipayConfig(env);
  if (!config) throw new Error("ALIPAY_API_NOT_CONFIGURED");
  return config;
}

// 解析轮询间隔配置，非法或缺省时用默认值，并限制在合理区间内。
function normalizePollSeconds(value: string | undefined): number {
  const seconds = Number(value ?? 2);
  return Number.isInteger(seconds) && seconds >= 2 && seconds <= 60 ? seconds : 2;
}

// 验证支付宝响应签名：签名原文为 timestamp\n + nonce\n + body\n，用支付宝公钥做 RSASSA-PKCS1-v1_5 + SHA-256。
// 响应头带了证书序列号时还要与本地配置的 alipayCertSn 比对，防止被换证书伪造响应。
async function verifyAlipayResponse(config: AlipayRuntimeConfig, headers: Headers, body: string): Promise<void> {
  const timestamp = headers.get("alipay-timestamp");
  const nonce = headers.get("alipay-nonce");
  const signature = headers.get("alipay-signature");
  const certSn = headers.get("alipay-sn");
  if (!timestamp || !nonce || !signature) throw new Error("ALIPAY_RESPONSE_SIGNATURE_MISSING");
  if (certSn && certSn !== config.alipayCertSn) throw new Error("ALIPAY_RESPONSE_CERT_MISMATCH");
  const key = await importPublicKey(config.alipayPublicKey);
  const valid = await crypto.subtle.verify(
    "RSASSA-PKCS1-v1_5",
    key,
    base64ToArrayBuffer(signature),
    new TextEncoder().encode(`${timestamp}\n${nonce}\n${body}\n`),
  );
  if (!valid) throw new Error("ALIPAY_RESPONSE_SIGNATURE_INVALID");
}

// 导入 SPKI PEM 公钥为 CryptoKey（仅验签）。
async function importPublicKey(value: string): Promise<CryptoKey> {
  if (cachedPublicKey?.source === value) return cachedPublicKey.key;
  const key = await crypto.subtle.importKey(
    "spki",
    pemToBytes(value),
    { name: "RSASSA-PKCS1-v1_5", hash: "SHA-256" },
    false,
    ["verify"],
  );
  cachedPublicKey = { source: value, key };
  return key;
}

// 剥掉 PEM 头尾与空白后 base64 解码。
function pemToBytes(value: string): ArrayBuffer {
  const base64 = value.replace(/-----BEGIN [^-]+-----|-----END [^-]+-----|\s+/g, "");
  return base64ToArrayBuffer(base64);
}

// base64 解码为 ArrayBuffer。
function base64ToArrayBuffer(value: string): ArrayBuffer {
  const binary = atob(value);
  return Uint8Array.from(binary, (character) => character.charCodeAt(0)).buffer as ArrayBuffer;
}

// 把支付宝的错误响应压成 code:message 形式，解析不出来就返回固定标识，不把原始响应体透出到日志与后台。
function safeAlipayError(body: string): string {
  try {
    const parsed = JSON.parse(body) as { code?: string; message?: string };
    return `${parsed.code ?? "UNKNOWN"}:${parsed.message ?? "request failed"}`;
  } catch {
    return "INVALID_ERROR_RESPONSE";
  }
}
