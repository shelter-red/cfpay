import { constantTimeEqual, hmacSha256Hex } from "./lib/crypto";
import { runtimeFirst, runtimePut } from "./runtime-data";
import type { Env } from "./types";

const SESSION_COOKIE = "cfpay_admin";
const SESSION_DURATION_SECONDS = 12 * 60 * 60;
const PASSWORD_ITERATIONS = 600_000;
// Cloudflare Workers 的 crypto.subtle 对 PBKDF2 单次调用有硬上限，超过就抛
// NotSupportedError: "Pbkdf2 failed: iteration counts above 100000 are not supported"。
// 因此把目标迭代次数拆成多轮链式派生：每轮 100000 次，上一轮输出当作下一轮口令，
// 盐保持不变，总工作量仍是 iterations 次，既能达到 OWASP 对 PBKDF2-SHA256 的 600000 次建议，
// 又不触碰运行时上限。iterations 必须是 100000 的整数倍，与 admin_setting 的 CHECK 区间一致。
const WORKER_PBKDF2_MAX_ITERATIONS = 100_000;
const WORKER_PBKDF2_MAX_TOTAL_ITERATIONS = 1_000_000;
const DEFAULT_ADMIN_PATH = "admin-change-me";

// 后台入口不再限制字符集和长度，但下面这些首段必须保留：
// 要么是 Worker 已注册的路由前缀，要么是 public/ 下真实存在的静态资源，
// 一旦被入口占用，管理台会顶掉接口/资源，或反过来永远访问不到。
const RESERVED_ADMIN_PATH_SEGMENTS = new Set([
  "admin",
  "admin-api",
  "admin.html",
  "admin.js",
  "admin-app",
  "api",
  "api.php",
  "index.html",
  "mapi.php",
  "pay",
  "payment-assets",
  "query.php",
  "styles.css",
  "submit.php",
]);

interface SessionPayload {
  sub: string;
  ver: number;
  exp: number;
}

interface AdminSettingRow {
  username: string;
  admin_path: string;
  password_hash: string | null;
  password_salt: string | null;
  password_iterations: number | null;
  session_version: number;
  updated_at: number;
}

export interface AdminRuntimeConfig {
  username: string;
  adminPath: string;
  passwordHash: string | null;
  passwordSalt: string | null;
  passwordIterations: number | null;
  sessionVersion: number;
  source: "database" | "environment";
  updatedAt: number | null;
}

export interface AdminSettingInput {
  username: unknown;
  adminPath: unknown;
  password?: unknown;
}

/** 验证当前密码后更新管理员密码，并递增会话版本使已有会话失效。 */
export async function changeAdminPassword(
  env: Env,
  currentPassword: unknown,
  newPassword: unknown,
): Promise<AdminRuntimeConfig> {
  if (typeof currentPassword !== "string" || !currentPassword) throw new Error("CURRENT_ADMIN_PASSWORD_REQUIRED");
  if (typeof newPassword !== "string" || newPassword.length < 12 || newPassword.length > 128) {
    throw new Error("INVALID_ADMIN_PASSWORD");
  }

  const current = await getAdminConfig(env);
  const currentPasswordValid =
    current.passwordHash && current.passwordSalt && current.passwordIterations
      ? await verifyAdminPassword(
          currentPassword,
          current.passwordHash,
          current.passwordSalt,
          current.passwordIterations,
        )
      : Boolean(env.ADMIN_PASSWORD && constantTimeEqual(currentPassword, env.ADMIN_PASSWORD));
  if (!currentPasswordValid) throw new Error("CURRENT_ADMIN_PASSWORD_MISMATCH");

  return updateAdminConfig(env, {
    username: current.username,
    adminPath: current.adminPath,
    password: newPassword,
  });
}

