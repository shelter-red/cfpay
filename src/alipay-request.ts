import type { AlipayRuntimeConfig } from "./alipay-config";

const ACCOUNT_LOG_PATH = "/v3/alipay/data/bill/accountlog/query";
let cachedPrivateKey: { source: string; key: CryptoKey } | null = null;

export interface AlipaySignedRequest {
  url: string;
  init: RequestInit;
  requestId: string;
}

// 构造支付宝账务明细查询（alipay.data.bill.accountlog.query）的 V3 签名请求。
// 证书模式下 authorization 头需要带 app_id、应用证书序列号、支付宝根证书序列号与 RSA2 签名，
// 另需 alipay-root-cert-sn 头；签名原文是 METHOD\nPATH\nQUERY\nBODY\nTIMESTAMP\nNONCE\n。
export async function buildAlipayLedgerRequest(
  config: AlipayRuntimeConfig,
  lookbackMinutes = 5,
  now = new Date(),
): Promise<AlipaySignedRequest> {
  const start = new Date(now.getTime() - lookbackMinutes * 60_000);
  const query = {
    end_time: formatAlipayTime(now),
    page_no: 1,
    page_size: 100,
    start_time: formatAlipayTime(start),
  };
  const path = `${ACCOUNT_LOG_PATH}?${canonicalQuery(query)}`;
  const nonce = crypto.randomUUID();
  const timestamp = Date.now();
  const authString = `app_id=${config.appId},app_cert_sn=${config.appCertSn},nonce=${nonce},timestamp=${timestamp}`;
  const signingString = `${authString}\nGET\n${path}\n\n`;
  const signature = await signBase64(config.appPrivateKey, signingString);
  const requestId = crypto.randomUUID();
  return {
    url: `https://openapi.alipay.com${path}`,
    init: {
      method: "GET",
      headers: {
        accept: "application/json",
        authorization: `ALIPAY-SHA256withRSA ${authString},sign=${signature}`,
        "alipay-request-id": requestId,
        "alipay-root-cert-sn": config.alipayRootCertSn,
        "user-agent": "cfpay-alipay-ledger/0.1",
      },
      signal: AbortSignal.timeout(15_000),
    },
    requestId,
  };
}

// 按参数名升序拼接查询串，值做 RFC3986 编码；顺序必须与签名原文完全一致。
function canonicalQuery(values: Record<string, string | number>): string {
  return Object.entries(values)
    .sort(([left], [right]) => (left < right ? -1 : left > right ? 1 : 0))
    .map(([key, value]) => `${rfc3986(key)}=${rfc3986(String(value))}`)
    .join("&");
}

// 严格的 RFC3986 百分号编码：encodeURIComponent 之后还要把 !'()* 也转义，否则支付宝侧验签会失败。
function rfc3986(value: string): string {
  return encodeURIComponent(value).replace(/[!'()*]/g, (character) => `%${character.charCodeAt(0).toString(16).toUpperCase()}`);
}

// 用应用私钥做 RSASSA-PKCS1-v1_5 + SHA-256 签名，输出 base64。
async function signBase64(privateKeyPem: string, value: string): Promise<string> {
  const key = await importPrivateKey(privateKeyPem);
  const signature = await crypto.subtle.sign("RSASSA-PKCS1-v1_5", key, new TextEncoder().encode(value));
  return bytesToBase64(new Uint8Array(signature));
}

// 导入 PKCS#8 PEM 私钥为 CryptoKey；只允许签名用途，不可导出。
async function importPrivateKey(value: string): Promise<CryptoKey> {
  if (cachedPrivateKey?.source === value) return cachedPrivateKey.key;
  const key = await crypto.subtle.importKey(
    "pkcs8",
    pemToBytes(value),
    { name: "RSASSA-PKCS1-v1_5", hash: "SHA-256" },
    false,
    ["sign"],
  );
  cachedPrivateKey = { source: value, key };
  return key;
}

// 剥掉 PEM 头尾与空白后做 base64 解码，得到 DER 字节。
function pemToBytes(value: string): ArrayBuffer {
  const base64 = value.replace(/-----BEGIN [^-]+-----|-----END [^-]+-----|\s+/g, "");
  return base64ToArrayBuffer(base64);
}

// base64 解码为 ArrayBuffer。
function base64ToArrayBuffer(value: string): ArrayBuffer {
  const binary = atob(value);
  return Uint8Array.from(binary, (character) => character.charCodeAt(0)).buffer as ArrayBuffer;
}

// 字节转 base64 字符串。
function bytesToBase64(value: Uint8Array): string {
  let binary = "";
  for (const byte of value) binary += String.fromCharCode(byte);
  return btoa(binary);
}

// 格式化支付宝要求的 ISO8601 时间（东八区、精确到秒、带 +08:00 偏移）。
function formatAlipayTime(date: Date): string {
  const parts = new Intl.DateTimeFormat("en-CA", {
    timeZone: "Asia/Shanghai",
    year: "numeric",
    month: "2-digit",
    day: "2-digit",
    hour: "2-digit",
    minute: "2-digit",
    second: "2-digit",
    hourCycle: "h23",
  }).formatToParts(date);
  const values = Object.fromEntries(parts.map((part) => [part.type, part.value]));
  return `${values.year}-${values.month}-${values.day} ${values.hour}:${values.minute}:${values.second}`;
}
