import { describe, expect, it } from "vitest";
import { publicHealthPayload } from "../src/health";

describe("public health response", () => {
  it("does not disclose service or release metadata", () => {
    expect(publicHealthPayload()).toEqual({ ok: true });
    expect(publicHealthPayload()).not.toHaveProperty("service");
    expect(publicHealthPayload()).not.toHaveProperty("version");
    expect(publicHealthPayload()).not.toHaveProperty("now");
  });
});
