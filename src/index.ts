import { Hono, type Context } from "hono";
import {
  deleteAlipayCredentials,
  getAlipayCredentialStatus,
  parseAlipayCredentialUpload,
  saveAlipayCredentials,
} from "./alipay-config";
import { activateAlipayPolling, getAlipayPollerStatus, isAlipayApiConfigured, queryAlipayLedger } from "./alipay";
import {
  changeAdminPassword,
  clearAdminSession,
  createAdminSession,
  decodeAdminPathname,
  BLOCKED_ADMIN_PATHNAMES,
  getAdminConfig,
  matchesAdminEntry,
  updateAdminConfig,
  verifyAdminCredentials,
  verifyAdminSession,
} from "./auth";
import { handleFailedCallbackForOrder, processCallbackForOrder, processDueCallbacks, retryFailedCallbackForOrder } from "./callbacks";
import { getD1Usage } from "./cloudflare-analytics";
import { getDashboardData } from "./dashboard";
import { getSettings } from "./db";
import {
  deleteIgnoredPaymentEvents,
  deleteOrders,
  exportIgnoredPaymentEvents,
  exportOrders,
  normalizeExportFormat,
  normalizeExportIds,
} from "./data-lifecycle";
import { describePublicError } from "./errors";
import { constantTimeEqual } from "./lib/crypto";
import { getClientIp } from "./lib/http";
import { formatFen } from "./lib/money";
import {
  normalizeComponentStatusPayload,
  recordPaymentEvent,
  verifyMonitorSignature,
} from "./monitor";
import {
  claimMonitorNonce,
  getMonitorLiveResponse,
  getMonitorLoginQrPayload,
  getMonitorSnapshot,
  getMonitorWechatChannelStatus,
  type MonitorStatusUpdate,
  recordMonitorStatus,
} from "./monitor-state";
import {
  appendParamsToUrl,
  buildEpayCallbackParams,
  cacheOrder,
  createOrder,
  getOrder,
  getOrderByMerchantNo,
  invalidateOrderViews,
  listOrders,
  normalizeProvider,
  verifyEpayRequest,
} from "./orders";
import {
  getPaymentEventDetail,
  ignorePaymentEvent,
  listPaymentEventCandidates,
  listPaymentEvents,
  manuallyMatchPaymentEvent,
  normalizePage,
  restorePaymentEvent,
} from "./payment-events";
import {
  createQuickPayOrder,
  getQuickPayDiagnostics,
  receiveQuickPayCallback,
} from "./quick-pay";
import { isAlipayInAppBrowser, renderHomePage, renderPaymentPage, siteFaviconHref } from "./pages";
import { getTurnstileConfig, verifyTurnstileToken } from "./turnstile";
import { publicHealthPayload } from "./health";
import {
  deleteQrCode,
  getQrCode,
  listQrCodes,
  normalizeQrProvider,
  renderQrCodeSvg,
  saveQrCode,
  qrCodeUrl,
} from "./qr-codes";
import type { CallbackJob, Env, PaymentOrder } from "./types";
import { APP_VERSION } from "./version";
import {
  clearRuntimeCache,
  getRuntimeCacheStats,
  runtimeAll,
  runtimeFirst,
  runtimeInvalidate,
} from "./runtime-data";

// 管理后台是 Vue SPA，构建产物在 public/admin-app；只经管理员配置的入口下发首页。
// 这里必须请求目录形式：资源层默认 html_handling 会把 /admin-app/index.html 以 307 跳到 /admin-app/。
const ADMIN_SPA_ENTRY = "/admin-app/";
import { normalizeWechatLoginPayload } from "./wechat-login";
import { requestParams } from "./epay-request";

export { AlipayLedgerPoller } from "./alipay";
export { MonitorState } from "./monitor-state";
export { RuntimeDataState } from "./runtime-data-state";

type AppEnv = { Bindings: Env };
const app = new Hono<AppEnv>();

function monitorField(value: unknown, maxLength: number): string | null {
  if (typeof value !== "string") return null;
  const normalized = value.trim();
  return normalized ? normalized.slice(0, maxLength) : null;
}

// 把一次已验签的状态请求转换为 MonitorState 的内存快照；receivedAt 由 Worker 生成，
// 在线判定不信任 Windows 本机时钟。组件和登录载荷仍复用原有严格校验函数。
function monitorStatusUpdate(
  request: Request,
  deviceId: string,
  nonce: string,
  payload: Record<string, unknown>,
  receivedAt: number,
): MonitorStatusUpdate {
  if (payload.login !== undefined && payload.login !== null && !isRecord(payload.login)) {
    throw new Error("INVALID_WECHAT_LOGIN_PAYLOAD");
  }
  return {
    deviceId,
    nonce,
    name: monitorField(payload.name, 100),
    version: monitorField(payload.version, 50),
    platform: monitorField(payload.platform, 50) ?? "pc",
    ip: getClientIp(request.headers),
    receivedAt,
    component:
      typeof payload.component === "string" && typeof payload.status === "string"
        ? normalizeComponentStatusPayload(payload, receivedAt)
        : null,
    login: isRecord(payload.login) ? normalizeWechatLoginPayload(payload.login, receivedAt) : null,
  };
}

app.onError((error, c) => {
  const descriptor = describePublicError(error);
  const errorId = descriptor.unknown ? `ERR-${Date.now().toString(36)}-${crypto.randomUUID().slice(0, 8)}` : undefined;
  console.error("请求处理异常", {
    errorId,
    error: descriptor.code,
    method: c.req.method,
    path: c.req.path,
    cause: error,
  });
  const message = errorId ? `${descriptor.message}（${errorId}）` : descriptor.message;
  // submit.php 是浏览器页面支付入口，错误必须保持纯文本；其它接口统一返回带稳定错误码的 JSON。
  if (c.req.path === "/submit.php") return c.text(message, descriptor.status);
  return c.json({ code: -1, msg: message, message, error: descriptor.code, ...(errorId ? { errorId } : {}) }, descriptor.status);
});

