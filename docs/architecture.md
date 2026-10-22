# 系统架构

## 设计范围

- 单商户、单管理员。
- 微信和支付宝固定收款码。
- PC 监控程序负责识别收款消息并向 Worker 上报。
- 每日低于 100 单，峰值约每分钟 10 单。
- 兼容易支付 V1 页面支付、API 支付和订单查询接口。

## 核心流程

1. 商户系统调用 `/submit.php` 或 `/mapi.php`。
2. Worker 校验易支付 MD5 签名，将金额转为整数分。
3. Worker 尝试插入等待支付订单；D1 部分唯一索引保证同渠道的有效订单不会使用相同实际金额。
4. 收银台展示根据已解析收款内容重新生成的固定二维码和分配后的实际支付金额。
5. PC 监控端通过 HMAC-SHA256 接口上报收款事件。
6. Worker 使用“渠道 + 实际金额 + 支付发生时间”匹配订单，并立即将订单标记为 `PAID`。
7. 商户通知作为独立任务立即执行；失败后按照 1、5、15、30、60 分钟退避重试。
8. 存在支付宝待支付订单时，全局每 2 秒查询一次账务；没有待支付订单时停止查询。

## 状态模型

订单状态：

- `PENDING`：等待付款。
- `PAID`：已确认付款。
- `EXPIRED`：超过有效期且未收到有效支付事件。
- `CLOSED`：人工关闭，首期仅预留。

通知状态：

- `NONE`：没有配置通知地址。
- `PENDING`：等待首次通知。
- `RETRYING`：通知失败，等待重试。
- `SUCCESS`：商户返回 `success`。
- `FAILED`：全部尝试失败。

支付状态不会因为商户回调失败而回滚。

## 并发与幂等

- `merchant_order_no` 唯一，重复下单且金额、渠道一致时返回原订单。
- `payment_order_active_amount_unique` 部分唯一索引限制同渠道等待订单的实际金额。
- PC 端 `eventId` 是支付事件幂等键。
- HMAC 请求 nonce 写入单例 `MonitorState` 内存，十分钟内禁止重放；nonce 不写 D1，实例重启后依靠五分钟时间戳窗口继续限制旧请求。
- 每个订单最多存在一个 `callback_job`。

## 管理后台前端

- 管理后台不再是手写的 `admin.html` + `admin.js` + `styles.css`，而是 `admin-ui/` 下的 Vue 3 单页应用，UI 框架与布局与 cffk 同源：Tailwind CSS v4 主题令牌、`reka-ui`（shadcn-vue new-york / neutral）、`@lucide/vue` 图标、`vue-sonner` 提示、`@tanstack/vue-table`。`admin-ui/src/components/ui/` 直接取自 cffk 的 `components/ui/`（去掉依赖 `@internationalized/date`、`@unovis/vue` 的日历与图表组件），`AdminSidebar` / `AdminLayout` / `AdminPageHeader` / `AdminDataTable` 沿用 cffk 后台的写法。
- 工具链隔离：`admin-ui/` 是独立 npm 包，自带 Vue/Vite/Tailwind/TypeScript 5 配置；Worker 侧继续用仓库根的 TypeScript 7 与 Vitest，两者互不影响。Vite `base` 固定为 `/admin-app/`，产物写入 `public/admin-app/`，由 `ASSETS` 绑定按该前缀提供。
- 入口注入：入口路径可由管理员随时修改，因此不写进构建产物。`admin-ui/index.html` 里留 `window.__CFPAY_ADMIN_BASE__ = "/"` 占位，Worker 下发首页时用 `injectAdminBase` 替换为真实入口，`vue-router` 以它作为 history base。占位符被构建工具改写时，兜底在 `</head>` 前追加一段更晚执行的赋值。
- 路由下发：`matchesAdminEntry` 命中入口本身及其所有子路径，Worker 一律返回同一份 SPA 首页，因此子路由刷新不会 404。首页必须以目录形式 `/admin-app/` 向资源层请求——默认 `html_handling` 会把 `/admin-app/index.html` 以 307 跳到目录，直接透传会让后台入口变成一个跳转响应；代码另保留一次跟随 `location` 的兜底。
- 数据层：沿用既有的 `/admin-api/*` REST 接口与会话 Cookie，前端 `lib/api.ts` 统一处理 JSON/FormData、15 秒超时与非 2xx 的 `message`/`msg` 提取，失败经 `vue-sonner` 弹出。设备与订单页各自轮询（监控端 5 秒、订单 15 秒），离开路由时清理定时器。
- 二维码解析：`lib/qr.ts` 优先用原生 `BarcodeDetector`，回退到 npm 包 `jsqr`（不再依赖 `public/vendor/jsQR.js`），原图仍只在浏览器内解析。
- 验证：`tools/verify-admin-ui.mjs` 用 Playwright 驱动真实 Chromium 打开线上后台，用路由拦截替换 `/admin-api/*` 为固定夹具，从而无需管理员口令即可巡检登录页、九个业务页面、侧边栏与面包屑、三类弹窗、客户端路由跳转与子路由刷新，并收集 console 与未捕获异常。
- 站点设置：`/system/site` 通过 `/admin-api/site-settings` 管理收银台站点名称、副标题、公告和页脚文案；字段独立保存，文案只进入公开收银台，不与支付密钥或监控凭据混用。

## 安全边界

