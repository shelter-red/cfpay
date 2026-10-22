// 整数分格式化成带 ¥ 的金额文本，不做浮点运算。
export function formatFen(fen: number | null | undefined): string {
  return `¥${(Number(fen ?? 0) / 100).toFixed(2)}`;
}

// 把字节数格式化为二进制单位，避免出现“7.44万 B”这类难读的混合单位。
export function formatBytes(value: number | null | undefined): string {
  const bytes = Math.max(0, Number(value ?? 0));
  const units = ["B", "KiB", "MiB", "GiB"];
  let amount = bytes;
  let unitIndex = 0;
  while (amount >= 1024 && unitIndex < units.length - 1) {
    amount /= 1024;
    unitIndex += 1;
  }
  const digits = unitIndex === 0 || amount >= 100 ? 0 : amount >= 10 ? 1 : 2;
  return `${amount.toFixed(digits).replace(/\.0+$|(?<=\.[0-9])0+$/g, "")} ${units[unitIndex]}`;
}

// 毫秒时间戳格式成本地日期时间；空值与非法值统一显示 “-”。
export function formatDateTime(value: number | string | null | undefined): string {
  if (value === null || value === undefined || value === "") return "-";
  const time = Number(value);
  return Number.isFinite(time) ? new Date(time).toLocaleString("zh-CN", { hour12: false }) : "-";
}

// 按剩余秒数格式化成 mm:ss，已过期时归零而不是显示负数。
export function formatCountdown(expiresAt: number, now = Date.now()): string {
  const seconds = Math.max(0, Math.round((Number(expiresAt) - now) / 1000));
  const mm = String(Math.floor(seconds / 60)).padStart(2, "0");
  const ss = String(seconds % 60).padStart(2, "0");
  return `${mm}:${ss}`;
}

// 把过去的时间点格式化成“x 秒/分钟/小时前”，用于心跳与上报时间。
export function formatRelative(pastAt: number, now = Date.now()): string {
  const seconds = Math.max(0, Math.round((now - Number(pastAt)) / 1000));
  if (seconds < 60) return `${seconds} 秒前`;
  if (seconds < 3600) return `${Math.floor(seconds / 60)} 分钟前`;
  return `${Math.floor(seconds / 3600)} 小时前`;
}

// 把上报的原始 JSON 美化输出；解析失败时原样返回，不因为格式问题丢掉内容。
export function prettyJson(raw: string): string {
  try {
    return JSON.stringify(JSON.parse(raw), null, 2);
  } catch {
    return raw;
  }
}

export const ORDER_STATUS_LABELS: Record<string, string> = {
  PENDING: "待支付",
  PAID: "已支付",
  EXPIRED: "已过期",
  CLOSED: "已关闭",
};

export const NOTIFY_STATUS_LABELS: Record<string, string> = {
  NONE: "无需通知",
  PENDING: "待回调",
  RETRYING: "回调重试中",
  SUCCESS: "回调成功",
  FAILED: "回调失败",
  HANDLED: "回调已处理",
};

export const CALLBACK_STATUS_LABELS: Record<string, string> = {
  PENDING: "待执行",
  PROCESSING: "执行中",
  SUCCESS: "成功",
  FAILED: "失败",
};

export const COMPONENT_NAMES: Record<string, string> = {
  wechat_hook: "微信 Hook",
  alipay_ledger: "支付宝账务",
};

export const COMPONENT_STATUS_LABELS: Record<string, string> = {
  STARTING: "启动中",
  HEALTHY: "正常",
  DEGRADED: "降级",
  ERROR: "异常",
  STOPPED: "已停止",
  UNSUPPORTED: "不支持",
  stale: "上报超时",
};

export const WECHAT_LOGIN_LABELS: Record<string, string> = {
  LOGIN_REQUIRED: "需要登录",
  LOGIN_CLICKED: "已点击登录",
  QR_READY: "等待扫码",
  LOGGED_IN: "已登录",
  RESTARTING: "正在重启",
};

export const PROVIDER_LABELS: Record<string, string> = {
  wxpay: "微信",
  alipay: "支付宝",
};

/** 状态色：shadcn-vue 的 Badge 变体只有几种语义色，这里统一映射，保证全站一致。 */
export function statusTone(value: string): "default" | "secondary" | "destructive" | "outline" {
  const positive = new Set(["PAID", "SUCCESS", "HEALTHY", "LOGGED_IN", "online"]);
  const warning = new Set(["PENDING", "RETRYING", "DEGRADED", "STARTING", "QR_READY", "LOGIN_CLICKED", "RESTARTING"]);
  const negative = new Set(["FAILED", "ERROR", "EXPIRED", "CLOSED", "STOPPED", "UNSUPPORTED", "stale", "LOGIN_REQUIRED"]);
  if (positive.has(value)) return "default";
  if (negative.has(value)) return "destructive";
  if (warning.has(value)) return "secondary";
  return "outline";
}
