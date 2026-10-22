import { describe, expect, it } from "vitest";
import { describePublicError } from "../src/errors";

describe("public error descriptions", () => {
  it("explains the D1 daily write limit and reset time", () => {
    const result = describePublicError(new Error("D1_ERROR: Your account has exceeded D1's free tier daily row write limit."));
    expect(result).toMatchObject({ code: "D1_WRITE_LIMIT_EXCEEDED", status: 429, unknown: false });
    expect(result.message).toContain("北京时间每日 08:00");
  });

  it("identifies SQL parameter binding bugs", () => {
    const result = describePublicError(new Error("D1_ERROR: Wrong number of parameter bindings for SQL query."));
    expect(result).toMatchObject({ code: "D1_QUERY_PARAMETER_MISMATCH", status: 500, unknown: false });
    expect(result.message).toContain("参数数量不匹配");
  });

  it("returns an actionable Analytics permission message", () => {
    const result = describePublicError(new Error("CLOUDFLARE_ANALYTICS_FORBIDDEN"));
    expect(result).toMatchObject({ code: "CLOUDFLARE_ANALYTICS_FORBIDDEN", status: 403, unknown: false });
    expect(result.message).toContain("Account Analytics / Read");
  });

  it("marks unknown runtime failures for trace-id handling", () => {
    const result = describePublicError(new Error("something entirely unexpected"));
    expect(result).toMatchObject({ code: "UNKNOWN_ERROR", status: 500, unknown: true });
    expect(result.message).toContain("追踪编号");
  });

  it("returns a client error for truncated JSON bodies", () => {
    const result = describePublicError(new SyntaxError("Unexpected end of JSON input"));
    expect(result).toMatchObject({ code: "INVALID_JSON", status: 400, unknown: false });
  });
});