// 读取管理员运行配置：优先用 D1 里后台改过的值，没有记录时回退到部署变量 ADMIN_USERNAME / ADMIN_PATH。
// 部署变量只是引导用的初值，后台改过一次之后就以数据库为准。
export async function getAdminConfig(env: Env): Promise<AdminRuntimeConfig> {
  const row = await runtimeFirst<AdminSettingRow>(
    env,
    "config:admin",
    "SELECT * FROM admin_setting WHERE id = 1",
    [],
    { ttlMs: null, pinned: true },
  );
  if (row) {
    return {
      username: row.username,
      adminPath: row.admin_path,
      passwordHash: row.password_hash,
      passwordSalt: row.password_salt,
      passwordIterations: row.password_iterations,
      sessionVersion: row.session_version,
      source: "database",
      updatedAt: row.updated_at,
    };
  }
  return {
    username: env.ADMIN_USERNAME,
    adminPath: normalizeAdminPath(env.ADMIN_PATH) || DEFAULT_ADMIN_PATH,
    passwordHash: null,
    passwordSalt: null,
    passwordIterations: null,
    sessionVersion: 1,
    source: "environment",
    updatedAt: null,
  };
}

// 保存管理员账号、入口路径与密码。
// 密码留空表示不修改；填写则重新做 PBKDF2 派生。session_version 每次自增，
// 使所有已签发的会话立即失效，管理员必须用新入口重新登录。
export async function updateAdminConfig(env: Env, input: AdminSettingInput): Promise<AdminRuntimeConfig> {
  const current = await getAdminConfig(env);
  const username = validateAdminUsername(input.username);
  const adminPath = validateAdminPath(input.adminPath);
  const password = typeof input.password === "string" ? input.password : "";
  if (password && (password.length < 12 || password.length > 128)) throw new Error("INVALID_ADMIN_PASSWORD");

  let passwordHash = current.passwordHash;
  let passwordSalt = current.passwordSalt;
  let passwordIterations = current.passwordIterations;
  if (password) {
    const hashed = await hashAdminPassword(password);
    passwordHash = hashed.hash;
    passwordSalt = hashed.salt;
    passwordIterations = hashed.iterations;
  }

  const now = Date.now();
  const sessionVersion = current.sessionVersion + 1;
  await env.DB.prepare(
    `INSERT INTO admin_setting (
       id, username, admin_path, password_hash, password_salt, password_iterations,
       session_version, created_at, updated_at
     ) VALUES (1, ?, ?, ?, ?, ?, ?, ?, ?)
     ON CONFLICT(id) DO UPDATE SET
       username = excluded.username,
       admin_path = excluded.admin_path,
       password_hash = excluded.password_hash,
       password_salt = excluded.password_salt,
       password_iterations = excluded.password_iterations,
       session_version = excluded.session_version,
       updated_at = excluded.updated_at`,
  )
    .bind(username, adminPath, passwordHash, passwordSalt, passwordIterations, sessionVersion, now, now)
    .run();

  await runtimePut<AdminSettingRow>(
    env,
    "config:admin",
    {
      username,
      admin_path: adminPath,
      password_hash: passwordHash,
      password_salt: passwordSalt,
      password_iterations: passwordIterations,
      session_version: sessionVersion,
      updated_at: now,
    },
    { ttlMs: null, pinned: true },
  );

  return {
    username,
    adminPath,
    passwordHash,
    passwordSalt,
    passwordIterations,
    sessionVersion,
    source: "database",
    updatedAt: now,
  };
}

// 校验管理员口令：账号用定长比较，口令优先用 D1 里的 PBKDF2 哈希校验，
// 从未在后台设过密码时回退到 Worker Secret ADMIN_PASSWORD。任一不匹配返回 null，不区分“账号错”和“密码错”。
export async function verifyAdminCredentials(
  env: Env,
  username: string,
  password: string,
): Promise<AdminRuntimeConfig | null> {
  const config = await getAdminConfig(env);
  if (!config.username || !constantTimeEqual(username, config.username)) return null;
  const valid =
    config.passwordHash && config.passwordSalt && config.passwordIterations
      ? await verifyAdminPassword(password, config.passwordHash, config.passwordSalt, config.passwordIterations)
      : Boolean(env.ADMIN_PASSWORD && constantTimeEqual(password, env.ADMIN_PASSWORD));
  return valid ? config : null;
}

