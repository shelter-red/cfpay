import { describe, expect, it } from "vitest";
import {
  decryptAlipayCredentials,
  encryptAlipayCredentials,
  normalizePrivateKey,
  type AlipayRuntimeConfig,
} from "../src/alipay-config";

const config: AlipayRuntimeConfig = {
  appId: "2021000000000000",
  appPrivateKey: "-----BEGIN PRIVATE KEY-----\ntest\n-----END PRIVATE KEY-----",
  appCertSn: "app-sn",
  alipayRootCertSn: "root-sn",
  alipayCertSn: "alipay-sn",
  alipayPublicKey: "-----BEGIN PUBLIC KEY-----\ntest\n-----END PUBLIC KEY-----",
};

describe("alipay credential encryption", () => {
  it("round trips encrypted credentials and binds them to SESSION_SECRET", async () => {
    const encrypted = await encryptAlipayCredentials(config, "session-secret-1");
    expect(encrypted.ciphertext).not.toContain(config.appId);
    await expect(decryptAlipayCredentials(encrypted.ciphertext, encrypted.iv, "session-secret-1")).resolves.toEqual(config);
    await expect(decryptAlipayCredentials(encrypted.ciphertext, encrypted.iv, "session-secret-2")).rejects.toThrow(
      "ALIPAY_CREDENTIAL_DECRYPT_FAILED",
    );
  });

  it("normalizes raw PKCS8 base64 into PEM", () => {
    const raw = "A".repeat(512);
    expect(normalizePrivateKey(raw)).toMatch(/^-----BEGIN PRIVATE KEY-----\n/);
    expect(() => normalizePrivateKey("short")).toThrow("INVALID_ALIPAY_PRIVATE_KEY");
  });
});
