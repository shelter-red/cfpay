import type { Env } from "./types";

const GRAPHQL_ENDPOINT = "https://api.cloudflare.com/client/v4/graphql";
const CACHE_TTL_MS = 5 * 60 * 1000;
const FREE_ROWS_READ_LIMIT = 5_000_000;
const FREE_ROWS_WRITTEN_LIMIT = 100_000;
const FREE_STORAGE_LIMIT_BYTES = 5_000_000_000;

type UsageLevel = "normal" | "warning" | "danger" | "exhausted" | "unlimited";

interface D1AnalyticsGroup {
  count: number;
  dimensions: { date: string };
  sum: { rowsRead: number; rowsWritten: number };
}

interface D1StorageGroup {
  dimensions: { databaseId: string; datetimeHour: string };
  max: { databaseSizeBytes: number };
}

interface GraphqlResponse {
  data?: {
    viewer?: {
      accounts?: Array<{
        d1AnalyticsAdaptiveGroups?: D1AnalyticsGroup[];
        d1StorageAdaptiveGroups?: D1StorageGroup[];
      }>;
    };
  };
  errors?: Array<{ message?: string }> | null;
}

interface D1UsageMetric {
  used: number;
  limit: number | null;
  ratio: number | null;
  level: UsageLevel;
}

interface D1UsageDay {
  date: string;
  rowsRead: number;
  rowsWritten: number;
  queries: number;
}

export interface D1UsageData {
  configured: true;
  plan: "free" | "paid" | "unknown";
  limitSource: "configured" | "unavailable";
  timezone: "UTC";
  resetAt: string;
  fetchedAt: number;
  cacheExpiresAt: number;
  today: {
    date: string;
    rowsRead: D1UsageMetric;
    rowsWritten: D1UsageMetric;
    queries: number;
  };
  storage: D1UsageMetric;
  trend: D1UsageDay[];
}

let usageCache: { key: string; expiresAt: number; data: D1UsageData } | null = null;

const D1_USAGE_QUERY = `
query D1Usage($accountTag: String!, $startDate: Date!, $endDate: Date!, $startTime: Time!, $endTime: Time!) {
  viewer {
    accounts(filter: { accountTag: $accountTag }) {
      d1AnalyticsAdaptiveGroups(
        limit: 10000
        filter: { date_geq: $startDate, date_leq: $endDate }
        orderBy: [date_ASC]
      ) {
        count
        dimensions { date }
        sum { rowsRead rowsWritten }
      }
      d1StorageAdaptiveGroups(
        limit: 10000
        filter: { datetime_geq: $startTime, datetime_leq: $endTime }
        orderBy: [datetimeHour_DESC]
      ) {
        dimensions { databaseId datetimeHour }
        max { databaseSizeBytes }
      }
    }
  }
}`;

// 计算下一个 UTC 00:00；D1 免费额度按这个时间点重置，北京时间对应次日 08:00。
function nextUtcMidnight(now: Date): string {
  return new Date(Date.UTC(now.getUTCFullYear(), now.getUTCMonth(), now.getUTCDate() + 1)).toISOString();
}

// 把 UTC 日期向前移动指定天数，生成 GraphQL 查询起点和趋势空桶。
function shiftUtcDate(date: string, days: number): string {
  const timestamp = Date.parse(`${date}T00:00:00Z`);
  return new Date(timestamp + days * 86_400_000).toISOString().slice(0, 10);
}

// 根据 70%、90%、100% 三档阈值生成统一预警级别；付费计划没有日硬上限时标记 unlimited。
function usageMetric(used: number, limit: number | null): D1UsageMetric {
  if (limit === null) return { used, limit, ratio: null, level: "unlimited" };
  const ratio = limit > 0 ? used / limit : 0;
  const level: UsageLevel = ratio >= 1 ? "exhausted" : ratio >= 0.9 ? "danger" : ratio >= 0.7 ? "warning" : "normal";
  return { used, limit, ratio, level };
}

// 汇总 GraphQL 的日数据为指定数量的 UTC 自然日，缺失日期补零。
function buildTrend(groups: D1AnalyticsGroup[], today: string, dayCount: number): D1UsageDay[] {
  const days = new Map<string, D1UsageDay>();
  for (let offset = -(dayCount - 1); offset <= 0; offset += 1) {
    const date = shiftUtcDate(today, offset);
    days.set(date, { date, rowsRead: 0, rowsWritten: 0, queries: 0 });
  }
  for (const group of groups) {
    const day = days.get(group.dimensions.date);
    if (!day) continue;
    day.rowsRead += Number(group.sum.rowsRead || 0);
    day.rowsWritten += Number(group.sum.rowsWritten || 0);
    day.queries += Number(group.count || 0);
  }
  return [...days.values()];
}