// 统一安全响应头。Workers 里来自 fetch() / Cache API 的响应头不可变，
// Hono 的 secureHeaders 会直接抛异常并把请求变成 500（静态资源、缓存命中的图片都会中招）；
// 这里逐条兜底，写不进去就跳过，宁可少几个头也不要让请求失败。
const SECURITY_HEADERS: ReadonlyArray<readonly [string, string]> = [
  ["strict-transport-security", "max-age=300"],
  ["x-content-type-options", "nosniff"],
  ["referrer-policy", "strict-origin-when-cross-origin"],
  ["permissions-policy", "geolocation=(), microphone=(), camera=()"],
];

function applySecurityHeaders(response: Response) {
  for (const [name, value] of SECURITY_HEADERS) {
    try {
      response.headers.set(name, value);
    } catch {
      // 响应头不可变：调用方透传了外部响应，跳过安全头即可，不要让请求失败。
      return;
    }
  }
}

app.use("*", async (c, next) => {
  await next();
  applySecurityHeaders(c.res);
});

app.get("/api/health", async (c) => {
  return c.json(publicHealthPayload());
});

// 公开端点：登录页据此决定是否渲染人机验证组件。siteKey 本身是公开值，secretKey 永不下发。
app.get("/api/security/turnstile", (c) => {
  const config = getTurnstileConfig(c.env);
  return c.json({ enabled: config.enabled, siteKey: config.enabled ? config.siteKey : null });
});

// 快捷支付内置回调只保留一个正式入口，避免同一功能存在多套路由。
app.get("/api/quick-pay/callback/:token", (c) => receiveQuickPayCallback(c.env, c.req.raw, c.req.param("token")));

app.post("/admin-api/login", async (c) => {
  // 先过人机验证再比对口令：Turnstile 的作用就是挡在暴力破解前面，顺序反了就失去意义。
  // 令牌走 x-captcha-response 头，与 cffk 的登录链路保持一致。
  const verification = await verifyTurnstileToken(c.env, c.req.header("x-captcha-response") ?? null, getClientIp(c.req.raw.headers));
  if (!verification.success) {
    console.warn("admin login turnstile rejected", verification.errorCodes);
    const outage = verification.errorCodes.some((code) => code.startsWith("verify-") || code.startsWith("http-") || code === "invalid-verify-response");
    throw new Error(
      verification.errorCodes.includes("missing-input-response")
        ? "TURNSTILE_REQUIRED"
        : outage
          ? "TURNSTILE_VERIFY_UNAVAILABLE"
          : "TURNSTILE_REJECTED",
    );
  }

  const payload = await c.req.json<{ username?: string; password?: string }>();
  const adminConfig = await verifyAdminCredentials(c.env, payload.username ?? "", payload.password ?? "");
  if (!adminConfig) {
    return c.json({ ok: false, message: "账号或密码错误" }, 401);
  }
  const sessionCookie = await createAdminSession(c.env, adminConfig);
  return c.json({ ok: true, username: adminConfig.username }, 200, { "set-cookie": sessionCookie });
});

app.use("/admin-api/*", async (c, next) => {
  if (!(await verifyAdminSession(c.req.raw, c.env))) {
    return c.json({ ok: false, message: "未登录或会话已过期" }, 401);
  }
  await next();
});

app.post("/admin-api/logout", (c) => c.json({ ok: true }, 200, { "set-cookie": clearAdminSession() }));
app.get("/admin-api/session", async (c) => {
  const config = await getAdminConfig(c.env);
  return c.json({ ok: true, username: config.username });
});

app.get("/admin-api/dashboard", async (c) => {
  if (c.req.query("refresh") === "1") await runtimeInvalidate(c.env, { prefixes: ["view:dashboard:"] });
  return c.json(await getDashboardData(c.env));
});

// 查询 Cloudflare D1 官方用量；结果只在 Worker 内存缓存五分钟，不产生任何 D1 写入。
app.get("/admin-api/cloudflare/d1-usage", async (c) => c.json(await getD1Usage(c.env, new Date(), c.req.query("refresh") === "1")));

app.get("/admin-api/runtime-cache", async (c) => c.json(await getRuntimeCacheStats(c.env)));

app.delete("/admin-api/runtime-cache", async (c) => c.json({ ok: true, ...(await clearRuntimeCache(c.env)) }));

// 创建普通快捷支付订单；商品字段只用于本次下单，不额外持久化商品配置。
app.post("/admin-api/quick-pay/orders", async (c) => {
  const result = await createQuickPayOrder(c.env, c.req.raw, await c.req.json<Record<string, unknown>>());
  return c.json({ ok: true, ...result });
});

// 快捷支付诊断只汇总普通订单的上下游状态，不改变订单的匹配、回调或统计逻辑。
app.get("/admin-api/quick-pay/orders/:id/diagnostics", async (c) =>
  c.json(await getQuickPayDiagnostics(c.env, c.req.raw, c.req.param("id"))),
);

// 管理页只需要知道是否配置完整，不回显任何 Key 原文。
app.get("/admin-api/security/turnstile", (c) => {
  const config = getTurnstileConfig(c.env);
  return c.json({
    enabled: config.enabled,
    siteKeyConfigured: Boolean(config.siteKey),
    secretKeyConfigured: Boolean(config.secretKey),
  });
});

app.get("/admin-api/admin-settings", async (c) => {
  const config = await getAdminConfig(c.env);
  return c.json({
    username: config.username,
    adminPath: config.adminPath,
    passwordConfigured: Boolean(config.passwordHash || c.env.ADMIN_PASSWORD),
    source: config.source,
    updatedAt: config.updatedAt,
  });
});

app.put("/admin-api/admin-settings", async (c) => {
  const payload = await c.req.json<Record<string, unknown>>();
  const config = await updateAdminConfig(c.env, {
    username: payload.username,
    adminPath: payload.adminPath,
    password: payload.password,
  });
  return c.json(
    { ok: true, username: config.username, adminPath: config.adminPath, updatedAt: config.updatedAt },
    200,
    { "set-cookie": clearAdminSession() },
  );
});

