# CFPay

基于 Cloudflare Workers + D1 的**单商户个人收款服务**：对外提供易支付 V1 兼容接口，对内用微信 / 支付宝固定收款码收款，并由 PC 监控端把真实到账实时回传，自动匹配订单、回调商户。

> 仅用于本人合法收款、内部业务与技术研究。请遵守所在地法律法规以及微信、支付宝等服务商的使用规则。微信侧实现基于固定版本客户端的只读观察，升级微信版本后必须重新校验签名。

---

## 1. 功能特性

- **易支付 V1 兼容网关**：`submit.php`（页面支付）、`mapi.php`（API 下单）、`api.php?act=order` 与 `query.php`（订单查询），MD5 签名。
- **双通道收款**：微信收款码 + 支付宝收款码；普通浏览器按渠道展示二维码，支付宝内置浏览器可直接点击按钮打开支付宝收款链接。
- **微信实时到账**：Windows 原生单 EXE 监控端，从微信固定版本的已解包协议对象中读取结构化支付通知（金额、交易号、时间戳），毫秒级入队并上报。
- **微信通道可用性守卫**：监控端离线、心跳超时或 Hook 未就绪时，微信收银台不再展示收款码，改为提示改用支付宝重新下单，避免付款人把钱付进无法自动核销的订单。
- **支付宝实时到账**：官方证书模式账务查询，Durable Object 在有存活订单期间每 2 秒轮询一次，无订单自动停止。
- **订单尾差匹配**：可为同金额并发订单自动增减 1 分（方向与次数可配），提高匹配确定性。
- **商户回调**：易支付风格 GET 通知，响应体需为 `success`；失败按 `0 → 60s → 5m → 15m → 30m → 60m` 退避重试，最多 6 次。
- **后台登录人机验证**：可选接入 Cloudflare Turnstile，站点 Key 与 Secret Key 同时配置即启用，登录时先过人机验证再比对口令。
- **D1 用量预警**：管理端读取 Cloudflare GraphQL Analytics，展示每日读写行数、存储量和七日趋势；查询结果仅缓存于 Worker 内存。
- **管理后台**：Vue 3 单页应用，复用 cffk 的 shadcn-vue 组件库与后台布局（可折叠侧边栏 + 面包屑 + 卡片式内容区），包含面板、微信渠道、支付宝渠道、订单管理、未匹配收款、支付配置、站点设置、安全配置和账户设置，支持随机密钥生成、订单详情与完整上报数据查看、人工补单。
- **安全存储**：管理员密码 PBKDF2-SHA256（16 字节盐、600,000 次等效迭代，因 Workers 单次上限 100,000 而拆成 6 轮链式派生）；支付宝凭据用 `SESSION_SECRET` 派生的 AES-GCM 加密后存入 D1；微信登录二维码、监控状态与 nonce 只短暂保存在 `MonitorState` 内存；监控端请求使用 HMAC-SHA256 + 时间戳 + 一次性 nonce。
- **单文件分发**：监控端编译为一个 EXE，观察 DLL 内嵌其中，运行时按需释放。

---

## 2. 架构

| 组件 | 说明 |
| --- | --- |
| Cloudflare Worker (`src/index.ts`) | Hono 应用：易支付网关、收银台、管理 API、监控端 API、静态资源 |
| Cloudflare D1 (`cfpay_db`) | 正式订单、收款事件、回调任务与尝试记录、系统设置、收款码内容、管理员设置、支付宝凭据 |
| Durable Object `AlipayLedgerPoller` | 支付宝账务轮询，仅在存在待支付订单时活跃 |
| Durable Object `MonitorState` | 监控设备、Hook、微信登录、登录二维码和 nonce 的瞬时内存状态；不写 D1 |
| Durable Object `RuntimeDataState` | D1 读穿缓存；内存未命中才读取 D1，业务写入后写穿或失效相关缓存 |
| Cron Trigger（每分钟） | 扫描并重试到期商户回调；仅在发现超时租约时恢复任务 |
| Workers Static Assets (`public/`) | 收银台资源与 Vue 管理后台生产构建产物 |
| Windows 监控端 (`native/wechat-observer/`) | 单 EXE：微信进程发现、Hook 生命周期、结构化支付通知解析、持久化队列、HMAC 上报、托盘与配置窗口 |
| 研究诊断 DLL | 会话摘要、记录槽位、NewSync 载荷等独立诊断，仅用于逆向取证，不参与生产收款 |

金额一律以整数「分」存储与计算，不使用浮点数。

数据流（微信）：

```text
微信客户端收到到账通知
  → NewSync 响应解析（Weixin.dll + 0x106fb10，DR0 入口 / DR1 返回采样）
  → CmdItem(cmdid=5) 载荷 protobuf → paymsg 结构化字段
  → 观察 DLL 通过命名管道输出 JSON（只转发结构化字段）
  → 监控端 EXE 校验并生成事件（eventId = sha256(transid)）
  → 持久化队列 → HMAC 上报 POST /api/monitor/payment
  → Worker 匹配订单 → 置为 PAID → 触发商户回调
```

数据流（支付宝）：

```text
下单 → 激活 AlipayLedgerPoller → 每 2 秒调用官方账务查询接口
  → 命中金额与时间窗口的账务 → 写入收款事件 → 匹配订单 → 回调
```

更详细的设计说明见 [`docs/architecture.md`](docs/architecture.md)。

---

