# 微信原生观察器设计

## 目标与边界

观察器只读取微信已经解包的当前账号资料，以及“微信收款助手”消息解析完成后的 `wcpayinfo` 结构，生成账号候选和收款候选事件并上报 Cloudflare Worker。

明确不做：

- 不 Hook 网络收发、TLS、HTTP 或 RPC。
- 不读取、解密或修改微信数据库。
- 不读取系统通知，不做 OCR。
- 不调用接受、拒绝、打开红包或其他微信业务函数。
- 不修改函数返回值或收款结构。
- 不绕过完整性检测、反调试或账号风控。

## 组件

### Windows 监控宿主

- 以普通用户权限运行，寻找加载 `Weixin.dll` 的微信主进程。
- 校验目标文件版本、SHA-256、收款与账号函数 RVA、入口签名和调用点。
- 创建本地命名管道和只读观察配置，再显式加载观察 DLL。
- 解析 DLL 输出的固定结构化事件，不接收原始 XML 或消息正文。
- 通过 HMAC 协议上报设备心跳、Hook 状态和收款事件。

### 原生观察 DLL

- 不包含 HTTP、Cloudflare 密钥或订单匹配逻辑。
- 使用 Windows 硬件执行断点观察目标函数，不改写 `Weixin.dll` 代码页。
- 收款解析器入口命中时保存 Windows x64 第一参数 `RCX`，在调用返回地址命中时只读输出结构；账号函数继续读取第二参数 `RDX`。
- 异常处理路径只复制到预分配环形队列；命名管道写入由独立工作线程完成。
- DLL 加载后不主动卸载。停止观察时清除硬件断点并进入惰性状态，DLL 随微信进程正常退出，避免重现动态代理卸载崩溃。

### Cloudflare Worker

- `/api/monitor/status` 是监控端唯一的状态上报入口：一次请求同时刷新设备在线状态、`wechat_hook` 组件健康状态与微信登录状态（`login` 子对象），服务端在同一个 batch 里写完三张表。
- `/api/monitor/payment` 接收已验证的收款事件并匹配订单。
- `/api/monitor/heartbeat` 仅作为 0.9.0 及更早监控端的过渡兼容路由保留；`/api/monitor/wechat-login` 已随合并删除。
- 管理端把设备在线、Hook 健康与微信登录状态合并在同一张卡片里显示，不再拆成两块。

## 目标锁定

当前只支持以下目标：

```text
微信版本：4.2.0.11
模块：Weixin.dll
文件大小：205929008
SHA-256：745cdf2955fa5128cdb00c056c97571156a5f107406a9bb15ac0754f3246b201
映像基址：0x180000000
收款观察入口 RVA：0x10b0230（NewSyncResponse 解析函数）
收款观察入口文件偏移：0x10af630
收款观察入口前 32 字节：55415741564155415657534883ec48488d6c2440 48c74500feffffff4889d6（连续 32 字节，见源码常量）
响应对象虚表 RVA：0x929f408
命令列表虚表 RVA：0x96d0558
命令对象 micromsg.CmdItem 虚表 RVA：0x96d04d8
账号资料函数 RVA：0x0e07f70
账号资料函数文件偏移：0x0e07370
账号资料入口前 32 字节：555657534881ecd8000000488dac248000000048c74550feffffff4889d64889
账号资料调用点 RVA：0x2915c3e
账号资料调用指令：e82d234ffe
账号资料调用目标：0x0e07f70
昵称资料入口 RVA：0x004b690，32 字节签名仍唯一命中。
昵称资料入口文件偏移：0x004aa90

4.2 类层级与 4.1 不同：`0x96d0558` 是命令列表解析器，`0x96d04d8` 是命令子项解析器；子项 `CmdID` 仍位于 `+0x10`，`+0x08` 指向字符串对象。此前把列表字段标成 CmdItem 的判断已更正。
```

任一条件不匹配时，不安装观察点，上报 `UNSUPPORTED / VERSION_MISMATCH` 或 `UNSUPPORTED / SIGNATURE_MISMATCH`。

## 运行时结构

生产收款链路读取微信自身已解包的 `paymsg` 结构化支付通知，不再使用展示文案，也不再使用旧的 `wcpayinfo` 反序列化器 `0x2293980` 或会话记录追加函数 `0x490d5c0`：

