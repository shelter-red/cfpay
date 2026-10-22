import { describe, expect, it } from "vitest";
import { createEpaySign } from "../src/lib/crypto";

describe("ePay signing", () => {
  it("sorts non-empty parameters and excludes signature fields", () => {
    const params = {
      type: "alipay",
      pid: "1000",
      money: "1.00",
      empty: "",
      sign: "ignored",
      sign_type: "MD5",
    };
    expect(createEpaySign(params, "secret")).toBe("5a05d9abf848f542b6e2c3208464bfa3");
  });
});