- D1 持久数据统一通过单例 `RuntimeDataState` Durable Object 建立读穿缓存：配置、管理员账号、支付宝凭据、收款码和已读取订单优先从实例内存返回，未命中才读取 D1；写入成功后更新实体缓存或失效相关查询视图。缓存只用于加速读取，D1 仍是订单、支付事件、回调和敏感配置的最终持久化来源。
- `RuntimeDataState` 的内存预算为 48 MiB，单条非固定缓存最大 256 KiB；固定配置和收款码不主动淘汰，历史业务数据按最近访问顺序淘汰。管理端可查看命中、回源和淘汰计数，也可只清空内存缓存而不删除 D1 数据。
- 快捷支付继续走正式订单、到账匹配与回调链路并写入 D1；订单生命周期、导出和删除与其他普通订单完全一致，不维护测试订单分支。

- 后台登录可选启用 Cloudflare Turnstile：`getTurnstileConfig` 要求站点 Key 与 Secret Key 同时存在才启用；公开端点 `GET /api/security/turnstile` 只向登录页下发渲染组件必需的 `enabled` 与公开 Site Key，Secret Key 永不出 Worker；已登录的安全配置接口只返回两个 Key 是否已配置的布尔值，不回显任何 Key 原文。登录时 `verifyTurnstileToken` 先于口令校验执行，未启用则直接放行，启用后任何一步失败都拒绝登录并给出可读原因。
- 监控端只有一条状态上报：`POST /api/monitor/status`（5 秒一次，写死不可配置）同时携带设备信息、组件状态与微信登录状态（`login` 子对象）；设备、组件、微信登录和 nonce 只保存在 `MonitorState` 内存，不写入 D1。旧 `/api/monitor/heartbeat` 与 `/api/monitor/wechat-login` 路由均已删除。
- 在线判定只有一个窗口：`MONITOR_ONLINE_WINDOW_MS` = 15 秒，即上报间隔 5 秒 × 连续 3 次未收到。设备在线与组件新鲜度由同一个请求写入，不存在第二个节奏，因此也不需要第二个窗口；窗口必须大于上报间隔，否则组件状态会永远判定过期、收银台长期隐藏微信收款码。
- 微信目标标识（版本号、文件大小、SHA-256、四个 RVA）集中在 `native/wechat-observer/wechat_target.h`：宿主用它校验磁盘文件，观察器用它下断点，监控端用它派生上报的 `targetVersion` 与 `hookOffset`。刻意不做成配置项——换版本必须重新逆向，只改版本字符串而偏移不变会把断点打在错误地址上导致微信崩溃。
- 监控端上报给后台的说明一律中文：观察 DLL 通过管道传回的内部英文描述由监控端的 `observerMessageDescription` 统一翻译，指标串复用 `statusMetrics` 的中文标签，映射不到时回退成诊断码的中文说明。
- 管理后台首次使用 `ADMIN_PATH`、`ADMIN_USERNAME` 和 `ADMIN_PASSWORD` 部署配置；后台修改后由 D1 保存动态账号、PBKDF2 密码哈希、入口路径和会话版本。
- Workers 的 `crypto.subtle` 对 PBKDF2 单次调用限制 100,000 次迭代，超出直接抛 `NotSupportedError: Pbkdf2 failed: iteration counts above 100000 are not supported`。管理员密码因此以 100,000 次为一轮做链式派生（上一轮输出作为下一轮口令、盐不变），默认 600,000 次等效迭代；`hashAdminPassword` 会拒绝不是 100,000 整数倍或超过 1,000,000 的迭代次数。
- 管理员账号与后台入口路径不做字符集和长度限制，只要求非空；入口首段不得与 Worker 路由前缀或 `public/` 静态资源同名（见 `RESERVED_ADMIN_PATH_SEGMENTS`），否则入口会顶掉接口/资源或永远无法访问。
- 入口匹配由 `matchesAdminEntry` 完成：先 `decodeURIComponent` 再去掉尾部斜杠，与 `/` + 配置值逐字比较，因此中文等多字节入口可用；畸形编码退回原始值，配置为空时直接不匹配，避免根路径变成后台入口。
- 管理员设置变更会使已有会话立即失效；`/admin`、`/admin.html`、`/admin.js`、`/admin-app`、`/admin-app/index.html` 由 Worker 拦截为 404（见 `BLOCKED_ADMIN_PATHNAMES`），`admin-app` 同时列入入口保留首段，SPA 产物只能经管理员配置的入口获取。
- PC 监控端使用 HMAC-SHA256；易支付外部兼容层保留 MD5。
- 商户回调地址拒绝 localhost 和常见内网地址，并启用 Workers 公网 fetch 限制。
- 管理后台不会读取或显示已经保存的商户密钥、监控端密钥。
- 易支付商户 ID 和密钥由单管理员自定义；随机密钥只在浏览器生成并在用户确认后随系统设置保存。
- 收款码原图只在管理员浏览器内解析；D1 保存二维码内容和摘要，公开路由重新生成不包含原图元数据的 SVG。
- 管理后台只提供收款码上传流程，不再暴露手工图片 URL 输入；每个渠道仅在 `payment_qr_code` 保存一条记录，公开地址根据渠道和摘要动态生成，不再向 `system_setting` 重复写入 URL。
- 易支付商户密钥和 PC 监控端密钥都由浏览器使用 `crypto.getRandomValues` 生成，弹窗确认后仍需显式保存系统设置。
- 支付宝私钥和证书组合先使用 `SESSION_SECRET` 派生的 AES-GCM 密钥加密，密文只保存在 D1 的 `alipay_credential` 表；D1 中同时保留 IV 与证书序列号，读取时用同一上下文解密。
- S3 兼容私密对象存储已在迁移 `0009_remove_object_storage.sql` 中整体移除（含 `object_storage_setting` 表、`storage_backend` / `object_key` / `object_sha256` 列与 `aws4fetch` 依赖）；支付宝凭据不再有 D1 之外的存放位置。