app.put("/admin-api/account/password", async (c) => {
  const payload = await c.req.json<Record<string, unknown>>();
  await changeAdminPassword(c.env, payload.currentPassword, payload.newPassword);
  return c.json({ ok: true }, 200, { "set-cookie": clearAdminSession() });
});

app.get("/admin-api/settings", async (c) => {
  const setting = await getSettings(c.env);
  return c.json({
    merchantId: setting.merchant_id,
    gatewayUrl: new URL(c.req.url).origin,
    merchantKeyConfigured: Boolean(setting.merchant_key),
    monitorSecretConfigured: Boolean(setting.monitor_secret),
    orderTtlMinutes: setting.order_ttl_minutes,
    amountDirection: setting.amount_direction,
    maxAmountAdjustments: setting.max_amount_adjustments,
  });
});

app.get("/admin-api/site-settings", async (c) => {
  const setting = await getSettings(c.env);
  return c.json({
    siteName: setting.site_name ?? "CFPay",
    siteSubtitle: setting.site_subtitle ?? "个人收款控制台",
    footerText: setting.footer_text ?? "",
    faviconDataUrl: setting.favicon_data_url ?? "",
  });
});

app.put("/admin-api/site-settings", async (c) => {
  const payload = await c.req.json<Record<string, unknown>>();
  const current = await getSettings(c.env);
  const siteName = siteText(payload.siteName, "SITE_NAME_REQUIRED", 1, 120, current.site_name ?? "CFPay");
  const siteSubtitle = siteText(payload.siteSubtitle, "SITE_SUBTITLE_INVALID", 0, 300, current.site_subtitle ?? "个人收款控制台");
  const footerText = siteText(payload.footerText, "FOOTER_TEXT_INVALID", 0, 1000, current.footer_text ?? "");
  const faviconDataUrl = siteFavicon(payload.faviconDataUrl, current.favicon_data_url ?? "");
  await c.env.DB.prepare(
    "UPDATE system_setting SET site_name = ?, site_subtitle = ?, footer_text = ?, favicon_data_url = ?, updated_at = ? WHERE id = 1",
  ).bind(siteName, siteSubtitle, footerText, faviconDataUrl, Date.now()).run();
  await runtimeInvalidate(c.env, { keys: ["config:system"], prefixes: ["view:dashboard:"] });
  return c.json({ ok: true });
});

app.put("/admin-api/settings", async (c) => {
  const payload = await c.req.json<Record<string, unknown>>();
  const has = (key: string) => Object.prototype.hasOwnProperty.call(payload, key);
  const current = await getSettings(c.env);
  const now = Date.now();

  // 只更新本次提交的字段，便于后台按功能拆分表单而不会互相清空。
  const merchantId = has("merchantId") ? stringValue(payload.merchantId).trim() : current.merchant_id;
  if (!/^[a-zA-Z0-9_-]{1,32}$/.test(merchantId)) throw new Error("INVALID_MERCHANT_ID");

  const orderTtlMinutes = has("orderTtlMinutes")
    ? integerValue(payload.orderTtlMinutes, 1, 60)
    : current.order_ttl_minutes;
  const maxAmountAdjustments = has("maxAmountAdjustments")
    ? integerValue(payload.maxAmountAdjustments, 0, 999)
    : current.max_amount_adjustments;
  const amountDirection = has("amountDirection")
    ? payload.amountDirection === "DOWN"
      ? "DOWN"
      : "UP"
    : current.amount_direction;
  const merchantKey = has("merchantKey") ? stringValue(payload.merchantKey).trim() : "";
  const monitorSecret = has("monitorSecret") ? stringValue(payload.monitorSecret).trim() : "";
  if (merchantKey && merchantKey.length < 16) throw new Error("MERCHANT_KEY_TOO_SHORT");
  if (monitorSecret && monitorSecret.length < 32) throw new Error("MONITOR_SECRET_TOO_SHORT");

  await c.env.DB.prepare(
    `UPDATE system_setting SET
       merchant_id = ?,
       merchant_key = CASE WHEN ? = '' THEN merchant_key ELSE ? END,
       monitor_secret = CASE WHEN ? = '' THEN monitor_secret ELSE ? END,
       order_ttl_minutes = ?,
       amount_direction = ?,
       max_amount_adjustments = ?,
       updated_at = ?
     WHERE id = 1`,
  )
    .bind(
      merchantId,
      merchantKey,
      merchantKey,
      monitorSecret,
      monitorSecret,
      orderTtlMinutes,
      amountDirection,
      maxAmountAdjustments,
      now,
    )
    .run();
  await runtimeInvalidate(c.env, { keys: ["config:system"] });
  return c.json({ ok: true });
});

app.get("/admin-api/qr-codes", async (c) => c.json({ items: await listQrCodes(c.env) }));

app.put("/admin-api/qr-codes/:provider", async (c) => {
  const provider = normalizeQrProvider(c.req.param("provider"));
  const payload = await c.req.json<{ payload?: unknown }>();
  return c.json({ ok: true, qrCode: await saveQrCode(c.env, provider, payload.payload) });
});

app.delete("/admin-api/qr-codes/:provider", async (c) => {
  const provider = normalizeQrProvider(c.req.param("provider"));
  await deleteQrCode(c.env, provider);
  return c.json({ ok: true });
});

app.get("/admin-api/orders", async (c) => {
  const provider = c.req.query("provider") ?? "";
  const status = c.req.query("status") ?? "";
  const notifyStatus = c.req.query("notifyStatus") ?? "";
  const legacyLimit = c.req.query("limit");
  if (provider && !["wxpay", "alipay"].includes(provider)) throw new Error("INVALID_ORDER_FILTER");
  if (status && !["PENDING", "PAID", "EXPIRED", "CLOSED"].includes(status)) throw new Error("INVALID_ORDER_FILTER");
  if (notifyStatus && !["NONE", "PENDING", "RETRYING", "SUCCESS", "FAILED", "HANDLED", "ACTIVE"].includes(notifyStatus)) throw new Error("INVALID_ORDER_FILTER");
  if (c.req.query("refresh") === "1") await runtimeInvalidate(c.env, { prefixes: ["view:orders:"] });
  return c.json(await listOrders(c.env, {
    page: normalizePage(c.req.query("page"), 1, 100_000),
    pageSize: normalizePage(c.req.query("pageSize") ?? legacyLimit, legacyLimit ? 100 : 20, 200),
    query: (c.req.query("q") ?? "").trim().slice(0, 100),
    provider: provider as "" | "wxpay" | "alipay",
    status: status as "" | "PENDING" | "PAID" | "EXPIRED" | "CLOSED",
    notifyStatus: notifyStatus as "" | "NONE" | "PENDING" | "RETRYING" | "SUCCESS" | "FAILED" | "HANDLED" | "ACTIVE",
  }));
});

