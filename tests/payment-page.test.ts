import { describe, expect, it } from "vitest";
import { isAlipayInAppBrowser, renderHomePage, renderPaymentPage } from "../src/pages";
import type { PaymentOrder, SystemSetting } from "../src/types";

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

function order(overrides: Partial<PaymentOrder> = {}): PaymentOrder {
  return {
    id: "o1",
    merchant_order_no: "USR1TEST",
    merchant_id: "m_test",
    provider: "wxpay",
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
    ...overrides,
  };
}

describe("payment page countdown and terminal states", () => {
  it("detects Alipay embedded browser user agents", () => {
    expect(isAlipayInAppBrowser("Mozilla/5.0 AlipayClient/10.6.80.8000")).toBe(true);
    expect(isAlipayInAppBrowser("Mozilla/5.0 AliApp(AP/10.6.80.8000)")).toBe(true);
    expect(isAlipayInAppBrowser("Mozilla/5.0 Chrome/140.0.0.0")).toBe(false);
  });

  it("renders the QR code and a live countdown for a pending order", () => {
    const html = renderPaymentPage(order(), setting);

    expect(html).toContain("测试订单");
    expect(html).toContain('<img class="qr" id="qr" src="/payment-assets/qr/wxpay"');
    expect(html).not.toContain("请严格按照页面金额付款");
    expect(html).toContain('id="timerWrap"');
    expect(html).toContain("剩余支付时间");
    expect(html).toContain("let finished=false;");
    expect(html).toMatch(/const deadline=Date\.now\(\)\+29\d\*1000;/);
  });

  it("escapes the product name and includes it in the page title", () => {
    const html = renderPaymentPage(order({ subject: "商品 <script>alert(1)</script>" }), setting);

    expect(html).toContain("商品 &lt;script&gt;alert(1)&lt;/script&gt;");
    expect(html).not.toContain("<script>alert(1)</script>");
    expect(html).toContain("- 微信支付 - CFPay</title>");
  });

  it("replaces the Alipay QR image with a direct-pay button inside Alipay", () => {
    const payload = "https://qr.alipay.com/fkx-test?amount=0.01&memo=CFPay";
    const html = renderPaymentPage(order({ provider: "alipay" }), setting, {
      qrImageUrl: "/payment-assets/qr/alipay",
      qrPayload: payload,
      alipayInApp: true,
    });

    expect(html).toContain('id="alipayDirectPay"');
    expect(html).toContain('data-pay-target="https://qr.alipay.com/fkx-test?amount=0.01&amp;memo=CFPay"');
    expect(html).toContain("打开支付宝付款");
    expect(html).toContain("location.assign(target)");
    expect(html).not.toContain('<img class="qr" id="qr"');
  });

  it("keeps the Alipay QR image in normal browsers and for unsafe direct targets", () => {
    const normalHtml = renderPaymentPage(order({ provider: "alipay" }), setting, {
      qrImageUrl: "/payment-assets/qr/alipay",
      qrPayload: "https://qr.alipay.com/fkx-test",
      alipayInApp: false,
    });
    const unsafeHtml = renderPaymentPage(order({ provider: "alipay" }), setting, {
      qrImageUrl: "/payment-assets/qr/alipay",
      qrPayload: "javascript:alert(1)",
      alipayInApp: true,
    });

    expect(normalHtml).toContain('<img class="qr" id="qr" src="/payment-assets/qr/alipay"');
    expect(normalHtml).not.toContain('id="alipayDirectPay"');
    expect(unsafeHtml).toContain('<img class="qr" id="qr" src="/payment-assets/qr/alipay"');
    expect(unsafeHtml).not.toContain('data-pay-target="javascript:');
  });

  it("hides the QR code and the countdown once the order expired by status", () => {
    for (const status of ["EXPIRED", "CLOSED"] as const) {
      const html = renderPaymentPage(order({ status }), setting);

      expect(html).toContain("<h2>订单已失效</h2>");
      expect(html).toContain("该订单已超过支付有效期，收款码不再可用");
      expect(html).toContain("系统无法再自动确认这笔订单");
      expect(html).not.toContain("付款无法自动到账");
      expect(html).not.toContain("<img class=\"qr\"");
      expect(html).not.toContain("剩余支付时间");
      expect(html).not.toContain('id="timerWrap"');
      expect(html).toContain("let finished=true;");
    }
  });

  it("hides the QR code as soon as expires_at passes even if the cron has not flipped the status", () => {
    // 定时任务每分钟才把 PENDING 改成 EXPIRED，这段空档里收银台不能继续展示可扫的收款码：
    // 到账匹配要求 expires_at >= paidAt，此时付款只会落进未匹配收款。
    const html = renderPaymentPage(order({ expires_at: wallClock - 1_000 }), setting);

    expect(html).toContain("<h2>订单已失效</h2>");
    expect(html).not.toContain("<img class=\"qr\"");
    expect(html).not.toContain("剩余支付时间");
    expect(html).toContain("let finished=true;");
  });

  it("shows a paid state without the QR code so the payer cannot pay twice", () => {
    const html = renderPaymentPage(order({ status: "PAID", paid_at: wallClock }), setting);

    expect(html).toContain("<h2>订单已支付</h2>");
    expect(html).toContain("无需重复付款");
    expect(html).toContain("可以关闭此页面");
    expect(html).not.toContain("<img class=\"qr\"");
    expect(html).not.toContain("剩余支付时间");
    expect(html).toContain("let finished=true;");
  });

  it("tells the payer that a paid order will return to the merchant when a return URL exists", () => {
    const html = renderPaymentPage(
      order({ status: "PAID", paid_at: wallClock, return_url: "https://merchant.example/return" }),
      setting,
    );

    expect(html).toContain("<p>本订单已完成支付，无需重复付款，即将返回商户页面。</p>");
  });

  it("uses the signed return URL when an already-paid page is opened directly", () => {
    const html = renderPaymentPage(
      order({ status: "PAID", paid_at: wallClock, return_url: "https://merchant.example/return?order=1" }),
      setting,
      { returnUrl: "https://merchant.example/return?order=1&trade_status=TRADE_SUCCESS&sign=abc" },
    );

    expect(html).toContain(
      'const initialReturnUrl="https://merchant.example/return?order=1&trade_status=TRADE_SUCCESS&sign=abc"',
    );
    expect(html).not.toContain('const initialReturnUrl="https://merchant.example/return?order=1"');
  });

  it("shows an actionable unavailable state when the QR code is missing", () => {
    const html = renderPaymentPage(order(), setting, { qrImageUrl: null });

    expect(html).toContain("当前支付渠道暂不可用");
    expect(html).toContain("请返回商户页面重新下单或联系商户");
    expect(html).not.toContain('<img class="qr"');
  });

  it("keeps a paid order as paid even after its expiry time has passed", () => {
    // 付款完成后 expires_at 自然会过去；若先判过期，付了款的人会看到“订单已失效”。
    const html = renderPaymentPage(
      order({ status: "PAID", paid_at: wallClock - 60_000, expires_at: wallClock - 1_000 }),
      setting,
    );

    expect(html).toContain("订单已支付");
    // 脚本会内嵌两个终态模板，因此只检查当前实际渲染到 paymentArea 的提示卡。
    expect(html).toContain('<div id="paymentArea"><div class="notice good">');
    expect(html).not.toContain('<div id="paymentArea"><div class="notice bad">');
    expect(html).not.toContain("<img class=\"qr\"");
  });

  it("uses the same terminal notice templates for polling updates and manual refreshes", () => {
    const html = renderPaymentPage(order(), setting);

    // 不用本地递减计数：后台标签页定时器被节流会让已过期的订单“复活”继续倒数。
    expect(html).toContain("const deadline=Date.now()+");
    expect(html).not.toMatch(/let seconds=/);
    expect(html).toContain("Math.round((deadline-Date.now())/1000)");
    // 动态终态与服务端刷新后的终态必须来自同一份提示卡模板，不能只替换底部状态文字。
    expect(html).toContain("function showTerminal(html){");
    expect(html).toContain("if(paymentArea)paymentArea.innerHTML=html;");
    expect(html).toContain("if(timerWrap)timerWrap.remove();");
    expect(html).toContain("showTerminal(paidNoticeHtml);");
    expect(html).toContain("showTerminal(expiredNoticeHtml);");
    expect(html).not.toContain('id="status"');
    expect(html).not.toContain("支付成功，可以关闭此页面");
    expect(html).not.toContain("status.textContent=data.returnUrl");
    expect(html).not.toContain("function finish(message)");
    expect(html).toContain("if(finished){");
    expect(html).toContain("}else{\n      tick();poll();");
  });

  it("re-checks immediately when the payer switches back to the page", () => {
    const html = renderPaymentPage(order(), setting);

    expect(html).toContain("document.addEventListener('visibilitychange'");
    expect(html).toContain("tick();schedulePoll(0);");
    // 轮询用单一 timer 句柄，避免 visibilitychange 触发第二条轮询链。
    expect(html).toContain("function schedulePoll(delay){");
    expect(html).toContain("if(pollTimer)clearTimeout(pollTimer);");
  });
});