- `DR0` 挂在 `NewSyncResponse` 解析入口 `0x10b0230`，`DR1` 按线程挂在真实返回地址；返回后校验响应对象虚表 `+0x929f408`。
- 响应对象 `+0x08` 指向命令列表对象；列表 `+0x00` 虚表为 `0x96d0558`，`+0x08` 为命令指针数组、`+0x10` 为命令数量、`+0x20` 为协议声明数量、`+0x28` 为存在位图。
- 每个 `micromsg.CmdItem` 为 `0x20` 字节，虚表为 `0x96d04d8`，`+0x08` 指向 `std::string` 对象、`+0x10` 是 `CmdID`、`+0x18` 是存在位图；字符串对象 `+0x10` 为长度、`+0x18` 为容量，容量不小于 16 时 `+0x00` 才是数据指针。
- 载荷字节按 protobuf 线格式读取：`field2/3` 为收付双方，`field4` 为消息类型（`10002` 即 `sysmsg`），`field5.1` 为内容体，`field9` 为创建时间（Unix 秒），`field12` 为服务端消息号。
- 内容体是 `<sysmsg type="paymsg">` 协议文本，监控端按标签名取值：`PayMsgType`、`transid`、`username`、`displayname`、`timestamp`、`fee`（整数分）、`feetype`、`type`、`scene`、`status`、`outtradeno`。
- 判定条件全部来自协议字段：`msgType=10002`、`status=1`、`fee>0`、`feetype=1`、`type` 等于配置的 `wechatPayTypeFilter`（默认 `wx_f2f`）、消息 `toUsername` 等于本机已确认收款账号；`transid` 缺失即拒绝。
- 事件标识为 `wechat:` 加 `transid` 的 SHA-256，天然唯一，去重不依赖时间窗口；`paidAt` 直接取协议 `timestamp`。
- 观察 DLL 只按固定布局读取并转发结构化字段，不做关键词匹配、不做金额文本拆解；内容体按 2048 字节截断并做 UTF-8 码点边界规范化，避免宿主 JSON 解析失败。

自有测试目标仍保留旧的结构化收款入口/返回布局，用于命名管道回归。测试目标返回时，入口 `RCX` 保存的输出地址按以下布局读取：

```text
+0x1c8  paysubtype             int32
+0x1d0  feedesc                MSVC x64 std::string
+0x1f0  transcationid          MSVC x64 std::string
+0x230  begintransfertime      int32
+0x238  pay_memo               MSVC x64 std::string
+0x258  receiver_username      MSVC x64 std::string
```

字符串对象大小为 32 字节，长度在 `+0x10`，容量在 `+0x18`。容量小于 `0x10` 时数据位于对象内，否则 `+0x00` 为堆指针。每个字段设置 64 KiB 上限；长度、容量、指针或内存页检查失败时丢弃该事件并上报 `READ_FAILED`。

账号资料函数返回时，`RDX` 保存的输出地址按以下布局读取：

```text
+0x00  uin                    uint64
+0x08  username / wxid       MSVC x64 std::string
+0x28  logged-in state       byte
```

账号字符串只在函数返回点复制，不保留微信进程内指针；相同 `uin + username` 的重复命中在异常处理路径通过固定长度 FNV-1a 指纹抑制。昵称通过 `Weixin.dll + 0x9aef50` 的只读返回对象单独观察，使用第三个动态硬件断点槽，不修改微信代码、参数或返回值。该函数的 `RDX` 不是 `std::string`，而是一个 8 字节输出指针；指针目标对象的 `+0x04` 为 32 位字节长度、`+0x10` 为相对对象起点的数据偏移，且有效数据偏移不小于 `0x18`。观察器只在返回点按此布局完成边界检查和复制。

## 硬件断点流程

1. DLL 注册向量异常处理器。
2. 工作线程枚举目标进程线程，在每个线程的 `DR0` 安装收款解析器入口断点，在 `DR2` 安装账号资料函数入口断点。
3. `DR0` 命中时保存当前线程的 `RCX` 和栈顶返回地址，并把 `DR1` 临时设置为该返回地址。
4. `DR1` 命中时读取完整输出结构，复制到预分配事件槽，随后清除该线程的 `DR1`。
5. `DR2` 命中时保存账号输出地址和返回地址，并临时把同一槽位改为返回地址；返回后复制账号资料并恢复 `DR2`。
6. `DR3` 观察昵称函数入口，临时改为返回地址；返回后复制昵称字符串并恢复 `DR3`。
7. 工作线程持续覆盖新建线程，并校验已有线程的入口断点未被清除。
8. 覆盖不完整时仍保持进程可用，但上报 `DEGRADED / THREAD_COVERAGE_INCOMPLETE`。

异常处理器不得执行业务解析、文件写入、网络请求、动态内存分配或等待锁。

## 状态机

```text
STARTING / WAITING_PROCESS
  -> STARTING / HOOK_INSTALLING
  -> HEALTHY / HOOK_ACTIVE
  -> DEGRADED / EVENT_TIMEOUT | THREAD_COVERAGE_INCOMPLETE | REPORT_RETRYING
  -> ERROR / HOOK_INSTALL_FAILED | READ_FAILED
  -> STOPPED / PROCESS_EXITED

任意校验失败
  -> UNSUPPORTED / VERSION_MISMATCH | SIGNATURE_MISMATCH
```

观察 DLL 每 5 秒重发一次当前状态、状态变化立即重发；监控端由网络线程每 5 秒把最近一次已知状态连同设备信息与微信登录状态一起上报，因此服务端看到的节奏就是 5 秒。管理端按 15 秒（连续 3 次未收到）判定组件“上报超时”，与设备在线判定共用同一个窗口。`lastEventAt` 只表示最后一次成功读取结构的时间；长期没有收款时不能单独判定 Hook 失效，必须结合线程断点自检结果。

## 数据处理

DLL 只向宿主输出固定字段，不传输原始消息正文。宿主直接接收：