// 导出选中订单的完整业务与审计数据，响应为附件且不产生 D1 写入。
app.post("/admin-api/orders/export", async (c) => {
  const payload = await c.req.json<{ ids?: unknown; format?: unknown }>();
  const file = await exportOrders(c.env, normalizeExportIds(payload.ids), normalizeExportFormat(payload.format));
  return new Response(file.body, { headers: { "content-type": file.contentType, "content-disposition": `attachment; filename="${file.filename}"` } });
});

// 批量永久删除选中订单及全部关联数据；不支持按筛选条件直接删除。
app.post("/admin-api/orders/bulk-delete", async (c) => {
  const payload = await c.req.json<{ ids?: unknown }>();
  return c.json({ ok: true, deleted: await deleteOrders(c.env, normalizeExportIds(payload.ids)) });
});

app.get("/admin-api/monitors", async (c) => c.json(await getMonitorSnapshot(c.env)));

app.get("/admin-api/monitors/live", (c) => getMonitorLiveResponse(c.env, c.req.raw));

app.get("/admin-api/payment-events/:id", async (c) => {
  const eventId = c.req.param("id");
  if (!/^wechat:[a-f0-9]{64}$|^alipay:[a-f0-9]{64}$|^[a-zA-Z0-9:._-]{8,160}$/.test(eventId)) {
    return c.json({ ok: false, message: "收款事件不存在" }, 404);
  }
  const detail = await getPaymentEventDetail(c.env, eventId);
  return detail ? c.json(detail) : c.json({ ok: false, message: "收款事件不存在" }, 404);
});

// 导出选中的已忽略收款及处理记录，待处理和已匹配收款不能通过此接口导出删除包。
app.post("/admin-api/payment-events/export", async (c) => {
  const payload = await c.req.json<{ ids?: unknown; format?: unknown }>();
  const file = await exportIgnoredPaymentEvents(c.env, normalizeExportIds(payload.ids), normalizeExportFormat(payload.format));
  return new Response(file.body, { headers: { "content-type": file.contentType, "content-disposition": `attachment; filename="${file.filename}"` } });
});

// 批量永久删除选中的已忽略收款。
app.post("/admin-api/payment-events/bulk-delete", async (c) => {
  const payload = await c.req.json<{ ids?: unknown }>();
  return c.json({ ok: true, deleted: await deleteIgnoredPaymentEvents(c.env, normalizeExportIds(payload.ids)) });
});

// 分页读取待处理或已处理的收款事件；超过 24 小时筛选只对待处理页签生效。
app.get("/admin-api/payment-events", async (c) => {
  const scope = c.req.query("scope") === "resolved" ? "resolved" : "pending";
  if (c.req.query("refresh") === "1") await runtimeInvalidate(c.env, { prefixes: ["view:payment-events:"] });
  return c.json(await listPaymentEvents(c.env, {
    scope,
    page: normalizePage(c.req.query("page"), 1, 100_000),
    pageSize: normalizePage(c.req.query("pageSize"), 20, 100),
    overdueOnly: c.req.query("overdue") === "1",
  }));
});

// 只列出与收款事件同渠道、同实付金额且尚未关联的普通订单。
app.get("/admin-api/payment-events/:id/candidates", async (c) => c.json(await listPaymentEventCandidates(c.env, {
  eventId: c.req.param("id"),
  query: (c.req.query("q") ?? "").trim().slice(0, 100),
  page: normalizePage(c.req.query("page"), 1, 100_000),
  pageSize: normalizePage(c.req.query("pageSize"), 20, 100),
})));

// 人工匹配收款事件后立即触发原订单的商户回调。
app.post("/admin-api/payment-events/:id/match", async (c) => {
  const payload = await c.req.json<{ orderId?: unknown }>();
  if (typeof payload.orderId !== "string" || !payload.orderId.trim()) throw new Error("ORDER_NOT_FOUND");
  const actor = (await getAdminConfig(c.env)).username;
  const orderId = await manuallyMatchPaymentEvent(c.env, c.req.param("id"), payload.orderId.trim(), actor);
  c.executionCtx.waitUntil(processCallbackForOrder(c.env, orderId));
  return c.json({ ok: true, orderId });
});

// 忽略原因必须明确填写，真实收款记录仍永久保留。
app.post("/admin-api/payment-events/:id/ignore", async (c) => {
  const payload = await c.req.json<{ reason?: unknown }>();
  const actor = (await getAdminConfig(c.env)).username;
  await ignorePaymentEvent(c.env, c.req.param("id"), typeof payload.reason === "string" ? payload.reason : "", actor);
  return c.json({ ok: true });
});

// 仅已忽略事件允许恢复，已匹配事件不能回滚到账和已发出的商户回调。
app.post("/admin-api/payment-events/:id/restore", async (c) => {
  const actor = (await getAdminConfig(c.env)).username;
  await restorePaymentEvent(c.env, c.req.param("id"), actor);
  return c.json({ ok: true });
});

app.get("/admin-api/wechat-login/:deviceId/qr", async (c) => {
  const payload = await getMonitorLoginQrPayload(c.env, c.req.param("deviceId"));
  if (!payload) return c.text("登录二维码不存在或已过期", 404);
  return new Response(await renderQrCodeSvg(payload), {
    status: 200,
    headers: {
      "cache-control": "no-store, no-cache, must-revalidate",
      "content-security-policy": "default-src 'none'",
      "content-type": "image/svg+xml; charset=utf-8",
      expires: "0",
      pragma: "no-cache",
      "x-content-type-options": "nosniff",
    },
  });
});

