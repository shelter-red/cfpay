import { DurableObject } from "cloudflare:workers";
import type {
  RuntimeCacheStats,
  RuntimeInvalidateRequest,
  RuntimePutRequest,
  RuntimeReadRequest,
} from "./runtime-data";
import type { Env } from "./types";

const MAX_CACHE_BYTES = 48 * 1024 * 1024;
const MAX_ENTRY_BYTES = 256 * 1024;
const DEFAULT_TTL_MS = 60_000;

interface CacheEntry {
  value: unknown;
  expiresAt: number | null;
  size: number;
  lastAccessAt: number;
  pinned: boolean;
}

export class RuntimeDataState extends DurableObject<Env> {
  private readonly cache = new Map<string, CacheEntry>();
  private estimatedBytes = 0;
  private hits = 0;
  private misses = 0;
  private d1Reads = 0;
  private evictions = 0;
  private readonly startedAt = Date.now();

  // 分发内部读穿、写穿、失效和统计请求；该 DO 没有公开外部路由。
  async fetch(request: Request): Promise<Response> {
    const url = new URL(request.url);
    if (request.method === "POST" && url.pathname === "/read") return this.handleRead(request);
    if (request.method === "GET" && url.pathname === "/cache") return this.handleGet(url);
    if (request.method === "PUT" && url.pathname === "/cache") return this.handlePut(request);
    if (request.method === "DELETE" && url.pathname === "/cache") return Response.json(this.clear());
    if (request.method === "POST" && url.pathname === "/invalidate") return this.handleInvalidate(request);
    if (request.method === "GET" && url.pathname === "/stats") return Response.json(this.stats());
    return new Response("Not Found", { status: 404 });
  }

  // 命中内存直接返回，未命中时执行一次参数化 D1 查询并缓存结果。
  private async handleRead(request: Request): Promise<Response> {
    const input = await request.json<RuntimeReadRequest>();
    this.validateKey(input.key);
    if (input.mode !== "first" && input.mode !== "all") return new Response("INVALID_READ_MODE", { status: 400 });
    const cached = this.read(input.key);
    if (cached.found) return Response.json({ value: cached.value, source: "memory" });
    this.misses += 1;
    this.d1Reads += 1;
    const statement = this.env.DB.prepare(input.sql).bind(...(input.bindings ?? []));
    const value = input.mode === "first" ? await statement.first() : (await statement.all()).results;
    this.write(input.key, value, input.ttlMs, input.pinned ?? false);
    return Response.json({ value, source: "d1" });
  }

  // 只查看已有缓存，供管理和组合视图使用。
  private handleGet(url: URL): Response {
    const key = url.searchParams.get("key") ?? "";
    this.validateKey(key);
    const cached = this.read(key);
    if (!cached.found) this.misses += 1;
    return Response.json(cached);
  }

  // 接收业务写入后的最新实体并写入内存。
  private async handlePut(request: Request): Promise<Response> {
    const input = await request.json<RuntimePutRequest>();
    this.validateKey(input.key);
    this.write(input.key, input.value, input.ttlMs, input.pinned ?? false);
    return Response.json({ ok: true });
  }

  // 删除精确键和指定前缀下的派生缓存。
  private async handleInvalidate(request: Request): Promise<Response> {
    const input = await request.json<RuntimeInvalidateRequest>();
    let removed = 0;
    for (const key of input.keys ?? []) removed += this.remove(key) ? 1 : 0;
    for (const prefix of input.prefixes ?? []) {
      for (const key of [...this.cache.keys()]) {
        if (key.startsWith(prefix)) removed += this.remove(key) ? 1 : 0;
      }
    }
    return Response.json({ ok: true, removed });
  }

  // 读取并刷新最近访问时间；过期条目视为未命中并立即删除。
  private read(key: string): { found: boolean; value: unknown | null } {
    const entry = this.cache.get(key);
    if (!entry) return { found: false, value: null };
    if (entry.expiresAt !== null && entry.expiresAt <= Date.now()) {
      this.remove(key);
      return { found: false, value: null };
    }
    entry.lastAccessAt = Date.now();
    this.hits += 1;
    return { found: true, value: entry.value };
  }

  // 写入条目并按 TTL、固定标记和容量预算维护缓存。
  private write(key: string, value: unknown, ttlMs: number | null | undefined, pinned: boolean): void {
    const size = new TextEncoder().encode(JSON.stringify(value)).byteLength;
    this.remove(key);
    if (size > MAX_ENTRY_BYTES && !pinned) return;
    const normalizedTtl = ttlMs === null ? null : Math.max(1_000, ttlMs ?? DEFAULT_TTL_MS);
    this.cache.set(key, {
      value,
      size,
      pinned,
      expiresAt: normalizedTtl === null ? null : Date.now() + normalizedTtl,
      lastAccessAt: Date.now(),
    });
    this.estimatedBytes += size;
    this.evict();
  }

  // 超过预算时优先淘汰最久未访问的非固定条目。
  private evict(): void {
    if (this.estimatedBytes <= MAX_CACHE_BYTES) return;
    const candidates = [...this.cache.entries()]
      .filter(([, entry]) => !entry.pinned)
      .sort((left, right) => left[1].lastAccessAt - right[1].lastAccessAt);
    for (const [key] of candidates) {
      if (this.estimatedBytes <= MAX_CACHE_BYTES) break;
      if (this.remove(key)) this.evictions += 1;
    }
  }

  // 删除一个条目并同步修正估算内存。
  private remove(key: string): boolean {
    const entry = this.cache.get(key);
    if (!entry) return false;
    this.estimatedBytes -= entry.size;
    this.cache.delete(key);
    return true;
  }

  // 清空全部条目但保留累计命中统计，方便观察清理后的回源变化。
  private clear(): RuntimeCacheStats {
    this.cache.clear();
    this.estimatedBytes = 0;
    return this.stats();
  }

  // 生成不包含任何业务内容和敏感值的缓存统计。
  private stats(): RuntimeCacheStats {
    return {
      entries: this.cache.size,
      estimatedBytes: this.estimatedBytes,
      budgetBytes: MAX_CACHE_BYTES,
      hits: this.hits,
      misses: this.misses,
      d1Reads: this.d1Reads,
      evictions: this.evictions,
      startedAt: this.startedAt,
    };
  }

  // 限制内部缓存键长度，避免畸形请求消耗异常内存。
  private validateKey(key: string): void {
    if (!key || key.length > 500) throw new Error("INVALID_RUNTIME_CACHE_KEY");
  }
}
