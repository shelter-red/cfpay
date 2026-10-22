import type { Env } from "./types";
import { runtimeAll, runtimeFirst } from "./runtime-data";

/**
 * 运营面板的数据来源。
 *
 * 口径说明：面板上的「今日」一律按东八区自然日计算，而不是 UTC。
 * D1 里存的是 UTC 毫秒时间戳，若直接按 UTC 切日，早上 8 点前的收款会被算进昨天，
 * 与商户自己对账的口径对不上。东八区没有夏令时，因此固定偏移 8 小时即可。
 */
const UTC_OFFSET_MS = 8 * 60 * 60 * 1000;

export type DashboardTrendRange = 7 | 30 | 90;

export interface DashboardOrderTrendPoint {
  date: string;
  timestamp: number;
  createdOrders: number;
  paidOrders: number;
}

export interface DashboardData {
  metrics: {
    todayOrders: number;
    todayPaidFen: number;
    totalPaidFen: number;
    pendingNotify: number;
    failedNotify: number;
    unmatched: number;
    staleUnmatched: number;
  };
  orderTrend: DashboardOrderTrendPoint[];
}

// 取某个时间戳所属的东八区自然日，返回 YYYY-MM-DD。
export function dayKeyOf(timestamp: number): string {
  return new Date(timestamp + UTC_OFFSET_MS).toISOString().slice(0, 10);
}

// 取某个东八区自然日 00:00 对应的 UTC 毫秒时间戳，用于把查询范围压到最近 N 天。
export function dayStartMs(dayKey: string): number {
  return Date.parse(`${dayKey}T00:00:00Z`) - UTC_OFFSET_MS;
}

// 把 YYYY-MM-DD 往前或往后推若干天，跨月跨年都由 UTC 构造保证正确。
function shiftDayKey(dayKey: string, days: number): string {
  const [year, month, day] = dayKey.split("-").map(Number);
  return new Date(Date.UTC(year!, month! - 1, day! + days)).toISOString().slice(0, 10);
}

interface OrderAggregateRow {
  total_orders: number | null;
  paid_orders: number | null;
  paid_fen: number | null;
  pending_notify: number | null;
  failed_notify: number | null;
  today_orders: number | null;
  today_paid_fen: number | null;
}

// 按自然日把订单归并成趋势点：先铺满 N 天的零值，再累加实际订单，
// 没有订单的日期也要有点，否则折线会在空档处直接连过去，看不出停摆的那天。
export function buildOrderTrend(
  rows: Array<{ created_at: number; status: string }>,
  now = Date.now(),
  days: DashboardTrendRange = 90,
): DashboardOrderTrendPoint[] {
  const today = dayKeyOf(now);
  const points = new Map<string, DashboardOrderTrendPoint>();
  for (let offset = -(days - 1); offset <= 0; offset += 1) {
    const date = shiftDayKey(today, offset);
    points.set(date, { date, timestamp: dayStartMs(date), createdOrders: 0, paidOrders: 0 });
  }
  for (const row of rows) {
    const point = points.get(dayKeyOf(row.created_at));
    if (!point) continue;
    point.createdOrders += 1;
    if (row.status === "PAID") point.paidOrders += 1;
  }
  return [...points.values()];
}

// 汇总面板数据：一次聚合查询取订单指标，另外取未匹配收款数和订单趋势。
export async function getDashboardData(env: Env, now = Date.now()): Promise<DashboardData> {
  const todayStart = dayStartMs(dayKeyOf(now));
  const trendStart = dayStartMs(shiftDayKey(dayKeyOf(now), -89));
  const [aggregate, unmatched, trendRows] = await Promise.all([
    runtimeFirst<OrderAggregateRow>(
      env,
      `view:dashboard:aggregate:${dayKeyOf(now)}`,
      `SELECT
         COUNT(*) AS total_orders,
         SUM(CASE WHEN o.status = 'PAID' THEN 1 ELSE 0 END) AS paid_orders,
         SUM(CASE WHEN o.status = 'PAID' THEN o.payable_amount_fen ELSE 0 END) AS paid_fen,
         SUM(CASE WHEN o.status = 'PAID' AND o.notify_status IN ('PENDING', 'RETRYING') THEN 1 ELSE 0 END) AS pending_notify,
         SUM(CASE WHEN o.status = 'PAID' AND o.notify_status = 'FAILED' AND j.handled_at IS NULL THEN 1 ELSE 0 END) AS failed_notify,
         SUM(CASE WHEN o.created_at >= ? THEN 1 ELSE 0 END) AS today_orders,
         SUM(CASE WHEN o.status = 'PAID' AND COALESCE(o.paid_at, o.created_at) >= ? THEN o.payable_amount_fen ELSE 0 END)
           AS today_paid_fen
       FROM payment_order o LEFT JOIN callback_job j ON j.order_id = o.id`,
      [todayStart, todayStart],
      { ttlMs: 60_000 },
    ),
    runtimeFirst<{ unmatched: number | null; stale_unmatched: number | null }>(
      env,
      "view:dashboard:unmatched",
      `SELECT COUNT(*) AS unmatched,
              SUM(CASE WHEN created_at <= ? THEN 1 ELSE 0 END) AS stale_unmatched
       FROM payment_event WHERE status = 'UNMATCHED'`,
      [now - 24 * 60 * 60_000],
      { ttlMs: 60_000 },
    ),
    runtimeAll<{ created_at: number; status: string }>(
      env,
      `view:dashboard:trend:${dayKeyOf(now)}`,
      "SELECT created_at, status FROM payment_order WHERE created_at >= ?",
      [trendStart],
      { ttlMs: 60_000 },
    ),
  ]);

  return {
    metrics: {
      todayOrders: aggregate?.today_orders ?? 0,
      todayPaidFen: aggregate?.today_paid_fen ?? 0,
      totalPaidFen: aggregate?.paid_fen ?? 0,
      pendingNotify: aggregate?.pending_notify ?? 0,
      failedNotify: aggregate?.failed_notify ?? 0,
      unmatched: unmatched?.unmatched ?? 0,
      staleUnmatched: unmatched?.stale_unmatched ?? 0,
    },
    orderTrend: buildOrderTrend(trendRows, now),
  };
}
