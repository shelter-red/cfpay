# PC 监控端协议

## 请求认证

每个请求携带以下 Header：

```text
X-Monitor-Id: windows-main
X-Timestamp: 1790500000
X-Nonce: 32位以上随机字符串
X-Signature: HMAC-SHA256十六进制小写结果
Content-Type: application/json
```

签名原文：

```text
timestamp + "\n" + nonce + "\n" + SHA256(rawBody)
```

以后台配置的 PC 监控端密钥作为 HMAC-SHA256 Key。服务端允许时间误差为 300 秒，同一个 nonce 不允许重复使用。

## 状态上报（设备、组件与微信登录合并为一次请求）

```http
POST /api/monitor/status
```

设备在线、Hook 健康与微信登录状态回答的是同一个问题
（“这台监控端活着、Hook 正常、微信处于什么登录状态”）。当前版本只保留这一条上报，
服务端校验后将设备、组件、登录二维码和 nonce 写入单例 `MonitorState` 内存，不访问 D1。

```json
{
  "name": "主监控电脑",
  "platform": "windows",
  "version": "0.9.2",
  "component": "wechat_hook",
  "status": "DEGRADED",
  "targetVersion": "Weixin 4.2.0.11",
  "processId": 4308,
  "hookOffset": "0x10b0230",
  "detailCode": "WEIXIN_LOGIN_REQUIRED",
  "message": "未检测到已登录的微信账号",
  "observedAt": 1790617183000,
  "lastEventAt": 1790617183000,
  "login": {
    "status": "QR_READY",
    "qrPayload": "https://weixin.qq.com/x/example",
    "observedAt": 1790617183000
  }
}
```

- 节奏：写死 5 秒一次（监控端 `kReportIntervalMs`），不是配置项——配置化只会让监控端与服务端的失联判定口径悄悄错开。状态一有变化立即上报，没变化则按同一节奏重申；上报失败按 1 秒退避重试，不会每 250 毫秒空转一次。
- 上报出口只有一个：改状态的线程只写内存快照（`setComponentState` / `setLoginState`），真正发请求的只有网络线程的 `postStatus`。过去 Hook 状态与微信登录状态各有一条上报路径、各有一套去重记账，同一个周期会朝同一个路由发两三次内容重叠的请求，后台还会看到两条互相矛盾的时间线。
- `login` 是可选子对象，允许的键与它单独成一条路由时完全一致（见下文「微信登录状态」）。
- 监控端刚启动、还没拿到任何 Hook 状态时可以只发设备字段（此时也可能已经解析出登录二维码）；服务端只刷新内存中的设备与登录状态。
- 设备字段缺省时 `MonitorState` 保留已有值。
- `observedAt` 一律取上报当时的时间：服务端拒绝超过 10 分钟的 `observedAt`，沿用最初的观察时间会让长期稳定的状态在 10 分钟后被判过期。
- 上报的 `message` 一律是中文：观察 DLL 通过管道传回的内部英文描述由监控端统一翻译后再上报，映射不到时回退成 `detailCode` 的中文说明；指标串（`loginState=` 等）按既有中文标签转换。

在线判定只有一个窗口：

| 信号 | 窗口 | 含义 |
| --- | --- | --- |
| 设备在线 / 组件新鲜 | `MONITOR_ONLINE_WINDOW_MS` = 15 秒 | 连续 3 次（5 秒 × 3）没收到上报即判定失联：设备从后台列表消失，收银台判定微信通道不可用 |

设备心跳与组件状态由同一个请求写入，因此不存在第二个节奏，也就不需要第二个窗口。
0.9.1 期间曾用 90 秒的组件窗口兼容「每 30 秒才重发一次状态」的旧观察 DLL，该窗口已随旧节奏一起删除。

微信 Hook 正常示例：

```json
{
  "component": "wechat_hook",
  "status": "HEALTHY",
  "version": "0.2.0",
  "targetVersion": "Weixin 4.2.0.11",
  "processId": 4308,
  "hookOffset": "0x10b0230",
  "detailCode": "HOOK_ACTIVE",
  "message": "Hook 已安装并通过自检",
  "lastEventAt": 1790500000000,
  "observedAt": 1790500030000
}
```

微信未运行示例：

```json
{
  "component": "wechat_hook",
  "status": "STOPPED",
  "version": "0.2.0",
  "targetVersion": "Weixin 4.2.0.11",
  "detailCode": "PROCESS_NOT_FOUND",
  "message": "未检测到正在运行的微信主程序",
  "observedAt": 1790500030000
}
```

允许的 `component`：

- `wechat_hook`：微信显示前结构观察器。
- `alipay_ledger`：支付宝官方账务查询轮询器。

允许的 `status`：

- `STARTING`：组件正在启动或等待目标进程。
- `HEALTHY`：Hook 已安装且自检正常。
- `DEGRADED`：组件仍运行，但事件超时或部分检查异常。
- `ERROR`：安装、读取或上报失败。
- `STOPPED`：目标程序或组件已停止。
- `UNSUPPORTED`：目标程序版本、函数签名或运行环境不受支持。