app.get("/admin-api/alipay/status", async (c) => {
  const credentials = await getAlipayCredentialStatus(c.env);
  return c.json({ ...credentials, poller: await getAlipayPollerStatus(c.env) });
});

app.post("/admin-api/alipay/config", async (c) => {
  const contentType = c.req.header("content-type") ?? "";
  if (!contentType.includes("multipart/form-data")) throw new Error("ALIPAY_MULTIPART_REQUIRED");
  const config = await parseAlipayCredentialUpload(await c.req.raw.formData());
  await saveAlipayCredentials(c.env, config);
  c.executionCtx.waitUntil(activateAlipayPolling(c.env));
  return c.json({ ok: true, appId: config.appId, appCertSn: config.appCertSn, updatedAt: Date.now() });
});

app.delete("/admin-api/alipay/config", async (c) => {
  await deleteAlipayCredentials(c.env);
  return c.json({ ok: true, fallbackConfigured: await isAlipayApiConfigured(c.env) });
});

app.post("/admin-api/alipay/query", async (c) => {
  const result = await queryAlipayLedger(c.env, 10);
  console.info("支付宝账务查询完成", {
    traceId: result.traceId,
    traceSource: result.traceSource,
    totalSize: result.totalSize,
    queriedAt: Date.now(),
  });
  return c.json({
    ok: true,
    traceId: result.traceId,
    traceSource: result.traceSource,
    totalSize: result.totalSize,
    details: result.details,
  });
});

interface PaymentEventRow {
  id: string;
  device_id: string;
  provider: string;
  amount_fen: number;
  paid_at: number;
  status: string;
  matched_order_id: string | null;
  raw_excerpt: string | null;
  created_at: number;
}

interface PaymentEventActionRow {
  id: number;
  event_id: string;
  action: "AUTO_MATCH" | "MANUAL_MATCH" | "IGNORE" | "RESTORE";
  order_id: string | null;
  reason: string | null;
  actor: string;
  created_at: number;
}

interface CallbackAttemptRow {
  attempt_no: number;
  response_status: number | null;
  response_excerpt: string | null;
  error: string | null;
  created_at: number;
}

app.get("/admin-api/orders/:id", async (c) => {
  const orderId = c.req.param("id");
  if (!/^[a-f0-9]{16,64}$/i.test(orderId)) return c.json({ ok: false, message: "订单不存在" }, 404);
  const order = await getOrder(c.env, orderId);
  if (!order) return c.json({ ok: false, message: "订单不存在" }, 404);

  const event = order.payment_event_id
    ? await runtimeFirst<PaymentEventRow>(
        c.env,
        `event:id:${order.payment_event_id}`,
        `SELECT id, device_id, provider, amount_fen, paid_at, status, matched_order_id, raw_excerpt, created_at
         FROM payment_event WHERE id = ?`,
        [order.payment_event_id],
        { ttlMs: null },
      )
    : null;
  const callback = await runtimeFirst<CallbackJob>(
    c.env,
    `callback:order:${orderId}`,
    `SELECT id, status, attempt_count, max_attempts, next_attempt_at, last_error,
            handled_at, handled_reason, handled_by, updated_at
     FROM callback_job WHERE order_id = ?`,
    [orderId],
    { ttlMs: null },
  );
  const attempts = await runtimeAll<CallbackAttemptRow>(
    c.env,
    `attempts:order:${orderId}:desc:10`,
    `SELECT a.attempt_no, a.response_status, a.response_excerpt, a.error, a.created_at
     FROM callback_attempt a JOIN callback_job j ON j.id = a.job_id
     WHERE j.order_id = ? ORDER BY a.attempt_no DESC LIMIT 10`,
    [orderId],
    { ttlMs: null },
  );
  const paymentActions = event
    ? await runtimeAll<PaymentEventActionRow>(
        c.env,
        `event:actions:${event.id}`,
        `SELECT id, event_id, action, order_id, reason, actor, created_at
         FROM payment_event_action WHERE event_id = ? ORDER BY id DESC`,
        [event.id],
        { ttlMs: null },
      )
    : [];

  let detail: Record<string, unknown> | null = null;
  const rawExcerpt = event?.raw_excerpt ?? null;
  if (rawExcerpt) {
    try {
      const parsed: unknown = JSON.parse(rawExcerpt);
      if (parsed && typeof parsed === "object") detail = parsed as Record<string, unknown>;
    } catch {
      detail = null;
    }
  }
  return c.json({
    order,
    paymentEvent: event ? { ...event, raw_excerpt: undefined } : null,
    paymentDetail: detail,
    paymentRaw: rawExcerpt,
    paymentActions,
    callback,
    callbackAttempts: attempts,
  });
});

// 永久删除单笔订单及其关联收款和回调数据。
app.delete("/admin-api/orders/:id", async (c) =>
  c.json({ ok: true, deleted: await deleteOrders(c.env, normalizeExportIds([c.req.param("id")])) }),
);

// 永久删除单笔已忽略收款，其他状态返回冲突错误。
app.delete("/admin-api/payment-events/:id", async (c) =>
  c.json({ ok: true, deleted: await deleteIgnoredPaymentEvents(c.env, normalizeExportIds([c.req.param("id")])) }),
);

