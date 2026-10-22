export type Provider = "wxpay" | "alipay";
type OrderStatus = "PENDING" | "PAID" | "EXPIRED" | "CLOSED";

export interface Env {
  DB: D1Database;
  ASSETS: Fetcher;
  ALIPAY_POLLER: DurableObjectNamespace;
  MONITOR_STATE: DurableObjectNamespace;
  RUNTIME_DATA: DurableObjectNamespace;
  ADMIN_PATH: string;
  ADMIN_USERNAME: string;
  ADMIN_PASSWORD: string;
  SESSION_SECRET: string;
  ALIPAY_APP_ID?: string;
  ALIPAY_APP_PRIVATE_KEY?: string;
  ALIPAY_APP_CERT_SN?: string;
  ALIPAY_ROOT_CERT_SN?: string;
  ALIPAY_CERT_SN?: string;
  ALIPAY_PUBLIC_KEY?: string;
  ALIPAY_ACTIVE_POLL_SECONDS?: string;
  /** Cloudflare Turnstile 站点 Key（公开值，下发给登录页渲染验证组件）。 */
  TURNSTILE_SITE_KEY?: string;
  /** Cloudflare Turnstile Secret Key（必须用 wrangler secret put 配置，不能写进 vars）。 */
  TURNSTILE_SECRET_KEY?: string;
  /** Cloudflare 账户 ID，用于管理端查询 D1 Analytics。 */
  CLOUDFLARE_ACCOUNT_ID?: string;
  /** 当前 D1 数据库 ID，用于限定 Analytics 查询范围。 */
  CLOUDFLARE_D1_DATABASE_ID?: string;
  /** D1 套餐口径；免费计划展示每日硬额度，付费计划展示实际用量。 */
  CLOUDFLARE_D1_PLAN?: "free" | "paid";
  /** 只读 Analytics API Token，必须通过 Worker Secret 配置。 */
  CLOUDFLARE_ANALYTICS_TOKEN?: string;
}

export interface SystemSetting {
  id: number;
  merchant_id: string;
  merchant_key: string | null;
  monitor_secret: string | null;
  order_ttl_minutes: number;
  amount_direction: "UP" | "DOWN";
  max_amount_adjustments: number;
  created_at: number;
  updated_at: number;
  site_name?: string;
  site_subtitle?: string;
  footer_text?: string;
  favicon_data_url?: string;
}

export interface PaymentOrder {
  id: string;
  merchant_order_no: string;
  merchant_id: string;
  provider: Provider;
  subject: string;
  amount_fen: number;
  payable_amount_fen: number;
  status: OrderStatus;
  notify_status: "NONE" | "PENDING" | "RETRYING" | "SUCCESS" | "FAILED";
  notify_url: string | null;
  return_url: string | null;
  attach: string;
  client_ip: string | null;
  device: string | null;
  payment_event_id: string | null;
  paid_source: "EVENT" | "MANUAL" | null;
  manual_paid_by: string | null;
  paid_at: number | null;
  expires_at: number;
  created_at: number;
  updated_at: number;
}

export interface CallbackJob {
  id: number;
  order_id: string;
  status: "PENDING" | "PROCESSING" | "SUCCESS" | "FAILED";
  attempt_count: number;
  max_attempts: number;
  next_attempt_at: number;
  lease_until: number | null;
  last_error: string | null;
  handled_at: number | null;
  handled_reason: string | null;
  handled_by: string | null;
  created_at: number;
  updated_at: number;
}
