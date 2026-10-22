#!/usr/bin/env node
/**
 * 部署入口：迁移 + 发布，自动选择配置来源。
 *
 * 1. 存在 wrangler.local.jsonc（被 Git 忽略，保存真实域名 / D1 ID）时直接使用它；
 * 2. 否则使用 CLOUDFLARE_API_TOKEN 自动发现 Account ID、cfpay_db 的 D1 ID 与
 *    cfpay 已绑定的自定义域名，供 GitHub Actions 零变量部署；
 * 3. 环境变量 CLOUDFLARE_ACCOUNT_ID、CFPAY_D1_DATABASE_ID、CFPAY_DOMAIN
 *    可覆盖自动发现结果。
 *
 * 仓库里的 wrangler.jsonc 只有占位值，绝不直接部署。
 * 传入 --check 只解析并打印配置，不迁移、不发布。
 */
import { spawnSync } from "node:child_process";
import { existsSync, mkdirSync, readFileSync, writeFileSync } from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const checkOnly = process.argv.includes("--check");

/** 去掉 JSONC 的行注释，仓库配置只用到 `//` 注释。 */
function parseJsonc(text) {
  let out = "";
  let inString = false;
  let escaped = false;
  for (let i = 0; i < text.length; i += 1) {
    const ch = text[i];
    if (inString) {
      out += ch;
      if (escaped) escaped = false;
      else if (ch === "\\") escaped = true;
      else if (ch === '"') inString = false;
      continue;
    }
    if (ch === '"') {
      inString = true;
      out += ch;
      continue;
    }
    if (ch === "/" && text[i + 1] === "/") {
      while (i < text.length && text[i] !== "\n") i += 1;
      out += "\n";
      continue;
    }
    out += ch;
  }
  return JSON.parse(out);
}

const base = parseJsonc(readFileSync(path.join(root, "wrangler.jsonc"), "utf8"));
const databaseName = base.d1_databases[0]?.database_name ?? "cfpay_db";
const workerName = base.name;

/** 调用 Cloudflare API，返回 result；失败时抛出可读错误。 */
async function api(endpoint, token) {
  const response = await fetch(`https://api.cloudflare.com/client/v4${endpoint}`, {
    headers: { Authorization: `Bearer ${token}`, "Content-Type": "application/json" },
  });
  const body = await response.json().catch(() => null);
  if (!response.ok || !body?.success) {
    const detail = body?.errors?.map((item) => item.message).filter(Boolean).join("；");
    throw new Error(`${endpoint} 请求失败（HTTP ${response.status}）${detail ? `：${detail}` : ""}`);
  }
  return body.result;
}

/** 从令牌自动发现部署参数；只读取，不修改任何云端资源。 */
async function discover(token) {
  const accountId = process.env.CLOUDFLARE_ACCOUNT_ID?.trim() || await (async () => {
    const accounts = await api("/accounts", token);
    if (!Array.isArray(accounts) || accounts.length !== 1) {
      throw new Error(`令牌可见 ${accounts?.length ?? 0} 个账户，无法自动判断；请设置 CLOUDFLARE_ACCOUNT_ID。`);
    }
    return accounts[0].id;
  })();

  const databaseId = process.env.CFPAY_D1_DATABASE_ID?.trim() || await (async () => {
    const databases = await api(`/accounts/${accountId}/d1/database`, token);
    const match = databases.find((item) => item.name === databaseName);
    if (!match) throw new Error(`账户内找不到 D1 数据库 ${databaseName}；请设置 CFPAY_D1_DATABASE_ID。`);
    return match.uuid;
  })();

  const domain = process.env.CFPAY_DOMAIN?.trim() || await (async () => {
    const domains = await api(`/accounts/${accountId}/workers/domains`, token);
    const match = domains.find((item) => item.service === workerName && item.environment === "production" && item.enabled !== false);
    if (!match) throw new Error(`找不到 ${workerName} 已绑定的自定义域名；请设置 CFPAY_DOMAIN。`);
    return match.hostname;
  })();

  // 复用线上现有的明文变量（尤其是后台入口与管理员账号），避免用仓库占位值覆盖生产配置。
  const settings = await api(`/accounts/${accountId}/workers/scripts/${workerName}/settings`, token);
  const plainVars = Object.fromEntries(
    (settings.bindings ?? []).filter((item) => item.type === "plain_text").map((item) => [item.name, item.text]),
  );
  if (!plainVars.ADMIN_PATH) {
    throw new Error(`无法读取线上 ${workerName} 的 ADMIN_PATH；拒绝用占位值部署。`);
  }

  return { accountId, databaseId, domain, plainVars };
}

/** 生成部署配置：优先本机文件，否则按发现结果生成临时配置。 */
async function resolveConfig() {
  const localConfig = path.join(root, "wrangler.local.jsonc");
  if (existsSync(localConfig)) {
    console.log(`使用本机部署配置：${path.relative(root, localConfig)}`);
    return { path: localConfig, local: true };
  }

  const token = process.env.CLOUDFLARE_API_TOKEN?.trim() ?? "";
  if (!token) {
    console.error(
      "缺少 CLOUDFLARE_API_TOKEN。\n" +
        "本机部署请复制 wrangler.jsonc 为 wrangler.local.jsonc 并填入真实值；" +
        "GitHub Actions 请在 Settings → Secrets and variables → Actions 中配置 CLOUDFLARE_API_TOKEN。",
    );
    process.exit(1);
  }

  let discovered;
  try {
    discovered = await discover(token);
  } catch (error) {
    console.error(`自动发现部署参数失败：${error.message}`);
    process.exit(1);
  }

  const { accountId, databaseId, domain, plainVars } = discovered;
  const config = structuredClone(base);
  config.workers_dev = false;
  config.vars = {
    ...config.vars,
    ...plainVars,
    CLOUDFLARE_ACCOUNT_ID: accountId,
    CLOUDFLARE_D1_DATABASE_ID: databaseId,
  };
  config.d1_databases[0].database_id = databaseId;
  // 配置文件放在 .wrangler/ 下，wrangler 会相对配置文件解析这些路径，必须改成绝对路径。
  config.main = path.join(root, "src/index.ts");
  config.assets.directory = path.join(root, "public");
  config.d1_databases[0].migrations_dir = path.join(root, "migrations");
  config.routes = [{ pattern: domain, custom_domain: true }];
  mkdirSync(path.join(root, ".wrangler"), { recursive: true });
  const configPath = path.join(root, ".wrangler", "deploy.generated.jsonc");
  writeFileSync(configPath, `${JSON.stringify(config, null, 2)}\n`);
  console.log(`已按令牌自动生成临时部署配置：${path.relative(root, configPath)}`);

  // 让后续 workflow 步骤（例如部署摘要）能读到实际域名。
  if (process.env.GITHUB_ENV) {
    writeFileSync(process.env.GITHUB_ENV, `CFPAY_DOMAIN=${domain}\n`, { flag: "a" });
  }
  return { path: configPath, local: false, domain };
}

const resolved = await resolveConfig();

if (checkOnly) {
  console.log(`配置检查通过：${resolved.path}${resolved.domain ? `（${resolved.domain}）` : ""}`);
  process.exit(0);
}

function run(args) {
  const result = spawnSync("npx", ["wrangler", ...args], { cwd: root, stdio: "inherit" });
  if (result.status !== 0) process.exit(result.status ?? 1);
}

run(["d1", "migrations", "apply", "DB", "--remote", "--config", resolved.path]);
run(["deploy", "--config", resolved.path]);