app.post("/admin-api/orders/:id/mark-paid", async (c) => {
  const order = await getOrder(c.env, c.req.param("id"));
  if (!order) return c.json({ ok: false, message: "订单不存在" }, 404);
  if (order.status === "PAID") return c.json({ ok: true, alreadyPaid: true });
  if (order.status === "CLOSED") return c.json({ ok: false, message: "订单已关闭" }, 409);
  const now = Date.now();
  const actor = (await getAdminConfig(c.env)).username;
  const results = await c.env.DB.batch([
    c.env.DB.prepare(
      `UPDATE payment_order SET status = 'PAID',
       notify_status = CASE WHEN notify_url IS NULL OR notify_url = '' THEN 'NONE' ELSE 'PENDING' END,
       payment_event_id = NULL, paid_source = 'MANUAL', manual_paid_by = ?,
       paid_at = ?, updated_at = ? WHERE id = ? AND status IN ('PENDING', 'EXPIRED')`,
    ).bind(actor, now, now, order.id),
    c.env.DB.prepare(
      `INSERT OR IGNORE INTO callback_job (order_id, status, attempt_count, max_attempts, next_attempt_at, created_at, updated_at)
       SELECT id, 'PENDING', 0, 6, ?, ?, ? FROM payment_order
       WHERE id = ? AND status = 'PAID' AND paid_source = 'MANUAL'
         AND notify_url IS NOT NULL AND notify_url <> ''`,
    ).bind(now, now, now, order.id),
  ]);
  if ((results[0]?.meta.changes ?? 0) === 0) {
    const current = await getOrder(c.env, order.id);
    return current?.status === "PAID"
      ? c.json({ ok: true, alreadyPaid: true })
      : c.json({ ok: false, message: "订单状态已变化，请刷新后重试" }, 409);
  }
  const paidOrder: PaymentOrder = {
    ...order,
    status: "PAID",
    notify_status: order.notify_url ? "PENDING" : "NONE",
    payment_event_id: null,
    paid_source: "MANUAL",
    manual_paid_by: actor,
    paid_at: now,
    updated_at: now,
  };
  await Promise.all([
    cacheOrder(c.env, paidOrder),
    invalidateOrderViews(c.env, order.id),
  ]);
  c.executionCtx.waitUntil(processCallbackForOrder(c.env, order.id));
  return c.json({ ok: true });
});

// 失败回调每次人工操作只追加一次立即尝试，原有尝试记录和错误原因全部保留。
app.post("/admin-api/orders/:id/retry-callback", async (c) => {
  const orderId = c.req.param("id");
  await retryFailedCallbackForOrder(c.env, orderId);
  c.executionCtx.waitUntil(processCallbackForOrder(c.env, orderId));
  return c.json({ ok: true });
});

// 管理员确认不再继续处理失败回调时记录处理说明；失败响应和尝试记录仍完整保留。
app.post("/admin-api/orders/:id/handle-callback", async (c) => {
  const payload = await c.req.json<{ reason?: unknown }>();
  const actor = (await getAdminConfig(c.env)).username;
  await handleFailedCallbackForOrder(c.env, c.req.param("id"), typeof payload.reason === "string" ? payload.reason : "", actor);
  return c.json({ ok: true });
});

app.all("/submit.php", async (c) => handleEpayCreate(c, true));
app.all("/mapi.php", async (c) => handleEpayCreate(c, false));

app.all("/query.php", handleEpayQuery);
app.all("/api.php", handleEpayQuery);

// 处理易支付订单查询（api.php?act=order 与 query.php）。
// 允许两种鉴权：pid+key 直传，或标准 MD5 签名；act 只支持 order。
async function handleEpayQuery(c: Context<AppEnv>) {
  const params = await requestParams(c.req.raw);
  if (params.act && params.act !== "order") return c.json({ code: -1, msg: "不支持的查询动作" }, 400);
  const setting = await getSettings(c.env);
  // 商户密钥属于秘密值，必须用定长比较；用 === 会在首个不同字节处短路，留下时序侧信道。
  const keyAuthenticated = Boolean(
    params.key &&
      setting.merchant_key &&
      params.pid === setting.merchant_id &&
      constantTimeEqual(params.key, setting.merchant_key),
  );
  if (!keyAuthenticated && !verifyEpayRequest(params, setting)) {
    logEpayAuthFailure(c.req.raw, params, "查询接口");
    return c.json({ code: -1, msg: "签名校验失败", error: "EPAY_SIGNATURE_INVALID" }, 401);
  }
  const order = params.out_trade_no
    ? await getOrderByMerchantNo(c.env, params.out_trade_no)
    : params.trade_no
      ? await getOrder(c.env, params.trade_no)
      : null;
  if (!order) return c.json({ code: -1, msg: "订单不存在" }, 404);
  return c.json({
    code: 1,
    msg: "查询订单号成功",
    trade_no: order.id,
    out_trade_no: order.merchant_order_no,
    type: order.provider,
    pid: order.merchant_id,
    money: formatFen(order.amount_fen),
    status: order.status === "PAID" ? 1 : 0,
  });
}

// 兼容 0.9.1 之前的监控端：旧版会单独发心跳。0.9.1 起设备与组件状态合并到
// /api/monitor/status 一次上报，本路由只用于旧客户端过渡，全部升级后可删除。
app.post("/api/monitor/payment", async (c) => {
  const rawBody = await c.req.text();
  const verified = await verifyMonitorSignature(c.env, c.req.raw, rawBody);
  await claimMonitorNonce(c.env, verified.deviceId, verified.nonce);
  const result = await recordPaymentEvent(c.env, verified.deviceId, JSON.parse(rawBody) as Record<string, unknown>);
  if (result.matched && result.orderId) c.executionCtx.waitUntil(processCallbackForOrder(c.env, result.orderId));
  return c.json({ ok: true, ...result });
});

// 监控端唯一的状态上报入口：设备在线、组件（Hook）状态与微信登录状态在同一个请求里一次写完。
// 微信登录状态放在 payload.login 子对象中，字段校验与它单独成一条路由时完全一致
// （只认 status / observedAt / qrPayload，多一个键就拒绝）。
app.post("/api/monitor/status", async (c) => {
  const rawBody = await c.req.text();
  const verified = await verifyMonitorSignature(c.env, c.req.raw, rawBody);
  const payload = rawBody ? (JSON.parse(rawBody) as Record<string, unknown>) : {};
  const receivedAt = Date.now();
  const update = monitorStatusUpdate(c.req.raw, verified.deviceId, verified.nonce, payload, receivedAt);
  await recordMonitorStatus(c.env, update);
  return c.json({
    ok: true,
    component: update.component?.component ?? null,
    status: update.component?.status ?? null,
    login: update.login?.status ?? null,
    serverTime: receivedAt,
  });
});