## 3. 目录结构

```text
src/                 Worker 源码（网关、订单、监控、支付宝、鉴权）
admin-ui/            管理后台前端源码（Vue 3 + Vite + Tailwind v4 + reka-ui）
public/              Worker 静态资源：落地页样式与 admin-ui 的构建产物 admin-app/
dist/                构建输出（已 Git 忽略）：只放 single-exe 产物与其构建缓存，
                     一次性调试脚本与接口响应转储不要写进来
migrations/          D1 迁移脚本（0001 ~ 0013）
native/wechat-observer/  Windows 原生监控端与观察/诊断 DLL（C++17，MinGW-w64）
tools/               构建、部署、审计、诊断与联调脚本
tools/ghidra/        Ghidra headless 逆向脚本（可复现的偏移与签名来源）
docs/                架构、易支付接口、监控端协议、原生观察器、集成日志
config/              本机配置样例；*.local.json 与证书目录已被 Git 忽略
tests/               Vitest 单元与集成测试
work/                逆向取证工作区（scope、时间线、证据）
```

---

## 4. 快速开始（本地）

要求 Node.js 20+。

```bash
npm install
npm run admin:install               # 安装管理后台前端依赖（独立 package）
npm run admin:build                 # 先构建 Windows 单 EXE，再将其打入 Vue SPA
cp .dev.vars.example .dev.vars      # 填写 ADMIN_PASSWORD 与 SESSION_SECRET
npm run db:migrate:local
npm run dev
```

前端热更新可另开一个终端跑 `npm run admin:dev`（Vite 默认 5173 端口），它请求的 `/admin-api/*` 需要自行代理到 `wrangler dev`；只改 Worker 时无需重启前端构建。

后台入口（默认路径来自 `wrangler.jsonc` 的 `ADMIN_PATH`）：

```text
http://localhost:8787/admin-change-me
```

若本机 glibc 低于 Workerd 要求，可用 Docker 跑迁移与开发服务：

```bash
docker run --rm -u "$(id -u):$(id -g)" -v "$PWD:/app" -w /app node:22-bookworm-slim npm run db:migrate:local
docker run --rm -u "$(id -u):$(id -g)" -p 0.0.0.0:8787:8787 -v "$PWD:/app" -w /app node:22-bookworm-slim npm run dev -- --ip 0.0.0.0 --port 8787
```

---

## 5. 部署到 Cloudflare

仓库里的 `wrangler.jsonc` 只有占位配置：不含生产域名、Cloudflare Account ID 与真实 D1 ID，`database_id` 为全零占位符，默认使用 `workers.dev` 地址。真实部署参数放在**被 Git 忽略**的 `wrangler.local.jsonc`（本机）或仓库变量（CI）中。

本机首次部署：

```bash
npx wrangler login
npx wrangler d1 create cfpay_db            # 记下返回的 database_id
cp wrangler.jsonc wrangler.local.jsonc     # 被 .gitignore 忽略，填真实 database_id、域名与后台入口
# 在 wrangler.local.jsonc 中修改 vars：ADMIN_PATH（随机长路径）、ADMIN_USERNAME
npx wrangler secret put ADMIN_PASSWORD
npx wrangler secret put SESSION_SECRET     # openssl rand -hex 32
npx wrangler secret put CLOUDFLARE_ANALYTICS_TOKEN  # Account / Account Analytics / Read
npm run deploy                             # = admin:build + 远程迁移 + wrangler deploy
```

`tools/deploy.mjs` 会优先使用 `wrangler.local.jsonc`；没有该文件时，用 `CLOUDFLARE_API_TOKEN` 自动发现 Account ID、`cfpay_db` 的 D1 ID 和 `cfpay` 已绑定的自定义域名，并复用线上现有的明文变量（后台入口、管理员账号等），再生成临时配置。也可以用 `CLOUDFLARE_ACCOUNT_ID`、`CFPAY_D1_DATABASE_ID`、`CFPAY_DOMAIN` 覆盖自动发现的结果；发现不到就退出，不会把占位 ID 部署上线。`node tools/deploy.mjs --check` 只校验配置、不发布。

自定义域名：Workers 项目 → **Settings → Domains & Routes → Add → Custom Domain**，选择已接入 Cloudflare 的域名；生产实例的域名只写在 `wrangler.local.jsonc` 或 CI 变量里，不提交到仓库。

GitHub Actions 部署（`.github/workflows/deploy.yml`）只需要 Secret `CLOUDFLARE_API_TOKEN`：部署脚本会自动发现 Account ID、D1 ID 与已绑定的自定义域名。仓库变量 `CLOUDFLARE_ACCOUNT_ID`、`CFPAY_D1_DATABASE_ID`、`CFPAY_DOMAIN` 可选，用于覆盖自动发现结果。

更换 `SESSION_SECRET` 后，D1 中现有支付宝凭据密文将无法解密，需要在后台重新上传支付宝证书；微信登录二维码只在内存中短暂存在，不受密钥轮换迁移影响。

---

## 6. 管理后台

管理后台是独立的 Vue 3 单页应用：源码在 `admin-ui/`，用 Vite 构建到 `public/admin-app/`（已 Git 忽略，部署前必须先构建）。技术栈与 cffk 一致——**Vue 3 + Tailwind CSS v4 + reka-ui（shadcn-vue new-york / neutral 主题）+ lucide 图标 + vue-sonner 提示**；`admin-ui/src/components/ui/` 下的组件直接来自 cffk 的组件库，`AdminSidebar` / `AdminLayout` / `AdminPageHeader` / `AdminDataTable` 也沿用 cffk 的后台布局与写法，不再有任何手写的 HTML / CSS / 原生 JS 管理页。

