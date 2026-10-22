import { getSettings } from "./db";
import { truncate, validatePublicHttpUrl } from "./lib/http";
import { appendParamsToUrl, buildEpayCallbackParams, cacheOrder, invalidateOrderViews } from "./orders";
import { runtimeFirst, runtimeInvalidate } from "./runtime-data";
import type { CallbackJob, Env, PaymentOrder } from "./types";

const RETRY_DELAYS_MS = [0, 60_000, 5 * 60_000, 15 * 60_000, 30 * 60_000, 60 * 60_000];
const PROCESSING_LEASE_MS = 30_000;

// 由每分钟 Cron 调用：取出一批到期的回调任务逐个投递，返回本次处理条数。
export async function processDueCallbacks(env: Env, limit = 20): Promise<number> {
  const now = Date.now();
  const jobs = await env.DB.prepare(
    `SELECT id, status FROM callback_job
     WHERE (status = 'PENDING' AND next_attempt_at <= ?)
        OR (status = 'PROCESSING' AND lease_until <= ? AND attempt_count < max_attempts)
     ORDER BY CASE WHEN status = 'PROCESSING' THEN lease_until ELSE next_attempt_at END ASC, id ASC
     LIMIT ?`,
  )
    .bind(now, now, limit)
    .all<Pick<CallbackJob, "id" | "status">>();

  let processed = 0;
  for (const job of jobs.results) {
    if (job.status === "PROCESSING") {
      await env.DB.prepare(
        `UPDATE callback_job
         SET status = 'PENDING', lease_until = NULL, last_error = 'PROCESSING_LEASE_EXPIRED', updated_at = ?
         WHERE id = ? AND status = 'PROCESSING' AND lease_until <= ?`,
      ).bind(now, job.id, now).run();
    }
    if (await processCallbackJob(env, job.id)) processed += 1;
  }
  return processed;
}

// 订单变为已支付后立即触发一次回调，不等 Cron；没有对应任务时返回 false。
export async function processCallbackForOrder(env: Env, orderId: string): Promise<boolean> {
  const job = await runtimeFirst<{ id: number }>(
    env,
    `callback:order:${orderId}:id`,
    "SELECT id FROM callback_job WHERE order_id = ?",
    [orderId],
    { ttlMs: null },
  );
  return job ? processCallbackJob(env, job.id) : false;
}

/** 将失败回调重新开放一次尝试，保留原尝试次数与历史记录；调用方负责立即调度执行。 */
export async function retryFailedCallbackForOrder(env: Env, orderId: string): Promise<void> {
  const order = await runtimeFirst<PaymentOrder>(env, `order:id:${orderId}`, "SELECT * FROM payment_order WHERE id = ?", [orderId], { ttlMs: null });
  if (!order) throw new Error("ORDER_NOT_FOUND");
  const now = Date.now();
  const results = await env.DB.batch([
    env.DB.prepare(
      `UPDATE callback_job
       SET status = 'PENDING', max_attempts = attempt_count + 1, next_attempt_at = ?,
           lease_until = NULL, last_error = NULL, updated_at = ?
       WHERE order_id = ? AND status = 'FAILED' AND handled_at IS NULL`,
    ).bind(now, now, orderId),
    env.DB.prepare(
      `UPDATE payment_order SET notify_status = 'PENDING', updated_at = ?
       WHERE id = ? AND status = 'PAID' AND notify_status = 'FAILED'`,
    ).bind(now, orderId),
  ]);
  if ((results[0]?.meta.changes ?? 0) === 0) throw new Error("CALLBACK_NOT_FAILED");
  await Promise.all([
    cacheOrder(env, { ...order, notify_status: "PENDING", updated_at: now }),
    runtimeInvalidate(env, { prefixes: [`callback:order:${orderId}`, `attempts:order:${orderId}`] }),
    invalidateOrderViews(env, orderId),
  ]);
}

