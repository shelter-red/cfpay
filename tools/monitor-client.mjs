import { createHash, createHmac, randomBytes, randomUUID } from "node:crypto";

const baseUrl = process.env.CFPAY_BASE_URL?.replace(/\/+$/g, "");
const monitorId = process.env.CFPAY_MONITOR_ID ?? "pc-main";
const monitorSecret = process.env.CFPAY_MONITOR_SECRET;

if (!baseUrl || !monitorSecret) {
  console.error("请设置 CFPAY_BASE_URL 和 CFPAY_MONITOR_SECRET");
  process.exit(1);
}

const [command, ...args] = process.argv.slice(2);
if (command === "payment") {
  const [provider, amount, eventId = `${provider}:${Date.now()}:${randomUUID()}`] = args;
  if (!provider || !amount) {
    console.error("用法: node tools/monitor-client.mjs payment <wxpay|alipay> <金额> [eventId]");
    process.exit(1);
  }
  await post("/api/monitor/payment", {
    eventId,
    provider,
    amount,
    paidAt: Date.now(),
    raw: `manual pc event ${provider} ${amount}`,
  });
} else if (command === "status") {
  const [component, status, detailCode = "MANUAL_TEST", message = "manual component status"] = args;
  if (!component || !status) {
    console.error("用法: node tools/monitor-client.mjs status <wechat_hook|alipay_ledger> <状态> [诊断代码] [说明]");
    process.exit(1);
  }
  await post("/api/monitor/status", {
    component,
    status,
    version: "0.1.0",
    targetVersion: component === "wechat_hook" ? "Weixin 4.2.0.11" : "Alipay OpenAPI v3",
    hookOffset: component === "wechat_hook" ? "0x10b0230" : undefined,
    detailCode,
    message,
    observedAt: Date.now(),
  });
} else {
  console.error("用法: node tools/monitor-client.mjs <payment|status>");
  process.exit(1);
}

async function post(path, payload) {
  const body = JSON.stringify(payload);
  const timestamp = String(Math.floor(Date.now() / 1000));
  const nonce = randomBytes(24).toString("base64url");
  const bodyHash = createHash("sha256").update(body).digest("hex");
  const signature = createHmac("sha256", monitorSecret)
    .update(`${timestamp}\n${nonce}\n${bodyHash}`)
    .digest("hex");
  const response = await fetch(`${baseUrl}${path}`, {
    method: "POST",
    headers: {
      "content-type": "application/json",
      "x-monitor-id": monitorId,
      "x-timestamp": timestamp,
      "x-nonce": nonce,
      "x-signature": signature,
    },
    body,
  });
  const text = await response.text();
  console.log(response.status, text);
  if (!response.ok) process.exitCode = 1;
}