app.get("/api/orders/:id/status", async (c) => {
  const order = await getOrder(c.env, c.req.param("id"));
  if (!order) return c.json({ message: "订单不存在" }, 404);
  let returnUrl: string | null = null;
  if (order.status === "PAID" && order.return_url) {
    const setting = await getSettings(c.env);
    if (setting.merchant_key) {
      returnUrl = appendParamsToUrl(order.return_url, buildEpayCallbackParams(order, setting.merchant_key));
    }
  }
  // 仅对仍在等待付款的微信订单探测通道可用性，收款页据此在监控端恢复/掉线时自动刷新。
  const wechatAvailable =
    order.provider === "wxpay" && order.status === "PENDING"
      ? (await getMonitorWechatChannelStatus(c.env)).available
      : null;
  return c.json({ status: order.status, notifyStatus: order.notify_status, returnUrl, wechatAvailable });
});

app.get("/payment-assets/qr/:provider", async (c) => {
  const provider = normalizeQrProvider(c.req.param("provider"));
  const qrCode = await getQrCode(c.env, provider);
  if (!qrCode) return c.text("收款码不存在", 404);
  const etag = `"${qrCode.payload_sha256}"`;
  const headers = {
    "cache-control": "public, max-age=300",
    "content-security-policy": "default-src 'none'",
    "content-type": "image/svg+xml; charset=utf-8",
    etag,
    "x-content-type-options": "nosniff",
  };
  if (c.req.header("if-none-match") === etag) return new Response(null, { status: 304, headers });
  return new Response(await renderQrCodeSvg(qrCode.payload), { status: 200, headers });
});

app.get("/", handleHomePage);
app.get("/index.html", handleHomePage);

app.get("/pay/:id", async (c) => {
  const order = await getOrder(c.env, c.req.param("id"));
  if (!order) return c.text("订单不存在", 404);
  const setting = await getSettings(c.env);
  const returnUrl =
    order.status === "PAID" && order.return_url && setting.merchant_key
      ? appendParamsToUrl(order.return_url, buildEpayCallbackParams(order, setting.merchant_key))
      : null;
  const qrCode = await getQrCode(c.env, order.provider);
  const qrImageUrl = qrCode ? qrCodeUrl(order.provider, qrCode.payload_sha256) : null;
  if (order.provider !== "wxpay" || order.status !== "PENDING") {
    const alipayInApp = order.provider === "alipay" && isAlipayInAppBrowser(c.req.header("user-agent"));
    return c.html(renderPaymentPage(order, setting, { qrImageUrl, qrPayload: qrCode?.payload ?? null, alipayInApp, returnUrl }));
  }
  const wechat = await getMonitorWechatChannelStatus(c.env);
  return c.html(renderPaymentPage(order, setting, { qrImageUrl, wechatAvailable: wechat.available, wechatReason: wechat.reason }));
});

// ASSETS.fetch() 返回的响应头在 Workers 运行时里是不可变的：直接把它交给后续写响应头的
// 中间件（安全头）会抛 "Can't modify immutable headers"。这里统一复制成可变响应；
// 只复制响应头和状态，响应体保持流式，不额外拷贝内容。
function withMutableHeaders(response: Response): Response {
  return new Response(response.body, {
    status: response.status,
    statusText: response.statusText,
    headers: new Headers(response.headers),
  });
}

app.get("*", async (c) => {
  const rawPathname = new URL(c.req.url).pathname;
  const pathname = decodeAdminPathname(rawPathname);
  if (pathname === "/admin" || pathname === "/admin.html") return c.text("Not Found", 404);
  if (BLOCKED_ADMIN_PATHNAMES.has(pathname)) return c.text("Not Found", 404);
  // 入口路径不再有字符集和长度限制，改为与配置值逐字比较（含入口下的 SPA 子路由）；
  // 配置为空时 matchesAdminEntry 直接返回 false，避免根路径变成后台入口。
  const config = await getAdminConfig(c.env);
  if (matchesAdminEntry(rawPathname, config.adminPath)) {
    const entryUrl = new URL(ADMIN_SPA_ENTRY, c.req.url);
    let response = await c.env.ASSETS.fetch(new Request(entryUrl, c.req.raw));
    // 兜底：若资源层仍返回跳转，跟随一次再取内容。
    const location = response.headers.get("location");
    if (response.status >= 300 && response.status < 400 && location) {
      response = await c.env.ASSETS.fetch(new Request(new URL(location, c.req.url), c.req.raw));
    }
    if (!response.ok) return withMutableHeaders(response);
    const setting = await getSettings(c.env);
    const html = injectAdminBase(await response.text(), `/${config.adminPath}`, siteFaviconHref(setting.favicon_data_url));
    return noStore(
      new Response(html, { status: response.status, statusText: response.statusText, headers: response.headers }),
    );
  }
  return withMutableHeaders(await c.env.ASSETS.fetch(c.req.raw));
});

// 公开首页直接读取 D1 站点设置，避免静态 HTML 与后台配置长期不一致。
async function handleHomePage(c: Context<AppEnv>): Promise<Response> {
  const setting = await getSettings(c.env);
  return noStore(c.html(renderHomePage(setting)));
}

// SPA 的 vue-router 需要知道真实入口，后台标签页也要使用 D1 中的站点图标。
// 两者都在下发 index.html 时动态注入，避免构建产物写死管理员配置。
function injectAdminBase(html: string, adminBase: string, favicon: string): string {
  const placeholder = /__CFPAY_ADMIN_BASE__\s*=\s*("[^"]*"|'[^']*')/;
  let injected = html;
  if (placeholder.test(html)) {
    injected = html.replace(placeholder, `__CFPAY_ADMIN_BASE__=${JSON.stringify(adminBase)}`);
  } else {
    const fallback = `<script>window.__CFPAY_ADMIN_BASE__=${JSON.stringify(adminBase)}</script>`;
    injected = html.includes("</head>") ? html.replace("</head>", `${fallback}</head>`) : fallback + html;
  }
  const faviconTag = `<link rel="icon" href="${favicon}">`;
  const faviconPattern = /<link\s+rel=(?:"icon"|'icon')[^>]*>/i;
  if (faviconPattern.test(injected)) return injected.replace(faviconPattern, faviconTag);
  return injected.includes("</head>") ? injected.replace("</head>", `${faviconTag}</head>`) : faviconTag + injected;
}

