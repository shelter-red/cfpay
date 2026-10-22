import { formatFen } from "./lib/money";
import type { PaymentOrder, SystemSetting } from "./types";

// 转义 HTML 特殊字符，收银台的所有插值都必须经过它，防止商户传入的订单名造成 XSS。
function escapeHtml(value: string): string {
  return value
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;")
    .replaceAll('"', "&quot;")
    .replaceAll("'", "&#039;");
}

// 只使用后台接口已校验过的图标格式；旧数据库没有图标时回退到内置 favicon。
export function siteFaviconHref(value: string | undefined): string {
  if (value) {
    const match = /^data:image\/(?:png|jpeg|webp|x-icon|vnd\.microsoft\.icon);base64,([A-Za-z0-9+/]+={0,2})$/.exec(value);
    if (match) {
      const padding = match[1].endsWith("==") ? 2 : match[1].endsWith("=") ? 1 : 0;
      const byteLength = Math.floor(match[1].length * 3 / 4) - padding;
      if (byteLength <= 512 * 1024) return value;
    }
  }
  return "/favicon.svg";
}

// 识别支付宝客户端内置浏览器的两种常见 UA 标记；普通浏览器和其他应用内浏览器均返回 false。
export function isAlipayInAppBrowser(userAgent: string | undefined): boolean {
  return /AlipayClient|AliApp\(AP\//i.test(userAgent ?? "");
}

// 把页脚中的邮箱、完整网页地址或裸域名转换成安全链接；其他显式协议一律拒绝。
function footerLinkHref(value: string): string | null {
  const address = value.trim();
  if (/^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(address)) return `mailto:${address}`;
  const candidate = /^[a-z][a-z0-9+.-]{1,31}:/i.test(address)
    ? address
    : `https://${address.replace(/^\/\//, "")}`;
  try {
    const url = new URL(candidate);
    if (url.protocol === "http:" || url.protocol === "https:" || url.protocol === "mailto:") {
      return url.toString();
    }
  } catch {
    return null;
  }
  return null;
}

// 按行渲染页脚；“显示文字|地址”生成始终在新窗口打开的链接，普通行只输出转义后的文本。
function renderFooter(value: string | undefined): string {
  const text = value?.trim();
  if (!text) return "";
  const lines = text.split(/\r?\n/).map((line) => {
    const separatorIndex = line.indexOf("|");
    if (separatorIndex < 0) return line ? `<div>${escapeHtml(line)}</div>` : "<br>";
    const label = line.slice(0, separatorIndex).trim();
    const href = footerLinkHref(line.slice(separatorIndex + 1));
    if (!label || !href) return `<div>${escapeHtml(line)}</div>`;
    return `<div><a href="${escapeHtml(href)}" target="_blank" rel="noopener noreferrer">${escapeHtml(label)}</a></div>`;
  });
  return `<div class="footer">${lines.join("")}</div>`;
}

// 渲染公开首页，品牌、浏览器图标和页脚均与后台站点设置保持一致。
export function renderHomePage(setting: SystemSetting): string {
  const siteName = setting.site_name?.trim() || "CFPay";
  const siteSubtitle = setting.site_subtitle?.trim() || "个人收款控制台";
  const favicon = siteFaviconHref(setting.favicon_data_url);
  const footer = renderFooter(setting.footer_text);
  return `<!doctype html>
<html lang="zh-CN">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <link rel="icon" href="${escapeHtml(favicon)}">
  <title>${escapeHtml(siteName)}</title>
  <link rel="stylesheet" href="/styles.css">
</head>
<body class="landing">
  <main class="landing-card">
    <h1>${escapeHtml(siteName)}</h1>
    <p class="landing-subtitle">${escapeHtml(siteSubtitle)}</p>
    <div class="health"><span></span> 服务运行中</div>
    ${footer}
  </main>
</body>
</html>`;
}

export interface PaymentPageOptions {
  /** 微信到账依赖 PC 监控端 Hook；仅对 wxpay 订单有意义，未探测时传 undefined。 */
  wechatAvailable?: boolean;
  wechatReason?: string;
  qrImageUrl?: string | null;
  /** 二维码原文仅用于支付宝内置浏览器直接跳转，普通浏览器仍只展示重新生成的二维码图片。 */
  qrPayload?: string | null;
  alipayInApp?: boolean;
  /** 已支付订单首次渲染时使用的完整商户回跳地址，包含易支付签名参数。 */
  returnUrl?: string | null;
}

// 只允许支付宝 HTTPS 收款链接或支付宝客户端协议用于页面跳转，拒绝脚本和其他自定义协议。
function alipayDirectPayHref(value: string | null | undefined): string | null {
  const payload = value?.trim();
  if (!payload) return null;
  try {
    const url = new URL(payload);
    if (url.protocol === "https:" && url.hostname.toLowerCase() === "qr.alipay.com") return payload;
    if (url.protocol === "alipays:") return payload;
  } catch {
    return null;
  }
  return null;
}

// 服务端渲染收银台。
// 三种终态各自渲染不同内容：已失效与已支付都不输出收款码和倒计时（避免付款人重复付款或付进无法核销的订单），
// 微信通道不可用时隐藏收款码并提示改用支付宝重新下单；支付宝内置浏览器改为按钮直接打开二维码内容。
// 倒计时用绝对截止时间，避免后台标签页定时器被节流后漂移。
export function renderPaymentPage(
  order: PaymentOrder,
  setting: SystemSetting,
  options: PaymentPageOptions = {},
): string {
  const now = Date.now();
  const qrImageUrl = options.qrImageUrl === undefined ? `/payment-assets/qr/${order.provider}` : options.qrImageUrl;
  const channelName = order.provider === "wxpay" ? "微信支付" : "支付宝";
  const channelClass = order.provider === "wxpay" ? "wechat" : "alipay";
  const siteName = setting.site_name?.trim() || "CFPay";
  const siteSubtitle = setting.site_subtitle?.trim() || "个人收款控制台";
  const favicon = siteFaviconHref(setting.favicon_data_url);
  const footer = renderFooter(setting.footer_text);
  // 过期以 expires_at 为准，不等定时任务把 status 改成 EXPIRED。否则在最长一分钟的空档里
  // 收银台仍会展示可扫的收款码，而到账匹配要求 expires_at >= paidAt，付款人扫了也匹配不上，
  // 钱只会落进「未匹配收款」等人工处理。
  // 已支付优先于已过期：付款完成后 expires_at 自然会过去，
  // 若先判过期，付了款的人回来会看到“订单已失效”。
  const paid = order.status === "PAID";
  const expired =
    !paid && (order.status === "EXPIRED" || order.status === "CLOSED" || order.expires_at <= now);
  // 只对仍在等待付款的订单拦截：已支付或已失效的订单再显示“不可用”会误导付款人。
  const wechatBlocked = !expired && !paid && order.provider === "wxpay" && options.wechatAvailable === false;
  const alipayDirectHref = order.provider === "alipay" && options.alipayInApp
    ? alipayDirectPayHref(options.qrPayload)
    : null;
  const initialSeconds = expired || paid ? 0 : Math.max(0, Math.floor((order.expires_at - now) / 1000));
  const expiredNotice = `<div class="notice bad">
      <h2>订单已失效</h2>
      <p>该订单已超过支付有效期，收款码不再可用，系统无法再自动确认这笔订单。</p>
      <p class="action">请返回商户页面重新下单。</p>
    </div>`;
  const paidNotice = `<div class="notice good">
      <h2>订单已支付</h2>
      <p>${order.return_url ? "本订单已完成支付，无需重复付款，即将返回商户页面。" : "本订单已完成支付，无需重复付款，可以关闭此页面。"}</p>
    </div>`;

  let paymentArea: string;
  if (expired) {
    paymentArea = expiredNotice;
  } else if (paid) {
    paymentArea = paidNotice;
  } else if (wechatBlocked) {
    paymentArea = `<div class="notice bad">
      <h2>微信支付暂不可用</h2>
      <p>微信支付通道暂时不可用，当前订单无法自动确认微信收款。</p>
      <p class="action">请返回商户页面，改用支付宝重新下单支付。</p>
    </div>`;
  } else {
    paymentArea = alipayDirectHref
      ? `<button class="direct-pay" id="alipayDirectPay" type="button" data-pay-target="${escapeHtml(alipayDirectHref)}">打开支付宝付款</button>`
      : qrImageUrl
        ? `<img class="qr" id="qr" src="${escapeHtml(qrImageUrl)}" alt="${channelName}收款码">`
      : '<div class="notice bad"><h2>当前支付渠道暂不可用</h2><p>收款码尚未配置。</p><p class="action">请返回商户页面重新下单或联系商户。</p></div>';
  }

  // 倒计时只在还能付款时渲染；已失效或已支付的订单不再显示 00:00 的假倒计时。
  const timerArea =
    expired || paid ? "" : '<div class="timer" id="timerWrap">剩余支付时间：<strong id="timer">--:--</strong></div>';

  return `<!doctype html>
<html lang="zh-CN">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <link rel="icon" href="${escapeHtml(favicon)}">
  <title>${escapeHtml(order.subject)} - ${escapeHtml(channelName)} - ${escapeHtml(siteName)}</title>
  <style>
    :root{color-scheme:light;background:#f3f5f7;font-family:Inter,"PingFang SC","Microsoft YaHei",sans-serif;color:#172033}
    *{box-sizing:border-box}body{margin:0;min-height:100vh;display:grid;place-items:center;padding:24px}
    .card{width:min(440px,100%);background:#fff;border-radius:24px;padding:28px;box-shadow:0 20px 60px rgba(28,39,60,.12);text-align:center}
    .brand{font-size:13px;color:#8a94a6;letter-spacing:.12em}.badge{display:inline-flex;margin:18px 0 10px;padding:8px 14px;border-radius:99px;font-weight:700}
    .wechat{background:#e9f9ef;color:#168742}.alipay{background:#eaf4ff;color:#0877d1}
    .subject{margin:4px auto 8px;max-width:100%;font-size:18px;font-weight:700;line-height:1.5;overflow-wrap:anywhere}
    .amount{font-size:44px;font-weight:800;letter-spacing:-.03em;margin:4px 0}.amount small{font-size:20px}
    .qr{display:block;width:250px;height:250px;object-fit:contain;border:1px solid #e7eaf0;border-radius:18px;padding:12px;background:#fff;margin:18px auto 0}
    .direct-pay{display:block;width:min(280px,100%);margin:24px auto 4px;border:0;border-radius:14px;padding:15px 20px;background:#1677ff;color:#fff;font:inherit;font-weight:800;cursor:pointer;box-shadow:0 10px 24px rgba(22,119,255,.22)}
    .direct-pay:hover{background:#0d6fe8}.direct-pay:focus-visible{outline:3px solid rgba(22,119,255,.28);outline-offset:3px}
    .timer{font-variant-numeric:tabular-nums;margin:18px 0 4px;color:#637083}.order{font-size:12px;color:#9aa3b2;word-break:break-all;margin-top:16px}
    .footer{font-size:12px;color:#7f8999;word-break:break-word;margin-top:14px;line-height:1.7}.footer div+div{margin-top:3px}.footer a{color:#526173;text-decoration:underline;text-underline-offset:3px}.footer a:hover{color:#172033}
    .notice{margin:18px 0 0;padding:18px 20px;border-radius:18px;text-align:left}
    .notice h2{margin:0 0 10px;font-size:18px;font-weight:800}
    .notice p{margin:6px 0;line-height:1.75;font-size:14px}
    .notice .action{margin-top:12px;font-size:15px;font-weight:800}
    .notice.bad{background:#fff5f5;border:1px solid #ffdada}.notice.bad h2,.notice.bad .action{color:#c0362c}.notice.bad p{color:#8a3b34}
    .notice.good{background:#f2fbf5;border:1px solid #cdeeda}.notice.good h2{color:#12823b}.notice.good p{color:#3c6b4d}
  </style>
</head>
<body>
  <main class="card">
    <div class="brand">${escapeHtml(siteName)} · ${escapeHtml(siteSubtitle)}</div>
    <div class="badge ${channelClass}">${channelName}</div>
    <div class="subject">${escapeHtml(order.subject)}</div>
    <div class="amount"><small>¥</small>${formatFen(order.payable_amount_fen)}</div>
    <div id="paymentArea">${paymentArea}</div>
    ${timerArea}
    <div class="order">订单号：${escapeHtml(order.merchant_order_no)}</div>
    ${footer}
  </main>
  <script>
    const orderId=${JSON.stringify(order.id)};
    let channelAvailable=${JSON.stringify(options.wechatAvailable === undefined ? null : options.wechatAvailable)};
    // 用绝对截止时间而不是本地递减计数：后台标签页的定时器会被浏览器节流，
    // 递减计数会严重滞后，切回页面时已过期的订单会“复活”继续倒数。
    const deadline=Date.now()+${initialSeconds}*1000;
    // 服务端已判定失效或已支付时直接进入终态：不倒计时、不展示收款码。
    let finished=${expired || paid ? "true" : "false"};
    const timer=document.getElementById('timer');
    const timerWrap=document.getElementById('timerWrap');
    const paymentArea=document.getElementById('paymentArea');
    const alipayDirectPay=document.getElementById('alipayDirectPay');
    const paidNoticeHtml=${JSON.stringify(paidNotice)};
    const expiredNoticeHtml=${JSON.stringify(expiredNotice)};
    const initialReturnUrl=${JSON.stringify(paid ? (options.returnUrl ?? order.return_url) : null)};
    const ticker=setInterval(tick,1000);
    let pollTimer=null;
    function schedulePoll(delay){
      if(pollTimer)clearTimeout(pollTimer);
      pollTimer=setTimeout(poll,delay);
    }
    // 动态终态直接复用服务端首次渲染的提示卡 HTML，确保轮询变化与手动刷新视觉完全一致。
    function showTerminal(html){
      if(finished)return;
      finished=true;clearInterval(ticker);
      if(pollTimer)clearTimeout(pollTimer);
      if(paymentArea)paymentArea.innerHTML=html;
      if(timerWrap)timerWrap.remove();
    }
    function tick(){
      if(finished||!timer)return;
      const safe=Math.max(0,Math.round((deadline-Date.now())/1000));
      timer.textContent=String(Math.floor(safe/60)).padStart(2,'0')+':'+String(safe%60).padStart(2,'0');
      if(safe===0)showTerminal(expiredNoticeHtml);
    }
    async function poll(){
      pollTimer=null;
      try{
        const response=await fetch('/api/orders/'+orderId+'/status',{cache:'no-store'});
        const data=await response.json();
        if(data.status==='PAID'){
          showTerminal(paidNoticeHtml);
          if(data.returnUrl) location.replace(data.returnUrl);
          return;
        }
        // 监控端恢复或掉线时整页刷新，让收款码与提示保持一致；终态页面不再刷新。
        if(!finished&&typeof data.wechatAvailable==='boolean'&&data.wechatAvailable!==channelAvailable){location.reload();return}
        if(data.status==='EXPIRED'||data.status==='CLOSED'){showTerminal(expiredNoticeHtml);return}
        schedulePoll(1000);
      }catch{schedulePoll(2000)}
    }
    // 支付宝内置浏览器中由用户点击触发跳转，保持浏览器对自定义协议和支付链接的用户手势要求。
    if(alipayDirectPay)alipayDirectPay.addEventListener('click',()=>{
      const target=alipayDirectPay.dataset.payTarget;
      if(target)location.assign(target);
    });
    // 切回页面时立刻按真实时间重算并重新拉状态，不必等被节流的定时器醒来。
    document.addEventListener('visibilitychange',()=>{
      if(document.hidden||finished)return;
      tick();schedulePoll(0);
    });
    // 服务端已经渲染终态时不再启动轮询；已支付且配置返回地址时仍按原行为跳回商户页面。
    if(finished){
      clearInterval(ticker);
      if(initialReturnUrl)setTimeout(()=>location.replace(initialReturnUrl),600);
    }else{
      tick();poll();
    }
  </script>
</body>
</html>`;
}
