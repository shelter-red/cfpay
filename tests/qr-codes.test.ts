import { describe, expect, it } from "vitest";
import { renderQrCodeSvg, validateQrPayload } from "../src/qr-codes";

describe("payment QR codes", () => {
  it("accepts common payment QR payload schemes", () => {
    expect(validateQrPayload("wxp://f2f-test-value", "wxpay")).toBe("wxp://f2f-test-value");
    expect(validateQrPayload("https://qr.alipay.com/test-value", "alipay")).toBe(
      "https://qr.alipay.com/test-value",
    );
  });

  it("rejects invalid content and obvious channel mismatches", () => {
    expect(() => validateQrPayload("plain text", "wxpay")).toThrow("INVALID_QR_PAYLOAD");
    expect(() => validateQrPayload("https://qr.alipay.com/test", "wxpay")).toThrow("QR_PROVIDER_MISMATCH");
    expect(() => validateQrPayload("wxp://f2f-test", "alipay")).toThrow("QR_PROVIDER_MISMATCH");
  });

  it("renders a fresh SVG without embedding the payload as markup", async () => {
    const payload = "wxp://f2f-test-value";
    const svg = await renderQrCodeSvg(payload);
    expect(svg).toContain("<svg");
    expect(svg).toContain("<path");
    expect(svg).not.toContain(payload);
  });
});
