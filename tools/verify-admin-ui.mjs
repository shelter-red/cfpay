#!/usr/bin/env node
/**
 * 管理后台前端（Vue SPA）端到端渲染验证。
 *
 * 用真实 Chromium 打开线上后台，用路由拦截把 /admin-api/* 替换成固定夹具，
 * 因此无需管理员口令即可验证登录页、十个业务页面、侧边栏、面包屑、
 * 订单/收款事件详情弹窗、微信登录二维码弹窗、密钥生成弹窗、客户端路由跳转与子路由刷新。
 * 同时收集 console 错误与未捕获异常，任何一条都会让对应用例失败。
 *
 * 前置依赖（不在项目 package.json 内，按需安装）：
 *   npm i -D playwright && npx playwright install chromium
 *
 * 用法：
 *   ADMIN_ENTRY=https://<你的网关域名>/<后台入口> node tools/verify-admin-ui.mjs
 * ADMIN_ENTRY 必填，脚本不会内置任何生产域名或后台入口。
 * 退出码 0 表示全部用例通过。
 */
import { mkdirSync, mkdtempSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import path from "node:path";

// playwright 还要额外下载浏览器，属于按需安装的重依赖，刻意不放进 package.json。
// 没装时用动态导入给出一句能直接照做的提示，而不是抛一个 ERR_MODULE_NOT_FOUND 让人猜。
let chromium;
try {
  ({ chromium } = await import("playwright"));
} catch {
  console.error("缺少 playwright。请先执行：npm i -D playwright && npx playwright install chromium");
  process.exit(2);
}

const ENTRY = process.env.ADMIN_ENTRY?.trim();
if (!ENTRY) {
  console.error("请设置 ADMIN_ENTRY，例如：ADMIN_ENTRY=https://pay.example.com/admin-change-me node tools/verify-admin-ui.mjs");
  process.exit(2);
}
const BASE = new URL(ENTRY).origin;
// 巡检覆盖的全部页面路径，桌面与移动端共用同一份清单。
const DESKTOP_PATHS = ["/dash", "/channels/wechat", "/channels/alipay", "/orders", "/unmatched", "/quick-pay", "/system/gateway", "/system/site", "/system/security", "/account"];
const ENTRY_PATH = new URL(ENTRY).pathname.replace(/\/+$/g, "");
// 默认把截图写进一次性临时目录并在结束时删除，避免每次验证都在磁盘上留下残留；
// 需要留存截图时显式指定 SHOT_DIR=/some/path。
const explicitShotDir = process.env.SHOT_DIR;
const tempShotDir = explicitShotDir ? null : mkdtempSync(path.join(tmpdir(), "cfpay-admin-verify-"));
const SHOT_DIR = explicitShotDir ?? tempShotDir;
mkdirSync(SHOT_DIR, { recursive: true });
const shot = (name) => path.join(SHOT_DIR, name);

const SVG = '<svg xmlns="http://www.w3.org/2000/svg" width="200" height="200"><rect width="200" height="200" fill="#ddd"/><text x="70" y="100">QR</text></svg>';
const BRAND_ICON = "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk+A8AAQUBAScY42YAAAAASUVORK5CYII=";
const UPDATE_MANIFEST_URL = process.env.VITE_UPDATE_MANIFEST_URL ?? "https://example.invalid/package.json";
const orderId = "33de1077605c40598c50557b704ea535";
const manualOrderId = "55de1077605c40598c50557b704ea537";
const quickPayOrderId = "44de1077605c40598c50557b704ea536";
const unmatchedEventId = `wechat:${"d".repeat(64)}`;
const ignoredEventId = `wechat:${"e".repeat(64)}`;

const nowStamp = Date.now();
const fixtures = {
  "/admin-api/dashboard": { metrics: { todayOrders: 2, todayPaidFen: 100, totalPaidFen: 12345, pendingNotify: 1, failedNotify: 1, unmatched: 1, staleUnmatched: 1 }, orderTrend: Array.from({ length: 90 }, (_, index) => ({ date: `2026-09-${String((index % 30) + 1).padStart(2, "0")}`, timestamp: nowStamp - (89 - index) * 86400000, createdOrders: index % 4, paidOrders: index % 3 })) },
  "/admin-api/cloudflare/d1-usage": { configured: true, plan: "free", timezone: "UTC", resetAt: "2026-10-06T00:00:00.000Z", fetchedAt: nowStamp, cacheExpiresAt: nowStamp + 300000,
    today: { date: "2026-10-05", rowsRead: { used: 12345, limit: 5000000, ratio: 0.002469, level: "normal" }, rowsWritten: { used: 4567, limit: 100000, ratio: 0.04567, level: "normal" }, queries: 789 },
    storage: { used: 1523712, limit: 5000000000, ratio: 0.0003047424, level: "normal" },
    trend: Array.from({ length: 30 }, (_, index) => ({
      date: new Date(Date.UTC(2026, 9, 5 - (29 - index))).toISOString().slice(0, 10),
      rowsRead: 1000 + index,
      rowsWritten: 100 + index,
      queries: 50 + index,
    })) },
  "/admin-api/settings": { merchantId: "m_demo", gatewayUrl: BASE, merchantKeyConfigured: true, monitorSecretConfigured: true,
    wechatQrImageUrl: "/payment-assets/qr/wxpay", alipayQrImageUrl: "/payment-assets/qr/alipay",
    orderTtlMinutes: 5, amountDirection: "UP", maxAmountAdjustments: 10 },
  "/admin-api/admin-settings": { username: "admin@example.com", adminPath: "admin-change-me", passwordConfigured: false, source: "database", updatedAt: Date.now() },
  "/admin-api/site-settings": { siteName: "测试收款台", siteSubtitle: "管理后台", footerText: "", faviconDataUrl: BRAND_ICON },
  "/admin-api/security/turnstile": { enabled: true, siteKeyConfigured: true, secretKeyConfigured: true },
  "/admin-api/qr-codes": { items: [{ provider: "wxpay", payloadSha256: "a".repeat(64), updatedAt: nowStamp - 1000, url: "/payment-assets/qr/wxpay" },
    { provider: "alipay", payloadSha256: "b".repeat(64), updatedAt: nowStamp - 2000, url: "/payment-assets/qr/alipay" }] },
  "/admin-api/alipay/status": { configured: true, source: "database", appId: "2021000000000000", appCertSn: "x", alipayRootCertSn: "y",
    alipayCertSn: "z", updatedAt: Date.now(), poller: { configured: true, alarm: null, lastRun: { at: Date.now(), error: null } } },
  "/admin-api/runtime-cache": { entries: 12, estimatedBytes: 74400, budgetBytes: 48 * 1024 * 1024, hits: 128, misses: 9, d1Reads: 9, evictions: 0, startedAt: Date.now() - 3600000 },
  "/admin-api/orders": { items: [
    { id: orderId, merchant_order_no: "USR1NOHnpMjJ1790617154", provider: "wxpay", subject: "TUC1", amount_fen: 100,
      payable_amount_fen: 100, status: "PAID", notify_status: "SUCCESS", paid_at: Date.now() - 60000,
      expires_at: Date.now() + 60000, created_at: Date.now() - 120000, device: null },
    { id: "2147a82c72b646e5aec5b64d9298e6c5", merchant_order_no: "USR1PENDING0001", provider: "alipay", subject: "TUC2",
      amount_fen: 200, payable_amount_fen: 201, status: "PENDING", notify_status: "NONE", paid_at: null,
      expires_at: Date.now() + 240000, created_at: Date.now() - 30000, device: null },
    { id: "5147a82c72b646e5aec5b64d9298e6c5", merchant_order_no: "USR1FAILED0001", provider: "wxpay", subject: "失败回调订单",
      amount_fen: 300, payable_amount_fen: 300, status: "PAID", notify_status: "FAILED", paid_at: Date.now() - 90000,
      expires_at: Date.now() - 30000, created_at: Date.now() - 180000, device: null, callback_handled_at: null,
      callback_handled_reason: null, callback_handled_by: null },
    { id: manualOrderId, merchant_order_no: "USR1MANUAL0001", provider: "wxpay", subject: "人工补单订单",
      amount_fen: 400, payable_amount_fen: 400, status: "PAID", notify_status: "NONE", paid_at: Date.now() - 45000,
      paid_source: "MANUAL", manual_paid_by: "admin@example.com",
      expires_at: Date.now() + 60000, created_at: Date.now() - 90000, device: null }], total: 4, page: 1, pageSize: 20 },
  [`/admin-api/quick-pay/orders/${quickPayOrderId}/diagnostics`]: {
    order: { id: quickPayOrderId, merchant_order_no: "CFPAY1790000000000abcd1234", provider: "wxpay", subject: "CFPay 快捷支付",
      amount_fen: 1, payable_amount_fen: 1, status: "PENDING", notify_status: "NONE", paid_at: null,
      expires_at: Date.now() + 240000, created_at: Date.now() - 30000, device: null },
    paymentEvent: null, paymentDetail: null, paymentRaw: null, callback: null, callbackAttempts: [],
    stages: [
      { key: "order", title: "商品与订单", status: "complete", summary: "商品与普通支付订单已创建。", data: { subject: "CFPay 快捷支付" } },
      { key: "channel", title: "通道与收银台", status: "complete", summary: "支付通道和收银台已就绪。", data: { qrConfigured: true } },
      { key: "capture", title: "真实收款采集", status: "waiting", summary: "等待真实收款。", data: { event: null } },
      { key: "match", title: "匹配与确认", status: "waiting", summary: "等待订单匹配。", data: { orderStatus: "PENDING" } },
      { key: "callback", title: "回调任务", status: "waiting", summary: "等待回调任务。", data: { callback: null } },
      { key: "response", title: "回调响应", status: "waiting", summary: "等待回调响应。", data: { attempts: [] } },
    ],
    completed: false, payUrl: `${BASE}/pay/${quickPayOrderId}` },
  [`/admin-api/orders/${orderId}`]: {
    order: { id: orderId, merchant_order_no: "USR1NOHnpMjJ1790617154", provider: "wxpay", amount_fen: 100, payable_amount_fen: 100,
      status: "PAID", notify_status: "SUCCESS", paid_at: Date.now() - 60000, expires_at: Date.now() + 60000,
      created_at: Date.now() - 120000, notify_url: "https://gateway.example.com/api/user/epay/notify", attach: "" },
    paymentEvent: { id: `wechat:${"c".repeat(64)}`, device_id: "wm_1", provider: "wxpay", amount_fen: 100,
      paid_at: Date.now() - 60000, status: "MATCHED", matched_order_id: orderId, created_at: Date.now() - 59000 },
    paymentDetail: { source: "paymsg", transid: "4200001234", fee: 100, feetype: 1, status: 1, type: "wx_f2f", timestamp: 1790617183 },
    paymentRaw: JSON.stringify({ source: "paymsg", transid: "4200001234", fee: 100, status: 1, type: "wx_f2f", timestamp: 1790617183 }),
    callback: { id: 6, status: "SUCCESS", attempt_count: 2, max_attempts: 6, next_attempt_at: null, last_error: null, updated_at: Date.now() },
    callbackAttempts: [{ attempt_no: 1, response_status: null, response_excerpt: null, error: "The operation was aborted due to timeout", created_at: Date.now() - 80000 },
      { attempt_no: 2, response_status: 200, response_excerpt: "success", error: null, created_at: Date.now() - 20000 }] },
  [`/admin-api/orders/${manualOrderId}`]: {
    order: { id: manualOrderId, merchant_order_no: "USR1MANUAL0001", provider: "wxpay", amount_fen: 400, payable_amount_fen: 400,
      status: "PAID", notify_status: "NONE", paid_at: Date.now() - 45000, expires_at: Date.now() + 60000,
      paid_source: "MANUAL", manual_paid_by: "admin@example.com",
      created_at: Date.now() - 90000, notify_url: null, attach: "" },
    paymentEvent: null,
    paymentDetail: null,
    paymentRaw: null,
    paymentActions: [],
    callback: null,
    callbackAttempts: [] },
  "/admin-api/monitors": {
    devices: [{ id: "wm_000000000000000000000000", name: "TC", version: "0.9.0", platform: "windows", last_ip: "1.2.3.4", last_heartbeat_at: Date.now() - 5000 }],
    components: [{ device_id: "wm_000000000000000000000000", component: "wechat_hook", status: "HEALTHY", component_version: "0.9.0",
      target_version: "Weixin 4.2.0.11", process_id: 4308, hook_offset: "0x10b0230", detail_code: "HOOK_ACTIVE",
      detail_message: "hardware breakpoint installed", last_event_at: Date.now() - 60000, reported_at: Date.now() - 5000, updated_at: Date.now() - 5000 }],
    wechatLogin: [{ deviceId: "wm_000000000000000000000000", status: "QR_READY", observedAt: Date.now() - 3000,
      expiresAt: Date.now() + 180000, updatedAt: Date.now() - 3000, qrAvailable: true, qrUrl: "/admin-api/wechat-login/wm_000000000000000000000000/qr?v=1" }],
    onlineWindowMs: 10000 },
  "/admin-api/payment-events": { items: [{ id: unmatchedEventId, device_id: "wm_000000000000000000000000", provider: "wxpay", amount_fen: 1,
    paid_at: Date.now() - 90000, status: "UNMATCHED", matched_order_id: null, matched_merchant_order_no: null,
    created_at: Date.now() - 25 * 60 * 60_000, updated_at: Date.now() - 90000, raw_length: 320,
    latest_action: null, latest_reason: null, latest_actor: null, latest_action_at: null }], total: 1, page: 1, pageSize: 20, now: Date.now() },
  [`/admin-api/payment-events/${unmatchedEventId}/candidates`]: { items: [{
    id: "candidate-order-1", merchant_order_no: "USR1CANDIDATE0001", provider: "wxpay", subject: "候选订单",
    amount_fen: 1, payable_amount_fen: 1, status: "EXPIRED", notify_status: "NONE", paid_at: null,
    expires_at: Date.now() - 30000, created_at: Date.now() - 120000, device: null,
  }], total: 1, page: 1, pageSize: 20 },
  [`/admin-api/payment-events/${unmatchedEventId}`]: {
    event: { id: unmatchedEventId, device_id: "wm_1", provider: "wxpay", amount_fen: 1, paid_at: Date.now() - 90000,
      status: "UNMATCHED", matched_order_id: null, created_at: Date.now() - 89000 },
    detail: { source: "paymsg", transid: "4200009999", fee: 1, status: 1, type: "wx_f2f" },
    raw: JSON.stringify({ source: "paymsg", transid: "4200009999", fee: 1 }), order: null,
    actions: [{ id: 1, event_id: unmatchedEventId, action: "RESTORE", order_id: null, reason: null, actor: "admin@example.com", created_at: Date.now() - 80000 }] },
};

const resolvedPaymentEvents = { items: [{
  id: ignoredEventId, device_id: "wm_000000000000000000000000", provider: "wxpay", amount_fen: 2,
  paid_at: Date.now() - 180000, status: "IGNORED", matched_order_id: null, matched_merchant_order_no: null,
  created_at: Date.now() - 170000, updated_at: Date.now() - 160000, raw_length: 420,
  latest_action: "IGNORE", latest_reason: "不是系统订单", latest_actor: "admin@example.com", latest_action_at: Date.now() - 160000,
}], total: 1, page: 1, pageSize: 20, now: Date.now() };


// Playwright 的 scrollIntoViewIfNeeded 偶发判定“元素在视口外”而无限重试；
// 这里先常规点击，失败则手动滚动并派发点击，仅用于自动化验证。
async function safeClick(page, locator) {
  try {
    await locator.click({ timeout: 6000 });
    return "normal";
  } catch (error) {
    const handle = await locator.elementHandle();
    await handle.evaluate((node) => { node.scrollIntoView({ block: "center", inline: "center" }); node.click(); });
    return `fallback(${error.message.split("\n")[0].slice(0, 40)})`;
  }
}

const browser = await chromium.launch({ args: ["--no-sandbox", "--disable-dev-shm-usage"] });
const results = [];
const clickNotes = [];

async function newPage({ loggedIn, interactiveLogin = false, viewport = { width: 1440, height: 1000 } }) {
  const page = await browser.newPage({ viewport });
  const problems = [];
  const updateRequests = { manifest: 0, health: 0 };
  const authRequests = { login: 0, session: 0 };
  let sessionActive = loggedIn;
  await page.addInitScript((adminBase) => {
    Object.defineProperty(window, "__CFPAY_ADMIN_BASE__", { value: adminBase, configurable: false, writable: false });
  }, new URL(ENTRY).pathname || "/");
  await page.addInitScript((snapshot) => {
    class MockWebSocket extends EventTarget {
      static CONNECTING = 0;
      static OPEN = 1;
      static CLOSING = 2;
      static CLOSED = 3;
      readyState = MockWebSocket.CONNECTING;

      constructor() {
        super();
        setTimeout(() => {
          if (this.readyState !== MockWebSocket.CONNECTING) return;
          this.readyState = MockWebSocket.OPEN;
          this.dispatchEvent(new Event("open"));
          this.dispatchEvent(new MessageEvent("message", { data: JSON.stringify({ type: "snapshot", data: snapshot }) }));
          const component = snapshot.components[0];
          this.dispatchEvent(new MessageEvent("message", { data: JSON.stringify({
            type: "update",
            data: {
              device: { ...snapshot.devices[0], last_heartbeat_at: Date.now() },
              component: component ? { ...component, reported_at: Date.now(), updated_at: Date.now() } : undefined,
              wechatLogin: snapshot.wechatLogin[0],
              serverTime: Date.now(),
            },
          }) }));
        }, 0);
      }

      send() {}

      close() {
        if (this.readyState === MockWebSocket.CLOSED) return;
        this.readyState = MockWebSocket.CLOSED;
        this.dispatchEvent(new CloseEvent("close"));
      }
    }
    Object.defineProperty(window, "WebSocket", { value: MockWebSocket, configurable: true });
  }, fixtures["/admin-api/monitors"]);
  page.on("console", (m) => { if (m.type() === "error") problems.push(`console: ${m.text()}`); });
  page.on("pageerror", (e) => problems.push(`pageerror: ${e.message}`));
  page.on("request", (request) => {
    if (new URL(request.url()).pathname === "/api/health") updateRequests.health += 1;
  });
  await page.route(UPDATE_MANIFEST_URL, async (route) => {
    updateRequests.manifest += 1;
    await route.fulfill({ status: 200, contentType: "application/json", body: JSON.stringify({ version: "0.9.3" }) });
  });
  await page.route("**/api/security/turnstile", (route) => route.fulfill({
    status: 200,
    contentType: "application/json",
    body: JSON.stringify({ enabled: false, siteKey: null }),
  }));
  await page.route("**/admin-api/**", async (route) => {
    const rawUrl = new URL(route.request().url());
    const url = new URL(rawUrl.origin + decodeURIComponent(rawUrl.pathname) + rawUrl.search);
    if (url.pathname === "/admin-api/session") {
      authRequests.session += 1;
      return sessionActive
        ? route.fulfill({ status: 200, contentType: "application/json", body: JSON.stringify({ ok: true, username: "admin@example.com" }) })
        : route.fulfill({ status: 401, contentType: "application/json", body: JSON.stringify({ ok: false, message: "未登录或会话已过期" }) });
    }
    if (url.pathname === "/admin-api/login" && route.request().method() === "POST" && interactiveLogin) {
      authRequests.login += 1;
      sessionActive = true;
      return route.fulfill({ status: 200, contentType: "application/json", body: JSON.stringify({ ok: true, username: "admin@example.com" }) });
    }
    if (url.pathname === "/admin-api/logout" && route.request().method() === "POST") {
      sessionActive = false;
      return route.fulfill({ status: 200, contentType: "application/json", body: JSON.stringify({ ok: true }) });
    }
    if (url.pathname === "/admin-api/quick-pay/orders" && route.request().method() === "POST") {
      return route.fulfill({ status: 200, contentType: "application/json", body: JSON.stringify({
        ok: true,
        order: fixtures[`/admin-api/quick-pay/orders/${quickPayOrderId}/diagnostics`].order,
        payUrl: `${BASE}/pay/${quickPayOrderId}`,
      }) });
    }
    if (url.pathname === "/admin-api/payment-events" && url.searchParams.get("scope") === "resolved") {
      return route.fulfill({ status: 200, contentType: "application/json", body: JSON.stringify(resolvedPaymentEvents) });
    }
    if (url.pathname.endsWith("/qr")) return route.fulfill({ status: 200, contentType: "image/svg+xml", body: SVG });
    const key = Object.keys(fixtures).find((p) => url.pathname === p) ?? Object.keys(fixtures).find((p) => url.pathname.startsWith(p));
    if (key) return route.fulfill({ status: 200, contentType: "application/json", body: JSON.stringify(fixtures[key]) });
    return route.fulfill({ status: 200, contentType: "application/json", body: "{}" });
  });
  await page.route("**/payment-assets/**", (route) => route.fulfill({ status: 200, contentType: "image/svg+xml", body: SVG }));
  return { page, problems, updateRequests, authRequests };
}

// 子元素不得超出父元素的边框盒：父级自己开了滚动/裁剪（overflow auto|scroll|hidden）时属于容器内滚动，不算超出。
// 绝对/固定定位的浮层（下拉、气泡、弹窗）按设计就会溢出父级，跳过。
function parentOverflowProbe(page) {
  return page.evaluate(() => {
    const offenders = [];
    for (const node of document.querySelectorAll("body *")) {
      if (node instanceof SVGElement) continue;
      const rect = node.getBoundingClientRect();
      if (!rect.width || !rect.height) continue;
      const style = getComputedStyle(node);
      if (style.position === "fixed" || style.position === "absolute" || style.visibility === "hidden") continue;
      const parent = node.parentElement;
      if (!parent) continue;
      const parentStyle = getComputedStyle(parent);
      if (parentStyle.position === "fixed") continue;
      const ox = parentStyle.overflowX;
      const oy = parentStyle.overflowY;
      if (ox === "auto" || ox === "scroll" || ox === "hidden" || ox === "clip") continue;
      if (oy === "auto" || oy === "scroll" || oy === "hidden" || oy === "clip") continue;
      const pRect = parent.getBoundingClientRect();
      const overRight = rect.right - pRect.right;
      const overLeft = pRect.left - rect.left;
      if (overRight <= 1 && overLeft <= 1) continue;
      offenders.push({
        tag: node.tagName,
        cls: (node.className || "").toString().replace(/\s+/g, " ").slice(0, 70),
        text: (node.textContent || "").trim().replace(/\s+/g, " ").slice(0, 30),
        overRight: Math.round(overRight),
        overLeft: Math.round(overLeft),
        parentTag: parent.tagName,
        parentCls: (parent.className || "").toString().replace(/\s+/g, " ").slice(0, 70),
      });
    }
    return offenders.slice(0, 6);
  });
}

// Alert 图标必须与标题分列；矩形相交说明图标占位失效，会直接压住文字。
function alertIconOverlapProbe(page) {
  return page.evaluate(() => {
    const overlaps = [];
    for (const alert of document.querySelectorAll('[data-slot="alert"]')) {
      const icon = alert.querySelector(':scope > svg');
      const title = alert.querySelector('[data-slot="alert-title"]');
      if (!icon || !title) continue;
      const iconRect = icon.getBoundingClientRect();
      const titleRect = title.getBoundingClientRect();
      const overlapX = Math.min(iconRect.right, titleRect.right) - Math.max(iconRect.left, titleRect.left);
      const overlapY = Math.min(iconRect.bottom, titleRect.bottom) - Math.max(iconRect.top, titleRect.top);
      if (overlapX > 0 && overlapY > 0) {
        overlaps.push({ title: title.textContent?.trim() ?? "", overlapX: Math.round(overlapX), overlapY: Math.round(overlapY) });
      }
    }
    return overlaps;
  });
}

async function goto(page, path, expectTexts) {
  await page.goto(`${ENTRY}${path}`, { waitUntil: "domcontentloaded", timeout: 25000 });
  const deadline = Date.now() + 12000;
  let body = "";
  while (Date.now() < deadline) {
    body = await page.evaluate(() => document.body.innerText);
    if (expectTexts.every((t) => body.includes(t))) break;
    await page.waitForTimeout(300);
  }
  return { body, missing: expectTexts.filter((t) => !body.includes(t)) };
}

// 1) 未登录 → 登录页（无侧边栏）
{
  const { page, problems } = await newPage({ loggedIn: false });
  const { missing, body } = await goto(page, "", ["CFPay 管理后台", "管理员账号", "密码", "登录"]);
  const hasSidebar = await page.evaluate(() => Boolean(document.querySelector("[data-sidebar='sidebar']")));
  results.push({ case: "未登录渲染登录页", ok: missing.length === 0 && !hasSidebar, missing, hasSidebar, problems,
    snippet: body.replace(/\n+/g, " | ").slice(0, 120) });
  await page.screenshot({ path: shot("10-login.png") });
  await page.close();
}

// 1.1) 登录成功响应直接建立本地会话并进入面板，不再追加一次可能覆盖状态的会话探测。
{
  const { page, problems, authRequests } = await newPage({ loggedIn: false, interactiveLogin: true });
  await goto(page, "", ["CFPay 管理后台", "管理员账号", "密码", "登录"]);
  await page.getByLabel("管理员账号").fill("admin@example.com");
  await page.getByLabel("密码").fill("correct-password");
  await page.getByRole("button", { name: "登录", exact: true }).click();
  await page.waitForURL((url) => url.pathname === `${ENTRY_PATH}/dash` || (!ENTRY_PATH && url.pathname === "/dash"), { timeout: 12000 });
  const body = await page.evaluate(() => document.body.innerText);
  results.push({
    case: "登录成功稳定跳转面板",
    ok: body.includes("面板") && !body.includes("单商户个人收款服务，请登录后继续")
      && authRequests.login === 1 && authRequests.session === 1,
    authRequests,
    problems,
  });
  await page.close();
}

// 2) 已登录 → 各页 + 弹窗
{
  const { page, problems, updateRequests } = await newPage({ loggedIn: true });

  // 各功能页逐页巡检：页面标题、区块标题与关键控件都要出现。
  const pageExpectations = [
    ["/dash", ["面板", "今日订单", "今日收款", "累计收款", "待完成回调", "回调失败", "超时未处理", "D1 用量", "当前计费日写入行数", "数据库存储", "订单趋势"]],
    ["/channels/wechat", ["微信渠道", "监控端接入", "PC 监控端密钥", "在线监控端", "微信 Hook", "微信登录", "微信收款码", "解析并保存"]],
    ["/channels/alipay", ["支付宝渠道", "证书与私钥", "应用私钥", "支付宝根证书", "测试账务查询", "上传并保存", "支付宝收款码"]],
    ["/orders", ["订单管理", "查询", "全部渠道", "全部订单状态", "全部回调状态", "USR1NOHnpMjJ1790617154", "已支付", "待支付", "详情", "补单"]],
    ["/unmatched", ["未匹配收款", "待处理", "处理记录", "匹配订单", "忽略", "详情"]],
    ["/quick-pay", ["快捷支付", "商品信息", "商品名称", "商品金额", "创建订单"]],
    ["/system/gateway", ["支付配置", "易支付兼容接口", "商户凭据与订单规则", "/submit.php", "/mapi.php", "/api.php?act=order", "/query.php", "保存网关设置"]],
    ["/system/site", ["站点设置", "基础信息", "站点名称", "品牌资源", "网站图标", "页脚", "保存设置"]],
    ["/system/security", ["安全配置", "后台入口路径", "Turnstile 已启用", "后台登录已启用人机验证。", "保存并重新登录"]],
    ["/account", ["账户设置", "管理员账号", "保存管理员账号", "修改密码", "当前密码"]],
  ];
  for (const [path, texts] of pageExpectations) {
    const rendered = await goto(page, path, texts);
    results.push({ case: `页面渲染 ${path}`, ok: rendered.missing.length === 0, missing: rendered.missing });
    await page.screenshot({ path: shot(`${path.slice(1).replaceAll("/", "-")}.png`), fullPage: true });
  }

  await goto(page, "/dash", ["D1 用量趋势", "订单趋势"]);
  const sevenDayButtons = page.getByRole("button", { name: "最近 7 天", exact: true });
  const selectedSevenDayCount = await sevenDayButtons.evaluateAll((buttons) => buttons.filter((button) => button.getAttribute("aria-pressed") === "true").length);
  results.push({ case: "面板趋势默认最近 7 天", ok: selectedSevenDayCount === 2, selectedSevenDayCount });
  clickNotes.push(await safeClick(page, page.getByRole("button", { name: /回调失败/ }).first()));
  await page.waitForTimeout(500);
  const metricTarget = await page.evaluate(() => ({ path: location.pathname, query: location.search }));
  results.push({ case: "异常卡片跳转", ok: metricTarget.path.endsWith("/orders") && metricTarget.query.includes("notifyStatus=FAILED"), ...metricTarget });

  // 侧边栏与面包屑
  const sidebarText = await page.evaluate(() => document.querySelector("[data-sidebar='sidebar']")?.innerText ?? "");
  results.push({ case: "侧边栏导航", ok: ["面板", "渠道管理", "微信渠道", "支付宝渠道", "交易管理", "订单管理", "未匹配收款", "快捷支付", "系统配置", "支付配置", "站点设置", "安全配置"].every((t) => sidebarText.includes(t)),
    missing: ["面板", "渠道管理", "微信渠道", "支付宝渠道", "交易管理", "订单管理", "未匹配收款", "快捷支付", "系统配置", "支付配置", "站点设置", "安全配置"].filter((t) => !sidebarText.includes(t)) });
  const brandButton = page.getByRole("button", { name: /测试收款台/ }).first();
  await brandButton.waitFor({ state: "visible", timeout: 5000 });
  const brandIcon = await brandButton.locator("img").getAttribute("src");
  results.push({ case: "侧边栏品牌图标", ok: brandIcon === BRAND_ICON, brandIcon });
  clickNotes.push(await safeClick(page, brandButton));
  clickNotes.push(await safeClick(page, page.getByText("当前版本 v0.9.2", { exact: false }).first()));
  await page.waitForTimeout(500);
  const updateText = await page.evaluate(() => document.body.innerText);
  results.push({ case: "GitHub 版本检测", ok: updateText.includes("最新 v0.9.3") && updateRequests.manifest === 1 && updateRequests.health === 0,
    updateRequests, latestVisible: updateText.includes("最新 v0.9.3") });
  await page.keyboard.press("Escape");
  await goto(page, "/orders", ["订单管理"]);
  const crumb = await page.evaluate(() => document.querySelector("header nav")?.innerText ?? "");
  results.push({ case: "面包屑", ok: crumb.includes("订单管理"), crumb: crumb.replace(/\n/g, " ") });


  const orderDetail = await goto(page, "/orders", ["订单管理", "USR1NOHnpMjJ1790617154"]);
  results.push({ case: "订单列表", ok: orderDetail.missing.length === 0, missing: orderDetail.missing });
  clickNotes.push(await safeClick(page, page.getByRole("button", { name: "详情" }).first()));
  await page.waitForTimeout(900);
  const dialogText = await page.evaluate(() => document.querySelector("[role='dialog']")?.innerText ?? "");
  results.push({ case: "订单详情弹窗", ok: ["订单", "收款事件", "解析字段", "完整上报 JSON", "商户回调", "回调尝试记录"].every((t) => dialogText.includes(t)),
    missing: ["订单", "收款事件", "解析字段", "完整上报 JSON", "商户回调", "回调尝试记录"].filter((t) => !dialogText.includes(t)),
    snippet: dialogText.replace(/\n+/g, " | ").slice(0, 300) });
  await page.screenshot({ path: shot("11-order-detail.png") });
  await page.keyboard.press("Escape");
  await page.waitForTimeout(500);

  // 人工补单属于订单支付确认来源，不得伪装成监控端真实上报或显示伪收款事件。
  const manualRow = page.getByRole("row").filter({ hasText: "USR1MANUAL0001" });
  clickNotes.push(await safeClick(page, manualRow.getByRole("button", { name: "详情" })));
  await page.waitForTimeout(500);
  const manualDetailText = await page.evaluate(() => document.querySelector("[role='dialog']")?.innerText ?? "");
  results.push({
    case: "人工补单独立审计展示",
    ok: manualDetailText.includes("支付确认")
      && manualDetailText.includes("操作管理员")
      && manualDetailText.includes("管理员人工补单")
      && !manualDetailText.includes("收款事件")
      && !manualDetailText.includes("完整上报 JSON")
      && !manualDetailText.includes("manual:"),
    snippet: manualDetailText.replace(/\n+/g, " | ").slice(0, 300),
  });
  await page.keyboard.press("Escape");
  await page.waitForTimeout(300);

  // 已支付订单删除前必须明确展示审计风险、可选导出入口和订单号确认要求。
  clickNotes.push(await safeClick(page, page.getByRole("button", { name: "删除", exact: true }).first()));
  await page.waitForTimeout(500);
  const orderDeleteText = await page.evaluate(() => document.querySelector("[role='dialog']")?.innerText ?? "");
  results.push({
    case: "订单永久删除弹窗",
    ok: ["永久删除订单", "删除前导出 JSON", "删除前导出 CSV", "包含已支付订单", "真实到账和回调审计数据", "USR1NOHnpMjJ1790617154"].every((text) => orderDeleteText.includes(text)),
    missing: ["永久删除订单", "删除前导出 JSON", "删除前导出 CSV", "包含已支付订单", "真实到账和回调审计数据", "USR1NOHnpMjJ1790617154"].filter((text) => !orderDeleteText.includes(text)),
    snippet: orderDeleteText.replace(/\n+/g, " | ").slice(0, 300),
  });
  await page.keyboard.press("Escape");
  await page.waitForTimeout(300);

  // 失败回调可记录处理说明，但不改变原始失败状态或尝试记录。
  clickNotes.push(await safeClick(page, page.getByRole("button", { name: "标记已处理", exact: true }).first()));
  await page.waitForTimeout(400);
  const callbackHandleText = await page.evaluate(() => document.querySelector("[role='dialog']")?.innerText ?? "");
  results.push({
    case: "失败回调处理弹窗",
    ok: ["处理失败回调", "处理说明", "确认已处理", "不再允许重试"].every((text) => callbackHandleText.includes(text)),
    missing: ["处理失败回调", "处理说明", "确认已处理", "不再允许重试"].filter((text) => !callbackHandleText.includes(text)),
    snippet: callbackHandleText.replace(/\n+/g, " | ").slice(0, 220),
  });
  await page.keyboard.press("Escape");
  await page.waitForTimeout(300);

  // 快捷支付必须创建普通订单，并展示完整六阶段实际诊断数据。
  await goto(page, "/quick-pay", ["商品信息", "商品名称", "商品金额", "创建订单"]);
  clickNotes.push(await safeClick(page, page.getByRole("button", { name: "创建订单", exact: true })));
  const quickPayRendered = await goto(page, `/quick-pay?order=${quickPayOrderId}`, [
    "商品订单", "CFPay 快捷支付", "商品与订单", "通道与收银台", "真实收款采集", "匹配与确认", "回调任务", "回调响应",
  ]);
  const quickPayPath = await page.evaluate(() => ({ path: location.pathname, query: location.search }));
  results.push({
    case: "快捷支付六阶段诊断",
    ok: quickPayRendered.missing.length === 0 && quickPayPath.path.endsWith("/quick-pay") && quickPayPath.query.includes(`order=${quickPayOrderId}`),
    missing: quickPayRendered.missing,
    ...quickPayPath,
  });

  await goto(page, "/channels/wechat", ["在线监控端", "微信 Hook"]);
  clickNotes.push(await safeClick(page, page.getByRole("button", { name: "版本信息", exact: true })));
  await page.waitForTimeout(300);
  const monitorVersionText = await page.evaluate(() => document.querySelector("[role='dialog']")?.innerText ?? "");
  results.push({
    case: "监控端版本信息弹窗",
    ok: ["Windows 监控端版本", "0.9.2", "4.2.0.11", "SHA-256"].every((text) => monitorVersionText.includes(text)),
    missing: ["Windows 监控端版本", "0.9.2", "4.2.0.11", "SHA-256"].filter((text) => !monitorVersionText.includes(text)),
  });
  await page.keyboard.press("Escape");
  await page.waitForTimeout(300);
  clickNotes.push(await safeClick(page, page.getByRole("button", { name: /显示登录二维码/ }).first()));
  await page.waitForTimeout(800);
  const qrText = await page.evaluate(() => document.querySelector("[role='dialog']")?.innerText ?? "");
  results.push({ case: "微信登录二维码弹窗", ok: qrText.includes("微信登录二维码") && qrText.includes("剩余有效时间"),
    snippet: qrText.replace(/\n+/g, " | ").slice(0, 160) });
  await page.screenshot({ path: shot("12-qr-dialog.png") });
  await page.keyboard.press("Escape");
  await page.waitForTimeout(500);
  await goto(page, "/unmatched", ["未匹配收款", "匹配订单", "忽略"]);
  clickNotes.push(await safeClick(page, page.getByRole("button", { name: "匹配订单" }).first()));
  await page.waitForTimeout(600);
  const matchText = await page.evaluate(() => document.querySelector("[role='dialog']")?.innerText ?? "");
  results.push({ case: "未匹配收款匹配弹窗", ok: matchText.includes("USR1CANDIDATE0001") && matchText.includes("选择并匹配"), snippet: matchText.replace(/\n+/g, " | ").slice(0, 180) });
  await page.keyboard.press("Escape");
  await page.waitForTimeout(300);
  clickNotes.push(await safeClick(page, page.getByRole("button", { name: "忽略", exact: true }).first()));
  await page.waitForTimeout(300);
  const ignoreText = await page.evaluate(() => document.querySelector("[role='dialog']")?.innerText ?? "");
  results.push({ case: "未匹配收款忽略弹窗", ok: ignoreText.includes("忽略原因") && ignoreText.includes("确认忽略"), snippet: ignoreText.replace(/\n+/g, " | ").slice(0, 160) });
  await page.keyboard.press("Escape");
  await page.waitForTimeout(300);
  clickNotes.push(await safeClick(page, page.getByRole("button", { name: "详情" }).first()));
  await page.waitForTimeout(900);
  const eventText = await page.evaluate(() => document.querySelector("[role='dialog']")?.innerText ?? "");
  results.push({ case: "未匹配收款详情弹窗", ok: eventText.includes("收款事件") && eventText.includes("解析字段") && eventText.includes("完整上报 JSON") && eventText.includes("处理记录"),
    snippet: eventText.replace(/\n+/g, " | ").slice(0, 160) });
  await page.keyboard.press("Escape");
  await page.waitForTimeout(400);

  // 只有已经忽略的收款记录允许永久删除，删除前同样提供两种导出格式。
  await goto(page, "/unmatched?scope=resolved", ["处理记录", "已忽略", "删除"]);
  clickNotes.push(await safeClick(page, page.getByRole("button", { name: "删除", exact: true }).first()));
  await page.waitForTimeout(400);
  const ignoredDeleteText = await page.evaluate(() => document.querySelector("[role='dialog']")?.innerText ?? "");
  results.push({
    case: "已忽略收款永久删除弹窗",
    ok: ["永久删除已忽略收款", "删除前导出 JSON", "删除前导出 CSV", ignoredEventId].every((text) => ignoredDeleteText.includes(text)),
    missing: ["永久删除已忽略收款", "删除前导出 JSON", "删除前导出 CSV", ignoredEventId].filter((text) => !ignoredDeleteText.includes(text)),
    snippet: ignoredDeleteText.replace(/\n+/g, " | ").slice(0, 260),
  });
  await page.keyboard.press("Escape");
  await page.waitForTimeout(300);

  await goto(page, "/system/gateway", ["易支付兼容接口", "商户凭据与订单规则"]);
  clickNotes.push(await safeClick(page, page.getByRole("button", { name: "生成", exact: true }).nth(1)));
  await page.waitForTimeout(700);
  const secretText = await page.evaluate(() => document.querySelector("[role='dialog']")?.innerText ?? "");
  results.push({ case: "生成密钥弹窗", ok: secretText.includes("保存商户密钥") && secretText.includes("我已妥善保存"),
    snippet: secretText.replace(/\n+/g, " | ").slice(0, 200) });
  await page.screenshot({ path: shot("13-secret-dialog.png") });
  await page.keyboard.press("Escape");
  await page.locator("[role='dialog']").waitFor({ state: "detached", timeout: 5000 }).catch(() => {});
  await page.waitForTimeout(400);

  // 侧边栏折叠按钮 + 客户端路由跳转（不整页刷新）
  clickNotes.push(await safeClick(page, page.locator('a[href$="/channels/alipay"]').first()));
  await page.waitForTimeout(1200);
  const afterNav = await page.evaluate(() => ({ path: location.pathname, text: document.body.innerText }));
  results.push({ case: "侧边栏客户端路由跳转", ok: afterNav.path.endsWith("/channels/alipay") && afterNav.text.includes("支付宝收款码"),
    path: afterNav.path });

  // 刷新子路由仍应正常（Worker 兜底）
  await page.reload({ waitUntil: "domcontentloaded" });
  await page.waitForTimeout(1500);
  const afterReload = await page.evaluate(() => document.body.innerText);
  results.push({ case: "子路由刷新不 404", ok: afterReload.includes("支付宝渠道") && afterReload.includes("支付宝收款码") });

  results.push({ case: "全程无 console/page 错误", ok: problems.length === 0, problems });
  await page.screenshot({ path: shot("14-alipay-channel.png"), fullPage: true });
  await page.close();
}

results.push({ case: "点击方式记录", clickNotes });

// 2.5) 桌面视口（1440x1000）同样不得有子元素超出父边框
{
  const { page } = await newPage({ loggedIn: true });
  for (const path of DESKTOP_PATHS) {
    await page.goto(`${ENTRY}${path}`, { waitUntil: "domcontentloaded", timeout: 25000 });
    await page.waitForTimeout(2000);
    const offenders = await parentOverflowProbe(page);
    results.push({ case: `桌面端不超父边框 ${path || "/"}`, ok: offenders.length === 0, offenders });
  }
  await page.close();
}

// 2.6) 宽屏桌面（2048x1200）逐页复查边界、Alert 图标和账户表单宽度。
{
  const { page } = await newPage({ loggedIn: true, viewport: { width: 2048, height: 1200 } });
  for (const path of DESKTOP_PATHS) {
    await page.goto(`${ENTRY}${path}`, { waitUntil: "domcontentloaded", timeout: 25000 });
    await page.waitForTimeout(1800);
    const offenders = await parentOverflowProbe(page);
    const alertOverlaps = await alertIconOverlapProbe(page);
    results.push({ case: `宽屏桌面布局 ${path || "/"}`, ok: offenders.length === 0 && alertOverlaps.length === 0, offenders, alertOverlaps });
  }

  await page.goto(`${ENTRY}/account`, { waitUntil: "domcontentloaded", timeout: 25000 });
  await page.waitForTimeout(1800);
  const accountWidths = await page.evaluate(() => {
    const cards = [...document.querySelectorAll('[data-slot="card"]')];
    const forms = [...document.querySelectorAll('form')];
    const inputs = [...document.querySelectorAll('input')];
    return {
      cardWidths: cards.map((node) => Math.round(node.getBoundingClientRect().width)),
      formWidths: forms.map((node) => Math.round(node.getBoundingClientRect().width)),
      inputWidths: inputs.map((node) => Math.round(node.getBoundingClientRect().width)),
    };
  });
  const minimumFormWidth = Math.min(...accountWidths.formWidths);
  const minimumInputWidth = Math.min(...accountWidths.inputWidths);
  const formsFillCards = accountWidths.cardWidths.length === accountWidths.formWidths.length
    && accountWidths.cardWidths.every((width, index) => accountWidths.formWidths[index] >= width - 64);
  results.push({
    case: "宽屏账户表单占满卡片",
    ok: minimumFormWidth > 0 && formsFillCards && minimumInputWidth >= minimumFormWidth - 2,
    ...accountWidths,
  });
  await page.close();
}

// 3) 移动端（390x844）不得横向溢出，也不得被祖先 overflow 裁掉
{
  const MOBILE = { width: 390, height: 844, isMobile: true, hasTouch: true, deviceScaleFactor: 2 };
  const { page } = await newPage({ loggedIn: true, viewport: MOBILE });
  const probe = () =>
    page.evaluate(() => {
      const vw = document.documentElement.clientWidth;
      const beyond = [];
      const clipped = [];
      for (const node of document.querySelectorAll("body *")) {
        if (node instanceof SVGElement) continue;
        const rect = node.getBoundingClientRect();
        if (!rect.width || !rect.height) continue;
        const style = getComputedStyle(node);
        if (style.position === "fixed" || style.visibility === "hidden") continue;
        // 位于可横向滚动容器内的元素属于正常的“容器内滚动”，不算溢出。
        // 遇到 hidden/clip 不能就此停止：那只是把元素裁住（例如表格里的 truncate 单元格），
        // 裁住的元素根本画不到屏幕外，仍要继续往外找是否存在滚动容器，否则数据表格会被误判成溢出。
        let scrollable = false;
        for (let up = node.parentElement; up; up = up.parentElement) {
          const ox = getComputedStyle(up).overflowX;
          if (ox === "auto" || ox === "scroll") { scrollable = true; break; }
        }
        const info = { tag: node.tagName, right: Math.round(rect.right),
          cls: (node.className || "").toString().replace(/\s+/g, " ").slice(0, 60),
          text: (node.textContent || "").trim().replace(/\s+/g, " ").slice(0, 28) };
        if (rect.right > vw + 1 && !scrollable) beyond.push(info);
        const parent = node.parentElement;
        if (parent) {
          const pRect = parent.getBoundingClientRect();
          const pox = getComputedStyle(parent).overflowX;
          if ((pox === "hidden" || pox === "clip") && rect.right > pRect.right + 1) {
            clipped.push({ ...info, parentCls: (parent.className || "").toString().replace(/\s+/g, " ").slice(0, 60) });
          }
        }
      }
      return { vw, docScrollWidth: document.documentElement.scrollWidth, beyond: beyond.slice(0, 4), clipped: clipped.slice(0, 4) };
    });

  for (const path of ["/dash", "/system/gateway", "/system/site", "/channels/wechat", "/channels/alipay", "/orders", "/unmatched", "/quick-pay", "/system/security", "/account"]) {
    await page.goto(`${ENTRY}${path}`, { waitUntil: "domcontentloaded", timeout: 25000 });
    await page.waitForTimeout(2200);
    const r = await probe();
    results.push({
      case: `移动端不溢出 ${path || "/"}`,
      ok: r.docScrollWidth <= r.vw && r.beyond.length === 0 && r.clipped.length === 0,
      vw: r.vw, docScrollWidth: r.docScrollWidth, beyond: r.beyond, clipped: r.clipped,
    });
  }

  for (const path of DESKTOP_PATHS) {
    await page.goto(`${ENTRY}${path}`, { waitUntil: "domcontentloaded", timeout: 25000 });
    await page.waitForTimeout(2000);
    const offenders = await parentOverflowProbe(page);
    results.push({ case: `移动端不超父边框 ${path || "/"}`, ok: offenders.length === 0, offenders });
  }

  // 弹窗在移动端同样不得溢出
  await page.goto(`${ENTRY}/orders`, { waitUntil: "domcontentloaded" });
  await page.waitForTimeout(2000);
  await page.getByRole("button", { name: "详情" }).first().evaluate((n) => n.click());
  await page.waitForTimeout(1200);
  const dialogProbe = await probe();
  results.push({ case: "移动端详情弹窗不溢出",
    ok: dialogProbe.docScrollWidth <= dialogProbe.vw && dialogProbe.clipped.length === 0,
    docScrollWidth: dialogProbe.docScrollWidth, vw: dialogProbe.vw, clipped: dialogProbe.clipped });
  await page.close();
}

// 4) 同页文案不得重复：抓说明性文案的叶子节点，用“最长公共子串（含中文，≥8 字）”判定语义重复
{
  const { page } = await newPage({ loggedIn: true });
  const dupProbe = () =>
    page.evaluate(() => {
      const texts = [];
      for (const node of document.querySelectorAll("h1,h2,h3,p,span,dd,dt,label,legend,small")) {
        if (node.closest("[data-sidebar='sidebar']") || node.closest("[role='dialog']")) continue;
        if (node.querySelector("h1,h2,h3,p,span,dd,dt,label,legend,small")) continue;
        const text = (node.innerText || "").trim().replace(/\s+/g, " ");
        if (text.length >= 8) texts.push(text);
      }
      const norm = (s) => s.replace(/[。；，、：\s·（）()]/g, "");
      const cjk = (s) => (s.match(/[\u4e00-\u9fff]/g) || []).length;
      const unique = [...new Set(texts)];
      const pairs = [];
      for (let i = 0; i < unique.length; i += 1) {
        for (let j = i + 1; j < unique.length; j += 1) {
          const a = norm(unique[i]);
          const b = norm(unique[j]);
          let best = "";
          for (let start = 0; start < a.length; start += 1) {
            for (let len = a.length - start; len > best.length; len -= 1) {
              if (b.includes(a.slice(start, start + len))) { best = a.slice(start, start + len); break; }
            }
          }
          // 只认含中文的重复，避免 "POST / GET"、文件名等结构性文本误报
          if (best.length >= 8 && cjk(best) >= 6) pairs.push({ shared: best, a: unique[i], b: unique[j] });
        }
      }
      const counts = new Map();
      for (const text of texts) counts.set(text, (counts.get(text) ?? 0) + 1);
      const repeated = [...counts].filter(([, n]) => n > 1).map(([text, n]) => ({ text, times: n }));
      return { pairs: pairs.slice(0, 8), repeated };
    });

  for (const path of ["/dash", "/system/gateway", "/system/site", "/channels/wechat", "/channels/alipay", "/orders", "/unmatched", "/quick-pay", "/system/security", "/account"]) {
    await page.goto(`${ENTRY}${path}`, { waitUntil: "domcontentloaded", timeout: 25000 });
    await page.waitForTimeout(2000);
    const r = await dupProbe();
    results.push({ case: `文案无重复 ${path}`, ok: r.pairs.length === 0 && r.repeated.length === 0, pairs: r.pairs, repeated: r.repeated });
  }
  await page.close();
}

// 5) 禁止已经统一过的旧术语、英文关闭文案和错误标点重新进入页面。
{
  const { page } = await newPage({ loggedIn: true });
  const banned = ["东八区自然日", "UTC 自然日", "估算内存", "原金额", "支付金额", "通知状态", "上报的原始 JSON", "暂无数据.", "检查中...", "Close"];
  for (const path of ["/dash", "/system/gateway", "/system/site", "/channels/wechat", "/channels/alipay", "/orders", "/unmatched", "/quick-pay", "/system/security", "/account"]) {
    await page.goto(`${ENTRY}${path}`, { waitUntil: "domcontentloaded", timeout: 25000 });
    await page.waitForTimeout(1200);
    const text = await page.evaluate(() => {
      const hidden = [...document.querySelectorAll(".sr-only")].map((node) => node.textContent ?? "").join(" ");
      return `${document.body.innerText} ${hidden}`;
    });
    const found = banned.filter((phrase) => text.includes(phrase));
    results.push({ case: `文案术语统一 ${path}`, ok: found.length === 0, found });
  }
  await page.close();
}

process.on("exit", () => { try { console.log("RESULTS:" + JSON.stringify(results)); } catch {} });
console.log(JSON.stringify(results, null, 2));
await browser.close();

if (tempShotDir) rmSync(tempShotDir, { recursive: true, force: true });

const failed = results.filter((row) => row.ok === false);
// 未登录时的 401 是会话探测的正常结果，不算应用错误。
const noisy = results.filter((row) => (row.problems ?? []).some((p) => !p.includes("401")));
if (failed.length || noisy.length) {
  console.error(`\n失败用例 ${failed.length} 个，异常输出 ${noisy.length} 个`);
  process.exitCode = 1;
} else {
  console.log(`\n全部 ${results.length} 项通过${explicitShotDir ? `，截图在 ${SHOT_DIR}` : "（截图为一次性临时文件，已自动删除；需要留存请设置 SHOT_DIR）"}`);
}