- `feedesc`：金额展示文本。
- `transcationid`：交易标识。
- `begintransfertime`：支付时间。
- `pay_memo`：备注。
- `receiver_username`：收款账号。

宿主必须同时检查收款账号、交易标识、金额和支付时间窗口。字段缺失、金额无法严格解析、账号未确认或时间超出窗口时，只记录脱敏诊断，不调用 `/api/monitor/payment`。

## 故障与恢复

- 微信未运行：上报 `STOPPED / PROCESS_NOT_FOUND`，等待进程启动。
- 微信升级：拒绝 Hook，上报 `UNSUPPORTED`，等待离线重新定位和发布新签名。
- 进程退出：清理宿主侧句柄，上报 `STOPPED / PROCESS_EXITED`，不尝试在已退出进程中卸载 DLL。
- Worker 暂时不可达：本地持久化状态和支付事件，指数退避重试；事件 ID 保持不变。
- 连续读取失败：停用该进程的观察点并上报 `ERROR / READ_FAILED`，不继续冒险读取。
- 微信出现完整性提示、账号告警或异常退出：停止该方案，不研究绕过方法。

## 验证门槛

1. 在自有测试进程验证硬件断点入口、返回点、线程新增和惰性停止。
2. 验证 DLL 全程不修改目标代码段，且停止后所有线程调试寄存器恢复。
3. 对伪造的 `std::string` 结构做边界和无效指针测试。
4. 对命名管道断开、环形队列满和宿主崩溃做恢复测试。
5. 验证所有状态转换均能在管理端出现。
6. 通过上述门槛后，才允许对微信做一次短时、受控运行验证。

## 自有测试进程

当前仓库提供不接触微信的 Windows x64 测试工具：

```bash
tools/build-wechat-hook-harness.sh
build_path=$(wslpath -w dist/wechat-hook-harness)
/home/user/.codex/skills/wsl-windows-bridge/scripts/psrun.sh \
  tools/run-windows-hook-harness.ps1 \
  -BuildDirectory "$build_path" \
  -Iterations 20
```

测试目标主动加载 DLL，并依次验证：

1. 已存在主线程命中两次解析函数。
2. DLL 加载后新建的线程被周期扫描发现并补装硬件断点。
3. 新线程命中一次解析函数。
4. 主线程和新线程分别命中账号资料函数，重复账号只输出一次。
5. `CfpayObserverStop` 从独立清理线程恢复所有存活线程的 `DR0/DR1/DR2/DR3`。
6. DLL 保持加载但进入惰性状态，停止后的解析和账号调用不再产生事件。
7. 目标进程正常退出，不执行运行期 `FreeLibrary`。

输出可使用以下命令验证：

```bash
node tools/verify-wechat-hook-harness.mjs \
  /mnt/c/Users/tc/AppData/Local/cfpay-hook-harness/observer-output-20.jsonl
```

## 受限宿主模式

受限宿主逻辑已编译进 `cfpay-wechat-monitor.exe`，生产命令统一使用 `--internal-host` 前缀：

```text
cfpay-wechat-monitor.exe --internal-host --inspect-weixin <Weixin.dll>
cfpay-wechat-monitor.exe --internal-host --test-inject <pid> <observer.dll>
cfpay-wechat-monitor.exe --internal-host --test-pipe-run <pid> <observer.dll>
cfpay-wechat-monitor.exe --internal-host --test-pipe-drop <pid> <observer.dll>
cfpay-wechat-monitor.exe --internal-host --run-weixin <pid> <observer.dll>
cfpay-wechat-monitor.exe --internal-host --stop-weixin <pid>
```

开发构建仍生成 `cfpay-wechat-host.exe`，仅用于既有离线脚本回归，不复制到生产发行目录。

`--inspect-weixin` 从 Windows 侧验证：

- 文件版本必须为 `4.2.0.11`。
- 文件大小和 SHA-256 必须与已分析样本一致。
- 解析器入口前 32 字节必须匹配。
- 唯一调用点 5 字节必须匹配，且 `CALL rel32` 必须解析回目标 RVA。

`--test-inject` 具有硬编码进程名限制，只接受 `cfpay-hook-test-target.exe`。加载器先取得目标进程的 `kernel32.dll` 基址，再使用本地 `LoadLibraryW` 相对偏移计算远程入口，不假设系统 DLL 在所有进程中地址相同。

测试命令：

```bash
build_path=$(wslpath -w dist/wechat-hook-harness)
/home/user/.codex/skills/wsl-windows-bridge/scripts/psrun.sh \
  tools/run-windows-hook-injection-harness.ps1 \
  -BuildDirectory "$build_path" \
  -Iterations 10
```

该测试只对仓库自有目标执行显式 `LoadLibraryW`，并额外确认对 PowerShell 等非测试进程的请求在打开远程进程前被拒绝。

## 命名管道传输

生产宿主在注入前创建以下只入站命名管道：

```text
\\.\pipe\cfpay-wechat-observer-{pid}
```

观察 DLL 连接后写入单行 JSON：

