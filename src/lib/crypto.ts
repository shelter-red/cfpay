import { md5 } from "js-md5";

const encoder = new TextEncoder();

// 把摘要/签名结果转成小写十六进制字符串，易支付与监控端协议都要求这种形式。
function bytesToHex(bytes: ArrayBuffer): string {
  return [...new Uint8Array(bytes)].map((byte) => byte.toString(16).padStart(2, "0")).join("");
}

// 易支付 V1 兼容层专用：外部商户协议规定用 MD5 签名，内部一律不使用 MD5。
export function legacyMd5(value: string): string {
  return md5(value);
}

// 计算字符串的 SHA-256 十六进制摘要，用于二维码内容指纹与监控端签名原文。
export async function sha256Hex(value: string): Promise<string> {
  return bytesToHex(await crypto.subtle.digest("SHA-256", encoder.encode(value)));
}

// 以 secret 为密钥计算 HMAC-SHA256 十六进制结果，用于管理端会话与监控端请求签名。
export async function hmacSha256Hex(secret: string, value: string): Promise<string> {
  const key = await crypto.subtle.importKey(
    "raw",
    encoder.encode(secret),
    { name: "HMAC", hash: "SHA-256" },
    false,
    ["sign"],
  );
  return bytesToHex(await crypto.subtle.sign("HMAC", key, encoder.encode(value)));
}

// 定长比较：先把长度差异混入结果，再逐字节异或累积，避免用短路比较泄露口令或签名的前缀信息。
export function constantTimeEqual(left: string, right: string): boolean {
  const leftBytes = encoder.encode(left);
  const rightBytes = encoder.encode(right);
  let difference = leftBytes.length ^ rightBytes.length;
  const length = Math.max(leftBytes.length, rightBytes.length);
  for (let index = 0; index < length; index += 1) {
    difference |= (leftBytes[index] ?? 0) ^ (rightBytes[index] ?? 0);
  }
  return difference === 0;
}

// 按易支付规则生成签名：剔除空值与 sign/sign_type，其余参数按名称 ASCII 升序拼成 key=value&…，末尾直接追加密钥后取小写 MD5。
export function createEpaySign(params: Record<string, string>, merchantKey: string): string {
  const canonical = Object.entries(params)
    .filter(([key, value]) => key !== "sign" && key !== "sign_type" && value !== "")
    .sort(([left], [right]) => (left < right ? -1 : left > right ? 1 : 0))
    .map(([key, value]) => `${key}=${value}`)
    .join("&");
  return legacyMd5(`${canonical}${merchantKey}`);
}