// 签发会话 Cookie：载荷只含账号、会话版本与过期时间，用 SESSION_SECRET 做 HMAC 签名。
// HttpOnly + Secure + SameSite=Strict，12 小时有效；会话版本让“改配置即踢下线”成为可能。
export async function createAdminSession(env: Env, config: AdminRuntimeConfig): Promise<string> {
  if (!env.SESSION_SECRET) throw new Error("SESSION_SECRET_NOT_CONFIGURED");
  const payload: SessionPayload = {
    sub: config.username,
    ver: config.sessionVersion,
    exp: Math.floor(Date.now() / 1000) + SESSION_DURATION_SECONDS,
  };
  const encodedPayload = encodeBase64Url(JSON.stringify(payload));
  const signature = await hmacSha256Hex(env.SESSION_SECRET, encodedPayload);
  return `${SESSION_COOKIE}=${encodedPayload}.${signature}; Path=/; HttpOnly; Secure; SameSite=Strict; Max-Age=${SESSION_DURATION_SECONDS}`;
}

// 生成清除会话的 Set-Cookie（Max-Age=0），用于退出登录与修改管理员配置后强制重新登录。
export function clearAdminSession(): string {
  return `${SESSION_COOKIE}=; Path=/; HttpOnly; Secure; SameSite=Strict; Max-Age=0`;
}

// 校验会话 Cookie：先验 HMAC 签名，再比对账号与会话版本、检查是否过期。
// 会话版本不匹配意味着管理员配置已变更，旧会话一律失效。
export async function verifyAdminSession(request: Request, env: Env): Promise<boolean> {
  if (!env.SESSION_SECRET) return false;
  const session = getCookie(request, SESSION_COOKIE);
  if (!session) return false;
  const separator = session.lastIndexOf(".");
  if (separator <= 0) return false;

  const encodedPayload = session.slice(0, separator);
  const providedSignature = session.slice(separator + 1);
  const expectedSignature = await hmacSha256Hex(env.SESSION_SECRET, encodedPayload);
  if (!constantTimeEqual(providedSignature, expectedSignature)) return false;

  try {
    const payload = JSON.parse(decodeBase64Url(encodedPayload)) as SessionPayload;
    const config = await getAdminConfig(env);
    return (
      payload.sub === config.username &&
      payload.ver === config.sessionVersion &&
      payload.exp > Math.floor(Date.now() / 1000)
    );
  } catch {
    return false;
  }
}

// 用 PBKDF2-HMAC-SHA256 派生口令哈希。
// Workers 的 crypto.subtle 对单次 PBKDF2 有 100000 次迭代上限，超出会抛 NotSupportedError，
// 因此按 100000 次一轮做链式派生：上一轮输出作为下一轮口令、盐保持不变，总工作量等于 iterations。
export async function hashAdminPassword(
  password: string,
  saltBytes = crypto.getRandomValues(new Uint8Array(16)),
  iterations = PASSWORD_ITERATIONS,
): Promise<{ hash: string; salt: string; iterations: number }> {
  if (
    !Number.isInteger(iterations) ||
    iterations < WORKER_PBKDF2_MAX_ITERATIONS ||
    iterations > WORKER_PBKDF2_MAX_TOTAL_ITERATIONS ||
    iterations % WORKER_PBKDF2_MAX_ITERATIONS !== 0
  ) {
    throw new Error("INVALID_ADMIN_PASSWORD_ITERATIONS");
  }
  const rounds = iterations / WORKER_PBKDF2_MAX_ITERATIONS;
  let material = new TextEncoder().encode(password);
  for (let round = 0; round < rounds; round += 1) {
    const key = await crypto.subtle.importKey("raw", material, "PBKDF2", false, ["deriveBits"]);
    const bits = await crypto.subtle.deriveBits(
      { name: "PBKDF2", hash: "SHA-256", salt: saltBytes, iterations: WORKER_PBKDF2_MAX_ITERATIONS },
      key,
      256,
    );
    material = new Uint8Array(bits);
  }
  return { hash: encodeBytesBase64Url(material), salt: encodeBytesBase64Url(saltBytes), iterations };
}

// 按存储的盐与迭代次数重新派生并做定长比较；迭代次数参与派生，因此改次数等于换哈希。
export async function verifyAdminPassword(
  password: string,
  expectedHash: string,
  salt: string,
  iterations: number,
): Promise<boolean> {
  const actual = await hashAdminPassword(password, Uint8Array.from(decodeBytesBase64Url(salt)), iterations);
  return constantTimeEqual(actual.hash, expectedHash);
}