- `status`：Hook 安装、健康、降级、错误和停止状态。
- `event`：已读取的固定收款助手结构字段。
- `account`：已读取的当前账号数值标识、`wxid` 和登录状态。
- `nickname`：当前登录账号的昵称字符串；观察器在登录后资料写入链路 `Weixin.dll +0x04b690` 入口保存 `RDX + 0x28`，函数返回后按 32 字节 MSVC `std::string` 读取。它可能晚于 `account` 记录到达，监控端会按当前进程账号合并到候选状态。

DLL 每 5 秒重复输出 `HEALTHY / HOOK_ACTIVE`，供上层监控服务刷新管理端组件状态；节奏与监控端的上报周期一致，两边不存在第二个口径。宿主或管道断开导致写入失败时，DLL 会异步清除所有硬件断点、移除异常处理器并进入惰性状态，不尝试动态卸载。

`--run-weixin` 只接受进程映像名 `Weixin.exe`，并在创建管道和加载 DLL 之前重新执行磁盘版本、SHA-256、收款入口、账号入口、昵称入口和既有调用点校验。DLL 进入目标后还会对 `Weixin.dll` 内存中的同一组固定版本字节做第二次校验。

每次监控端启动、宿主重启或目标 PID 变化时，登录状态都从未知重新判断。断点安装成功只输出 `STARTING / WEIXIN_LOGIN_CHECKING`；只有本次进程观察到 `loggedIn=true`、非零账号标识和有效 `wxid` 后才输出 `HEALTHY / HOOK_ACTIVE`。明确登出或连续 90 秒未再次观察到登录账号时输出 `DEGRADED / WEIXIN_LOGIN_REQUIRED`，因此微信停在登录页时不会显示健康。

版本 `0.9.2` 包含独立登录辅助线程、NewSync 支付通知结构化 Hook、当前微信昵称只读 Hook、Windows 原生现场测试状态机，以及合并后的单一状态上报出口。线程不会把进程存在等同于已登录，也不会在登录证据未知时点击主界面：只有观察器明确给出未登录证据，或目标路径下的微信进程尚未加载 `Weixin.dll` 时，才检查同进程可见顶层窗口。

配置页“当前微信账号”下拉框显示 Hook 捕获的 `昵称（wxid）`。右侧“重新获取”按钮重新读取本机 Hook 候选状态并保留当前选择；尚未捕获昵称时会明确提示，配置页不再只能依赖窗口创建时的一次性读取。

- 二维码：仅在本机内存中使用 GDI 截取微信窗口并交给固定版本 `quirc` 解码；不使用 OCR，不保存或上传截图，不把二维码内容写入调试日志。识别成功后把 `QR_READY` 和二维码内容作为 `POST /api/monitor/status` 载荷里的 `login` 子对象上报，Worker 负责重新生成二维码。
- 登录按钮：先检查标准 Win32 按钮并发送 `BM_CLICK`；微信 `mmui::LoginWindow` 的实际 Win32 类为固定版本的 `Qt51514QWindowIcon`，不使用 Windows Active Accessibility。Qt 窗口只接受符合固定登录窗尺寸范围的竖向窗口，由 C++ 通过 `SendMessageTimeoutW` 在“进入微信”按钮中心发送鼠标消息；不再发送 `Enter`，避免键盘焦点位于“网络代理设置”时误入设置页。调用设置 3 秒超时，同一登录过程只点击一次。
- 受限重启：按钮调用 20 秒后仍没有本次进程的有效登录证据时，先请求观察器清理硬件断点，再只结束映像绝对路径与配置完全一致且文件名为 `Weixin.exe` 的进程，随后从同一绝对路径启动一次。一次监控端运行期间未成功登录前最多自动重启一次，禁止循环点击和循环重启。
- 状态上报：只上传 `LOGIN_REQUIRED`、`LOGIN_CLICKED`、`QR_READY`、`LOGGED_IN`、`RESTARTING` 及时间戳；按钮缺失、调用失败、重启错误等原因只进入本机中文调试日志，不发送 Worker。0.9.2 起登录状态不再单独成一条请求，而是随组件状态合并进 `login` 子对象。
- 状态去重：监控端为组件状态与登录状态各算一份内容指纹，与最近一次成功上报的指纹一致就不标记待上报，只由 5 秒周期重申；状态、详情、进程、二维码内容或最后收款时间任一变化都立即提交。上报失败时保留待上报标记并按 1 秒退避重试，不会每 250 毫秒空转一次。
- 登录成功：账号 Hook 产生新鲜有效登录证据后立即上报 `LOGGED_IN`，清除本轮点击和重启限制；每次监控端启动仍从未知状态重新判断，不复用旧健康结论。
- 二维码停点：同一微信进程一旦成功解析并上报 `QR_READY`，即使二维码短暂失效或界面识别回退为登录按钮，本轮也禁止自动点击；只有微信进程变化或完成登录后才清除该保护，避免扫码测试被自动登录打断。
- 现场测试：主窗口提供“登录测试”“扫码测试”“收款测试”三个按钮。登录测试由当前 EXE 受限重启微信、调用普通登录按钮并等待 `HOOK_ACTIVE`；扫码测试由当前 EXE 重启微信、调用固定位置的“切换账号”、上报二维码后暂停等待管理员扫码；收款测试只接受测试开始后的新事件，且仅在 `/api/monitor/payment` 返回 `matched=true` 时通过。
- 自动序列：从 Windows 本机 `WeChatFieldTest-*` 目录启动，或显式追加 `--field-test-sequence` 时，程序自动串行执行“登录测试 -> 扫码测试 -> 收款测试”。只有二维码扫描和真实付款需要管理员参与；任一阶段失败即停止，不用后续阶段掩盖失败。
- 执行边界：现场测试不得由 WSL 调用 Windows 程序发起。便携测试包可从 Windows 本机启动；当包内没有 `config.json` 时，EXE 只读复用 `%LOCALAPPDATA%\CFPay\WeChatMonitor\config.json`，不会把配置或密钥复制回仓库。

