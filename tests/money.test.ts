import { describe, expect, it } from "vitest";
import { formatFen, parseMoneyToFen } from "../src/lib/money";

describe("money helpers", () => {
  it("parses decimal money without floating point", () => {
    expect(parseMoneyToFen("1")).toBe(100);
    expect(parseMoneyToFen("1.2")).toBe(120);
    expect(parseMoneyToFen("1.23")).toBe(123);
  });

  it("rejects unsupported precision", () => {
    expect(() => parseMoneyToFen("1.001")).toThrow("INVALID_MONEY");
    expect(() => parseMoneyToFen("0")).toThrow("INVALID_MONEY");
  });

  it("formats fen", () => {
    expect(formatFen(123)).toBe("1.23");
  });
});
