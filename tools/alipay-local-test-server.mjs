import http from "node:http";
import { AlipayRequestError } from "alipay-sdk";
import { createAlipayClient, mask, queryAlipayLedger, sanitizeLedgerDetail } from "./lib/alipay-client.mjs";

const host = process.env.HOST ?? "0.0.0.0";
const port = Number(process.env.PORT ?? 8790);
const client = await createAlipayClient();

const server = http.createServer(async (request, response) => {
  const url = new URL(request.url ?? "/", `http://${host}:${port}`);
  if (request.method === "GET" && url.pathname === "/api/ledger") {
    try {
      const result = await queryAlipayLedger(client);
      json(response, 200, {
        ok: true,
        appId: mask(client.config.appId),
        query: result.query,
        totalSize: result.totalSize,
        traceId: result.traceId,
        details: result.details.map(sanitizeLedgerDetail),
      });
    } catch (error) {
      json(response, 502, {
        ok: false,
        message: error instanceof Error ? error.message : String(error),
        code: error instanceof AlipayRequestError ? error.code : null,
        traceId: error instanceof AlipayRequestError ? error.traceId : null,
      });
    }
    return;
  }
  if (request.method === "GET" && url.pathname === "/") {
    response.writeHead(200, { "content-type": "text/html; charset=utf-8", "cache-control": "no-store" });
    response.end(renderPage(mask(client.config.appId)));
    return;
  }
  response.writeHead(404, { "content-type": "text/plain; charset=utf-8" });
  response.end("Not Found");
});

server.listen(port, host, () => {
  console.log(`支付宝账务测试服务已启动：http://${host}:${port}`);
});

function json(response, status, body) {
  response.writeHead(status, { "content-type": "application/json; charset=utf-8", "cache-control": "no-store" });
  response.end(JSON.stringify(body));
}

function renderPage(appId) {
  return `<!doctype html><html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>支付宝账务测试</title><style>
  :root{font-family:Inter,"Microsoft YaHei",sans-serif;color:#172033;background:#f3f5f8}*{box-sizing:border-box}body{margin:0;padding:28px}.shell{max-width:1100px;margin:auto}.top{display:flex;justify-content:space-between;align-items:end;margin-bottom:20px}h1{margin:4px 0;font-size:30px}.muted{color:#758096}.grid{display:grid;grid-template-columns:360px 1fr;gap:18px}.panel{background:#fff;border:1px solid #e5e9ef;border-radius:16px;padding:20px;box-shadow:0 12px 35px rgba(29,39,60,.06)}input[type=file]{width:100%;padding:10px;border:1px solid #d8dee8;border-radius:10px}.qr{width:100%;aspect-ratio:1;object-fit:contain;margin-top:16px;border:1px dashed #d8dee8;border-radius:12px;padding:10px;display:none}.state{font-weight:700}.ok{color:#16813a}.bad{color:#b42318}table{width:100%;border-collapse:collapse;margin-top:14px}th,td{font-size:13px;text-align:left;padding:10px;border-bottom:1px solid #edf0f4;white-space:nowrap}.scroll{overflow:auto}.amount{font-weight:800}.toolbar{display:flex;justify-content:space-between;gap:12px;align-items:center}button{border:0;border-radius:9px;background:#1769e0;color:#fff;padding:9px 14px;font-weight:700;cursor:pointer}@media(max-width:800px){.grid{grid-template-columns:1fr}.top{align-items:flex-start;flex-direction:column}}
  </style></head><body><main class="shell"><header class="top"><div><div class="muted">ALIPAY ACCOUNT LOG</div><h1>账务查询联调</h1></div><div>App ID：${appId}</div></header><div class="grid"><section class="panel"><h2>经营码</h2><input id="file" type="file" accept="image/png,image/jpeg,image/webp"><img id="qr" class="qr" alt="经营码"></section><section class="panel"><div class="toolbar"><div><h2>最近账务</h2><div id="state" class="state muted">正在连接支付宝…</div></div><button id="refresh">刷新</button></div><div class="scroll"><table><thead><tr><th>时间</th><th>方向</th><th>金额</th><th>摘要</th><th>账务编号</th></tr></thead><tbody id="rows"></tbody></table></div></section></div></main><script>
  const file=document.querySelector('#file'),qr=document.querySelector('#qr'),state=document.querySelector('#state'),rows=document.querySelector('#rows');
  file.addEventListener('change',()=>{const selected=file.files[0];if(!selected)return;qr.src=URL.createObjectURL(selected);qr.style.display='block'});
  document.querySelector('#refresh').addEventListener('click',load);
  async function load(){state.textContent='正在查询支付宝…';state.className='state muted';try{const response=await fetch('/api/ledger',{cache:'no-store'});const data=await response.json();if(!response.ok)throw new Error((data.code?data.code+': ':'')+data.message);state.textContent='连接正常，共 '+data.totalSize+' 条';state.className='state ok';rows.innerHTML=data.details.map(item=>'<tr><td>'+e(item.trans_dt)+'</td><td>'+e(item.direction)+'</td><td class="amount">'+e(item.trans_amount)+'</td><td>'+e(item.trans_memo)+'</td><td>'+e(item.account_log_id)+'</td></tr>').join('')||'<tr><td colspan="5">当前查询窗口没有账务记录</td></tr>'}catch(error){state.textContent=error.message;state.className='state bad'}}
  function e(value){return String(value??'-').replace(/[&<>\"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','\"':'&quot;'}[c]))}load();setInterval(load,2000);
  </script></body></html>`;
}