登录辅助的隔离测试使用自有窗口，不连接或操作真实微信：

```bash
build_path=$(wslpath -w dist/wechat-hook-harness)
/home/user/.codex/skills/wsl-windows-bridge/scripts/psrun.sh \
  tools/run-windows-wechat-login-assistant-harness.ps1 \
  -BuildDirectory "$build_path"
```

测试覆盖标准登录按钮调用、窗口二维码捕获与解码，以及把自有测试程序复制为临时 `Weixin.exe` 后按绝对路径执行一次结束和重启。重启测试会校验旧 PID 退出、新 PID 窗口出现，并只清理临时测试路径。

`--stop-weixin` 同样只接受已通过版本和签名校验的 `Weixin.exe`。宿主从目标进程已加载的 `cfpay-hook-observer.dll` 计算 `CfpayObserverStop` 导出地址，通过受限远程线程请求清除 `DR0/DR1/DR2/DR3` 和异常处理器；DLL 保持惰性加载，不执行动态卸载。该命令用于监控服务关闭和真实进程短时验证，避免依赖 5 秒心跳写失败进行延迟清理。

观察 DLL 使用 `STOPPED / STARTING / RUNNING / STOPPING` 四态生命周期。停止完成后会关闭工作线程、队列事件和旧命名管道，但保留 DLL 映像；后续 `--run-weixin` 发现同名 DLL 已加载时，不再重复 `LoadLibraryW`，而是比较目标内已加载 DLL 文件与当前发行包 DLL 的 SHA-256，直接解析目标进程内 PE 导出并校验运行时 API 版本，再调用 `CfpayObserverStart` 重新连接新管道并安装硬件断点。文件哈希、内存运行时版本不一致、实例仍在运行或生命周期调用失败时均拒绝恢复，要求等待旧实例停止或重启微信，避免新旧观察器混用。

真实进程短时验证使用 `tools/run-windows-weixin-observer-smoke.ps1`。脚本支持首次注入和已加载 DLL 原地恢复，等待 `HOOK_ACTIVE`、按指定秒数只读观察、调用 `--stop-weixin`、确认 `HOOK_STOPPED` 和微信进程仍存活。验证产物额外记录启动前是否已加载观察 DLL，并只保留状态、事件数量和账号事件数量，不保存 `wxid`、账号数值标识、转账双方或转账内容。

当前通过 `--test-pipe-run` 和 `--test-pipe-drop` 在自有测试目标验证正常传输与宿主提前断开恢复，并完成同一测试进程连续两轮 `WEIXIN_LOGIN_REQUIRED → HOOK_ACTIVE → 3 条收款 → 3 条账号状态 → HOOK_STOPPED` 的原地恢复测试。版本 `0.9.2` 已对真实微信磁盘 DLL 完成入口、调用点、版本和 SHA-256 静态校验；现场测试必须从 Windows 本机启动便携包或已安装监控端，不得通过 WSL 启动、停止或操作微信。

## Windows 单 EXE 监控端

生产发行包只有单个 `cfpay-wechat-monitor.exe`：观察 DLL 以只读数据形式内嵌在 EXE 中（`.incbin` 引用构建产物，不生成大体积 C 数组），启动时释放到 EXE 同目录的 `cfpay-hook-observer.dll` 再注入。已存在且内容完全一致时不重写；文件被运行中的微信占用且内容不同时，以 `OBSERVER_DLL_LOCKED_BY_RUNNING_WEIXIN` 明确要求先完整退出微信，不做原地覆盖升级。启动日志记录释放结果、字节数与 SHA-256，便于核对现场二进制。同一个 EXE 负责：

