import { Buffer } from "node:buffer";
import { Certificate } from "@fidm/x509";
import { legacyMd5 } from "./lib/crypto";
import { runtimeFirst, runtimePut } from "./runtime-data";
import type { Env } from "./types";

const ENCRYPTION_CONTEXT = "cfpay:alipay-credential:v1";
const MAX_PRIVATE_KEY_BYTES = 16 * 1024;
const MAX_CERTIFICATE_BYTES = 128 * 1024;

export interface AlipayRuntimeConfig {
  appId: string;
  appPrivateKey: string;
  appCertSn: string;
  alipayRootCertSn: string;
  alipayCertSn: string;
  alipayPublicKey: string;
}

export interface AlipayCredentialStatus {
  configured: boolean;
  source: "database" | "environment" | null;
  appId: string | null;
  appCertSn: string | null;
  alipayRootCertSn: string | null;
  alipayCertSn: string | null;
  updatedAt: number | null;
}

interface AlipayCredentialRow {
  app_id: string;
  app_cert_sn: string;
  alipay_root_cert_sn: string;
  alipay_cert_sn: string;
  encrypted_payload: string;
  encryption_iv: string;
  updated_at: number;
}

let runtimeCache: { updatedAt: number; config: AlipayRuntimeConfig } | null = null;

// 解析后台上传的支付宝证书模式凭据（App ID + 应用私钥 + 三份证书）。
// 逐个校验：App ID 必须是 16~32 位数字且与应用证书文件名一致；证书必须能解析且处于有效期内；
// 应用私钥必须与应用证书里的公钥匹配；支付宝公钥必须可导入。
// 任何一步失败都抛明确的错误码，避免把半套凭据存进去导致账务查询签名失败。
export async function parseAlipayCredentialUpload(form: FormData): Promise<AlipayRuntimeConfig> {
  const appPrivateKeyFile = requiredFile(form, "appPrivateKey", MAX_PRIVATE_KEY_BYTES);
  const appCertFile = requiredFile(form, "appCert", MAX_CERTIFICATE_BYTES);
  const alipayPublicCertFile = requiredFile(form, "alipayPublicCert", MAX_CERTIFICATE_BYTES);
  const alipayRootCertFile = requiredFile(form, "alipayRootCert", MAX_CERTIFICATE_BYTES);
  const suppliedAppId = form.get("appId");
  const filenameAppId = appCertFile.name.match(/appCertPublicKey_(\d{16,32})\.crt$/i)?.[1] ?? "";
  const appId = typeof suppliedAppId === "string" && suppliedAppId.trim() ? suppliedAppId.trim() : filenameAppId;
  if (!/^\d{16,32}$/.test(appId)) throw new Error("INVALID_ALIPAY_APP_ID");
  if (filenameAppId && filenameAppId !== appId) throw new Error("ALIPAY_APP_ID_FILENAME_MISMATCH");

  const [privateKeyText, appCertText, alipayPublicCertText, alipayRootCertText] = await Promise.all([
    appPrivateKeyFile.text(),
    appCertFile.text(),
    alipayPublicCertFile.text(),
    alipayRootCertFile.text(),
  ]);
  const appPrivateKey = normalizePrivateKey(privateKeyText);

  let appCertificate: Certificate;
  let alipayCertificate: Certificate;
  let rootCertificates: Certificate[];
  try {
    appCertificate = Certificate.fromPEM(Buffer.from(appCertText));
    alipayCertificate = Certificate.fromPEM(Buffer.from(alipayPublicCertText));
    rootCertificates = Certificate.fromPEMs(Buffer.from(alipayRootCertText));
  } catch {
    throw new Error("INVALID_ALIPAY_CERTIFICATE");
  }
  if (rootCertificates.length === 0) throw new Error("INVALID_ALIPAY_ROOT_CERTIFICATE");
  validateCertificateTime(appCertificate);
  validateCertificateTime(alipayCertificate);

  const appPublicKey = certificatePublicKeyPem(appCertificate);
  const alipayPublicKey = certificatePublicKeyPem(alipayCertificate);
  await verifyPrivateKeyMatchesCertificate(appPrivateKey, appPublicKey);
  await importPublicKey(alipayPublicKey);

  return {
    appId,
    appPrivateKey,
    appCertSn: certificateSn(appCertificate),
    alipayRootCertSn: rootCertificates
      .filter((certificate) => certificate.signatureOID.startsWith("1.2.840.113549.1.1"))
      .map(certificateSn)
      .join("_"),
    alipayCertSn: certificateSn(alipayCertificate),
    alipayPublicKey,
  };
}

