// 把查询串与表单/JSON 请求体合并成参数表，兼容易支付商户用 GET、POST 表单或 JSON 三种方式提交。
// JSON 中的数字和布尔值转成协议需要的字符串，null 视为空值；对象和数组不是易支付标量参数，直接拒绝。
export async function requestParams(request: Request): Promise<Record<string, string>> {
  const url = new URL(request.url);
  const params: Record<string, string> = Object.fromEntries(url.searchParams.entries());
  if (request.method !== "GET" && request.method !== "HEAD") {
    const contentType = request.headers.get("content-type") ?? "";
    if (contentType.includes("application/x-www-form-urlencoded") || contentType.includes("multipart/form-data")) {
      const body = await request.formData();
      for (const [key, value] of body.entries()) if (typeof value === "string") params[key] = value;
    } else if (contentType.includes("application/json") || contentType.includes("+json")) {
      const text = await request.text();
      if (text.trim()) {
        const body: unknown = JSON.parse(text);
        if (typeof body !== "object" || body === null || Array.isArray(body)) throw new Error("INVALID_PAYLOAD");
        for (const [key, value] of Object.entries(body)) {
          if (value === null) params[key] = "";
          else if (["string", "number", "boolean"].includes(typeof value)) params[key] = String(value);
          else throw new Error("INVALID_PAYLOAD");
        }
      }
    }
  }
  return params;
}
