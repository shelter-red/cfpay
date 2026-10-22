import { readdirSync, readFileSync } from "node:fs";
import { DatabaseSync } from "node:sqlite";
import { describe, expect, it } from "vitest";
import {
  deleteIgnoredPaymentEvents,
  deleteOrders,
  exportIgnoredPaymentEvents,
  exportOrders,
  normalizeExportFormat,
  normalizeExportIds,
} from "../src/data-lifecycle";
import type { Env } from "../src/types";

type BoundStatement = {
  sql: string;
  bindings: unknown[];
  all<T>(): Promise<{ results: T[] }>;
  first<T>(): Promise<T | null>;
  run(): Promise<{ meta: { changes: number } }>;
};

type SQLiteValue = string | number | bigint | Uint8Array | null;

/** 用 Node 内存 SQLite 包装出测试所需的 D1 最小接口。 */
function d1(database: DatabaseSync): D1Database {
  const bound = (sql: string, bindings: unknown[] = []): BoundStatement => ({
    sql,
    bindings,
    async all<T>() {
      return { results: database.prepare(sql).all(...bindings as SQLiteValue[]) as T[] };
    },
    async first<T>() {
      return (database.prepare(sql).get(...bindings as SQLiteValue[]) as T | undefined) ?? null;
    },
    async run() {
      const result = database.prepare(sql).run(...bindings as SQLiteValue[]);
      return { meta: { changes: Number(result.changes) } };
    },
  });
  return {
    prepare(sql: string) {
      return {
        ...bound(sql),
        bind(...bindings: unknown[]) {
          return bound(sql, bindings);
        },
      };
    },
    async batch(statements: BoundStatement[]) {
      database.exec("BEGIN");
      try {
        const results = [];
        for (const statement of statements) results.push(await statement.run());
        database.exec("COMMIT");
        return results;
      } catch (error) {
        database.exec("ROLLBACK");
        throw error;
      }
    },
  } as unknown as D1Database;
}

/** 创建应用全部迁移后的内存数据库。 */
function migratedDatabase(): DatabaseSync {
  const database = new DatabaseSync(":memory:");
  for (const filename of readdirSync("migrations").filter((name) => /^\d{4}_.+\.sql$/.test(name)).sort()) {
    database.exec(readFileSync(`migrations/${filename}`, "utf8"));
  }
  database.exec("PRAGMA foreign_keys = ON;");
  return database;
}

/** 写入一组带完整收款和回调审计链的订单测试数据。 */
function seedOrder(database: DatabaseSync): void {
  database.exec(`
    INSERT INTO payment_order (
      id, merchant_order_no, merchant_id, provider, subject, amount_fen, payable_amount_fen,
      status, notify_status, notify_url, return_url, attach, client_ip, device,
      payment_event_id, paid_at, expires_at, created_at, updated_at
    ) VALUES (
      'order-12345678901234567890', '123456789012345678901234', '1000', 'wxpay', '导出测试商品', 1, 1,
      'PAID', 'FAILED', 'https://merchant.example/notify', NULL, '', NULL, NULL,
      'wechat:event-export', 1791000000000, 1791000300000, 1790999900000, 1791000000000
    );
    INSERT INTO payment_event (
      id, device_id, provider, amount_fen, paid_at, status, matched_order_id, raw_excerpt, created_at, updated_at
    ) VALUES (
      'wechat:event-export', 'device-1', 'wxpay', 1, 1791000000000, 'MATCHED',
      'order-12345678901234567890', '{"transid":"42000000000000000001"}', 1791000000000, 1791000000000
    );
    INSERT INTO payment_event_action (event_id, action, order_id, reason, actor, created_at)
    VALUES ('wechat:event-export', 'AUTO_MATCH', 'order-12345678901234567890', NULL, '系统', 1791000000000);
    INSERT INTO callback_job (
      order_id, status, attempt_count, max_attempts, next_attempt_at, lease_until, last_error,
      handled_at, handled_reason, handled_by, created_at, updated_at
    ) VALUES (
      'order-12345678901234567890', 'FAILED', 1, 1, 1791000000000, NULL, 'HTTP_500',
      1791000100000, '商户确认线下处理', 'owner', 1791000000000, 1791000100000
    );
    INSERT INTO callback_attempt (job_id, attempt_no, response_status, response_excerpt, error, created_at)
    SELECT id, 1, 500, 'failed', 'HTTP_500', 1791000000000 FROM callback_job
    WHERE order_id = 'order-12345678901234567890';
  `);
}

