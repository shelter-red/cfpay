import { toast } from "vue-sonner";

const REQUEST_TIMEOUT_MS = 15_000;

export class ApiError extends Error {
  readonly status: number;

  constructor(message: string, status = 0) {
    super(message);
    this.name = "ApiError";
    this.status = status;
  }
}

type ApiOptions = Omit<RequestInit, "body"> & { body?: BodyInit | null };

/**
 * 统一的后端调用封装：JSON 请求自动带 content-type，FormData 交给浏览器生成 boundary；
 * 15 秒超时；非 2xx 时把后端返回的 message / msg 抛成 ApiError，避免前端只剩“请求失败”。
 */
export async function api<T = unknown>(path: string, options: ApiOptions = {}): Promise<T> {
  const extraHeaders = (options.headers ?? {}) as Record<string, string>;
  const headers =
    options.body instanceof FormData
      ? { ...extraHeaders }
      : { "content-type": "application/json", ...extraHeaders };

  const controller = new AbortController();
  const timeout = setTimeout(() => controller.abort(), REQUEST_TIMEOUT_MS);
  let response: Response;
  try {
    response = await fetch(path, { ...options, headers, signal: controller.signal });
  } catch (error) {
    const aborted = error instanceof Error && error.name === "AbortError";
    throw new ApiError(
      aborted
        ? "请求超时，请检查网络后重试"
        : navigator.onLine
          ? "无法连接服务器，请稍后重试"
          : "网络已断开，请检查连接后重试",
    );
  } finally {
    clearTimeout(timeout);
  }

  const data = (await response.json().catch(() => ({ message: `服务器响应格式异常（HTTP ${response.status}），请稍后重试` }))) as Record<string, unknown>;
  if (!response.ok) {
    const message =
      typeof data.message === "string" && data.message
        ? data.message
        : typeof data.msg === "string" && data.msg
          ? data.msg
          : `服务器返回 HTTP ${response.status}，但未提供错误原因`;
    throw new ApiError(message, response.status);
  }
  return data as T;
}

/** 下载后端生成的附件，并沿用统一的超时和中文错误解析。 */
export async function downloadFile(path: string, options: ApiOptions = {}): Promise<void> {
  const controller = new AbortController();
  const timeout = setTimeout(() => controller.abort(), REQUEST_TIMEOUT_MS);
  let response: Response;
  try {
    response = await fetch(path, {
      ...options,
      headers: { "content-type": "application/json", ...((options.headers ?? {}) as Record<string, string>) },
      signal: controller.signal,
    });
  } catch (error) {
    const aborted = error instanceof Error && error.name === "AbortError";
    throw new ApiError(aborted ? "导出请求超时，请稍后重试" : "无法连接服务器，请稍后重试");
  } finally {
    clearTimeout(timeout);
  }
  if (!response.ok) {
    const data = await response.json().catch(() => ({})) as Record<string, unknown>;
    throw new ApiError(typeof data.message === "string" ? data.message : `导出失败（HTTP ${response.status}）`, response.status);
  }
  const disposition = response.headers.get("content-disposition") ?? "";
  const filename = disposition.match(/filename="([^"]+)"/)?.[1] ?? `cfpay-export-${Date.now()}`;
  const url = URL.createObjectURL(await response.blob());
  const anchor = document.createElement("a");
  anchor.href = url;
  anchor.download = filename;
  document.body.append(anchor);
  anchor.click();
  anchor.remove();
  URL.revokeObjectURL(url);
}

// 把任意异常压成可展示的中文文本。
export function errorMessage(error: unknown): string {
  return error instanceof Error ? error.message : "发生未识别的客户端错误，请刷新后重试";
}

// 统一用 toast 弹出错误，避免每个页面各写一遍提示逻辑。
export function notifyError(error: unknown): void {
  toast.error(errorMessage(error));
}