| 路由 | 页面 | 内容 |
| --- | --- | --- |
| `/` | 登录 | 未登录时唯一可见页面，不带侧边栏 |
| `/dash` | 面板 | 今日订单、收款汇总和订单趋势 |
| `/channels/wechat` | 微信渠道 | PC 监控端密钥、在线设备、微信登录状态、Hook 状态与微信收款码 |
| `/channels/alipay` | 支付宝渠道 | App ID、应用私钥、应用公钥证书、支付宝公钥证书、根证书；支持测试账务查询与删除配置 |
| `/orders` | 订单列表 | 最近订单、状态、通知状态、人工补单、**详情弹窗**（订单 + 收款事件 + 结构化字段 + 上报 JSON + 回调任务与尝试记录） |
| `/unmatched` | 未匹配收款 | 已收到但尚未匹配订单的收款事件，点击详情查看完整结构化数据 |
| `/system/gateway` | 支付配置 | 易支付兼容网关地址、四个接口路径、商户 ID / 商户密钥、订单有效期与尾差规则 |
| `/system/site` | 站点设置 | 收银台站点名称、副标题、网站图标和页脚文案；保存后新订单立即使用 |
| `/system/security` | 安全配置 | 后台入口路径与 Cloudflare Turnstile |
| `/account` | 账户设置 | 修改管理员账号和登录密码；保存后当前会话失效并要求重新登录 |

设计要点：

- **入口即路由 base**：`vue-router` 的 history base 来自 Worker 下发 `index.html` 时注入的 `window.__CFPAY_ADMIN_BASE__`，入口路径不写进构建产物；入口及其所有子路径都由 Worker 返回同一份 SPA 首页，子路由刷新不会 404。
- **产物不直连**：`/admin-app`、`/admin-app/index.html`、`/admin`、`/admin.html`、`/admin.js` 一律 404，`admin-app` 也进了入口保留首段，后台只能从管理员配置的入口进入。
- **资源层跳转已处理**：Cloudflare 资源层默认 `html_handling` 会把 `/admin-app/index.html` 以 307 跳到目录形式，Worker 直接请求目录形式并保留一次跟随跳转的兜底，避免入口变成一个跳转响应。

- **设备不持久化**：管理端只展示最近 15 秒内有状态上报的监控端；连续三次（约 15 秒）未收到 5 秒一次的上报即判定离线，刷新页面不会继续展示离线设备。超过 10 分钟无上报的设备及其组件状态由定时任务删除。
- **二维码按需显示**：默认不渲染。设备上报 `QR_READY` 且未过期时才出现「显示登录二维码（剩余 mm:ss）」按钮，点击才弹窗加载；上报 `LOGGED_IN` 或二维码失效自动关闭；收到新二维码自动换图。不点击就永远不会请求二维码接口。
- **设置按字段部分更新**：`PUT /admin-api/settings` 只更新本次提交的字段，多个表单互不覆盖。

---

## 7. 易支付兼容接口

| 用途 | 方法与路径 |
| --- | --- |
| 页面支付 | `POST|GET /submit.php` |
| API 支付 | `POST|GET /mapi.php` |
| 标准订单查询 | `POST|GET /api.php?act=order` |
| 兼容订单查询 | `POST|GET /query.php` |

签名：删除空参数与 `sign`、`sign_type`，其余参数按名称 ASCII 升序拼接 `key=value&...`，末尾直接追加密钥，取小写 MD5。

商户回调：向订单的 `notify_url` 发起 GET，携带 `pid`、`trade_no`、`out_trade_no`、`type`、`name`、`money`、`trade_status`、`param`、`sign`、`sign_type`；商户需返回纯文本 `success`，否则按退避策略重试（最多 6 次）。

完整参数、返回字段与错误码见 [`docs/epay-api.md`](docs/epay-api.md)。

---

## 8. 收款通道实现

### 8.1 支付宝

- 后台上传 App ID 与四份证书 / 私钥文件，服务端用 `SESSION_SECRET` 派生 AES-GCM 密钥加密后存入 D1（也可用 `npm run alipay:worker-secrets` 生成 Worker Secrets 作为回退）。
- 下单后激活 `AlipayLedgerPoller`，在存在待支付订单期间全局每 2 秒查询一次账务；无订单自动停止，避免无谓调用。
- 本地联调：`npm run alipay:test`（读取 `config/alipay-test.local.json`）。

### 8.2 微信

目标固定为 **Weixin 4.1.15.13**（`Weixin.dll` 大小 201,552,944，SHA-256 `10f8e995453e2da46d4f2b5080cd6da1f13cc5147746adc119ceae38cb039de5`）。磁盘文件与进程内存都会二次校验版本、SHA-256 与函数签名字节，任何一项不符即拒绝安装并上报 `UNSUPPORTED`。

观察点（硬件断点，不修改微信代码页）：

| 槽位 | 地址 | 用途 |
| --- | --- | --- |
| DR0 | `Weixin.dll + 0x106fb10` | `NewSyncResponse` 解析入口 |
| DR1 | 入口栈顶返回地址（按线程动态） | 解析返回后读取已解包响应对象 |
| DR2 | `Weixin.dll + 0x0dc08d0` | 当前登录账号（`wxid`、登录状态） |
| DR3 | `Weixin.dll + 0x004b690` | 当前账号昵称 |