// 归一化入口路径：去首尾空白与首尾斜杠。空值回退在调用方处理，这里不做兜底，便于区分“未配置”与“配了个空值”。
export function normalizeAdminPath(value: string): string {
  return value.trim().replace(/^\/+|\/+$/g, "");
}

// 入口路径允许中文等非 ASCII 字符，而 URL.pathname 是百分号编码后的形式，
// 比较前先解码；遇到畸形编码（例如孤立的 %）时退回原始值。
export function decodeAdminPathname(rawPathname: string): string {
  let decoded = rawPathname;
  try {
    decoded = decodeURIComponent(rawPathname);
  } catch {
    decoded = rawPathname;
  }
  return decoded.replace(/\/+$/g, "") || "/";
}

// 这些路径固定 404：默认入口名不能对外，SPA 的 index.html 也只能经管理员配置的入口下发，
// 否则拿到静态文件路径就等于绕过了“入口即密钥”的约定。
export const BLOCKED_ADMIN_PATHNAMES = new Set([
  "/admin",
  "/admin.html",
  "/admin.js",
  "/admin-app",
  "/admin-app/index.html",
]);

// 判断请求路径是否命中管理员配置的后台入口。
// 入口下所有子路径都要命中，因为后台是 Vue SPA，子路由刷新需要服务端返回同一份首页。
export function matchesAdminEntry(rawPathname: string, adminPath: string): boolean {
  if (!adminPath) return false;
  const pathname = decodeAdminPathname(rawPathname);
  if (BLOCKED_ADMIN_PATHNAMES.has(pathname)) return false;
  const entry = `/${adminPath}`;
  // 管理后台是 Vue SPA，入口下的所有子路径都要返回同一份 index.html，交给 vue-router 处理。
  return pathname === entry || pathname.startsWith(`${entry}/`);
}

// 入口路径允许任意字符、任意长度、多级路径，只要求非空且不占用保留首段。
export function validateAdminPath(value: unknown): string {
  if (typeof value !== "string") throw new Error("ADMIN_PATH_REQUIRED");
  const normalized = normalizeAdminPath(value);
  if (!normalized) throw new Error("ADMIN_PATH_REQUIRED");
  const firstSegment = normalized.split("/")[0].toLowerCase();
  if (RESERVED_ADMIN_PATH_SEGMENTS.has(firstSegment)) throw new Error("ADMIN_PATH_RESERVED");
  return normalized;
}

// 管理员账号允许任意字符与长度（含中文、邮箱、空格），只要求非空。
export function validateAdminUsername(value: unknown): string {
  if (typeof value !== "string") throw new Error("ADMIN_USERNAME_REQUIRED");
  const username = value.trim();
  if (!username) throw new Error("ADMIN_USERNAME_REQUIRED");
  return username;
}

// 把字符串编码成 base64url（无填充），用于会话载荷。
function encodeBase64Url(value: string): string {
  return encodeBytesBase64Url(new TextEncoder().encode(value));
}

// 把 base64url 解码回字符串，用于读取会话载荷。
function decodeBase64Url(value: string): string {
  return new TextDecoder().decode(decodeBytesBase64Url(value));
}

// 把字节编码成 base64url，用于口令哈希与盐的存储形式。
function encodeBytesBase64Url(bytes: Uint8Array): string {
  let binary = "";
  for (const byte of bytes) binary += String.fromCharCode(byte);
  return btoa(binary).replaceAll("+", "-").replaceAll("/", "_").replace(/=+$/g, "");
}

// 把 base64url 还原成字节，自动补齐 padding。
function decodeBytesBase64Url(value: string): Uint8Array {
  const padded = value.replaceAll("-", "+").replaceAll("_", "/").padEnd(Math.ceil(value.length / 4) * 4, "=");
  return Uint8Array.from(atob(padded), (character) => character.charCodeAt(0));
}

// 从 Cookie 头里取指定名称的值；同名多次出现时取第一个，值里含等号也能正确还原。
function getCookie(request: Request, name: string): string | null {
  const cookieHeader = request.headers.get("cookie");
  if (!cookieHeader) return null;
  for (const item of cookieHeader.split(";")) {
    const [key, ...valueParts] = item.trim().split("=");
    if (key === name) return valueParts.join("=");
  }
  return null;
}
