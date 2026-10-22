import { chmod, writeFile } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { createAlipayClient } from "./lib/alipay-client.mjs";

const rootDirectory = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const targetPath = path.join(rootDirectory, "config/alipay-worker-secrets.local.json");
const { sdk, config } = await createAlipayClient();
const secrets = {
  ALIPAY_APP_ID: config.appId,
  ALIPAY_APP_PRIVATE_KEY: sdk.config.privateKey,
  ALIPAY_APP_CERT_SN: sdk.config.appCertSn,
  ALIPAY_ROOT_CERT_SN: sdk.config.alipayRootCertSn,
  ALIPAY_CERT_SN: sdk.config.alipayCertSn,
  ALIPAY_PUBLIC_KEY: sdk.config.alipayPublicKey,
  ALIPAY_ACTIVE_POLL_SECONDS: "2",
};

for (const [key, value] of Object.entries(secrets)) {
  if (!value) throw new Error(`无法生成 ${key}`);
}

await writeFile(targetPath, `${JSON.stringify(secrets, null, 2)}\n`, { encoding: "utf8", mode: 0o600 });
await chmod(targetPath, 0o600);
console.log(`已生成 ${targetPath}`);
console.log("部署 Worker 后执行：npx wrangler secret bulk config/alipay-worker-secrets.local.json");