已还原的对象布局：

```text
NewSyncResponse   +0x00 虚表(0x8fc0d48)  +0x08 命令列表  +0x30 存在位图
命令列表          +0x08 命令指针数组     +0x10 命令数量
micromsg.CmdItem  +0x00 虚表(0x93d3c78)  +0x08 载荷对象  +0x10 CmdID  +0x18 存在位图   (0x20 字节)
载荷对象          +0x00 虚表(0x8e3d058)  +0x08 std::string(或空串哨兵 0xb916de0)       (0x20 字节)
std::string       +0x00 数据指针(容量≥16) +0x10 长度      +0x18 容量
```

`CmdID=5` 载荷是消息 protobuf：`field2/3` 收付双方、`field4` 消息类型（`10002` = sysmsg）、`field5.1` 内容体、`field9` 创建时间（Unix 秒）、`field12` 服务端消息号。内容体为 `<sysmsg type="paymsg">`，监控端按标签名取结构化字段：

| 字段 | 含义 |
| --- | --- |
| `PayMsgType` | 支付消息子类型（收款到账为 9） |
| `transid` | 唯一交易号，用作事件标识来源 |
| `fee` | **金额（整数分）** |
| `feetype` | 币种（1 = CNY） |
| `timestamp` | 支付时间（Unix 秒） |
| `status` | 状态（1 = 成功） |
| `type` | 收款类型（收款码为 `wx_f2f`） |
| `scene` / `outtradeno` | 场景 / 微信支付侧商户单号 |
| `username` / `displayname` | 付款方账号与昵称 |

判定条件全部来自协议字段：`msgType=10002`、`status=1`、`fee>0`、`feetype=1`、`type` 等于配置的 `wechatPayTypeFilter`（默认 `wx_f2f`）、消息 `toUsername` 等于本机已确认收款账号、`transid` 非空。**不解析展示文案、不做 OCR、不读取数据库、不接触加密网络流量。**

事件标识为 `wechat:` + `sha256(transid)`，天然唯一；同一笔到账的重复通知由监控端持久化队列与 Worker 双层去重。上报的 `raw` 是解析后的结构化 JSON（不含微信原文），完整落库并可在订单详情中查看。

实测指标：到账 → 入队 2~11 毫秒，上报成功约 1 秒（网络正常时）。

### 8.3 微信通道可用性守卫

微信到账完全依赖 PC 监控端：Hook 不在线时付款人扫码付的钱系统根本看不到，订单会一直停在 `PENDING` 直到过期，连「未匹配收款」都不会有记录。因此收银台在渲染前先判定通道可用性（`getWechatChannelStatus`）：

| 判定 | 条件 | 收银台表现 |
| --- | --- | --- |
| `OK` | 存在 `wechat_hook` 组件上报 `HEALTHY`，微信已登录，且统一状态上报在 15 秒内 | 正常展示微信收款码 |
| `NO_MONITOR` | 没有任何 `wechat_hook` 组件记录（或表不存在） | 隐藏收款码，提示「当前没有接入微信收款监控端」 |
| `STALE` | 统一状态上报达到 15 秒 | 隐藏收款码，提示「微信收款监控端已离线」 |
| `UNHEALTHY` | 上报新鲜但状态不是 `HEALTHY`（`STARTING` / `DEGRADED` / `ERROR` / `STOPPED` / `UNSUPPORTED`） | 隐藏收款码，提示「微信收款监控端未就绪（微信未登录或 Hook 未激活）」 |

不可用时收银台统一提示「请返回商户页面，改用支付宝重新下单支付」。这里刻意**不做原地改渠道**：`merchant_order_no` 是唯一键，同一商户单号换渠道会与商户对账口径冲突，因此让商户用新的 `out_trade_no` 重新下 `type=alipay` 的单。

守卫只作用于 `status = 'PENDING'` 的微信订单：已支付或已失效的订单继续按原样渲染，避免对已经付完款的人显示「不可用」。

`GET /api/orders/:id/status` 对仍在 `PENDING` 的微信订单额外返回 `wechatAvailable` 布尔值；收银台每秒轮询该接口，发现可用性翻转即整页刷新，监控端恢复后无需付款人手动刷新就能看到收款码。支付宝订单不依赖 PC 监控端（走账务 API 轮询），因此永不隐藏收款码。

支付宝订单会根据请求的浏览器标识判断是否处于支付宝客户端内。普通浏览器继续显示支付宝收款二维码；支付宝内置浏览器把二维码替换为“打开支付宝付款”按钮，用户点击后直接跳转到二维码原文。页面只允许 `https://qr.alipay.com/` 和 `alipays:` 目标用于直接跳转，其他协议继续回退为二维码，避免形成脚本链接。

站点页脚按行配置：不含 `|` 的行显示普通文字；“显示文字|邮箱、链接或域名”生成 `<a>` 链接并在新窗口打开。裸域名自动补全 HTTPS，显式危险协议不会生成链接。

协议细节与验证记录见 [`docs/wechat-native-observer.md`](docs/wechat-native-observer.md) 与 [`docs/integration-journal.md`](docs/integration-journal.md)。

---

## 9. PC 监控端

### 9.1 单 EXE 与内嵌 DLL