// 给响应加上禁止缓存的头，用于后台入口 HTML 与 admin.js——这两者必须每次都取最新，否则改配置后浏览器仍用旧页面。
function noStore(response: Response): Response {
  const headers = new Headers(response.headers);
  headers.set("cache-control", "no-store, no-cache, must-revalidate");
  headers.set("expires", "0");
  headers.set("pragma", "no-cache");
  return new Response(response.body, { status: response.status, statusText: response.statusText, headers });
}

// 处理易支付下单（submit.php 页面支付 / mapi.php 接口支付）。
// 先验签再校验必填参数，创建订单后：页面支付 302 跳到收银台，接口支付返回 JSON（含 payurl 与 qrcode）。
async function handleEpayCreate(c: Context<AppEnv>, htmlRedirect: boolean) {
  const params = await requestParams(c.req.raw);
  const setting = await getSettings(c.env);
  if (!verifyEpayRequest(params, setting)) {
    logEpayAuthFailure(c.req.raw, params, htmlRedirect ? "页面下单" : "接口下单");
    return htmlRedirect
      ? c.text("签名校验失败", 401)
      : c.json({ code: -1, msg: "签名校验失败", error: "EPAY_SIGNATURE_INVALID" }, 401);
  }
  const required = ["out_trade_no", "type", "money", "name"];
  if (required.some((key) => !params[key])) {
    return htmlRedirect ? c.text("订单参数不完整", 400) : c.json({ code: -1, msg: "订单参数不完整" }, 400);
  }

  const result = await createOrder(c.env, {
    merchantOrderNo: params.out_trade_no,
    provider: normalizeProvider(params.type),
    subject: params.name,
    amount: params.money,
    notifyUrl: params.notify_url,
    returnUrl: params.return_url,
    attach: params.param,
    clientIp: params.clientip || getClientIp(c.req.raw.headers),
    device: params.device,
  });
  if (result.order.provider === "alipay") {
    c.executionCtx.waitUntil(activateAlipayPolling(c.env));
  }
  const payUrl = `${new URL(c.req.url).origin}/pay/${result.order.id}`;
  if (htmlRedirect) return c.redirect(payUrl, 302);
  return c.json({
    code: 1,
    msg: "success",
    trade_no: result.order.id,
    out_trade_no: result.order.merchant_order_no,
    payurl: payUrl,
    qrcode: payUrl,
    money: formatFen(result.order.amount_fen),
    really_money: formatFen(result.order.payable_amount_fen),
  });
}

// 只记录易支付请求形态和字段摘要，不记录 key、sign 或请求体，避免排障日志变成凭据泄露点。
function logEpayAuthFailure(request: Request, params: Record<string, string>, endpoint: string): void {
  console.warn("易支付接口鉴权失败", {
    endpoint,
    method: request.method,
    path: new URL(request.url).pathname,
    contentType: request.headers.get("content-type") ?? "",
    fields: Object.keys(params).sort(),
    pid: params.pid || null,
    act: params.act || null,
    signType: params.sign_type || null,
    hasSign: Boolean(params.sign),
    signLength: params.sign?.length ?? 0,
    hasKey: Boolean(params.key),
    hasOrderNo: Boolean(params.out_trade_no || params.trade_no),
  });
}

// 取字符串参数，非字符串一律当空串处理，避免把 undefined 拼进 SQL 或签名。
function stringValue(value: unknown): string {
  return typeof value === "string" ? value : "";
}

// 校验站点文案长度并保留旧值，避免后台拆分保存时缺少字段会意外清空其它文案。
function siteText(value: unknown, errorCode: string, minimum: number, maximum: number, fallback: string): string {
  if (value === undefined) return fallback;
  if (typeof value !== "string") throw new Error(errorCode);
  const text = value.trim();
  if (text.length < minimum || text.length > maximum) throw new Error(errorCode);
  return text;
}

// 只接受小型栅格图标的 data URL，避免把任意 HTML、脚本或外部地址写入公开页面。
function siteFavicon(value: unknown, fallback: string): string {
  if (value === undefined) return fallback;
  if (typeof value !== "string") throw new Error("SITE_FAVICON_INVALID");
  const favicon = value.trim();
  if (!favicon) return "";
  const match = /^data:image\/(?:png|jpeg|webp|x-icon|vnd\.microsoft\.icon);base64,([A-Za-z0-9+/]+={0,2})$/.exec(favicon);
  if (!match) throw new Error("SITE_FAVICON_INVALID");
  const padding = match[1].endsWith("==") ? 2 : match[1].endsWith("=") ? 1 : 0;
  const byteLength = Math.floor(match[1].length * 3 / 4) - padding;
  if (byteLength > 512 * 1024) throw new Error("SITE_FAVICON_INVALID");
  return favicon;
}

// 取整数参数并校验区间，越界或非整数抛 INVALID_INTEGER。
function integerValue(value: unknown, minimum: number, maximum: number): number {
  const number = typeof value === "number" ? value : Number(value);
  if (!Number.isInteger(number) || number < minimum || number > maximum) throw new Error("INVALID_INTEGER");
  return number;
}

// 判断上报字段是否为 JSON 对象：微信登录状态以子对象形式随状态上报一起送达，
// 数组、字符串与 null 都不算，避免把畸形载荷当成「没有登录状态」静默忽略。
function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === "object" && value !== null && !Array.isArray(value);
}

// 每分钟 Cron 只处理持久业务数据；监控端在线状态与 nonce 都在 MonitorState 内存中自动过期。
async function runScheduled(env: Env): Promise<void> {
  const callbacks = await processDueCallbacks(env);
  console.log("scheduled maintenance", { callbacks });
}

export default {
  fetch: app.fetch,
  scheduled(_controller: ScheduledController, env: Env, context: ExecutionContext) {
    context.waitUntil(runScheduled(env));
  },
} satisfies ExportedHandler<Env>;
