import type { Env } from "./types";

const RUNTIME_DATA_NAME = "single-merchant-runtime-data";
type ReadMode = "first" | "all";

export interface RuntimeReadRequest {
  key: string;
  sql: string;
  bindings?: unknown[];
  mode: ReadMode;
  ttlMs?: number | null;
  pinned?: boolean;
}

export interface RuntimePutRequest {
  key: string;
  value: unknown;
  ttlMs?: number | null;
  pinned?: boolean;
}

export interface RuntimeInvalidateRequest {
  keys?: string[];
  prefixes?: string[];
}

export interface RuntimeCacheStats {
  entries: number;
  estimatedBytes: number;
  budgetBytes: number;
  hits: number;
  misses: number;
  d1Reads: number;
  evictions: number;
  startedAt: number;
}

function runtimeStub(env: Env): DurableObjectStub {
  return env.RUNTIME_DATA.get(env.RUNTIME_DATA.idFromName(RUNTIME_DATA_NAME));
}

// 判断当前环境是否已经绑定运行缓存；单元测试旧桩没有该绑定时直接回退 D1。
function hasRuntimeData(env: Env): boolean {
  return Boolean((env as Partial<Env>).RUNTIME_DATA);
}

// 统一解析内部 DO 响应，并把非成功状态转换为稳定异常。
async function requireJson<T>(responseInput: Response | Promise<Response>): Promise<T> {
  const response = await responseInput;
  if (response.ok) return response.json<T>();
  const body = await response.text();
  throw new Error(body || `RUNTIME_DATA_HTTP_${response.status}`);
}

// 读取一行数据：先查运行缓存，未命中时由 DO 查询 D1 并回填。
export async function runtimeFirst<T>(
  env: Env,
  key: string,
  sql: string,
  bindings: unknown[] = [],
  options: { ttlMs?: number | null; pinned?: boolean } = {},
): Promise<T | null> {
  if (!hasRuntimeData(env)) {
    const statement = env.DB.prepare(sql);
    return (bindings.length ? statement.bind(...bindings) : statement).first<T>();
  }
  const response = await runtimeStub(env).fetch("https://runtime-data/read", {
    method: "POST",
    headers: { "content-type": "application/json" },
    body: JSON.stringify({ key, sql, bindings, mode: "first", ...options } satisfies RuntimeReadRequest),
  });
  const payload = await requireJson<{ value: T | null }>(response);
  return payload.value;
}

// 读取多行数据：列表和统计结果同样按业务键缓存。
export async function runtimeAll<T>(
  env: Env,
  key: string,
  sql: string,
  bindings: unknown[] = [],
  options: { ttlMs?: number | null; pinned?: boolean } = {},
): Promise<T[]> {
  if (!hasRuntimeData(env)) {
    const statement = env.DB.prepare(sql);
    return (await (bindings.length ? statement.bind(...bindings) : statement).all<T>()).results;
  }
  const response = await runtimeStub(env).fetch("https://runtime-data/read", {
    method: "POST",
    headers: { "content-type": "application/json" },
    body: JSON.stringify({ key, sql, bindings, mode: "all", ...options } satisfies RuntimeReadRequest),
  });
  const payload = await requireJson<{ value: T[] }>(response);
  return payload.value;
}

// 主动写入一个内存实体，用于 D1 写成功后的写穿同步。
export async function runtimePut<T>(
  env: Env,
  key: string,
  value: T,
  options: { ttlMs?: number | null; pinned?: boolean } = {},
): Promise<void> {
  if (!hasRuntimeData(env)) return;
  await requireJson(
    runtimeStub(env).fetch("https://runtime-data/cache", {
      method: "PUT",
      headers: { "content-type": "application/json" },
      body: JSON.stringify({ key, value, ...options } satisfies RuntimePutRequest),
    }),
  );
}

// 按精确键或前缀失效缓存，供业务写入后清理派生视图。
export async function runtimeInvalidate(
  env: Env,
  input: RuntimeInvalidateRequest,
): Promise<void> {
  if (!hasRuntimeData(env)) return;
  await requireJson(
    runtimeStub(env).fetch("https://runtime-data/invalidate", {
      method: "POST",
      headers: { "content-type": "application/json" },
      body: JSON.stringify(input),
    }),
  );
}

// 返回运行缓存命中、回源、内存和淘汰统计。
export async function getRuntimeCacheStats(env: Env): Promise<RuntimeCacheStats> {
  if (!hasRuntimeData(env)) {
    return {
      entries: 0,
      estimatedBytes: 0,
      budgetBytes: 48 * 1024 * 1024,
      hits: 0,
      misses: 0,
      d1Reads: 0,
      evictions: 0,
      startedAt: Date.now(),
    };
  }
  return requireJson(runtimeStub(env).fetch("https://runtime-data/stats"));
}

// 清空运行缓存但不删除 D1 持久数据。
export async function clearRuntimeCache(env: Env): Promise<RuntimeCacheStats> {
  if (!hasRuntimeData(env)) return getRuntimeCacheStats(env);
  return requireJson(runtimeStub(env).fetch("https://runtime-data/cache", { method: "DELETE" }));
}