发行包只有一个文件：`cfpay-wechat-monitor.exe`（约 5.3 MB）。观察 DLL 用 `.incbin` 放进 EXE 只读数据段，启动时释放到 EXE 同目录的 `cfpay-hook-observer.dll` 再注入微信：

- 已存在且字节完全一致 → 不重写；
- 需要更新 → 先写 `.tmp` 再原子替换；
- 文件被运行中的微信占用且内容不同 → 报 `OBSERVER_DLL_LOCKED_BY_RUNNING_WEIXIN`，要求先完整退出微信（禁止原地覆盖升级）。

EXE 只导入 Windows 系统 DLL（`kernel32`、`user32`、`gdi32`、`shell32`、`ole32`、`comctl32`、`version`、`winhttp`、`bcrypt`、`msvcrt`），运行库静态链接，不需要 Node、PowerShell 或任何常驻依赖。

发布到 GitHub Release 页时，同一个 EXE 会按支持的微信版本重命名为 `cfpay-wechat-monitor-<监控端版本>-weixin-<微信版本>-win64.exe`（当前为 `cfpay-wechat-monitor-0.9.0-weixin-4.1.15.13-win64.exe`），字节内容与固定名副本完全一致；文件名里的微信版本就是观察器唯一通过静态校验的目标版本，详见 10.1。

> 构建不可逐字节复现：PE 时间戳每次不同，因此每次新构建的 DLL 哈希都会变化，升级时必须先退出微信。

### 9.2 配置文件

`config.json` 与 EXE 同目录（缺失时可回退读取 `%LOCALAPPDATA%\CFPay\WeChatMonitor\config.json`）：

| 字段 | 必填 | 说明 |
| --- | --- | --- |
| `baseUrl` | 是 | Worker 网关地址，必须 `https://` |
| `monitorId` | 是 | 监控端 ID，可在后台随机生成 |
| `monitorSecret` | 是 | 与后台一致的 PC 监控端密钥（≥32 位） |
| `expectedReceiverUsername` | 否 | 已确认的收款 `wxid`；未确认时只记录候选并上报 `RECEIVER_CONFIRMATION_REQUIRED` |
| `wechatPayTypeFilter` | 否 | 收款类型过滤，默认 `wx_f2f`；留空表示不限制 |
| `weixinExecutablePath` | 是 | `Weixin.exe` 绝对路径 |
| `weixinDllPath` | 否 | `Weixin.dll` 路径，缺省由主程序推算 |
| `observerDllPath` | 否 | 观察 DLL 路径，默认 EXE 同目录（由内嵌映像释放） |
| `statePath` / `logPath` | 否 | 队列状态与日志路径，默认在 `%LOCALAPPDATA%\CFPay` |
| `debugLogging` | 否 | 是否写中文调试日志 |
| `processPollSeconds` / `queuePollSeconds` / `maxEventAgeSeconds` | 否 | 进程发现（5 秒）、队列发送（1 秒）与事件时间窗口（600 秒） |

状态上报间隔**不是配置项**：监控端写死 5 秒一次（`kReportIntervalMs`），服务端写死 15 秒判定失联（`MONITOR_ONLINE_WINDOW_MS`，即连续 3 次未收到）。两边做成可配置只会让口径悄悄错开，因此都不开放配置；配置文件里残留的 `heartbeatIntervalSeconds` 会被忽略。

### 9.3 运行时能力

- 进程发现：只接受绝对路径与配置一致的 `Weixin.exe`，且已加载 `Weixin.dll`。
- 登录辅助：优先原生点击「进入微信」按钮（不使用无障碍接口）；停在二维码页时截屏解码并上报 `QR_READY`，由后台重新生成二维码供管理员扫描；按钮点击后 20 秒仍无登录证据时**最多自动重启一次**微信，达到上限只上报 `LOGIN_REQUIRED`，绝不循环点击或循环重启。
- 健康判定：只有本次进程观察到 `loggedIn=true` + 有效 `wxid` 才上报 `HEALTHY / HOOK_ACTIVE`；未登录一律 `DEGRADED / WEIXIN_LOGIN_REQUIRED`，进程存在不等于健康。
- 队列：收款事件先落本地 JSON 状态文件，再按 HMAC 协议上报；失败指数退避（最长 5 分钟），进程重启后继续投递。
- 界面：单窗口整合状态与配置，托盘常驻；关闭窗口即隐藏到托盘，托盘菜单可显示状态、还原窗口、打开配置、退出。启动后自动静默校验微信目标，只有失败才在左下角提示具体原因。

### 9.4 监控端 API

- `POST /api/monitor/status`：统一上报设备在线、组件健康（`wechat_hook`、`alipay_ledger`）和微信登录状态
- `POST /api/monitor/payment`：收款事件

认证为 HMAC-SHA256：签名原文 `timestamp + "\n" + nonce + "\n" + sha256(rawBody)`，时间误差 300 秒，nonce 不可重用。完整协议与状态码见 [`docs/monitor-protocol.md`](docs/monitor-protocol.md)。

无 Windows 环境时可用命令行客户端联调：

```bash
export CFPAY_BASE_URL=https://pay.example.com
export CFPAY_MONITOR_SECRET=后台配置的监控端密钥
node tools/monitor-client.mjs status wechat_hook HEALTHY HOOK_ACTIVE "hardware breakpoint installed"
node tools/monitor-client.mjs payment wxpay 10.01
```

---

