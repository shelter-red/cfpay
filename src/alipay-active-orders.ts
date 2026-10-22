import { runtimeFirst, runtimeInvalidate } from "./runtime-data";
import type { Env } from "./types";

// 统计仍处于待支付的支付宝订单数，用来决定是否需要继续轮询。
// 固定缓存键可能保存最早到期的订单；该订单自然过期后必须失效并立即回源，
// 否则后面仍有效的支付宝订单会因为第一次缓存结果过期而错误停止轮询。
export async function pendingAlipayOrders(env: Env, now = Date.now()): Promise<number> {
  const readActiveOrder = () => runtimeFirst<{ id: string; expires_at: number }>(
    env,
    "view:alipay:active-order",
    "SELECT id, expires_at FROM payment_order WHERE provider = 'alipay' AND status = 'PENDING' AND expires_at > ? ORDER BY expires_at ASC LIMIT 1",
    [now],
    { ttlMs: null },
  );
  const row = await readActiveOrder();
  if (!row) return 0;
  if (row.expires_at > now) return 1;
  await runtimeInvalidate(env, { keys: ["view:alipay:active-order"] });
  return (await readActiveOrder()) ? 1 : 0;
}