// 把凭据用 SESSION_SECRET 派生的 AES-GCM 密钥加密后写入 D1（固定第 1 行）。
// 明文只在内存里存在，D1 里只有密文与 IV；写入成功后刷新进程内缓存。
export async function saveAlipayCredentials(env: Env, config: AlipayRuntimeConfig): Promise<void> {
  const encrypted = await encryptAlipayCredentials(config, env.SESSION_SECRET);
  const now = Date.now();
  await env.DB.prepare(
    `INSERT INTO alipay_credential (
       id, app_id, app_cert_sn, alipay_root_cert_sn, alipay_cert_sn,
       encrypted_payload, encryption_iv, created_at, updated_at
     ) VALUES (1, ?, ?, ?, ?, ?, ?, ?, ?)
     ON CONFLICT(id) DO UPDATE SET
       app_id = excluded.app_id,
       app_cert_sn = excluded.app_cert_sn,
       alipay_root_cert_sn = excluded.alipay_root_cert_sn,
       alipay_cert_sn = excluded.alipay_cert_sn,
       encrypted_payload = excluded.encrypted_payload,
       encryption_iv = excluded.encryption_iv,
       updated_at = excluded.updated_at`,
  )
    .bind(
      config.appId,
      config.appCertSn,
      config.alipayRootCertSn,
      config.alipayCertSn,
      encrypted.ciphertext,
      encrypted.iv,
      now,
      now,
    )
    .run();
  await runtimePut<AlipayCredentialRow>(
    env,
    "config:alipay-credential",
    {
      app_id: config.appId,
      app_cert_sn: config.appCertSn,
      alipay_root_cert_sn: config.alipayRootCertSn,
      alipay_cert_sn: config.alipayCertSn,
      encrypted_payload: encrypted.ciphertext,
      encryption_iv: encrypted.iv,
      updated_at: now,
    },
    { ttlMs: null, pinned: true },
  );
  runtimeCache = { updatedAt: now, config };
}

// 删除后台上传的凭据并清空缓存；Worker Secrets 里的回退配置不受影响。
export async function deleteAlipayCredentials(env: Env): Promise<void> {
  await env.DB.prepare("DELETE FROM alipay_credential WHERE id = 1").run();
  await runtimePut(env, "config:alipay-credential", null, { ttlMs: null, pinned: true });
  runtimeCache = null;
}

// 取当前生效的支付宝凭据：优先 D1 里的密文（带进程内缓存，按 updated_at 失效），
// 没有记录时回退到 Worker Secrets 的环境变量配置。解密失败抛 ALIPAY_CREDENTIAL_DECRYPT_FAILED。
export async function resolveAlipayConfig(env: Env): Promise<AlipayRuntimeConfig | null> {
  const row = await getCredentialRow(env);
  if (row) {
    if (runtimeCache?.updatedAt === row.updated_at) return runtimeCache.config;
    const config = await decryptAlipayCredentials(row.encrypted_payload, row.encryption_iv, env.SESSION_SECRET);
    runtimeCache = { updatedAt: row.updated_at, config };
    return config;
  }
  return environmentAlipayConfig(env);
}

// 给后台返回凭据状态：是否已配置、来源（D1 加密存储 / Worker Secrets）、App ID 与三份证书序列号、更新时间。
// 会顺带解密一次以确认密文可用，但绝不返回私钥或证书原文。
export async function getAlipayCredentialStatus(env: Env): Promise<AlipayCredentialStatus> {
  const row = await getCredentialRow(env);
  if (row) {
    await resolveAlipayConfig(env);
    return {
      configured: true,
      source: "database",
      appId: row.app_id,
      appCertSn: row.app_cert_sn,
      alipayRootCertSn: row.alipay_root_cert_sn,
      alipayCertSn: row.alipay_cert_sn,
      updatedAt: row.updated_at,
    };
  }
  const environment = environmentAlipayConfig(env);
  return {
    configured: Boolean(environment),
    source: environment ? "environment" : null,
    appId: environment?.appId ?? null,
    appCertSn: environment?.appCertSn ?? null,
    alipayRootCertSn: environment?.alipayRootCertSn ?? null,
    alipayCertSn: environment?.alipayCertSn ?? null,
    updatedAt: null,
  };
}