- 每 5 秒上报一次状态（`kReportIntervalMs`，写死在代码里、不可配置）。设备信息、组件状态与微信登录状态合并为同一个 `POST /api/monitor/status` 请求，不再单独发心跳，也不再有独立的登录上报路由；服务端以 15 秒内收到上报判定在线，即连续 3 次未收到即离线。
- 通过 Win32 Toolhelp API 发现与配置绝对路径一致且已加载 `Weixin.dll` 的 `Weixin.exe`。
- 以自身子进程的 `--internal-host --run-weixin` 模式执行受限注入和命名管道转发，不依赖独立宿主 EXE。
- 逐行消费宿主 JSONL，并把原生状态补充为 Worker 组件状态协议。
- 严格校验收款助手结构字段、已确认接收账号、交易标识、金额和事件时间窗口。
- 未确认接收账号时只把结构中的 `receiverUsername` 写入本机候选状态，并上报 `DEGRADED / RECEIVER_CONFIRMATION_REQUIRED`；该事件不进入支付队列，也不调用支付上报接口。
- 账号资料 Hook 命中时直接把结构中的 `wxid` 写入同一候选状态；昵称 Hook 随后更新同一候选的昵称；与管理员已确认账号不一致时上报 `DEGRADED / ACCOUNT_MISMATCH`。
- 确认接收账号后先持久化支付事件，再按 HMAC 协议上报；失败时指数退避，重启后继续。
- 宿主模式退出后上报 `STOPPED / PROCESS_EXITED` 并重新等待目标进程。
- 提供状态窗口和托盘；托盘右键菜单显示当前状态，并支持还原窗口、打开配置和退出。

配置文件不再提供模板：默认值写在代码里（`wechat_config.cpp` 的 `defaultConfigJson`），首次运行自动在 `%LOCALAPPDATA%\CFPay\WeChatMonitor\config.json` 创建；文件已存在时只补缺失字段，用户填过的值一律不覆盖，同时清掉程序早已不读的废弃键（`hostPath`、`locatorScriptPath`、`heartbeatIntervalSeconds` 等）。格式损坏时先备份成 `config.json.invalid-<时间戳>` 再重建。程序创建的所有文件（配置、释放出的观察 DLL、收款队列状态、日志）都集中在这个目录，EXE 本身可以从任意位置运行。旧的 Node 联调脚本仍使用被 Git 忽略的 `config/wechat-monitor.local.json`。启动前可先执行：

```powershell
powershell -ExecutionPolicy Bypass -File tools\start-windows-wechat-monitor.ps1 -CheckOnly
```

### Win32 原生配置

运行状态和配置表单编译在 `cfpay-wechat-monitor.exe` 的同一个 Win32 主窗口内，不启动第二个配置进程、Node 配置服务、浏览器或 PowerShell。开始菜单配置入口执行：

```powershell
.\cfpay-wechat-monitor.exe --config-window --config .\config.json
```

`--config-window` 保留为兼容参数：未运行时启动完整监控主窗口，已经运行时通过单实例消息还原现有窗口，不会创建第二个 EXE 窗口。

界面可配置 Worker HTTPS 网关、监控端 ID、PC 监控端密钥、微信主程序和本人微信账号。旧默认值 `windows-main` 会在载入时替换为 `wm_` 前缀的随机标识，也可点击“生成”重新生成；该值由 Worker 自动登记，不需要预建。

微信主程序通过 C++ COM `IFileOpenDialog` 选择，过滤器只显示 `Weixin.exe`。选中后程序推导同目录下的 `Weixin.dll`，再调用同一 EXE 的 `--internal-host --inspect-weixin` 模式执行版本、大小、SHA-256、解析函数入口和调用点校验。创建或显示系统文件选择器失败时，界面显示 HRESULT，不再静默无反馈。

本人微信账号下拉框不再把 `%APPDATA%\Tencent\xwechat\login\wxid_*` 目录名作为账号来源。原生配置程序回显已保存账号，并读取监控状态文件中的 Hook 账号候选；只有一个候选时自动选中，仍需管理员保存确认。保存后的 `weixinExecutablePath` 用于常驻运行时按绝对路径发现进程；`weixinDllPath` 由配置程序推导，不由管理员手工输入。

配置文件继续兼容现有 JSON 字段，保存时删除旧版 `hostPath` 和 `locatorScriptPath`，使用同目录临时文件、刷新磁盘和原子替换写入。密钥输入框不会回显已保存值，留空表示保留原密钥。连接测试使用 WinHTTP 请求 Worker，并使用 BCrypt 生成 HMAC 签名；程序只依赖 Windows 系统 DLL 和静态链接的 MinGW 运行库。

“写入本地调试日志”默认关闭。开启后常驻监控服务把启动、进程发现、宿主生命周期、Hook 状态、入口与返回命中计数、读取失败、队列丢弃、事件类型、规则拒绝原因、支付队列结果写入 `%LOCALAPPDATA%\CFPay\wechat-monitor.log`；时间使用本地可读格式，文件达到 2 MiB 时轮转为 `.1`，只保留一个备份。配置窗口的“打开日志”按钮会创建尚不存在的空日志文件，并使用 Windows 记事本打开当前配置的日志路径。

每一次接口调用都会记录完整现场：请求路径、HTTP 状态码、WinHTTP 错误码、服务端返回的业务码与中文说明、脱敏后的请求体与响应体。失败记录不受调试开关控制、无条件落盘——过去失败只记一个 `httpStatus=401`，而服务端其实已经回了 `{"code":-1,"msg":"监控端签名错误"}`，只因响应体只在调用方显式要 JSON 时才读取而被丢弃，现场只能靠猜。

