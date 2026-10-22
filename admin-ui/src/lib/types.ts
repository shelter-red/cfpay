export type Provider = "wxpay" | "alipay";

export type SessionInfo = {
  ok: boolean;
  username?: string;
};

export type GatewaySettings = {
  merchantId: string;
  gatewayUrl: string;
  merchantKeyConfigured: boolean;
  monitorSecretConfigured: boolean;
  orderTtlMinutes: number;
  amountDirection: "UP" | "DOWN";
  maxAmountAdjustments: number;
};

export type AdminSettings = {
  username: string;
  adminPath: string;
  passwordConfigured: boolean;
  source: "database" | "environment";
  updatedAt: number | null;
};

export type SiteSettings = {
  siteName: string;
  siteSubtitle: string;
  footerText: string;
  faviconDataUrl: string;
};

export type QrCodeItem = {
  provider: Provider;
  payloadSha256: string;
  updatedAt: number;
  url: string;
};

export type AlipayPollerStatus = {
  configured?: boolean;
  alarm?: number | null;
  lastRun?: { at?: number; error?: string | null; [key: string]: unknown } | null;
};

export type AlipayStatus = {
  configured: boolean;
  source: "database" | "environment" | null;
  appId: string | null;
  appCertSn: string | null;
  alipayRootCertSn: string | null;
  alipayCertSn: string | null;
  updatedAt: number | null;
  poller?: AlipayPollerStatus;
};

export type OrderRow = {
  id: string;
  merchant_order_no: string;
  provider: Provider;
  subject: string;
  amount_fen: number;
  payable_amount_fen: number;
  status: string;
  notify_status: string;
  paid_source: "EVENT" | "MANUAL" | null;
  manual_paid_by: string | null;
  paid_at: number | null;
  expires_at: number;
  created_at: number;
  device: string | null;
  callback_handled_at: number | null;
  callback_handled_reason: string | null;
  callback_handled_by: string | null;
};

export type QuickPayDiagnosticStage = {
  key: string;
  title: string;
  status: "complete" | "waiting" | "failed";
  summary: string;
  data: Record<string, unknown>;
};

export type QuickPayRun = {
  order: OrderRow;
  paymentEvent: {
    id: string;
    device_id: string;
    provider: Provider;
    amount_fen: number;
    paid_at: number;
    status: string;
    matched_order_id: string | null;
    raw_excerpt: string | null;
    created_at: number;
  } | null;
  callback: {
    status: string;
    attempt_count: number;
    max_attempts: number;
    next_attempt_at: number | null;
    last_error: string | null;
    handled_at: number | null;
    handled_reason: string | null;
    handled_by: string | null;
    updated_at: number;
  } | null;
  callbackAttempts: Array<Record<string, unknown>>;
  paymentDetail: Record<string, unknown> | null;
  paymentRaw: string | null;
  stages: QuickPayDiagnosticStage[];
  completed: boolean;
  payUrl: string;
};

export type OrderDetail = {
  order: OrderRow & Record<string, unknown>;
  paymentEvent: Record<string, unknown> | null;
  paymentDetail: Record<string, unknown> | null;
  paymentRaw: string | null;
  paymentActions: PaymentEventAction[];
  callback: {
    id: number;
    status: string;
    attempt_count: number;
    max_attempts: number;
    next_attempt_at: number | null;
    last_error: string | null;
    handled_at: number | null;
    handled_reason: string | null;
    handled_by: string | null;
    updated_at: number;
  } | null;
  callbackAttempts: Array<{
    attempt_no: number;
    response_status: number | null;
    response_excerpt: string | null;
    error: string | null;
    created_at: number;
  }>;
};

export type MonitorDevice = {
  id: string;
  name: string | null;
  version: string | null;
  platform: string | null;
  last_ip: string | null;
  last_heartbeat_at: number;
};

export type MonitorComponent = {
  device_id: string;
  component: string;
  status: string;
  component_version: string | null;
  target_version: string | null;
  process_id: number | null;
  hook_offset: string | null;
  detail_code: string | null;
  detail_message: string | null;
  last_event_at: number | null;
  reported_at: number;
  updated_at: number;
};

export type WechatLoginSession = {
  deviceId: string;
  status: string;
  observedAt: number;
  expiresAt: number;
  updatedAt: number;
  qrAvailable: boolean;
  qrUrl: string | null;
};

export type UnmatchedEvent = {
  id: string;
  device_id: string;
  provider: Provider;
  amount_fen: number;
  paid_at: number;
  created_at: number;
  raw_length: number | null;
  raw_head: string | null;
};

export type PaymentEventListItem = UnmatchedEvent & {
  status: "UNMATCHED" | "MATCHED" | "IGNORED";
  matched_order_id: string | null;
  matched_merchant_order_no: string | null;
  updated_at: number;
  latest_action: "AUTO_MATCH" | "MANUAL_MATCH" | "IGNORE" | "RESTORE" | null;
  latest_reason: string | null;
  latest_actor: string | null;
  latest_action_at: number | null;
};

export type PaymentEventAction = {
  id: number;
  event_id: string;
  action: "AUTO_MATCH" | "MANUAL_MATCH" | "IGNORE" | "RESTORE";
  order_id: string | null;
  reason: string | null;
  actor: string;
  created_at: number;
};

export type PaymentEventCandidate = OrderRow;

export type WechatChannelStatus = {
  available: boolean;
  reason: "OK" | "NO_MONITOR" | "STALE" | "UNHEALTHY";
  componentStatus: string | null;
  detailCode: string | null;
  deviceId: string | null;
  reportedAt: number | null;
  loginStatus: string | null;
};

export type MonitorsResponse = {
  devices: MonitorDevice[];
  components: MonitorComponent[];
  wechatLogin: WechatLoginSession[];
  wechatChannel: WechatChannelStatus;
  /** 唯一的存活判定窗口：连续 3 次（15 秒）未收到上报即失联。 */
  onlineWindowMs: number;
  serverTime?: number;
};

export type MonitorLiveUpdate = {
  device: MonitorDevice;
  component?: MonitorComponent;
  wechatLogin?: WechatLoginSession;
  serverTime?: number;
};

export type PaymentEventDetail = {
  event: Record<string, unknown>;
  detail: Record<string, unknown> | null;
  raw: string | null;
  order: Record<string, unknown> | null;
  actions: PaymentEventAction[];
};

export type TurnstileStatus = {
  enabled: boolean;
  siteKeyConfigured: boolean;
  secretKeyConfigured: boolean;
};

export type D1UsageLevel = "normal" | "warning" | "danger" | "exhausted" | "unlimited";

export type D1UsageMetric = {
  used: number;
  limit: number | null;
  ratio: number | null;
  level: D1UsageLevel;
};

export type D1UsageData = {
  configured: true;
  plan: "free" | "paid";
  timezone: "UTC";
  resetAt: string;
  fetchedAt: number;
  cacheExpiresAt: number;
  today: {
    date: string;
    rowsRead: D1UsageMetric;
    rowsWritten: D1UsageMetric;
    queries: number;
  };
  storage: D1UsageMetric;
  trend: Array<{ date: string; rowsRead: number; rowsWritten: number; queries: number }>;
};

export type RuntimeCacheStats = {
  entries: number;
  estimatedBytes: number;
  budgetBytes: number;
  hits: number;
  misses: number;
  d1Reads: number;
  evictions: number;
  startedAt: number;
};