// 把整套凭据序列化成 JSON 后做 AES-GCM 加密，附加数据绑定固定上下文，防止把这份密文当作别的用途解密。
export async function encryptAlipayCredentials(
  config: AlipayRuntimeConfig,
  secret: string,
): Promise<{ ciphertext: string; iv: string }> {
  const key = await encryptionKey(secret);
  const iv = crypto.getRandomValues(new Uint8Array(12));
  const plaintext = new TextEncoder().encode(JSON.stringify(config));
  const encrypted = await crypto.subtle.encrypt(
    { name: "AES-GCM", iv, additionalData: new TextEncoder().encode(ENCRYPTION_CONTEXT) },
    key,
    plaintext,
  );
  return { ciphertext: bytesToBase64(new Uint8Array(encrypted)), iv: bytesToBase64(iv) };
}

// 解密并校验结构完整性；密文被篡改、上下文不符或字段缺失都会抛 ALIPAY_CREDENTIAL_DECRYPT_FAILED。
export async function decryptAlipayCredentials(
  ciphertext: string,
  iv: string,
  secret: string,
): Promise<AlipayRuntimeConfig> {
  try {
    const key = await encryptionKey(secret);
    const decrypted = await crypto.subtle.decrypt(
      {
        name: "AES-GCM",
        iv: base64ToBytes(iv),
        additionalData: new TextEncoder().encode(ENCRYPTION_CONTEXT),
      },
      key,
      base64ToBytes(ciphertext),
    );
    const parsed = JSON.parse(new TextDecoder().decode(decrypted)) as AlipayRuntimeConfig;
    if (!isRuntimeConfig(parsed)) throw new Error("INVALID_PAYLOAD");
    return parsed;
  } catch {
    throw new Error("ALIPAY_CREDENTIAL_DECRYPT_FAILED");
  }
}

// 归一化私钥文本：统一换行、补齐 PKCS#8 头尾，兼容用户直接粘贴裸 base64 的情况。
export function normalizePrivateKey(value: string): string {
  const trimmed = value.trim();
  if (/-----BEGIN PRIVATE KEY-----[\s\S]+-----END PRIVATE KEY-----/.test(trimmed)) return trimmed;
  const compact = trimmed.replace(/\s+/g, "");
  if (!/^[a-zA-Z0-9+/]+={0,2}$/.test(compact) || compact.length < 512) throw new Error("INVALID_ALIPAY_PRIVATE_KEY");
  return `-----BEGIN PRIVATE KEY-----\n${compact.match(/.{1,64}/g)?.join("\n")}\n-----END PRIVATE KEY-----`;
}

// 取必填的上传文件并校验大小上限，缺失或超限抛明确错误码。
function requiredFile(form: FormData, name: string, maximumBytes: number): File {
  const value = form.get(name);
  if (!(value instanceof File) || value.size === 0) throw new Error("ALIPAY_CREDENTIAL_FILE_REQUIRED");
  if (value.size > maximumBytes) throw new Error("ALIPAY_CREDENTIAL_FILE_TOO_LARGE");
  return value;
}

// 取证书序列号的十六进制大写形式，即支付宝要求的 cert_sn。
function certificateSn(certificate: Certificate): string {
  const principalName = certificate.issuer.attributes
    .reduceRight((previous, current) => `${previous}${current.shortName}=${current.value},`, "")
    .slice(0, -1);
  const decimalSerial = BigInt(`0x${certificate.serialNumber}`).toString(10);
  return legacyMd5(principalName + decimalSerial);
}

// 把证书里的公钥导出成 PEM，用于校验私钥是否与之匹配。
function certificatePublicKeyPem(certificate: Certificate): string {
  const base64 = Buffer.from(certificate.publicKeyRaw).toString("base64");
  return `-----BEGIN PUBLIC KEY-----\n${base64.match(/.{1,64}/g)?.join("\n")}\n-----END PUBLIC KEY-----`;
}

// 校验证书当前处于有效期内，避免上传已过期或尚未生效的证书。
function validateCertificateTime(certificate: Certificate): void {
  const now = Date.now();
  if (certificate.validFrom.getTime() > now || certificate.validTo.getTime() < now) {
    throw new Error("ALIPAY_CERTIFICATE_EXPIRED");
  }
}