## 10. 编译与打包

依赖：`cmake`、`ninja`（可选）、`x86_64-w64-mingw32-g++`、`python3`、`sha256sum`。

一键生成单 EXE（推荐）：

```bash
npm run wechat:monitor:single                 # 等价于 bash tools/build-single-exe.sh
bash tools/build-single-exe.sh --clean        # 全新构建
bash tools/build-single-exe.sh --self-test    # 构建后运行 Windows 自检
bash tools/build-single-exe.sh --output /path/to/dist --jobs 8
```

脚本会输出 EXE 与内嵌 DLL 的大小、SHA-256、文件类型、导入表，并通过在 EXE 中检索 DLL 中段字节验证内嵌成功。版本号不在脚本里硬编码：监控端版本取自 `native/wechat-observer/wechat_monitor_version.h` 的 `kVersion`，微信版本取自 `native/wechat-observer/wechat_target.h` 的 `kWeixinVersion`，也可用 `--version` / `--weixin-version` 覆盖。

| 产物 | 说明 |
| --- | --- |
| `dist/single-exe/cfpay-wechat-monitor.exe` | 固定名副本，供本机部署与安装脚本使用 |
| `dist/single-exe/cfpay-wechat-monitor-<监控端版本>-weixin-<微信版本>-win64.exe` | 发布工件，例如 `cfpay-wechat-monitor-0.9.0-weixin-4.1.15.13-win64.exe` |
| `dist/single-exe/BUILD_INFO` | 版本、观察器 API 版本、EXE 与内嵌 DLL 的 SHA-256、编译器、构建时间与提交号 |
| `dist/single-exe/SHA256SUMS` | 两个 EXE 的校验和清单，`sha256sum -c SHA256SUMS` 验证 |

### 10.1 管理后台前端

管理后台是 `admin-ui/` 下的独立 npm 包（与 Worker 的 TypeScript 7 工具链隔离，互不干扰）：

```bash
npm run admin:install     # 安装 admin-ui 依赖
npm run admin:build       # 先构建 Windows 单 EXE，再构建到 public/admin-app（base=/admin-app/）
npm run admin:typecheck   # vue-tsc 类型检查
npm run admin:dev         # 本地热更新
```

`npm run deploy` 已串好顺序：先由 `admin:build` 构建 Windows 微信监控端单 EXE，并把带版本号的发行文件复制到管理端静态资源，再执行 Vite 构建、应用远端迁移，最后运行 `wrangler deploy`。微信渠道页会显示支持的微信版本、文件大小与 SHA-256，并提供同源下载。构建产物 `public/admin-app/`、`admin-ui/public/downloads/` 与 `admin-ui/node_modules/` 都在 `.gitignore` 中，仓库只保存源码。

GitHub 的部署工作流会在执行 `admin:build` 前安装 `g++-mingw-w64-x86-64`、`cmake` 与 `ninja-build`。如果调整管理端构建顺序，不得删除该工具链步骤，否则 Linux Runner 无法生成要打入前端的 Windows 单 EXE。

依赖与 cffk 对齐：`vue`、`vue-router`、`reka-ui`、`@vueuse/core`、`@lucide/vue`、`class-variance-authority`、`clsx`、`tailwind-merge`、`vue-sonner`、`@tanstack/vue-table`、`jsqr`，构建侧为 `vite`、`@vitejs/plugin-vue`、`tailwindcss`、`@tailwindcss/vite`、`vue-tsc`。主题令牌取自 cffk 的 `pages/tailwind.css`，只去掉了店面专用的 `login-payment` / `login-delivery` 令牌与富文本样式。

### 10.2 发布到 GitHub Release

发布由 `.github/workflows/release.yml` 完成，触发方式是推送 `v*` 标签（也支持在 Actions 页面手动 `workflow_dispatch`，可选标记为预发布）：

```bash
# 1. 改版本号：只改 native/wechat-observer/wechat_monitor_version.h 的 kVersion
#    （EXE 上报、WinHTTP UA、配置界面探测请求和构建脚本工件名都从它派生）
# 2. 本地验证构建
bash tools/build-single-exe.sh --clean
# 3. 提交后打标签并推送
git tag v0.9.0
git push origin v0.9.0
```

Workflow 在 `ubuntu-latest` 上安装 `g++-mingw-w64-x86-64`、`cmake`、`ninja-build`，执行同一个构建脚本，校验 `SHA256SUMS`，检查标签与源码监控端版本一致（不一致直接失败，防止工件与版本号错配），再用内置 `GITHUB_TOKEN` 创建 Release 并上传版本化 EXE、`SHA256SUMS`、`BUILD_INFO`；Release 已存在时用 `--clobber` 覆盖同名工件。发布说明自动生成版本信息表、工件清单、部署步骤与上一标签以来的提交列表。

构建全部原生目标（含研究诊断 DLL 与自有测试目标）：

```bash
tools/build-wechat-hook-harness.sh            # 产物在 dist/wechat-hook-harness/
npm run wechat:monitor:build                  # 生成单 EXE 发行包目录
```

