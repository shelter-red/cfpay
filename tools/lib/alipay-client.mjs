import { readFile } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { AlipaySdk } from "alipay-sdk";

const rootDirectory = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "../..");
const configPath = path.join(rootDirectory, "config/alipay-test.local.json");
const configDirectory = path.dirname(configPath);

export async function createAlipayClient() {
  const config = JSON.parse(await readFile(configPath, "utf8"));
  validateConfig(config);
  const privateKey = (await readFile(resolveConfigPath(config.files.appPrivateKey), "utf8")).trim();
  const sdk = new AlipaySdk({
    appId: config.appId,
    privateKey,
    keyType: "PKCS8",
    signType: "RSA2",
    endpoint: config.endpoint,
    appCertPath: resolveConfigPath(config.files.appCert),
    alipayPublicCertPath: resolveConfigPath(config.files.alipayPublicCert),
    alipayRootCertPath: resolveConfigPath(config.files.alipayRootCert),
    timeout: 15_000,
    camelcase: false,
  });
  return { sdk, config };
}

export async function queryAlipayLedger(client, lookbackMinutes = client.config.query.lookbackMinutes) {
  const end = new Date();
  const start = new Date(end.getTime() - lookbackMinutes * 60_000);
  const query = {
    start_time: formatAlipayTime(start),
    end_time: formatAlipayTime(end),
    page_no: 1,
    page_size: client.config.query.pageSize,
  };
  const result = await client.sdk.curl("GET", "/v3/alipay/data/bill/accountlog/query", { query });
  const data = result.data ?? result;
  const details = Array.isArray(data.detail_list) ? data.detail_list : [];
  return {
    query,
    responseHttpStatus: result.responseHttpStatus,
    traceId: result.traceId,
    pageNo: data.page_no,
    pageSize: data.page_size,
    totalSize: data.total_size,
    details,
  };
}

export function sanitizeLedgerDetail(detail) {
  return {
    account_log_id: detail.account_log_id ?? null,
    trans_dt: detail.trans_dt ?? detail.trans_date ?? detail.trans_time ?? null,
    trans_amount: detail.trans_amount ?? null,
    direction: detail.direction ?? null,
    alipay_order_no: detail.alipay_order_no ?? null,
    merchant_order_no: detail.merchant_order_no ?? null,
    trans_memo: detail.trans_memo ?? null,
    other_account: mask(detail.other_account ?? detail.other_account_name ?? ""),
  };
}

export function mask(value) {
  const text = String(value ?? "");
  if (text.length <= 6) return text ? "***" : "";
  return `${text.slice(0, 3)}***${text.slice(-3)}`;
}

function resolveConfigPath(value) {
  return path.resolve(configDirectory, value);
}

function validateConfig(config) {
  if (!config || config.mode !== "certificate") throw new Error("配置必须使用 certificate 模式");
  if (!/^\d{16,32}$/.test(config.appId ?? "")) throw new Error("appId 格式错误");
  if (!/^https:\/\//.test(config.endpoint ?? "")) throw new Error("endpoint 必须是 HTTPS 地址");
  for (const key of ["appPrivateKey", "appCert", "alipayPublicCert", "alipayRootCert"]) {
    if (typeof config.files?.[key] !== "string" || !config.files[key]) throw new Error(`缺少 files.${key}`);
  }
  if (!Number.isInteger(config.query?.lookbackMinutes) || config.query.lookbackMinutes < 1 || config.query.lookbackMinutes > 1440) {
    throw new Error("query.lookbackMinutes 必须为 1 到 1440 的整数");
  }
  if (!Number.isInteger(config.query?.pageSize) || config.query.pageSize < 1 || config.query.pageSize > 2000) {
    throw new Error("query.pageSize 必须为 1 到 2000 的整数");
  }
}

function formatAlipayTime(date) {
  const parts = new Intl.DateTimeFormat("en-CA", {
    timeZone: "Asia/Shanghai",
    year: "numeric",
    month: "2-digit",
    day: "2-digit",
    hour: "2-digit",
    minute: "2-digit",
    second: "2-digit",
    hourCycle: "h23",
  }).formatToParts(date);
  const values = Object.fromEntries(parts.map((part) => [part.type, part.value]));
  return `${values.year}-${values.month}-${values.day} ${values.hour}:${values.minute}:${values.second}`;
}
