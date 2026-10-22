import { describe, expect, it } from "vitest";
import { renderPaymentPage } from "../src/pages";
import type { PaymentOrder, SystemSetting } from "../src/types";

// 监控端新鲜度判定用固定参考时间；收银台渲染用真实时钟，避免夹具随时间“过期”。
const wallClock = Date.now();

const setting: SystemSetting = {
  id: 1,
  merchant_id: "m_test",
  merchant_key: "k",
  monitor_secret: "s",
  order_ttl_minutes: 5,
  amount_direction: "UP",
  max_amount_adjustments: 10,
  created_at: wallClock,
  updated_at: wallClock,
};

function order(provider: PaymentOrder["provider"]): PaymentOrder {
  return {
    id: "o1",
    merchant_order_no: "USR1TEST",
    merchant_id: "m_test",
    provider,
    subject: "测试订单",
    amount_fen: 100,
    payable_amount_fen: 101,
    status: "PENDING",
    notify_status: "NONE",
    notify_url: null,
    return_url: null,
    attach: "",
    client_ip: null,
    device: null,
    payment_event_id: null,
    paid_source: null,
    manual_paid_by: null,
    paid_at: null,
    expires_at: wallClock + 300_000,
    created_at: wallClock,
    updated_at: wallClock,
  };
}

describe("payment page wechat notice", () => {
  it("hides the WeChat QR code and asks the payer to reorder with Alipay", () => {
    const html = renderPaymentPage(order("wxpay"), setting, {
      wechatAvailable: false,
      wechatReason: "STALE",
    });

    expect(html).toContain("微信支付暂不可用");
    expect(html).toContain("<p>微信支付通道暂时不可用，当前订单无法自动确认微信收款。</p>");
    expect(html).toContain("请返回商户页面，改用支付宝重新下单支付。");
    expect(html).not.toContain("微信收款监控端已离线");
    expect(html).not.toContain("当前没有接入微信收款监控端");
    expect(html).not.toContain("Hook 未激活");
    expect(html).not.toContain('<img class="qr"');
    expect(html).not.toContain("请严格按照页面金额付款");
  });

  it("does not expose the internal reason when no monitor is connected", () => {
    const html = renderPaymentPage(order("wxpay"), setting, {
      wechatAvailable: false,
      wechatReason: "NO_MONITOR",
    });

    expect(html).toContain("微信支付通道暂时不可用");
    expect(html).not.toContain("当前没有接入微信收款监控端");
  });

  it("keeps the normal QR page while the hook is healthy", () => {
    const html = renderPaymentPage(order("wxpay"), setting, { wechatAvailable: true, wechatReason: "OK" });

    expect(html).toContain('<img class="qr" id="qr" src="/payment-assets/qr/wxpay"');
    expect(html).not.toContain("请严格按照页面金额付款");
    expect(html).not.toContain("微信支付暂不可用");
  });

  it("never blocks Alipay orders because Alipay does not depend on the PC monitor", () => {
    const html = renderPaymentPage(order("alipay"), setting, {
      wechatAvailable: false,
      wechatReason: "NO_MONITOR",
    });

    expect(html).toContain('<img class="qr" id="qr" src="/payment-assets/qr/alipay"');
    expect(html).not.toContain("微信支付暂不可用");
  });

  it("does not show the unavailable notice for orders that are no longer pending", () => {
    // 非 PENDING 订单走各自的终态渲染，见 tests/payment-page.test.ts。
    for (const status of ["PAID", "EXPIRED", "CLOSED"] as const) {
      const html = renderPaymentPage({ ...order("wxpay"), status }, setting, {
        wechatAvailable: false,
        wechatReason: "STALE",
      });

      expect(html).not.toContain("微信支付暂不可用");
    }
  });

  it("publishes the rendered availability so the page can reload on change", () => {
    const blocked = renderPaymentPage(order("wxpay"), setting, { wechatAvailable: false });
    const available = renderPaymentPage(order("wxpay"), setting, { wechatAvailable: true });

    expect(blocked).toContain("let channelAvailable=false;");
    expect(available).toContain("let channelAvailable=true;");
  });
});