| 产物 | 用途 |
| --- | --- |
| `cfpay-wechat-monitor.exe` | 生产单 EXE（内嵌观察 DLL、宿主与配置界面） |
| `cfpay-hook-observer.dll` | 生产观察器（由 EXE 内嵌释放） |
| `cfpay-hook-test-target.exe` | 自有测试目标，验证 Hook 与管道协议，不接触微信 |
| `cfpay-hook-diagnostic.dll` | 会话摘要显示链诊断（历史研究） |
| `cfpay-hook-record-diagnostic.dll` | `0x300` 服务会话记录槽位转储 |
| `cfpay-hook-sync-diagnostic.dll` | NewSync 命令与载荷 protobuf 转储 |
| `cfpay-hook-network-diagnostic.dll` | NewSync / AddMsg 解析与调用链诊断 |
| `cfpay-hook-wmpf-diagnostic.dll` | 小程序渲染进程事件诊断 |

诊断宿主命令（均在 EXE 内，目标与文件名白名单严格校验）：

```text
cfpay-wechat-monitor.exe --internal-host --run-weixin <pid> <observer.dll>
cfpay-wechat-monitor.exe --internal-host --stop-weixin <pid>
cfpay-wechat-monitor.exe --internal-host --run-weixin-record-diagnostic <pid> <diagnostic.dll>
cfpay-wechat-monitor.exe --internal-host --run-weixin-sync-diagnostic <pid> <diagnostic.dll>
cfpay-wechat-monitor.exe --internal-host --inspect-weixin <Weixin.dll>
cfpay-wechat-monitor.exe --self-test
```

诊断输出只写本机文件、逐行落盘，不进入收款队列、不调用 Worker；含消息内容的转储文件在分析完成后应删除。

---

## 11. 安装与运维

```powershell
# 安装（只复制单个 EXE，运行时自动释放观察 DLL）
powershell -ExecutionPolicy Bypass -File tools\install-windows-wechat-monitor.ps1 `
  -PackageDirectory dist\windows-wechat-monitor `
  -ConfigPath config\wechat-monitor.local.json `
  -StartNow

# 只检查、不安装（输出 BINARY_UPGRADE_REQUIRED / CLEAN_UPGRADE_READY）
powershell -ExecutionPolicy Bypass -File tools\install-windows-wechat-monitor.ps1 `
  -PackageDirectory dist\windows-wechat-monitor `
  -ConfigPath config\wechat-monitor.local.json -CheckOnly

# 卸载（含开机自启）：追加 -Uninstall
```

升级规则：必须**先完整退出微信与托盘监控端**再替换 EXE。观察 DLL 已被微信加载时无法替换文件，强行覆盖会造成磁盘与内存映像不一致。

只读生产复核（不创建订单、不改配置、不输出密钥）：

```powershell
powershell -ExecutionPolicy Bypass -File tools\audit-production-readiness.ps1 -ConfigPath config\production.local.json
# 代理未设为系统代理时可加 -ProxyUrl http://127.0.0.1:7890
```

轮换易支付商户密钥（同步更新被忽略的本机生产配置，只输出短指纹）：

```powershell
powershell -ExecutionPolicy Bypass -File tools\rotate-production-merchant-key.ps1 -ConfigPath config\production.local.json
```

回调链路通过管理端“快捷支付”使用真实付款验证，快捷支付订单使用唯一的内置签名回调地址，并在页面展示到账采集、订单匹配和回调验签各阶段结果。

---

## 12. 安全与隐私

- 后台登录可选启用 Cloudflare Turnstile：`TURNSTILE_SITE_KEY` 与 `TURNSTILE_SECRET_KEY` 必须同时配置才启用，只配一个视为未启用（避免前端弹了验证框而后端不校验）。Secret Key 只放在 Worker Secret 中，后台只读展示“是否已配置”，绝不回显原文。启用后登录请求先经 Cloudflare `siteverify` 校验（令牌走 `x-captcha-response` 头，与 cffk 一致），通过后才比对账号口令；校验服务不可用时返回 503 并拒绝登录（fail-closed）。
- 管理后台是 Vue SPA，构建产物只能通过管理员配置的入口获取：`/admin-app`、`/admin-app/index.html`、`/admin`、`/admin.html`、`/admin.js` 全部 404，入口路径也不写进前端产物（由 Worker 下发时注入）。管理后台入口是随机长路径，账号密码保存为 PBKDF2-SHA256 哈希；修改后强制重新登录。入口路径与管理员账号本身不做字符集或长度限制，服务端按百分号解码后与配置值逐字比较，非 ASCII 入口也能正常访问。
- 管理员密码哈希为 PBKDF2-HMAC-SHA256、16 字节随机盐、256 位输出、600,000 次等效迭代。Cloudflare Workers 的 `crypto.subtle` 对 PBKDF2 单次调用有 100,000 次迭代的硬上限（超出抛 `NotSupportedError`），因此按 100,000 次一轮做链式派生：上一轮输出作为下一轮口令、盐不变，总工作量不变。`password_iterations` 必须是 100,000 的整数倍且不超过 1,000,000，与 `admin_setting` 的 CHECK 区间一致。
- 入口路径的保留首段（`admin`、`admin.html`、`admin.js`、`admin-api`、`api`、`api.php`、`index.html`、`mapi.php`、`pay`、`payment-assets`、`query.php`、`styles.css`、`submit.php`、`vendor`）会被拒绝：这些是 Worker 已注册的路由前缀或 `public/` 下的真实静态资源，占用后管理台会顶掉接口/资源，或反过来永远访问不到。`/admin` 与 `/admin.html` 始终返回 404，即使入口被配成同名也不放行。
- 支付宝凭据在 D1 中保存为 AES-GCM 密文，密钥由 `SESSION_SECRET` 派生；微信登录二维码不写 D1，只在 `MonitorState` 内存中保留至登录状态变化或会话过期。
- 收款码原图只在管理员浏览器内解析（原生 `BarcodeDetector` 或本地 `jsQR`），不上传到 Worker 或 D1。
- 监控端上报不携带聊天内容、内存地址、密钥或 HMAC 头；微信事件上报的是解析后的结构化 JSON。
- 监控端本机日志为中文，只记录金额分值、字节数、状态码与时间，不记录交易号原文、付款方昵称或消息正文。
- 逆向诊断输出的字符串默认做掩码（数字 → `#`、字母 → `a`），全文转储仅写本机文件并在分析后删除。

