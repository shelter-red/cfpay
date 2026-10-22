// 把「元」字符串解析成整数「分」。全程用整数运算，绝不经过浮点，避免 0.1+0.2 这类误差影响尾差匹配；格式非法或非正数抛 INVALID_MONEY。
export function parseMoneyToFen(value: string): number {
  const normalized = value.trim();
  if (!/^\d{1,10}(?:\.\d{1,2})?$/.test(normalized)) {
    throw new Error("INVALID_MONEY");
  }

  const [yuan, decimal = ""] = normalized.split(".");
  const fen = Number(yuan) * 100 + Number(decimal.padEnd(2, "0"));
  if (!Number.isSafeInteger(fen) || fen <= 0) {
    throw new Error("INVALID_MONEY");
  }
  return fen;
}

// 把整数「分」格式化回「元」字符串，同样只用整数除法与取余，不做浮点运算。
export function formatFen(value: number): string {
  if (!Number.isSafeInteger(value)) {
    throw new Error("INVALID_FEN");
  }
  return `${Math.trunc(value / 100)}.${Math.abs(value % 100).toString().padStart(2, "0")}`;
}