// 用私钥签一段固定文本、再用证书公钥验签，确认两者配对。
// 这一步能在上传当场拦住“私钥和证书不是一套”的常见错误，而不是等到账务查询失败才发现。
async function verifyPrivateKeyMatchesCertificate(privateKeyPem: string, publicKeyPem: string): Promise<void> {
  try {
    const privateKey = await importPrivateKey(privateKeyPem);
    const publicKey = await importPublicKey(publicKeyPem);
    const challenge = crypto.getRandomValues(new Uint8Array(32));
    const signature = await crypto.subtle.sign("RSASSA-PKCS1-v1_5", privateKey, challenge);
    if (!(await crypto.subtle.verify("RSASSA-PKCS1-v1_5", publicKey, signature, challenge))) {
      throw new Error("MISMATCH");
    }
  } catch {
    throw new Error("ALIPAY_PRIVATE_KEY_MISMATCH");
  }
}

// 导入 PKCS#8 PEM 私钥为 CryptoKey（仅签名、不可导出）。
async function importPrivateKey(value: string): Promise<CryptoKey> {
  return crypto.subtle.importKey(
    "pkcs8",
    pemToBytes(value),
    { name: "RSASSA-PKCS1-v1_5", hash: "SHA-256" },
    false,
    ["sign"],
  );
}

// 导入 SPKI PEM 公钥为 CryptoKey（仅验签）。
async function importPublicKey(value: string): Promise<CryptoKey> {
  return crypto.subtle.importKey(
    "spki",
    pemToBytes(value),
    { name: "RSASSA-PKCS1-v1_5", hash: "SHA-256" },
    false,
    ["verify"],
  );
}

// 由 SESSION_SECRET 派生 AES-GCM 密钥，派生上下文与其它模块不同，密钥不通用。
async function encryptionKey(secret: string): Promise<CryptoKey> {
  const material = await crypto.subtle.digest(
    "SHA-256",
    new TextEncoder().encode(`${ENCRYPTION_CONTEXT}\n${secret}`),
  );
  return crypto.subtle.importKey("raw", material, "AES-GCM", false, ["encrypt", "decrypt"]);
}

// 读取凭据行；表不存在（迁移未跑）时返回 null 而不是抛错，便于回退到环境变量配置。
async function getCredentialRow(env: Env): Promise<AlipayCredentialRow | null> {
  try {
    return runtimeFirst<AlipayCredentialRow>(
      env,
      "config:alipay-credential",
      "SELECT * FROM alipay_credential WHERE id = 1",
      [],
      { ttlMs: null, pinned: true },
    );
  } catch (error) {
    const message = error instanceof Error ? error.message : String(error);
    if (message.includes("no such table")) return null;
    throw error;
  }
}

// 从 Worker Secrets 组装凭据作为回退来源；六个变量缺任意一个就返回 null。
function environmentAlipayConfig(env: Env): AlipayRuntimeConfig | null {
  if (
    !env.ALIPAY_APP_ID ||
    !env.ALIPAY_APP_PRIVATE_KEY ||
    !env.ALIPAY_APP_CERT_SN ||
    !env.ALIPAY_ROOT_CERT_SN ||
    !env.ALIPAY_CERT_SN ||
    !env.ALIPAY_PUBLIC_KEY
  ) {
    return null;
  }
  return {
    appId: env.ALIPAY_APP_ID,
    appPrivateKey: env.ALIPAY_APP_PRIVATE_KEY,
    appCertSn: env.ALIPAY_APP_CERT_SN,
    alipayRootCertSn: env.ALIPAY_ROOT_CERT_SN,
    alipayCertSn: env.ALIPAY_CERT_SN,
    alipayPublicKey: env.ALIPAY_PUBLIC_KEY,
  };
}

// 解密后的结构校验：六个字段都必须是非空字符串，否则视为密文损坏。
function isRuntimeConfig(value: AlipayRuntimeConfig): boolean {
  return Boolean(
    value &&
      /^\d{16,32}$/.test(value.appId) &&
      value.appPrivateKey &&
      value.appCertSn &&
      value.alipayRootCertSn &&
      value.alipayCertSn &&
      value.alipayPublicKey,
  );
}

// 剥掉 PEM 头尾与空白后 base64 解码成 DER 字节。
function pemToBytes(value: string): ArrayBuffer {
  const base64 = value.replace(/-----BEGIN [^-]+-----|-----END [^-]+-----|\s+/g, "");
  return base64ToBytes(base64).buffer as ArrayBuffer;
}

// 字节转 base64，用于密文与 IV 的存储形式。
function bytesToBase64(value: Uint8Array): string {
  let binary = "";
  for (const byte of value) binary += String.fromCharCode(byte);
  return btoa(binary);
}

// base64 还原成字节。
function base64ToBytes(value: string): Uint8Array<ArrayBuffer> {
  const binary = atob(value);
  return Uint8Array.from(binary, (character) => character.charCodeAt(0));
}