后台达到 15 秒未收到组件状态时，会把该状态显示为“上报超时”。`detailCode` 应使用稳定的机器可读代码；`message` 只写脱敏诊断摘要，不上传聊天内容、内存地址、联系人或原始交易数据。

微信观察器标准诊断代码：

- `WAITING_PROCESS`：监控服务已启动，等待微信主进程。
- `HOOK_INSTALLING`：目标版本和函数签名已通过，正在安装观察点。
- `HOOK_ACTIVE`：观察点已安装，线程覆盖自检正常。
- `PROCESS_NOT_FOUND`：微信未运行。
- `PROCESS_DISCOVERY_FAILED`：Windows 监控服务无法枚举微信进程。
- `PROCESS_EXITED`：已观察的微信进程退出。
- `VERSION_MISMATCH`：微信或 `Weixin.dll` 版本不匹配。
- `SIGNATURE_MISMATCH`：目标 RVA 首字节签名不匹配，拒绝安装。
- `HOOK_INSTALL_FAILED`：观察点安装失败。
- `THREAD_COVERAGE_INCOMPLETE`：部分线程未安装硬件执行断点。
- `EVENT_TIMEOUT`：Hook 仍在，但超过配置时长没有任何解析命中。
- `READ_FAILED`：命中后读取结构失败，事件不会作为收款上报。
- `REPORT_RETRYING`：Cloudflare 状态或收款事件上报暂时失败，正在重试。

## 微信登录状态

微信登录状态不再是独立接口，而是 `POST /api/monitor/status` 载荷里的 `login` 子对象；
签名与鉴权自然也和状态上报完全相同（本来就是同一个请求）。

`login` 只允许 `status`、`observedAt` 和按条件出现的 `qrPayload`，多一个键就整批拒绝，
不接受失败原因、窗口截图、账号、昵称或其他诊断字段。`login` 不是 JSON 对象（字符串、数组等）同样拒绝，
避免畸形载荷被当成「没有登录状态」静默忽略。登录载荷非法时整批写入都不执行，
否则会出现「设备在线但登录状态是旧的」这种自相矛盾的记录。

允许的 `status`：

- `LOGIN_REQUIRED`：微信处于登录页，需要执行登录辅助。
- `LOGIN_CLICKED`：本机已点击微信登录按钮，等待进入主界面。
- `QR_READY`：本机已解析登录二维码，必须同时提交 `qrPayload`。
- `LOGGED_IN`：已进入登录后的微信主界面；服务端立即清除该设备旧二维码内存数据。
- `RESTARTING`：登录按钮超时后，监控端正在执行一次受限重启。

`login` 子对象示例：

```json
{
  "status": "QR_READY",
  "qrPayload": "https://weixin.qq.com/x/example",
  "observedAt": 1790500030000
}
```

二维码只在 `QR_READY` 时上行：其它状态即使内存里还留着二维码内容，监控端也不会放进载荷，
服务端同样会在非 `QR_READY` 时清空内存中的二维码内容，避免后台继续显示已经失效的登录码。

`qrPayload` 只允许 HTTP、HTTPS、`weixin:` 或 `wxp:` URL，最长 2048 字节。Worker 只把它保存在单例 `MonitorState` 内存，不写入 Durable Object Storage 或 D1，也不保存登录页截图。二维码会话有效期为 2 分钟，其他登录状态有效期为 3 分钟；每次读取或上报时由 `MonitorState` 清理过期记录。

管理员登录后，`GET /admin-api/monitors` 返回通用登录状态；只有 `QR_READY` 会给出受管理员会话保护的二维码地址。`GET /admin-api/wechat-login/:deviceId/qr` 从 `MonitorState` 读取二维码内容并重新生成 SVG，响应禁止缓存。登录成功、状态切换或会话过期后，该地址不再返回二维码。

## 收款事件

```http
POST /api/monitor/payment
```

请求体：

```json
{
  "eventId": "wechat:20260927:000001",
  "provider": "wxpay",
  "amount": "10.01",
  "paidAt": 1790500000000,
  "raw": "微信支付收款10.01元"
}
```

字段说明：

- `eventId`：PC 客户端生成并持久化的唯一事件编号，重试时保持不变。
- `provider`：`wxpay` 或 `alipay`。
- `amount`：十进制字符串，最多两位小数。
- `paidAt`：收款发生时间，Unix 毫秒时间戳或 ISO 8601 字符串。
- `raw`：可选的脱敏摘要，服务端最多保存 500 个字符。

响应示例：

```json
{
  "ok": true,
  "duplicate": false,
  "matched": true,
  "orderId": "系统订单号"
}
```

当 `matched` 为 `false` 时，事件会保留在后台“未匹配收款”中供人工核对。
