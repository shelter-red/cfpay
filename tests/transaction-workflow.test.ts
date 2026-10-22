import { readFileSync } from "node:fs";
import { DatabaseSync } from "node:sqlite";
import { describe, expect, it } from "vitest";
import { handleFailedCallbackForOrder, retryFailedCallbackForOrder } from "../src/callbacks";
import { ignorePaymentEvent, normalizePage, restorePaymentEvent } from "../src/payment-events";
import { listOrders } from "../src/orders";
import type { Env, PaymentOrder } from "../src/types";

const paidOrder: PaymentOrder = {
  id: "order-1",
  merchant_order_no: "M-1",
  merchant_id: "1000",
  provider: "wxpay",
  subject: "测试",
  amount_fen: 100,
  payable_amount_fen: 100,
  status: "PAID",
  notify_status: "FAILED",
  notify_url: "https://merchant.example/notify",
  return_url: null,
  attach: "",
  client_ip: null,
  device: null,
  payment_event_id: "wechat:event",
  paid_source: "EVENT",
  manual_paid_by: null,
  paid_at: 1,
  expires_at: 2,
  created_at: 1,
  updated_at: 1,
};

describe("payment event resolution migration", () => {
  it("preserves matched events and backfills an audit action", () => {
    const database = new DatabaseSync(":memory:");
    for (let index = 1; index <= 13; index += 1) {
      const filename = `migrations/${String(index).padStart(4, "0")}_${[
        "initial", "monitor_component_status", "alipay_credential", "payment_qr_code", "admin_setting",
        "object_storage", "wechat_login_session", "callback_sink", "remove_object_storage",
        "relax_admin_setting_constraints", "site_settings", "site_favicon", "runtime_cache_cleanup",
      ][index - 1]}.sql`;
      database.exec(readFileSync(filename, "utf8"));
    }
    database.exec("PRAGMA foreign_keys = ON;");
    database.exec(`
      INSERT INTO payment_order (
        id, merchant_order_no, merchant_id, provider, subject, amount_fen, payable_amount_fen,
        status, notify_status, notify_url, return_url, attach, client_ip, device,
        payment_event_id, paid_at, expires_at, created_at, updated_at
      ) VALUES ('order-1', 'M-1', '1000', 'wxpay', '测试', 100, 100,
        'PAID', 'NONE', NULL, NULL, 'cfpay-payment-test', NULL, 'cfpay-admin-payment-test', 'wechat:event', 1000, 2000, 500, 1000);
      INSERT INTO payment_event (
        id, device_id, provider, amount_fen, paid_at, status, matched_order_id, raw_excerpt, created_at, updated_at
      ) VALUES ('wechat:event', 'device-1', 'wxpay', 100, 1000, 'MATCHED', 'order-1', '{}', 900, 1000);
    `);
    database.exec(readFileSync("migrations/0014_payment_event_resolution.sql", "utf8"));
    database.exec(readFileSync("migrations/0015_callback_resolution.sql", "utf8"));
    database.exec(readFileSync("migrations/0016_normalize_quick_pay_orders.sql", "utf8"));

    database.exec(`
      INSERT INTO payment_order (
        id, merchant_order_no, merchant_id, provider, subject, amount_fen, payable_amount_fen,
        status, notify_status, notify_url, return_url, attach, client_ip, device,
        payment_event_id, paid_at, expires_at, created_at, updated_at
      ) VALUES ('manual-order', 'M-MANUAL', '1000', 'wxpay', '人工补单', 200, 200,
        'PAID', 'NONE', NULL, NULL, '', NULL, NULL, 'manual:legacy', 1500, 2500, 1000, 1500);
      INSERT INTO payment_event (
        id, device_id, provider, amount_fen, paid_at, status, matched_order_id, raw_excerpt, created_at, updated_at
      ) VALUES ('manual:legacy', 'admin', 'wxpay', 200, 1500, 'MATCHED', 'manual-order', 'manual settlement', 1500, 1500);
    `);
    database.exec(readFileSync("migrations/0017_normalize_manual_settlement.sql", "utf8"));
    database.exec(readFileSync("migrations/0018_manual_payment_source.sql", "utf8"));

    expect(database.prepare("SELECT status, matched_order_id FROM payment_event WHERE id = ?").get("wechat:event")).toEqual({
      status: "MATCHED",
      matched_order_id: "order-1",
    });
    expect(database.prepare("SELECT action, order_id, actor FROM payment_event_action WHERE event_id = ?").get("wechat:event")).toEqual({
      action: "AUTO_MATCH",
      order_id: "order-1",
      actor: "系统",
    });
    database.prepare(
      "INSERT INTO payment_event (id, device_id, provider, amount_fen, paid_at, status, created_at, updated_at) VALUES (?, ?, ?, ?, ?, 'IGNORED', ?, ?)",
    ).run("wechat:ignored", "device-1", "wxpay", 1, 1000, 1000, 1000);
    expect(database.prepare("SELECT status FROM payment_event WHERE id = ?").get("wechat:ignored")).toEqual({ status: "IGNORED" });
    expect(database.prepare("SELECT device, attach FROM payment_order WHERE id = ?").get("order-1")).toEqual({ device: null, attach: "" });
    expect(database.prepare("SELECT paid_source, manual_paid_by, payment_event_id FROM payment_order WHERE id = ?").get("manual-order")).toEqual({
      paid_source: "MANUAL",
      manual_paid_by: "历史管理员",
      payment_event_id: null,
    });
    expect(database.prepare("SELECT id FROM payment_event WHERE id = ?").get("manual:legacy")).toBeUndefined();
    expect(database.prepare("SELECT paid_source FROM payment_order WHERE id = ?").get("order-1")).toEqual({ paid_source: "EVENT" });
    database.close();
  });
});

