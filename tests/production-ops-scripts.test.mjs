import { readFile } from "node:fs/promises";
import { describe, expect, it } from "vitest";

const auditPath = new URL("../tools/audit-production-readiness.ps1", import.meta.url);
const rotationPath = new URL("../tools/rotate-production-merchant-key.ps1", import.meta.url);

describe("production operations scripts", () => {
  it("keeps the ePay merchant key out of request URLs and errors", async () => {
    const source = await readFile(auditPath, "utf8");

    expect(source).toContain('-Path "/api.php" -FormBody $epayForm');
    expect(source).not.toMatch(/\/api\.php\?[^"\r\n]*key=/);
    expect(source).not.toContain('throw "$Method $Path');
    expect(source).toContain("alipayClosedLoopEvidence");
    expect(source).toContain("callbackSuccessOrderCount");
  });

  it("rotates keys with a cryptographic RNG without printing them", async () => {
    const source = await readFile(rotationPath, "utf8");

    expect(source).toContain("RandomNumberGenerator");
    expect(source).toContain("KEY_FINGERPRINT=");
    expect(source).not.toMatch(/Write-Output\s+\$newMerchantKey/);
    expect(source).not.toMatch(/MERCHANT_KEY=(?!ROTATED)/);
  });
});
