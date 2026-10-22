import QRCode from "qrcode";
import { runtimeAll, runtimeFirst, runtimeInvalidate, runtimePut } from "./runtime-data";
import type { Env, Provider } from "./types";

const MAX_QR_PAYLOAD_LENGTH = 2048;

export interface QrCodeStatus {
  provider: Provider;
  payloadSha256: string;
  updatedAt: number;
  url: string;
}

interface QrCodeRow {
  provider: Provider;
  payload: string;
  payload_sha256: string;
  updated_at: number;
}

// 把各种渠道写法归一成 wxpay / alipay，非法值抛 QR_PROVIDER_MISMATCH 之外的 UNSUPPORTED_PROVIDER。
export function normalizeQrProvider(value: string): Provider {
  if (value === "wxpay" || value === "alipay") return value;
  throw new Error("UNSUPPORTED_PROVIDER");
}

// 校验浏览器解析出的二维码内容：长度受限，且必须与所选渠道匹配（微信 wxp:// 或 http 链接、支付宝 https://qr.alipay.com/…），避免把微信码存成支付宝码。
export function validateQrPayload(value: unknown, provider: Provider): string {
  if (typeof value !== "string") throw new Error("QR_PAYLOAD_REQUIRED");
  const payload = value.trim();
  if (payload.length < 3 || payload.length > MAX_QR_PAYLOAD_LENGTH || /[\u0000-\u001f\u007f]/.test(payload)) {
    throw new Error("INVALID_QR_PAYLOAD");
  }
  if (!/^[a-z][a-z0-9+.-]{1,31}:/i.test(payload)) throw new Error("INVALID_QR_PAYLOAD");

  const normalized = payload.toLowerCase();
  if (provider === "wxpay" && (normalized.startsWith("alipays:") || normalized.includes("qr.alipay.com"))) {
    throw new Error("QR_PROVIDER_MISMATCH");
  }
  if (
    provider === "alipay" &&
    (normalized.startsWith("wxp:") || normalized.includes("weixin.qq.com") || normalized.includes("tenpay.com"))
  ) {
    throw new Error("QR_PROVIDER_MISMATCH");
  }
  return payload;
}

// 列出两个渠道的收款码状态（不含原文），供后台展示；原文只保存在服务端，接口只给指纹与渲染地址。
export async function listQrCodes(env: Env): Promise<QrCodeStatus[]> {
  const rows = await runtimeAll<Pick<QrCodeRow, "provider" | "payload_sha256" | "updated_at">>(
    env,
    "config:qr:list",
    "SELECT provider, payload_sha256, updated_at FROM payment_qr_code ORDER BY provider",
    [],
    { ttlMs: null, pinned: true },
  );
  return rows.map(toQrCodeStatus);
}

// 按渠道读取收款码整行（含二维码原文），只用于服务端重新生成 SVG。
export async function getQrCode(env: Env, provider: Provider): Promise<QrCodeRow | null> {
  return runtimeFirst<QrCodeRow>(
    env,
    `config:qr:${provider}`,
    "SELECT * FROM payment_qr_code WHERE provider = ?",
    [provider],
    { ttlMs: null, pinned: true },
  );
}

// 保存二维码内容并计算 SHA-256 指纹；图片本身不入库，公开路由按指纹做 ETag 缓存。
export async function saveQrCode(env: Env, provider: Provider, value: unknown): Promise<QrCodeStatus> {
  const database = env.DB;
  const payload = validateQrPayload(value, provider);
  const payloadSha256 = await sha256Hex(payload);
  const now = Date.now();
  const url = qrCodeUrl(provider, payloadSha256);
  await database
    .prepare(
      `INSERT INTO payment_qr_code (provider, payload, payload_sha256, created_at, updated_at)
       VALUES (?, ?, ?, ?, ?)
       ON CONFLICT(provider) DO UPDATE SET
         payload = excluded.payload,
         payload_sha256 = excluded.payload_sha256,
         updated_at = excluded.updated_at`,
    )
    .bind(provider, payload, payloadSha256, now, now)
    .run();

  await Promise.all([
    runtimePut<QrCodeRow>(env, `config:qr:${provider}`, { provider, payload, payload_sha256: payloadSha256, updated_at: now }, { ttlMs: null, pinned: true }),
    runtimeInvalidate(env, { keys: ["config:qr:list"] }),
  ]);

  return { provider, payloadSha256, updatedAt: now, url };
}

// 删除某渠道的收款码，并清空系统设置里对应的图片地址，避免收款页继续引用已删除的码。
export async function deleteQrCode(env: Env, provider: Provider): Promise<void> {
  const database = env.DB;
  await database.prepare("DELETE FROM payment_qr_code WHERE provider = ?").bind(provider).run();
  await Promise.all([
    runtimePut(env, `config:qr:${provider}`, null, { ttlMs: null, pinned: true }),
    runtimeInvalidate(env, { keys: ["config:qr:list"] }),
  ]);
}

// 由二维码原文即时生成 SVG。不存图片、不带原图元数据，响应头配 CSP default-src 'none' 与 nosniff。
export async function renderQrCodeSvg(payload: string): Promise<string> {
  return QRCode.toString(payload, {
    type: "svg",
    errorCorrectionLevel: "M",
    margin: 2,
    width: 480,
    color: { dark: "#111827ff", light: "#ffffffff" },
  });
}

// 计算二维码内容的 SHA-256，作为 ETag 与去重指纹。
async function sha256Hex(value: string): Promise<string> {
  const digest = new Uint8Array(await crypto.subtle.digest("SHA-256", new TextEncoder().encode(value)));
  return [...digest].map((byte) => byte.toString(16).padStart(2, "0")).join("");
}

// 拼出该渠道收款码的公开渲染地址。
export function qrCodeUrl(provider: Provider, payloadSha256: string): string {
  return `/payment-assets/qr/${provider}?v=${payloadSha256.slice(0, 16)}`;
}

// 把数据库行投影成对外的状态结构，刻意不包含 payload 原文。
function toQrCodeStatus(row: Pick<QrCodeRow, "provider" | "payload_sha256" | "updated_at">): QrCodeStatus {
  return {
    provider: row.provider,
    payloadSha256: row.payload_sha256,
    updatedAt: row.updated_at,
    url: qrCodeUrl(row.provider, row.payload_sha256),
  };
}
