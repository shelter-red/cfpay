import { describe, expect, it } from "vitest";
import { requestParams } from "../src/epay-request";

describe("易支付请求参数解析", () => {
  it("合并查询串和表单参数", async () => {
    const request = new Request("https://pay.example/api.php?pid=1000", {
      method: "POST",
      headers: { "content-type": "application/x-www-form-urlencoded" },
      body: new URLSearchParams({ out_trade_no: "form-order", money: "0.01" }),
    });

    await expect(requestParams(request)).resolves.toMatchObject({
      pid: "1000",
      out_trade_no: "form-order",
      money: "0.01",
    });
  });

  it("解析 JSON 请求中的字符串、数字和布尔值", async () => {
    const request = new Request("https://pay.example/mapi.php", {
      method: "POST",
      headers: { "content-type": "application/json; charset=utf-8" },
      body: JSON.stringify({ pid: 1000, money: 0.01, device: true, param: null }),
    });

    await expect(requestParams(request)).resolves.toEqual({
      pid: "1000",
      money: "0.01",
      device: "true",
      param: "",
    });
  });

  it("拒绝 JSON 数组和嵌套对象", async () => {
    const arrayRequest = new Request("https://pay.example/api.php", {
      method: "POST",
      headers: { "content-type": "application/json" },
      body: "[]",
    });
    const nestedRequest = new Request("https://pay.example/api.php", {
      method: "POST",
      headers: { "content-type": "application/json" },
      body: JSON.stringify({ extra: { invalid: true } }),
    });

    await expect(requestParams(arrayRequest)).rejects.toThrow("INVALID_PAYLOAD");
    await expect(requestParams(nestedRequest)).rejects.toThrow("INVALID_PAYLOAD");
  });
});