// 调用 Cloudflare GraphQL Analytics API；错误统一转换成稳定错误码，令牌和查询内容不会回显。
async function requestUsage(env: Env, now: Date): Promise<D1UsageData> {
  const accountId = env.CLOUDFLARE_ACCOUNT_ID?.trim();
  const token = env.CLOUDFLARE_ANALYTICS_TOKEN?.trim();
  if (!accountId || !token) throw new Error("CLOUDFLARE_ANALYTICS_NOT_CONFIGURED");

  const today = now.toISOString().slice(0, 10);
  const startDate = shiftUtcDate(today, -29);
  const startTime = `${startDate}T00:00:00.000Z`;
  let response: Response;
  try {
    response = await fetch(GRAPHQL_ENDPOINT, {
      method: "POST",
      headers: { authorization: `Bearer ${token}`, "content-type": "application/json" },
      body: JSON.stringify({
        query: D1_USAGE_QUERY,
        variables: {
          accountTag: accountId,
          startDate,
          endDate: today,
          startTime,
          endTime: now.toISOString(),
        },
      }),
    });
  } catch {
    throw new Error("CLOUDFLARE_ANALYTICS_UNAVAILABLE");
  }
  if (response.status === 401 || response.status === 403) throw new Error("CLOUDFLARE_ANALYTICS_FORBIDDEN");
  if (!response.ok) throw new Error("CLOUDFLARE_ANALYTICS_UNAVAILABLE");

  const payload = await response.json<GraphqlResponse>();
  if (payload.errors?.length) {
    const message = payload.errors.map((item) => item.message ?? "").join(" ");
    if (/permission|access|auth/i.test(message)) throw new Error("CLOUDFLARE_ANALYTICS_FORBIDDEN");
    if (/cannot request a time range wider/i.test(message)) throw new Error("CLOUDFLARE_ANALYTICS_RANGE_TOO_WIDE");
    throw new Error(`CLOUDFLARE_ANALYTICS_QUERY_FAILED:${message.slice(0, 500)}`);
  }
  const account = payload.data?.viewer?.accounts?.[0];
  if (!account) throw new Error("CLOUDFLARE_ANALYTICS_ACCOUNT_NOT_FOUND");

  const plan = env.CLOUDFLARE_D1_PLAN === "free" || env.CLOUDFLARE_D1_PLAN === "paid" ? env.CLOUDFLARE_D1_PLAN : "unknown";
  const limitSource = plan === "unknown" ? "unavailable" : "configured";
  const dailyLimit = plan === "free" ? { rowsRead: FREE_ROWS_READ_LIMIT, rowsWritten: FREE_ROWS_WRITTEN_LIMIT, storage: FREE_STORAGE_LIMIT_BYTES } : null;
  const trend = buildTrend(account.d1AnalyticsAdaptiveGroups ?? [], today, 30);
  const current = trend.at(-1) ?? { date: today, rowsRead: 0, rowsWritten: 0, queries: 0 };
  const latestStorageByDatabase = new Map<string, { hour: string; bytes: number }>();
  for (const row of account.d1StorageAdaptiveGroups ?? []) {
    const databaseId = row.dimensions.databaseId;
    const latest = latestStorageByDatabase.get(databaseId);
    if (!latest || row.dimensions.datetimeHour > latest.hour) {
      latestStorageByDatabase.set(databaseId, { hour: row.dimensions.datetimeHour, bytes: Number(row.max.databaseSizeBytes ?? 0) });
    }
  }
  const storageBytes = [...latestStorageByDatabase.values()].reduce((total, row) => total + row.bytes, 0);
  const fetchedAt = now.getTime();
  return {
    configured: true,
    plan,
    limitSource,
    timezone: "UTC",
    resetAt: nextUtcMidnight(now),
    fetchedAt,
    cacheExpiresAt: fetchedAt + CACHE_TTL_MS,
    today: {
      date: current.date,
      rowsRead: usageMetric(current.rowsRead, dailyLimit?.rowsRead ?? null),
      rowsWritten: usageMetric(current.rowsWritten, dailyLimit?.rowsWritten ?? null),
      queries: current.queries,
    },
    storage: usageMetric(storageBytes, dailyLimit?.storage ?? null),
    trend,
  };
}

// 读取 D1 用量并缓存五分钟；缓存只存在 Worker isolate 内存，不占用 D1 写入额度。
// 管理员主动刷新时可跳过本地缓存，普通页面进入仍复用五分钟结果。
export async function getD1Usage(env: Env, now = new Date(), fresh = false): Promise<D1UsageData> {
  const key = `${env.CLOUDFLARE_ACCOUNT_ID ?? ""}:${env.CLOUDFLARE_D1_PLAN ?? "unknown"}`;
  if (!fresh && usageCache?.key === key && usageCache.expiresAt > now.getTime()) return usageCache.data;
  const data = await requestUsage(env, now);
  usageCache = { key, expiresAt: data.cacheExpiresAt, data };
  return data;
}