describe("payment page footer", () => {
  it("renders plain footer text and escapes HTML", () => {
    const html = renderPaymentPage(order(), { ...setting, footer_text: "个人收款服务\n<script>alert(1)</script>" });

    expect(html).toContain("<div>个人收款服务</div>");
    expect(html).toContain("&lt;script&gt;alert(1)&lt;/script&gt;");
    expect(html).not.toContain("<script>alert(1)</script>");
  });

  it("opens email, web and bare-domain footer entries in a new window", () => {
    const html = renderPaymentPage(order(), {
      ...setting,
      footer_text: "联系我们|example@site.com\n帮助中心|https://example.com/help\n服务状态|status.example.com",
    });

    expect(html).toContain('<a href="mailto:example@site.com" target="_blank" rel="noopener noreferrer">联系我们</a>');
    expect(html).toContain('<a href="https://example.com/help" target="_blank" rel="noopener noreferrer">帮助中心</a>');
    expect(html).toContain('<a href="https://status.example.com/" target="_blank" rel="noopener noreferrer">服务状态</a>');
  });

  it("does not create links for unsafe footer protocols", () => {
    const html = renderPaymentPage(order(), { ...setting, footer_text: "点击这里|javascript:alert(1)" });

    expect(html).toContain("点击这里|javascript:alert(1)");
    expect(html).not.toContain('href="javascript:');
  });
});

describe("public home page", () => {
  it("uses the configured brand, favicon and footer links", () => {
    const html = renderHomePage({
      ...setting,
      site_name: "Shelter Pay",
      site_subtitle: "内部收款服务",
      favicon_data_url: "data:image/png;base64,AA==",
      footer_text: "联系我们|example@site.com",
    });

    expect(html).toContain("<title>Shelter Pay</title>");
    expect(html).toContain("<h1>Shelter Pay</h1>");
    expect(html).toContain('<p class="landing-subtitle">内部收款服务</p>');
    expect(html).toContain('<link rel="icon" href="data:image/png;base64,AA==">');
    expect(html).toContain('<a href="mailto:example@site.com" target="_blank" rel="noopener noreferrer">联系我们</a>');
  });

  it("escapes configured homepage text", () => {
    const html = renderHomePage({ ...setting, site_name: "<script>alert(1)</script>" });

    expect(html).toContain("&lt;script&gt;alert(1)&lt;/script&gt;");
    expect(html).not.toContain("<script>alert(1)</script>");
  });
});
