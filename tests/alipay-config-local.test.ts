import { existsSync, readFileSync } from "node:fs";
import { readFile } from "node:fs/promises";
import path from "node:path";
import { describe, expect, it } from "vitest";
import { encryptAlipayCredentials, parseAlipayCredentialUpload, resolveAlipayConfig } from "../src/alipay-config";
import { buildAlipayLedgerRequest } from "../src/alipay-request";
import type { Env } from "../src/types";

const configPath = path.resolve("config/alipay-test.local.json");
const secretsPath = path.resolve("config/alipay-worker-secrets.local.json");

// 入口配置存在还不够：真正的证书与私钥在 config/alipay/ 下，同样被 Git 忽略。
// 只检查入口文件会出现「配置在、证书不在」的半残状态——测试不跳过而是抛 ENOENT，
// 让任何一台没有本地证书的机器上 npm test 都是红的。私钥现在的权威副本在 D1，
// 本地证书只是可选的联调材料，缺了就整组跳过。
function localCredentialsAvailable(): boolean {
  if (!existsSync(configPath) || !existsSync(secretsPath)) return false;
  try {
    const local = JSON.parse(readFileSync(configPath, "utf8")) as { files?: Record<string, string> };
    const files = Object.values(local.files ?? {});
    return files.length > 0 && files.every((file) => existsSync(path.resolve("config", file)));
  } catch {
    return false;
  }
}

describe.skipIf(!localCredentialsAvailable())("local Alipay credential files", () => {
  it("matches the SDK-derived runtime configuration", async () => {
    const parsed = await parseLocalCredentialFiles();
    const expected = JSON.parse(await readFile(secretsPath, "utf8")) as Record<string, string>;
    expect(parsed.appId).toBe(expected.ALIPAY_APP_ID);
    expect(parsed.appCertSn).toBe(expected.ALIPAY_APP_CERT_SN);
    expect(parsed.alipayRootCertSn).toBe(expected.ALIPAY_ROOT_CERT_SN);
    expect(parsed.alipayCertSn).toBe(expected.ALIPAY_CERT_SN);
    expect(parsed.alipayPublicKey.replace(/\s+/g, "")).toBe(expected.ALIPAY_PUBLIC_KEY.replace(/\s+/g, ""));
    expect(parsed.appPrivateKey).toMatch(/^-----BEGIN PRIVATE KEY-----/);
  });

  it("decrypts D1 credentials and signs an Alipay request", async () => {
    const config = await parseLocalCredentialFiles();
    const secret = "local-integration-session-secret";
    const encrypted = await encryptAlipayCredentials(config, secret);
    const env = {
      SESSION_SECRET: secret,
      DB: {
        prepare: () => ({
          first: async () => ({
            app_id: config.appId,
            app_cert_sn: config.appCertSn,
            alipay_root_cert_sn: config.alipayRootCertSn,
            alipay_cert_sn: config.alipayCertSn,
            encrypted_payload: encrypted.ciphertext,
            encryption_iv: encrypted.iv,
            updated_at: Date.now(),
          }),
        }),
      },
    } as unknown as Env;

    const resolved = await resolveAlipayConfig(env);
    expect(resolved).not.toBeNull();
    const signedRequest = await buildAlipayLedgerRequest(resolved!);
    const headers = signedRequest.init.headers as Record<string, string>;
    expect(signedRequest.url).toContain("https://openapi.alipay.com/v3/alipay/data/bill/accountlog/query?");
    expect(headers.authorization).toContain(`app_id=${config.appId}`);
    expect(headers.authorization).toContain(`app_cert_sn=${config.appCertSn}`);
    expect(headers.authorization).toContain(",sign=");
    expect(headers["alipay-root-cert-sn"]).toBe(config.alipayRootCertSn);
  });
});

async function parseLocalCredentialFiles() {
  const local = JSON.parse(await readFile(configPath, "utf8")) as {
    appId: string;
    files: Record<string, string>;
  };
  const form = new FormData();
  form.set("appId", local.appId);
  for (const [field, key] of [
    ["appPrivateKey", "appPrivateKey"],
    ["appCert", "appCert"],
    ["alipayPublicCert", "alipayPublicCert"],
    ["alipayRootCert", "alipayRootCert"],
  ]) {
    const filePath = path.resolve("config", local.files[key]);
    form.set(field, new File([await readFile(filePath)], path.basename(filePath)));
  }
  return parseAlipayCredentialUpload(form);
}