describe("data lifecycle", () => {
  it("validates export inputs", () => {
    expect(normalizeExportIds(["a", "a", "b"])).toEqual(["a", "b"]);
    expect(() => normalizeExportIds([])).toThrow("EXPORT_IDS_INVALID");
    expect(normalizeExportFormat("csv")).toBe("csv");
    expect(() => normalizeExportFormat("xlsx")).toThrow("EXPORT_FORMAT_INVALID");
  });

  it("exports complete order data and prevents spreadsheet scientific notation", async () => {
    const database = migratedDatabase();
    seedOrder(database);
    const env = { DB: d1(database) } as Env;

    const json = await exportOrders(env, ["order-12345678901234567890"], "json");
    const payload = JSON.parse(json.body) as { records: Array<Record<string, unknown>> };
    expect(payload.records).toHaveLength(1);
    expect(JSON.stringify(payload.records[0])).toContain("42000000000000000001");
    expect(JSON.stringify(payload.records[0])).toContain("商户确认线下处理");

    const csv = await exportOrders(env, ["order-12345678901234567890"], "csv");
    expect(csv.body.startsWith("\uFEFF")).toBe(true);
    expect(csv.body).toContain("\t123456789012345678901234");
    expect(csv.body).toContain("callback_attempts");
    database.close();
  });

  it("deletes an order and every related audit row", async () => {
    const database = migratedDatabase();
    seedOrder(database);
    const env = { DB: d1(database) } as Env;

    expect(await deleteOrders(env, ["order-12345678901234567890"])).toBe(1);
    for (const table of ["payment_order", "payment_event", "payment_event_action", "callback_job", "callback_attempt"]) {
      expect(database.prepare(`SELECT COUNT(*) AS count FROM ${table}`).get()).toEqual({ count: 0 });
    }
    database.close();
  });

  it("only exports and deletes ignored unmatched payments", async () => {
    const database = migratedDatabase();
    database.exec(`
      INSERT INTO payment_event (id, device_id, provider, amount_fen, paid_at, status, created_at, updated_at)
      VALUES ('wechat:ignored-export', 'device-1', 'wxpay', 1, 1791000000000, 'IGNORED', 1791000000000, 1791000000000),
             ('wechat:pending-export', 'device-1', 'wxpay', 2, 1791000000000, 'UNMATCHED', 1791000000000, 1791000000000);
      INSERT INTO payment_event_action (event_id, action, reason, actor, created_at)
      VALUES ('wechat:ignored-export', 'IGNORE', '不是系统订单', 'owner', 1791000000000);
    `);
    const env = { DB: d1(database) } as Env;

    const exported = await exportIgnoredPaymentEvents(env, ["wechat:ignored-export"], "json");
    expect(exported.body).toContain("不是系统订单");
    await expect(exportIgnoredPaymentEvents(env, ["wechat:pending-export"], "json")).rejects.toThrow("PAYMENT_EVENT_DELETE_FORBIDDEN");
    expect(await deleteIgnoredPaymentEvents(env, ["wechat:ignored-export"])).toBe(1);
    expect(database.prepare("SELECT status FROM payment_event WHERE id = 'wechat:pending-export'").get()).toEqual({ status: "UNMATCHED" });
    database.close();
  });
});