脱敏规则：登录二维码内容（`login.qrPayload`，谁拿到谁就能登录该微信号）与收款原文（`raw`，属于消息正文）只记录字节数与 SHA-256 前 12 位；请求体不是合法 JSON 时只记录字节数，不写原文。日志始终不写配置 JSON、监控密钥、HMAC 请求头、wxid 或昵称。关闭开关后监控服务不创建或追加日志文件，但上报失败的原因仍会落盘。

本机日志的说明文字、布尔值、上报结果、登录状态和计数指标名使用中文；时间戳、PID/TID、状态码、函数或组件名、`reason`、`processId`、`amountFen`、`capturedAt`、HTTP 状态码和 WinHTTP 错误码等诊断字段保持原格式。观察器或宿主返回的英文状态句不直接落盘，监控端按 `detailCode` 写入中文说明；账号与收款记录被拒绝时保留固定 `reason` 诊断码，并在同一行追加中文“说明”。未知诊断码使用中文兜底说明，不把原始英文消息写入日志。测试会扫描全部 `appendLog(...)` 调用，阻止没有中文说明的新增落盘日志进入版本库。

JSON 解析使用仓库内固定版本的 `nlohmann/json` 单头文件 `3.11.3`，来源为项目官方单头发行文件，SHA-256 为 `9bea4c8066ef4a1c206b2be5a36302f8926f7fdc6087af5d20b417d0cf103ea6`。升级该文件时必须重新记录版本、摘要并完成原生构建和 Windows 回归。

二维码解码使用 `quirc` 提交 `927d680904dc95fdff4cd9d022eb374b438ff8f2`，仅编译其 `lib` 目录所需源码并保留原始 ISC 许可证；仓库内许可证和六个源码/头文件按文件名排序计算的 SHA-256 清单摘要为 `be7c8af1296d8d07a7777f1f10000a9e38aeffb4f7893b730d2ab7575a69b3c4`。升级依赖时必须重新记录提交、摘要并重复登录二维码窗口测试。登录状态码与界面诊断码在本地日志中保留原值，同时追加中文说明；日志不得写入二维码内容或登录页截图。

开发构建仍保留独立宿主和配置目标用于既有离线回归，但它们不进入生产发行目录。生产受限命令统一通过 `cfpay-wechat-monitor.exe --internal-host <命令>` 调用。

端到端联调使用：

```bash
node tools/mock-monitor-server.mjs \
  --host 0.0.0.0 \
  --port 8799 \
  --secret <测试密钥> \
  --output dist/wechat-monitor-e2e.jsonl \
  --expected-payments 3 \
  --fail-first-payment true
```

生产与测试均直接运行 C++ 单 EXE；Worker 协议联调使用 `tools/mock-monitor-server.mjs` 作为临时接收端，并通过原生监控端日志核对统一状态上报、失败重试和结构化收款数据，不再维护 Node 版监控服务或其转发链路。

## 版本化发布工件

`tools/build-single-exe.sh` 除固定名 `dist/single-exe/cfpay-wechat-monitor.exe` 外，还产出按版本命名的发布工件 `cfpay-wechat-monitor-<监控端版本>-weixin-<微信版本>-win64.exe`，当前微信目标为 `4.2.0.11`。文件名里的监控端版本取自 `wechat_monitor_version.h` 的 `kVersion`，微信版本取自 `wechat_target.h` 的 `kWeixinVersion`，也就是观察器唯一通过静态校验的目标版本，避免把 EXE 部署到不受支持的微信上。

版本号以 C++ 头文件为唯一来源：监控端版本读 `wechat_monitor_version.h` 的 `kVersion`，微信版本读 `wechat_target.h` 的 `kWeixinVersion`，`--version` 与 `--weixin-version` 只用于临时覆盖。脚本每次构建还会清理输出目录里上一版的版本化工件，避免升版本时堆积旧 EXE。同目录还产出 `BUILD_INFO`（版本、观察器 API 版本、EXE 与内嵌 DLL 的 SHA-256、编译器、构建时间、提交号与工作区是否 dirty）和 `SHA256SUMS`（两个 EXE 的校验和）。

发布由 `.github/workflows/release.yml` 完成：推送 `v*` 标签后在 `ubuntu-latest` 安装 `g++-mingw-w64-x86-64`、`cmake`、`ninja-build`，执行同一构建脚本并 `sha256sum -c SHA256SUMS`，随后校验标签必须等于 `v<监控端版本>`（不一致直接失败，防止工件与版本号错配），最后用内置 `GITHUB_TOKEN` 创建 Release 并上传版本化 EXE、`SHA256SUMS`、`BUILD_INFO`；Release 已存在时以 `--clobber` 覆盖同名工件。发布说明自动生成版本信息表、工件清单、部署步骤与上一标签以来的提交列表。手动触发（`workflow_dispatch`）时可勾选预发布，标签缺省按源码版本推导。

## Windows 安装包

`npm run wechat:monitor:build` 只把内嵌观察器的 `cfpay-wechat-monitor.exe` 输出到 `dist/windows-wechat-monitor`。Node 只用于仓库构建与验证脚本，不进入 Windows 发行包，也不参与生产运行。

会话摘要动态验证使用独立的 `cfpay-hook-diagnostic.dll`（历史诊断，生产链路已不使用）。内部宿主命令仅限固定微信目标和固定诊断文件名：