---

## 13. 测试与验证

```bash
npm run typecheck        # Worker 侧 TypeScript 类型检查
npm run admin:typecheck  # 管理后台 vue-tsc 类型检查
npm test                 # Vitest（当前 94 项）
```

管理后台前端渲染验证（需要一次性安装 Playwright 与 Chromium，不在项目依赖内）：

```bash
npm i -D playwright && npx playwright install chromium
ADMIN_ENTRY=https://pay.example.com/<你的入口> node tools/verify-admin-ui.mjs
```

该脚本用真实浏览器打开线上后台，通过路由拦截把 `/admin-api/*` 换成固定夹具，因此无需管理员口令即可巡检登录页、九个业务页面、侧边栏与面包屑、订单详情弹窗、未匹配收款详情弹窗、微信登录二维码弹窗、密钥生成弹窗、客户端路由跳转、子路由刷新，并收集 console 错误与未捕获异常；全部通过退出码为 0，截图落在 `SHOT_DIR`。

原生侧验证：

```bash
bash tools/build-wechat-hook-harness.sh                       # 构建全部目标
node tools/verify-wechat-hook-harness.mjs <output.jsonl>      # 校验自有测试目标的 Hook 与管道协议
```

真实收款验证流程：

1. 后台确认监控端在线且组件为 `HEALTHY / HOOK_ACTIVE`、`hookOffset=0x106fb10`。
2. 用 `mapi.php` 创建一笔小额订单并打开支付页。
3. 完成支付后检查：监控端日志出现「支付通知已入队」，订单变为 `PAID`，`api.php?act=order` 返回 `status=1`。
4. 在订单列表点「详情」，核对结构化字段与上报 JSON。

---

## 14. 故障排查

| 现象 | 原因与处理 |
| --- | --- |
| `UNSUPPORTED / SIGNATURE_MISMATCH` | 微信版本或 `Weixin.dll` 不是固定版本，或函数签名字节不匹配；必须使用受支持版本并重新提取签名 |
| `OBSERVER_DLL_LOCKED_BY_RUNNING_WEIXIN` | 观察 DLL 已被运行中的微信加载且与新映像不同；完整退出微信后重启监控端 |
| `WEIXIN_LOGIN_REQUIRED` | 微信未登录或本次进程尚未观察到登录证据；完成登录后自动恢复 |
| `RECEIVER_CONFIRMATION_REQUIRED` | 尚未在配置中确认收款 `wxid`；在配置窗口选择并保存候选账号 |
| `ACCOUNT_MISMATCH` | 当前登录微信与已确认收款账号不一致；换回正确账号或更新配置 |
| `winError=12002` | WinHTTP 超时，通常是代理或到 Cloudflare 的出口抖动；事件留在本地队列并自动重试 |
| 设备在后台消失 | 统一状态上报达到 15 秒即判定离线并从管理端列表移除，超过 10 分钟由定时任务删除记录；确认监控端与网络状态 |
| 订单未匹配 | 金额、渠道或时间窗口不符（要求 `created_at ≤ paidAt ≤ expires_at`）；在「监控端 → 未匹配收款」查看详情 |
| 回调一直 `RETRYING` | 商户端未返回纯文本 `success`；在订单详情查看每次尝试的 HTTP 状态与响应摘要 |

---

## 15. 已知边界

- 微信侧只支持固定版本 4.1.15.13；微信更新后需要重新逆向并更新签名常量。
- 观察 DLL 以文件形式释放后由 `LoadLibraryW` 注入，运行目录会出现该 DLL；若要求磁盘不落 DLL，需改为反射式注入并重写已加载模块哈希校验。
- 收款到账依赖微信客户端在线并接收通知；客户端离线期间的到账不会补抓。
- 尾差匹配只在同渠道、同基础金额、订单有效期内生效。
- 构建产物含 PE 时间戳，不可逐字节复现。

---

## 16. 文档索引

| 文档 | 内容 |
| --- | --- |
| [`docs/architecture.md`](docs/architecture.md) | 系统架构与数据模型 |
| [`docs/epay-api.md`](docs/epay-api.md) | 易支付接口参数、返回与回调协议 |
| [`docs/monitor-protocol.md`](docs/monitor-protocol.md) | 监控端 HMAC 协议、状态码与心跳规则 |
| [`docs/wechat-native-observer.md`](docs/wechat-native-observer.md) | 原生观察器目标锁定、对象布局、登录辅助与诊断命令 |
| [`docs/integration-journal.md`](docs/integration-journal.md) | 逐条实现、验证与风险决策记录（含每次真实收款取证） |
| [`work/wechat-receipt-preview/`](work/wechat-receipt-preview/) | 逆向取证工作区：scope、时间线与证据 |
