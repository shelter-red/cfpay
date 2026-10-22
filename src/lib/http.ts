const blockedHostPatterns = [
  /^localhost$/i,
  /^127\./,
  /^10\./,
  /^0\./,
  /^169\.254\./,
  /^192\.168\./,
  /^172\.(1[6-9]|2\d|3[01])\./,
  /^\[?::1\]?$/,
];

// 校验商户回调/跳转地址：只允许 http(s)，拒绝 localhost 与常见内网网段，防止 Worker 被当作 SSRF 跳板；同时清空 URL 里的账号密码。空值返回 null 而不是报错。
export function validatePublicHttpUrl(value: string | null | undefined): string | null {
  if (!value?.trim()) return null;
  const url = new URL(value.trim());
  if (url.protocol !== "https:" && url.protocol !== "http:") {
    throw new Error("INVALID_URL_PROTOCOL");
  }
  if (blockedHostPatterns.some((pattern) => pattern.test(url.hostname))) {
    throw new Error("PRIVATE_URL_NOT_ALLOWED");
  }
  url.username = "";
  url.password = "";
  return url.toString();
}

// 取真实客户端 IP：优先 Cloudflare 的 cf-connecting-ip，回退 x-forwarded-for 的第一个地址。
export function getClientIp(headers: Headers): string | null {
  return headers.get("cf-connecting-ip") ?? headers.get("x-forwarded-for")?.split(",")[0]?.trim() ?? null;
}

// 按字符数截断并补省略号，用于把响应体、错误信息写库前限制长度。
export function truncate(value: string, maxLength = 500): string {
  return value.length <= maxLength ? value : `${value.slice(0, maxLength)}…`;
}