describe("payment event operations", () => {
  it("normalizes pagination bounds", () => {
    expect(normalizePage(undefined, 20, 100)).toBe(20);
    expect(normalizePage("0", 20, 100)).toBe(20);
    expect(normalizePage("500", 20, 100)).toBe(100);
  });

  it("requires a reason and records ignore and restore transitions", async () => {
    const batches: Array<Array<{ sql: string; bindings: unknown[] }>> = [];
    const database = {
      prepare(sql: string) {
        return { bind: (...bindings: unknown[]) => ({ sql, bindings }) };
      },
      async batch(statements: Array<{ sql: string; bindings: unknown[] }>) {
        batches.push(statements);
        return statements.map((_, index) => ({ meta: { changes: index === 0 ? 1 : 1 } }));
      },
    } as unknown as D1Database;
    const env = { DB: database } as Env;

    await expect(ignorePaymentEvent(env, "wechat:event", "", "owner")).rejects.toThrow("PAYMENT_EVENT_IGNORE_REASON_INVALID");
    await ignorePaymentEvent(env, "wechat:event", "线下核对完成", "owner");
    await restorePaymentEvent(env, "wechat:event", "owner");

    expect(batches[0]?.[0]?.sql).toContain("status = 'IGNORED'");
    expect(batches[0]?.[1]?.bindings).toContain("线下核对完成");
    expect(batches[1]?.[0]?.sql).toContain("status = 'UNMATCHED'");
    expect(batches[1]?.[1]?.sql).toContain("'RESTORE'");
  });
});

describe("orders and callback recovery", () => {
  it("builds a paginated order query with effective expiry and filters", async () => {
    const calls: Array<{ sql: string; bindings: unknown[]; mode: "all" | "first" }> = [];
    const database = {
      prepare(sql: string) {
        return {
          bind(...bindings: unknown[]) {
            return {
              async all() {
                calls.push({ sql, bindings, mode: "all" });
                return { results: [] };
              },
              async first() {
                calls.push({ sql, bindings, mode: "first" });
                return { total: 0 };
              },
            };
          },
        };
      },
    } as unknown as D1Database;

    await listOrders({ DB: database } as Env, {
      page: 2,
      pageSize: 20,
      query: "M-1",
      provider: "wxpay",
      status: "EXPIRED",
      notifyStatus: "FAILED",
    });

    expect(calls).toHaveLength(2);
    expect(calls[0]?.sql).toContain("CASE WHEN o.status = 'PENDING' AND o.expires_at <= ? THEN 'EXPIRED'");
    expect(calls[0]?.sql).toContain("callback_handled_at IS NULL");
    expect(calls[0]?.sql).toContain("effective_status = ?");
    expect(calls[0]?.bindings).toContain("EXPIRED");
    expect(calls[0]?.bindings.slice(-2)).toEqual([20, 20]);
  });

  it("reopens exactly one callback attempt while preserving the attempt counter", async () => {
    const statements: Array<{ sql: string; bindings: unknown[] }> = [];
    const database = {
      prepare(sql: string) {
        return {
          bind(...bindings: unknown[]) {
            const statement = { sql, bindings, first: async () => paidOrder };
            return statement;
          },
        };
      },
      async batch(input: Array<{ sql: string; bindings: unknown[] }>) {
        statements.push(...input);
        return [{ meta: { changes: 1 } }, { meta: { changes: 1 } }];
      },
    } as unknown as D1Database;

    await retryFailedCallbackForOrder({ DB: database } as Env, paidOrder.id);

    expect(statements[0]?.sql).toContain("max_attempts = attempt_count + 1");
    expect(statements[0]?.sql).toContain("status = 'FAILED'");
    expect(statements[0]?.sql).toContain("handled_at IS NULL");
    expect(statements[1]?.sql).toContain("notify_status = 'PENDING'");
  });

  it("marks an unresolved failed callback as handled with an audit reason", async () => {
    const calls: Array<{ sql: string; bindings: unknown[] }> = [];
    const database = {
      prepare(sql: string) {
        return {
          bind(...bindings: unknown[]) {
            calls.push({ sql, bindings });
            return {
              first: async () => paidOrder,
              run: async () => ({ meta: { changes: 1 } }),
            };
          },
        };
      },
    } as unknown as D1Database;

    await expect(handleFailedCallbackForOrder({ DB: database } as Env, paidOrder.id, "", "owner")).rejects.toThrow("CALLBACK_HANDLE_REASON_INVALID");
    await handleFailedCallbackForOrder({ DB: database } as Env, paidOrder.id, "商户已线下确认", "owner");

    const update = calls.find((call) => call.sql.includes("handled_at = ?"));
    expect(update?.sql).toContain("status = 'FAILED' AND handled_at IS NULL");
    expect(update?.bindings).toContain("商户已线下确认");
    expect(update?.bindings).toContain("owner");
  });
});