/** 将已失败的回调标记为管理员已处理，保留失败状态和全部尝试记录。 */
export async function handleFailedCallbackForOrder(
  env: Env,
  orderId: string,
  reason: string,
  actor: string,
): Promise<void> {
  const normalizedReason = reason.trim();
  if (normalizedReason.length < 1 || normalizedReason.length > 200) throw new Error("CALLBACK_HANDLE_REASON_INVALID");
  const order = await runtimeFirst<PaymentOrder>(env, `order:id:${orderId}`, "SELECT * FROM payment_order WHERE id = ?", [orderId], { ttlMs: null });
  if (!order) throw new Error("ORDER_NOT_FOUND");
  const now = Date.now();
  const result = await env.DB.prepare(
    `UPDATE callback_job
     SET handled_at = ?, handled_reason = ?, handled_by = ?, updated_at = ?
     WHERE order_id = ? AND status = 'FAILED' AND handled_at IS NULL`,
  ).bind(now, normalizedReason, actor, now, orderId).run();
  if ((result.meta.changes ?? 0) === 0) throw new Error("CALLBACK_NOT_OPEN_FAILED");
  await Promise.all([
    runtimeInvalidate(env, { prefixes: [`callback:order:${orderId}`, `attempts:order:${orderId}`] }),
    invalidateOrderViews(env, orderId),
  ]);
}

// 投递单个回调任务。
// 先用条件 UPDATE 抢占租约（status=PENDING 且到期），保证同一任务不会被并发的 Cron 重复投递；
// 商户必须返回纯文本 success 才算成功，否则按 RETRY_DELAYS_MS 退避重试，直到 max_attempts 用尽转 FAILED。
async function processCallbackJob(env: Env, jobId: number): Promise<boolean> {
  const now = Date.now();
  const claim = await env.DB.prepare(
    `UPDATE callback_job
     SET status = 'PROCESSING', attempt_count = attempt_count + 1, lease_until = ?, updated_at = ?
     WHERE id = ? AND status = 'PENDING' AND next_attempt_at <= ?`,
  )
    .bind(now + PROCESSING_LEASE_MS, now, jobId, now)
    .run();
  if ((claim.meta.changes ?? 0) === 0) return false;

  const row = await env.DB.prepare(
    `SELECT
       j.id AS job_id, j.attempt_count, j.max_attempts,
       o.*
     FROM callback_job j
     JOIN payment_order o ON o.id = j.order_id
     WHERE j.id = ?`,
  )
    .bind(jobId)
    .first<(PaymentOrder & { job_id: number; attempt_count: number; max_attempts: number })>();
  if (!row?.notify_url) {
    await markTerminal(env, jobId, row?.id ?? null, "CALLBACK_URL_MISSING", now, row ?? null);
    return true;
  }

  const setting = await getSettings(env);
  if (!setting.merchant_key) {
    await markFailedAttempt(env, row, null, null, "MERCHANT_KEY_MISSING", now);
    return true;
  }

  let responseStatus: number | null = null;
  let responseText: string | null = null;
  let errorMessage: string | null = null;
  try {
    const callbackUrl = validatePublicHttpUrl(row.notify_url);
    if (!callbackUrl) throw new Error("CALLBACK_URL_MISSING");
    const requestUrl = appendParamsToUrl(callbackUrl, buildEpayCallbackParams(row, setting.merchant_key));
    const response = await fetch(requestUrl, {
      method: "GET",
      headers: { "user-agent": "CFPay-Callback/0.1" },
      signal: AbortSignal.timeout(15_000),
    });
    responseStatus = response.status;
    responseText = truncate(await response.text(), 500);
    if (response.ok && responseText.trim().toLowerCase() === "success") {
      await env.DB.batch([
        env.DB.prepare(
          "INSERT INTO callback_attempt (job_id, attempt_no, response_status, response_excerpt, created_at) VALUES (?, ?, ?, ?, ?)",
        ).bind(jobId, row.attempt_count, responseStatus, responseText, now),
        env.DB.prepare(
          "UPDATE callback_job SET status = 'SUCCESS', lease_until = NULL, last_error = NULL, updated_at = ? WHERE id = ?",
        ).bind(now, jobId),
        env.DB.prepare("UPDATE payment_order SET notify_status = 'SUCCESS', updated_at = ? WHERE id = ?").bind(now, row.id),
      ]);
      await Promise.all([
        cacheOrder(env, { ...row, notify_status: "SUCCESS", updated_at: now }),
        runtimeInvalidate(env, { prefixes: [`callback:order:${row.id}`, `attempts:order:${row.id}`] }),
        invalidateOrderViews(env, row.id),
      ]);
      return true;
    }
    errorMessage = `HTTP_${responseStatus}:${responseText}`;
  } catch (error) {
    errorMessage = error instanceof Error ? error.message : String(error);
  }

  await markFailedAttempt(env, row, responseStatus, responseText, errorMessage, now);
  return true;
}