```text
cfpay-wechat-monitor.exe --internal-host --run-weixin-diagnostic <pid> <cfpay-hook-diagnostic.dll>
cfpay-wechat-monitor.exe --internal-host --stop-weixin-diagnostic <pid>
```

运行前必须先停止生产观察器，避免两组硬件断点并存。诊断输出只包含计数和脱敏文本形态，不进入收款队列，也不调用 Worker 收款接口。

网络解包后来源溯源使用独立的 `cfpay-hook-network-diagnostic.dll`：

```text
cfpay-wechat-monitor.exe --internal-host --run-weixin-network-diagnostic <pid> <cfpay-hook-network-diagnostic.dll>
cfpay-wechat-monitor.exe --internal-host --stop-weixin-network-diagnostic <pid>
```

网络写入来源诊断的返回点与 AddMsg 解析入口尚未针对 4.2.0.11 验证，本次不将其纳入支持目标；本次只迁移已确认的生产 NewSync 解析路径。

诊断从异常 `CONTEXT` 使用 `RtlLookupFunctionEntry` 和 `RtlVirtualUnwind` 最多展开 `32` 层，并保留最多 `16` 个位于 `Weixin.dll` 内的模块相对返回地址。事件类型为 `newsync_response_parse` 或 `addmsg_parse`，输出序号、解析次数、线程 ID、`callerRva`、`callChainRvas`、展开层数、完成状态和上述结构元数据；不输出模块基址、模块外地址、原始栈内存、聊天正文、金额、摘要、账号或交易标识。首次解析命中前每 `25ms` 补充覆盖新建线程，命中后恢复为 `250ms`；健康状态每 `5` 秒发送一次，避免连续重复上报。

旧文档中关于 `+0x106eb90`、对象约 `0x50` 字节以及存在位图 `+0x48` 的结论已被后续虚表和现场签名核对推翻；不得继续据此部署生产 Hook。当前证据只证明 NewSync 响应和 AddMsg 解析入口可被稳定观测，尚未证明 `messageType=49` 就是收款消息，也没有确认最近一笔付款在本次现场诊断窗口内命中。

网络溯源诊断拒绝与正式观察器或会话摘要诊断并存。停止命令会先清理硬件断点和异常处理器，再卸载诊断 DLL。

`tools/install-windows-wechat-monitor.ps1` 会在安装前拒绝 HTTP Worker 地址、短监控密钥、格式错误的非空接收账号和不存在的微信目标；空接收账号进入本机发现模式。安装器还会比较发行包与安装目录中的 EXE、DLL 哈希：监控端仍在运行时禁止覆盖，二进制需要升级但任一 `Weixin.exe` 仍在运行时也拒绝安装，防止已加载旧 DLL 的进程继续复用。`-CheckOnly` 只输出运行进程数、是否需要二进制升级和 `CLEAN_UPGRADE_READY`，不结束进程或修改文件。安装后配置改写为 `%LOCALAPPDATA%` 下的绝对路径，并通过当前用户 Startup 文件夹中的快捷方式直接启动同一 EXE 的 `--tray` 模式。开始菜单配置入口指向同一 EXE 的 `--config-window` 兼容模式，只会启动或还原同一个主窗口；安装器不查找 Node、不生成 `run.ps1`，也不申请管理员权限。状态队列保存在安装目录外，重新安装不会清除已交付事件记录。

发行验证使用：

```powershell
powershell -ExecutionPolicy Bypass -File tools\verify-windows-wechat-single-exe.ps1 `
  -PackageDirectory dist\windows-wechat-monitor `
  -WeixinDllPath "D:\Program Files\Tencent\Weixin\4.2.0.11\Weixin.dll" `
  -VerifyUi
```

验证脚本要求发行目录恰好包含一个 EXE 和观察 DLL，并检查原生自测、内置宿主目标校验、状态窗口、配置窗口、托盘菜单和退出流程。

生产机维护工具：

- `tools/audit-windows-wechat-monitor.ps1`：脱敏检查运行进程、安装文件、配置完整性、状态队列数量、快捷方式和观察 DLL 哈希。
- `tools/start-installed-windows-wechat-monitor.ps1`：通过 Windows CIM 启动已安装托盘进程，避免启动进程随 WSL 命令会话退出；追加 `-Restart` 时先向托盘窗口发送正常退出命令，确认进程结束后再启动。
- `tools/stop-installed-windows-wechat-monitor.ps1`：向已安装主窗口发送托盘退出命令，并兼容关闭旧版遗留的独立配置窗口，等待内部宿主正常停止，不强制结束进程。
- `tools/restart-windows-weixin-for-monitor.ps1`：仅在所有 `Weixin.exe` 路径都与指定目标一致时结束并重新启动微信，用于清除旧版本惰性观察 DLL；脚本不点击界面、不登录或切换账号。
- `tools/verify-windows-wechat-account-refresh.ps1`：把待测 EXE 复制到 Windows 本地目录后，脱敏验证当前账号下拉框显示昵称格式、“重新获取”按钮和操作结果对话框，避免直接从 WSL UNC 路径启动 GUI EXE。
