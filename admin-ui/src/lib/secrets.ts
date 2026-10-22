// 用 crypto.getRandomValues 生成 32 字节随机密钥并转 base64url；密钥只在浏览器内生成，不经过服务端。
export function randomSecret(): string {
  const bytes = crypto.getRandomValues(new Uint8Array(32));
  let binary = "";
  for (const byte of bytes) binary += String.fromCharCode(byte);
  return btoa(binary).replaceAll("+", "-").replaceAll("/", "_").replace(/=+$/g, "");
}

// 生成带前缀的随机标识（如商户 ID m_xxx），同样只用浏览器 CSPRNG。
export function randomIdentifier(prefix: string): string {
  const bytes = crypto.getRandomValues(new Uint8Array(12));
  return `${prefix}${Array.from(bytes, (byte) => byte.toString(16).padStart(2, "0")).join("")}`;
}

/** 优先用异步剪贴板 API，非安全上下文或被拒时回退到 execCommand。 */
export async function copyText(value: string): Promise<void> {
  try {
    await navigator.clipboard.writeText(value);
    return;
  } catch {
    const input = document.createElement("textarea");
    input.value = value;
    input.style.position = "fixed";
    input.style.opacity = "0";
    document.body.append(input);
    input.select();
    document.execCommand("copy");
    input.remove();
  }
}