// 记录一次失败尝试：写入 callback_attempt 明细，按退避表排下次时间，并同步订单的 notify_status（未用尽为 RETRYING，用尽为 FAILED）。
async function markFailedAttempt(
  env: Env,
  row: PaymentOrder & { job_id: number; attempt_count: number; max_attempts: number },
  responseStatus: number | null,
  responseText: string | null,
  errorMessage: string,
  now: number,
): Promise<void> {
  const database = env.DB;
  const exhausted = row.attempt_count >= row.max_attempts;
  const delay = RETRY_DELAYS_MS[Math.min(row.attempt_count, RETRY_DELAYS_MS.length - 1)] ?? 60 * 60_000;
  const nextAttemptAt = now + delay;
  await database.batch([
    database.prepare(
      `INSERT INTO callback_attempt (
        job_id, attempt_no, response_status, response_excerpt, error, created_at
      ) VALUES (?, ?, ?, ?, ?, ?)`,
    ).bind(row.job_id, row.attempt_count, responseStatus, responseText, truncate(errorMessage, 500), now),
    database.prepare(
      `UPDATE callback_job
       SET status = ?, next_attempt_at = ?, lease_until = NULL, last_error = ?, updated_at = ?
       WHERE id = ?`,
    ).bind(exhausted ? "FAILED" : "PENDING", nextAttemptAt, truncate(errorMessage, 500), now, row.job_id),
    database.prepare("UPDATE payment_order SET notify_status = ?, updated_at = ? WHERE id = ?")
      .bind(exhausted ? "FAILED" : "RETRYING", now, row.id),
  ]);
  const notifyStatus = exhausted ? "FAILED" : "RETRYING";
  await Promise.all([
    cacheOrder(env, { ...row, notify_status: notifyStatus, updated_at: now }),
    runtimeInvalidate(env, { prefixes: [`callback:order:${row.id}`, `attempts:order:${row.id}`] }),
    invalidateOrderViews(env, row.id),
  ]);
}

// 把任务置为终态（如回调地址缺失导致的 CALLBACK_URL_MISSING），这类错误重试也不会成功，因此不进入退避队列。
async function markTerminal(
  env: Env,
  jobId: number,
  orderId: string | null,
  errorMessage: string,
  now: number,
  order: PaymentOrder | null,
): Promise<void> {
  const database = env.DB;
  const statements = [
    database.prepare(
      "UPDATE callback_job SET status = 'FAILED', lease_until = NULL, last_error = ?, updated_at = ? WHERE id = ?",
    ).bind(errorMessage, now, jobId),
  ];
  if (orderId) {
    statements.push(
      database.prepare("UPDATE payment_order SET notify_status = 'FAILED', updated_at = ? WHERE id = ?").bind(now, orderId),
    );
  }
  await database.batch(statements);
  if (orderId) {
    await Promise.all([
      order ? cacheOrder(env, { ...order, notify_status: "FAILED", updated_at: now }) : Promise.resolve(),
      runtimeInvalidate(env, { prefixes: [`callback:order:${orderId}`, `attempts:order:${orderId}`] }),
      invalidateOrderViews(env, orderId),
    ]);
  }
}
