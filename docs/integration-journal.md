# 支付通道接入追溯日志

本文记录支付通道接入过程中的需求、环境、操作、证据、结论和待办。日志不得写入私钥正文、管理员密码、会话密钥、完整聊天内容或其他可直接用于登录的信息。

## 记录规则

- 每次侦察、修改、安装、运行和验证都追加独立编号。
- 记录时间、目标、实际操作、结果、风险和下一步。
- 二进制文件记录版本和 SHA-256；升级后必须重新定位观察点。
- 动态分析只针对本机本人账号，不拦截网络、不解密数据库、不自动操作支付。
- 不绕过客户端完整性校验、反调试或账号风控；发现异常立即停止。
- 每个独立步骤完成后执行 Git 提交；提交前确认私钥、本地配置、运行日志和构建产物没有进入暂存区。

## 系统基线

- 日期：2026-09-27。
- 架构：Cloudflare Workers、D1、Static Assets、Cron Trigger、Durable Object。
- 业务范围：单商户、单管理员、支付宝和微信个人收款、易支付 V1 兼容接口。
- 订单规模：每日低于 100 单，峰值约每分钟 10 单。
- 金额：统一使用整数分；通过渠道、实际金额和支付时间匹配订单。

## 支付宝

### ALI-001 证书材料确认

- 目标：确认支付宝开放平台证书模式所需材料。
- 输入文件：支付宝公钥证书、支付宝根证书、应用公钥证书、CSR、应用公钥和应用私钥。
- 本地目录：`config/alipay/`。
- 安全措施：`config/alipay/`、本地配置和生成的 Worker Secrets 文件均由 `.gitignore` 排除。
- 结论：现有材料可用于支付宝开放平台证书模式；CSR 和应用公钥不是 Worker 运行时请求所必需。

### ALI-002 本地证书模式查询

- 目标：验证是否存在可用的官方账务 HTTP API。
- 接口：`GET /v3/alipay/data/bill/accountlog/query`。
- 实现：`tools/lib/alipay-client.mjs`、`tools/alipay-ledger-query.mjs`。
- 结果：支付宝返回 HTTP 200，响应签名验证通过。
- 证据：测试窗口内查询到一条 `0.01` 元收入，账务编号为 `1570974125900526121`，时间为 `2026-09-27 18:44:40 +08:00`。
- 结论：支付宝不需要桌面 Hook，优先使用官方账务 API。

### ALI-003 匹配与幂等设计

- 幂等键：`account_log_id`，转换为事件编号 `alipay:<account_log_id>`。
- 过滤条件：仅处理 `direction = 收入` 且金额、时间有效的记录。
- 匹配条件：渠道、实际金额和支付时间。
- 结算链路：复用 `recordTrustedPaymentEvent()`，匹配成功后复用现有商户回调任务。
- 结果：订单支付状态与商户回调状态保持独立。

### ALI-004 Worker 接入

- 实现文件：`src/alipay.ts`、`src/index.ts`、`src/types.ts`、`wrangler.jsonc`。
- 请求认证：支付宝 V3 `ALIPAY-SHA256withRSA`，RSA2 请求签名和响应验签。
- 调度：`AlipayLedgerPoller` Durable Object；支付宝下单时激活，Cron 每分钟兜底激活。
- 停止条件：没有支付宝待支付订单时不再设置 Alarm。
- 管理接口：`GET /admin-api/alipay/status`、`POST /admin-api/alipay/query`。

### ALI-005 轮询时延调整

- 原始间隔：8 秒。
- 问题：支付刚好发生在一次查询之后时，后端确认可能接近 8 秒，收银台还需等待下一次状态查询。
- 调整：支付宝账务查询改为 2 秒，收银台订单状态查询改为 1 秒。
- 预期：正常情况下页面约 1 至 3 秒显示支付成功；接口错误时账务轮询至少退避 30 秒。
- 配置：`ALIPAY_ACTIVE_POLL_SECONDS=2`。

### ALI-006 局域网联调服务

- 服务：`tools/alipay-local-test-server.mjs`。
- 监听：`0.0.0.0:8790`，测试地址曾为 `http://172.24.163.152:8790`。
- 页面刷新：每 2 秒查询一次 `/api/ledger`。
- Windows 启动脚本：`tools/start-windows-alipay-test.ps1`；启动前按端口终止旧测试进程。
- 测试约束：按需求仅在当前局域网联调阶段不加访问令牌。

### ALI-007 验证结果

- `npm run alipay:worker-secrets`：通过，生成文件中的轮询间隔为 `2`。
- `npm run typecheck`：通过。
- `npm test`：2 个测试文件、4 个测试通过。
- `npx wrangler deploy --dry-run --outdir dist`：通过，识别 D1、Static Assets 和 `ALIPAY_POLLER` Durable Object 绑定。
- 局域网页面：HTTP 200，确认加载 `setInterval(load,2000)`。
- 账务接口：HTTP 200，测试收入记录仍可查询。
- 状态：尚未实际部署 Cloudflare。

### ALI-008 管理后台上传要求

- 新需求：部署后由管理员页面上传支付宝证书和密钥文件，不要求手工运行 Wrangler Secret 命令。
- 状态：待设计和实现。
- 安全约束：Worker 运行时不能直接创建 Cloudflare Secret；应用私钥不能明文写入 D1。
- 建议方案：保留一个部署时设置的 `CONFIG_ENCRYPTION_KEY` Secret，管理员上传后在 Worker 内加密材料，再将密文和证书序列号保存到 D1。

## 微信

### WX-001 需求和边界

- 目标：获取微信桌面程序已经解包并准备显示到界面的结构化收款数据。
- 明确排除：系统通知读取、OCR、网络拦截、网络协议解密和微信数据库解密。
- 首选位置：消息模型到 Qt 界面之间的函数或系统级 UI 事件。
- 风险原则：先静态和只读侦察，再做可撤销的观察；不绕过反调试或完整性校验。

### WX-002 Windows 环境侦察

- 微信版本：`4.1.15.13`。
- 主程序：`D:\Program Files\Tencent\Weixin\Weixin.exe`。
- 主模块：`D:\Program Files\Tencent\Weixin\4.1.15.13\Weixin.dll`。
- 架构：x86-64。
- 主界面进程：侦察时 PID 为 `4308`，窗口标题为“微信”；PID 只用于本次会话。
- 工具状态：未安装 Frida、x64dbg、WinDbg 或 CDB；Windows 已有 Python 3.14 和 Node.js。
- `Weixin.exe` SHA-256：`a832fb321ddb5f9701dd3a7bb1958804f09b620b9ad328f52e2e352651ab886c`。
- `Weixin.dll` SHA-256：`10f8e995453e2da46d4f2b5080cd6da1f13cc5147746adc119ceae38cb039de5`。

### WX-003 UI Automation 只读验证

- 工具：`tools/dump-wechat-uia.ps1`。
- 结果：微信 4.x 主界面使用 Qt，根窗口类为 `mmui::MainWindow`。
- 可见结构：`mmui::ChatSessionList`、`mmui::ChatPage`、`mmui::ChatDetailView` 等控件可通过 UI Automation 枚举。
- 数据形态：会话项通过 `Name` 暴露结构化摘要、未读数和时间；追溯日志不保存实际联系人或聊天正文。
- 结论：可以先使用进程外 UI Automation 事件 Hook 做低风险实时观察，但仍需验证收款消息气泡是否暴露金额和唯一标识。

### WX-004 二进制静态只读扫描

- 对象：当前版本 `Weixin.dll`，未修改文件、未附加调试器。
- 发现的相关符号和字符串：`PayTransferManager`、`paymsgid`、`paymsg`、`PayMsgType`、`wcpay_info`、`transfer_msg_type`、`CoAcceptPayTransfer`、`default_transfer_pay_style`。
- UI 相关字符串：`chat_message_list`、`chat_message_page`、聊天消息 ViewModel 源路径。
- 结论：收款或转账消息在进入 UI 前存在结构化模型；仅凭字符串尚不能确定稳定函数地址和参数布局。

### WX-005 工具安装授权

- 用户授权：缺少的软件可下载安装使用。
- 执行约束：记录来源、版本、哈希和安装命令；优先官方来源或可审计开源项目。
- 下一步：先实现 UI Automation 实时事件观察器并触发一笔测试收款；若信息不足，再安装静态分析工具定位当前版本的消息 ViewModel 路径。

### WX-006 本机静态分析能力检查

- WSL 已有：GNU `objdump` 和 Java。
- WSL 缺少：LLVM PE 工具、radare2、Rizin 和 Ghidra。
- 模块大小：`Weixin.dll` 为 201,552,944 字节；`Weixin.exe` 为 2,939,440 字节。
- 符号状态：安装目录没有 PDB、MAP 或独立符号文件。
- 导出表：`Weixin.dll` 主要导出 `mmcronet` 网络传输函数和 `WeChatMain`，未导出支付消息或 Qt ViewModel 函数。
- 结论：无法依靠导出函数直接 Hook 收款展示链路；下一步优先安装 Ghidra 做字符串交叉引用和调用图定位，动态附加延后。

### WX-007 Ghidra 版本与依赖选型

- 查询来源：NSA 官方 GitHub 仓库 `NationalSecurityAgency/ghidra` 的 Releases API 和对应版本 `GettingStarted.md`。
- 选择版本：Ghidra `12.1.4`，发布于 2026-09-21。
- 官方文件：`ghidra_12.1.4_PUBLIC_20260921.zip`，大小 569,649,598 字节。
- 官方 SHA-256：`ddac49f903da9d5bac833e5cc79395098b9c33cfd3279be5f31bd00387d2d4db`。
- 运行要求：64 位 JDK 21；当前 WSL 为 OpenJDK `17.0.15`，不满足要求。
- 决策：独立安装 JDK 21 和 Ghidra 到用户目录，不替换系统 JDK 17；静态分析项目和下载包不提交 Git。

### WX-008 静态分析工具安装

- JDK 来源：Eclipse Adoptium 官方 API 指向的 Temurin GitHub Release。
- JDK 版本：Temurin `21.0.12.1+1 LTS`，Linux x64 HotSpot JDK。
- JDK 文件 SHA-256：`ce79869e1307ed8ee1e2baa86a412b1eb5b75d10a01006d788a6f968bcfaee94`，与官方 API 一致。
- JDK 安装目录：`~/.local/opt/jdk-21.0.12.1+1`。
- Ghidra 文件 SHA-256：`ddac49f903da9d5bac833e5cc79395098b9c33cfd3279be5f31bd00387d2d4db`，与官方 Release 一致。
- Ghidra 安装目录：`~/.local/opt/ghidra_12.1.4_PUBLIC`。
- 隔离性：未更改系统 `PATH`、`JAVA_HOME` 或系统 JDK 17；运行 Ghidra 时显式指定独立 JDK 21。
- 验证：`support/analyzeHeadless` 成功输出命令帮助和 JDK 21 版本信息。

### WX-009 Ghidra 支付字符串交叉引用脚本

- 脚本：`tools/ghidra/FindWechatPaymentRefs.java`。
- 输入：Ghidra 已定义字符串，不扫描运行时内存，不读取用户数据。
- 目标词：`PayTransferManager`、`paymsgid`、`paymsg`、`PayMsgType`、`wcpay_info`、`transfer_msg_type` 和转账气泡样式标识。
- 输出：字符串地址、引用指令地址、包含该引用的函数名和函数入口地址。
- 用途：将 2 亿字节模块的后续分析范围收敛到支付消息模型和聊天 UI 附近的函数。
- 状态：脚本已创建，待随 `Weixin.dll` 首次 Ghidra Headless 分析执行。

### WX-010 Ghidra 首次导入路径失败

- 尝试项目目录：`~/.cache/cfpay-ghidra`。
- 结果：Headless Analyzer 在导入前退出，错误为 `Path element starting with '.' is not permitted`。
- 影响：尚未分析或修改 `Weixin.dll`，没有生成有效项目。
- 原因：Ghidra 本地项目路径不允许任一路径元素以点开头。
- 修正：项目迁移到非隐藏目录 `~/ghidra-projects/cfpay`；运行日志仍可保存在项目目录内。

### WX-011 默认分析耗时中止

- 项目目录：`~/ghidra-projects/cfpay/Weixin_4_1_15_13`。
- 导入结果：PE 加载成功，识别为 `x86:LE:64:default:clangwindows`；找到多个当前安装目录内的依赖库导出表。
- 分析现象：混淆或非标准 RTTI 产生大量无效类型名警告，但没有修改原始 DLL。
- 资源：Java 最大堆 2 GiB，实际堆使用在可控范围；主机内存和磁盘充足。
- 中止时间：运行约 38 分钟后人工中止。
- 确认耗时点：Java 线程栈显示正在执行 `ConstantPropagationAnalyzer`，该分析器不是定位静态支付字符串和直接引用的必要条件。
- 项目状态：中止后仅留下未完成索引和锁文件，没有可复用程序数据库。
- 决策：删除未完成项目，新增 Headless 预处理脚本，关闭常量传播、反编译参数识别、函数 ID、栈和 Windows RTTI/异常处理等高成本分析器后重新导入。

### WX-012 Ghidra 分析选项枚举脚本

- 脚本：`tools/ghidra/ListWechatAnalysisOptions.java`。
- 方法：调用 GhidraScript 官方 API `getCurrentAnalysisOptionsAndValues()`，按名称排序输出当前模块可用的分析选项和值。
- 目的：先用 `-noanalysis` 导入并枚举真实选项名称，再生成精确的快速分析配置，避免因版本差异误关必要分析器。
- 输出不包含微信用户数据，只包含 Ghidra 分析器配置。

### WX-013 分析选项枚举结果

- 执行方式：使用 `-noanalysis` 导入当前 `Weixin.dll`，随后运行 `ListWechatAnalysisOptions.java`。
- 耗时：约 49 秒；导入和项目保存成功。
- 选项数量：119。
- 确认保留：`ASCII Strings`、`Disassemble Entry Points`、`Reference`、`Data Reference`、`Subroutine References`、`Scalar Operand References`、`Demangler Microsoft`。
- 确认关闭：`Call Convention ID`、`Decompiler Switch Analysis`、`Function ID`、`Non-Returning Functions - Discovered`、`Stack`、`Windows x86 PE Exception Handling`、`Windows x86 PE RTTI Analyzer`、`x86 Constant Reference Analyzer`。
- 已默认关闭：`Decompiler Parameter ID`。
- 说明：支付定位依赖字符串、基本反汇编和直接引用；关闭项主要执行反编译、常量传播、栈恢复、RTTI 和异常元数据恢复，是上一轮主要耗时来源。

### WX-014 快速静态分析配置

- 脚本：`tools/ghidra/ConfigureWechatFastAnalysis.java`。
- 保留能力：ASCII 字符串创建、入口点反汇编、代码和数据引用、子程序引用、标量引用、Microsoft 名称解码。
- 关闭能力：数据归档、调用约定识别、地址表恢复、反编译参数和分支恢复、嵌入媒体、函数 ID、动态不返回函数恢复、PDB、共享返回、栈恢复、Windows 异常和 RTTI、TEB、资源引用、x86 常量传播。
- 执行位置：作为 Ghidra `-preScript` 在默认分析启动前应用。
- 目标：将分析范围限制到字符串地址、基本函数边界和直接引用，避免再次进入 38 分钟的高成本常量传播阶段。

### WX-015 第一阶段聚焦分析结果

- 输入：已保存的 `Weixin_Options` 无分析项目。
- 限制：单文件分析上限 600 秒。
- 实际总耗时：607 秒，达到上限后正常停止并保存项目。
- 主要耗时：`Disassemble Entry Points` 513.853 秒、`Scalar Operand References` 64.717 秒、`Subroutine References` 22.834 秒。
- 已完成：入口点反汇编、部分函数体、标量和子程序引用；未再执行 RTTI、异常处理或常量传播。
- 支付字符串结果：`matches=0`、`xrefs=0`。原因是分析按顺序耗尽时间，`ASCII Strings` 尚未执行，而不是二进制中不存在目标字符串。
- 决策：保留本轮项目，第二阶段关闭入口点反汇编、标量和子程序引用，仅执行字符串创建及数据/代码引用，再运行支付交叉引用脚本。

### WX-016 精确字节搜索优化

- 修改：`FindWechatPaymentRefs.java` 不再依赖 Ghidra 预先创建全部字符串。
- 新方法：对 10 个支付标识逐个执行 `Memory.findBytes()` 精确 ASCII 搜索，然后读取目标地址已有的代码和数据引用。
- 优点：不需要扫描并定义模块中的全部普通字符串；可以直接复用第一阶段已保存的部分反汇编结果。
- 执行方式：对现有项目使用 `-noanalysis`，只运行支付定位脚本。

### WX-017 支付字符串与函数引用定位结果

- 执行耗时：约 29 秒，不运行自动分析。
- 汇总：12 个精确字符串命中，12 个已有代码引用。
- `PayTransferManager`：字符串 `0x188f8c908`，引用函数 `0x180e88fd0`，引用指令 `0x180e8aaf9`。
- `paymsgid`：字符串 `0x18924fe40`，引用函数 `0x182292ce0`、`0x182293980`。
- `wcpay_info`：字符串 `0x1897d1a48`，引用函数 `0x185297710`、`0x18529ed20`、`0x1852a2c80`，共 6 处引用。
- `CoAcceptPayTransfer`：字符串 `0x1897cfdc0`，引用函数 `0x1852814a0`。
- `transfer_msg_type`：字符串 `0x1897f13d8`，当前部分反汇编没有建立直接引用。
- 转账气泡样式：`default_transfer_pay_style`、`inactive_transfer_pay_style`、`active_transfer_pay_style` 位于 `0x189b006b8` 至 `0x189b006f8`，当前部分反汇编没有建立直接引用。
- 初步判断：`0x18529xxxx` 一组函数集中解析 `wcpay_info`，更接近结构化支付消息模型；`0x180e88fd0` 可能是支付转账管理器初始化或注册；下一步对 7 个候选函数做定点反编译。

### WX-018 支付候选函数定点反编译脚本

- 脚本：`tools/ghidra/DecompileWechatPaymentCandidates.java`。
- 目标函数：`0x180e88fd0`、`0x182292ce0`、`0x182293980`、`0x1852814a0`、`0x185297710`、`0x18529ed20`、`0x1852a2c80`。
- 限制：每个函数最多反编译 60 秒，只读取已保存的静态项目，不附加运行中的微信进程。
- 输出：函数入口、Ghidra 函数名、完成状态和 C 风格伪代码。

### WX-019 支付候选函数反编译结论

- 执行结果：7 个候选函数全部在 60 秒限制内成功反编译；总日志约 9,500 行、312 KB。
- `0x180e88fd0`：大型管理器注册或初始化函数，依次注册大量组件；`PayTransferManager` 只是其中一个名称，不适合作为收款事件 Hook 点。
- `0x182292ce0`：将一个包含大量字符串和整数成员的支付消息结构写入键值/XML 风格输出，`paymsgid` 是其中一个字段。
- `0x182293980`：从键值/XML 风格输入读取同一批字段并写入结构体，与 `0x182292ce0` 互为序列化/反序列化路径。
- `0x1852814a0`：包含 `CoAcceptPayTransfer` 标识，属于主动接受转账或相关请求流程，不应作为被动收款监控入口，以免改变业务状态。
- `0x185297710`、`0x18529ed20`、`0x1852a2c80`：均接收单个上下文对象，从 `param_1 + 0x398` 附近取得共享数据，查找 `wcpay_info`，然后构造多个显示或消息模型对象并调用后续帮助函数。
- 三个 `wcpay_info` 候选分别调用 `0x18529a850`、`0x1852a2110`、`0x1852a58d0`，可能对应不同方向、状态或消息气泡类型。
- 当前判断：最合适的观察层在这三个函数进入后、调用对应后续帮助函数之前；此时网络和数据库处理已经完成，`wcpay_info` 仍以结构化对象存在。
- 下一步：解析三个函数的代码调用者和数据引用，确认其虚表、ViewModel 类或消息类型分派关系，再决定唯一 Hook 点或三个并行 Hook 点。

### WX-020 候选函数调用者与虚表定位脚本

- 脚本：`tools/ghidra/FindWechatPaymentCallers.java`。
- 目标：三个 `wcpay_info` 候选函数及其下游帮助函数 `0x18529a850`、`0x1852a2110`、`0x1852a58d0`。
- 方法一：读取 Ghidra 已建立的代码和数据引用，输出调用函数和引用类型。
- 方法二：在模块内搜索目标虚拟地址的 64 位小端函数指针，输出所在 PE 内存块和是否可执行，用于识别虚表或回调表槽位。
- 执行限制：静态项目内运行，不附加微信进程。

### WX-021 候选函数调用关系结果

- `0x185297710`：没有普通代码调用；唯一函数指针位于 `.rdata` `0x1897d1988`。
- `0x18529ed20`：没有普通代码调用；唯一函数指针位于 `.rdata` `0x1897d2348`。
- `0x1852a2c80`：没有普通代码调用；唯一函数指针位于 `.rdata` `0x1897d25c8`。
- 三个候选仅有额外 `.pdata` 异常表引用，符合虚函数或回调函数特征。
- `0x18529a850` 仅被 `0x185297710` 调用 4 次。
- `0x1852a2110` 仅被 `0x18529ed20` 调用 4 次。
- `0x1852a58d0` 仅被 `0x1852a2c80` 调用 4 次。
- 结论：三个候选是三类相关消息对象的表驱动方法，每类具有独立的后续构建函数；不能只根据代码相似性任意选择一个。
- 下一步：检查 `.rdata` 指针槽前后的函数表、字符串和对表地址的构造函数引用，识别三个对象类型。

### WX-022 支付回调表邻域解析脚本

- 脚本：`tools/ghidra/DumpWechatPaymentTables.java`。
- 目标槽：`.rdata` `0x1897d1988`、`0x1897d2348`、`0x1897d25c8`。
- 输出：每个槽前后各 `0x80` 字节的 64 位值、目标内存块、已识别函数名、可打印 ASCII 字符串。
- 同时输出 Ghidra 已知的槽地址引用及其所在函数，用于寻找对象构造或虚表安装代码。
- 执行方式：静态项目 `-noanalysis` 后处理脚本。

### WX-023 支付回调表邻域解析结果

- 执行结果：三个目标槽均位于 `.rdata`，每组都形成相同结构的连续 7 个函数指针槽，目标 `wcpay_info` 回调位于第 3 个槽。
- 第一组 `0x1897d1978` 至 `0x1897d19a0`：`0x185297630`、公共 `0x18000af10`、目标 `0x185297710`、`0x18529a770`、`0x18529a780`、公共 `0x180009ec0`；前置元数据指针为 `0x1897d19b0`。
- 第二组 `0x1897d2338` 至 `0x1897d2360`：`0x18529eb00`、公共 `0x18000af10`、目标 `0x18529ed20`、`0x1852a20c0`、`0x1852a20d0`、公共 `0x180009ec0`；前置元数据指针为 `0x1897d2370`。
- 第三组 `0x1897d25b8` 至 `0x1897d25e0`：`0x1852a2ba0`、公共 `0x18000af10`、目标 `0x1852a2c80`、`0x1852a58c0`、共享 `0x18529a780`、公共 `0x180009ec0`；前置元数据指针为 `0x1897d25f0`。
- 共同特征：第 2 槽和末槽完全相同，第一组与第三组还共享 `0x18529a780`，说明三组对象实现同一接口族，但具有不同的构造、消息处理或状态行为。
- 引用状态：当前部分 Ghidra 分析没有为目标槽生成 `SLOT_REF`，不能据此断言运行代码不引用这些表；更可能是相关构造函数尚未反汇编或 RIP 相对数据引用尚未建立。
- 下一步：对每组地址点 `slot - 0x10`、`slot - 0x08` 和 `slot` 做静态 RIP 相对指令扫描，定位安装这些表地址的构造函数或分派器，再反编译以区分消息方向和状态。

### WX-024 支付回调表 RIP 相对引用扫描脚本

- 脚本：`tools/ghidra/FindWechatPaymentTableRefs.java`。
- 目标地址：三组表的 `slot - 0x10`、`slot - 0x08` 和 `slot`，共 9 个可能作为对象地址点或分派入口的地址。
- 方法：按 1 MiB 分块只读扫描可执行内存块中的 32 位位移，反算最多带 4 字节尾随立即数的 RIP 相对目标；仅在命中目标地址时调用 Ghidra 伪反汇编器验证完整指令。
- 输出：目标地址、指令地址、原始字节、反汇编文本、已知所属函数；无法验证为完整指令的命中单独输出为原始候选。
- 边界：不创建指令、不修改程序数据库、不附加微信进程，也不触发完整自动分析。
- 执行方式：在现有静态项目上以 `-noanalysis` 运行，结果单独记录。

### WX-025 支付回调表 RIP 相对引用扫描结果

- 执行耗时：约 10 秒；扫描 9 个目标地址，得到 7 个位移候选，其中 6 个验证为完整的 `LEA RCX,[目标地址]`，1 个为不能解码成目标引用的原始算术碰撞。
- 第一组地址点 `0x1897d1978`：引用指令 `0x185297327`，所属函数 `0x185297290`；引用指令 `0x185297655`，所属函数 `0x185297630`。
- 第二组地址点 `0x1897d2338`：引用指令 `0x18529e51f`，所属函数 `0x18529e3b0`；引用指令 `0x18529eb2a`，所属函数 `0x18529eb00`。
- 第三组地址点 `0x1897d25b8`：引用指令 `0x1852a2897`，所属函数 `0x1852a2800`；引用指令 `0x1852a2bc5`，所属函数 `0x1852a2ba0`。
- 未发现对三组 `slot - 0x08` 或 `slot` 地址的有效 RIP 相对引用；这确认对象使用的表地址点是 `slot - 0x10`，目标 `wcpay_info` 回调位于该地址点后 `0x10`。
- 每组两个引用函数中，后一个正好是表的第 1 个函数槽；前一个位于其之前，可能是构造、工厂或相邻生命周期函数。仅凭地址邻近不能定性，需反编译六个引用函数。
- 边界：本次仍为离线静态分析，没有启动、停止或附加微信进程。
- 下一步：定点反编译上述 6 个函数，比较对象初始化、表地址写入位置、参数和调用关系，识别三个对象类型。

### WX-026 支付回调表引用函数反编译脚本

- 脚本：`tools/ghidra/DecompileWechatPaymentTableRefs.java`。
- 目标函数：`0x185297290`、`0x185297630`、`0x18529e3b0`、`0x18529eb00`、`0x1852a2800`、`0x1852a2ba0`。
- 限制：每个函数最多反编译 60 秒；只读取现有 Ghidra 程序数据库，不运行全模块自动分析。
- 输出：请求地址、实际函数入口、反编译完成状态和 C 风格伪代码。
- 用途：确认每组两个引用函数分别承担对象构造、生命周期管理还是回调表方法，并比较三类支付消息对象的字段差异。

### WX-027 支付回调表引用函数反编译结果

- 执行结果：6 个目标函数全部反编译成功；完整日志约 614 行、21 KB。
- 第一组 `0x185297290`：分配 `0x538` 字节对象，写入表地址点 `0x1897d1978`，复制公共消息字段和 `param_1 + 0x448` 的 8 字节字段，再通过统一分派路径提交，分派编号为 `0x5c`。
- 第一组 `0x185297630`：分配同样的 `0x538` 字节对象，写入同一表地址点，从源对象复制公共字段和偏移 `0x530` 的末尾字段；其行为符合该对象类型的复制或克隆工厂，而不是析构函数。
- 第二组 `0x18529e3b0`：分配 `0x558` 字节对象，写入表地址点 `0x1897d2338`，除公共字段外还复制一个带小字符串优化布局的可变长字符串，再通过统一分派路径提交，分派编号为 `0xc3`。
- 第二组 `0x18529eb00`：复制同类 `0x558` 字节对象，包括偏移 `0x530` 至 `0x550` 的额外字符串和末尾 8 字节字段；反编译器明确识别其返回新对象指针。
- 第三组 `0x1852a2800` 与第一组创建流程和 `0x538` 字节布局几乎相同，仅表地址点改为 `0x1897d25b8`、统一分派编号改为 `0x13a`。
- 第三组 `0x1852a2ba0` 与第一组复制流程相同，安装第三组表地址并复制公共字段和偏移 `0x530` 的末尾字段。
- 结论：三组都是相关消息模型类型；第一类和第三类字段布局相同但类型或状态不同，第二类额外携带一个字符串字段。当前尚不能仅凭分派编号可靠命名为收款、付款或状态更新。
- 下一步：追踪三个创建函数和三个复制函数的代码调用者，并检查统一分派编号附近的注册或类型元数据，将三组模型映射到具体消息语义。

### WX-028 支付模型函数调用者扫描脚本

- 脚本：`tools/ghidra/FindWechatPaymentModelCallers.java`。
- 目标：三组模型的创建函数 `0x185297290`、`0x18529e3b0`、`0x1852a2800` 和复制函数 `0x185297630`、`0x18529eb00`、`0x1852a2ba0`。
- 方法一：输出 Ghidra 当前已建立的全部代码和数据引用及所属函数。
- 方法二：分块扫描可执行段中的 `CALL rel32` 和 `JMP rel32`，反算目标地址并用伪反汇编验证完整指令，补足部分分析可能遗漏的直接调用。
- 边界：只读静态项目；不创建引用、不反汇编整个模块、不附加运行中的微信。
- 输出：目标、引用或分支指令、引用类型、内存块、调用函数入口和汇总计数。

### WX-029 支付模型函数调用者扫描结果

- Ghidra 已知引用：6 个目标函数各自只有 1 个 `.pdata` 异常展开元数据引用，没有已建立的普通代码调用或数据表引用。
- 原始分支扫描：对全部可执行段扫描并验证 `CALL rel32`、`JMP rel32`，命中数量为 0。
- 结论：三组创建和复制函数均不是通过模块内普通直接调用进入，更可能由统一事件系统保存函数对象、通过间接调用执行，或由尚未识别的数据结构承载。
- 影响：继续扩展普通调用图不会直接得到消息语义；应转向 `FUN_18019a500` 构造的分派描述、共同元数据 `0x1897d2e30` 和分派编号 `0x5c`、`0xc3`、`0x13a`。
- 边界：结果来自离线静态扫描，没有运行时 Hook 或进程操作。
- 下一步：解析共同元数据邻域，并查找三个分派编号在注册表、跳转表或类型描述中的静态关联。

### WX-030 支付模型共同分派元数据检查脚本

- 脚本：`tools/ghidra/InspectWechatPaymentDispatch.java`。
- 元数据目标：三个模型创建函数共同传入的 `0x1897d2e30`。
- 静态输出：目标的全部 Ghidra 引用，以及前后各 `0x100` 字节的十六进制和可打印 ASCII 视图。
- 反编译目标：描述构造函数 `0x18019a500` 和统一分派提交函数 `0x1802fb010`，每个函数限制 60 秒。
- 用途：判断共同参数是类型描述、序列化数据还是注册表节点，并确认三个分派编号在后续调用中的实际角色。
- 边界：仅处理离线程序数据库，不执行微信代码。

### WX-031 支付模型共同分派元数据检查结果

- 元数据引用：当前部分 Ghidra 分析没有为 `0x1897d2e30` 建立引用；邻域是带长度或标记字节的十六进制样编码数据，不是普通 C 字符串、函数指针表或可直接读取的类名。
- `0x18019a500`：仅向 32 字节描述对象依次写入 `param_2`、`param_3`、32 位 `param_4` 和调用返回地址，不执行消息注册或类型查找。
- 三个模型创建函数传入的共同值分别是全局指针 `0x188d84f50`、元数据 `0x1897d2e30` 和编号 `0x5c`、`0xc3`、`0x13a`；结合返回地址字段，这组数据更符合诊断、日志或源码位置描述，而不是业务消息类型。
- `0x1802fb010`：可选执行一段等级 4 的诊断日志处理，从参数对象的第 8 个槽取得或转移一个可调用对象，随后统一转交 `0x1802f9fd0`；函数本身不按上述三个编号分派。
- 修正：`WX-027` 中的“分派编号”只能理解为传入诊断描述的编号，不能据此映射收款、付款或状态类型。
- 结论：共同分派基础设施不能提供三类模型的业务命名；语义仍需从三个 `wcpay_info` 回调的字段差异、下游帮助函数或受控运行时触发中确认。
- 下一步：对三个 `wcpay_info` 回调及其下游帮助函数做结构化差异提取，优先寻找稳定字段偏移、常量和状态分支；静态信息不足时再设计只读运行时观察点。

### WX-032 支付显示模型帮助函数反编译脚本

- 脚本：`tools/ghidra/DecompileWechatPaymentRenderHelpers.java`。
- 目标函数：第一组 `0x18529a850`、第二组 `0x1852a2110`、第三组 `0x1852a58d0`。
- 来源：三个函数分别只被对应的 `wcpay_info` 回调调用 4 次，是三类消息在成功、缺字段和错误分支中共同汇入的专属下游。
- 限制：每个函数最多反编译 60 秒；不运行全模块分析。
- 用途：比较最终输出对象布局、状态常量和 UI 字段写入，判断三类模型的显示语义及适合作为只读观察点的位置。

### WX-033 `wcpay_info` 三组模型语义纠正

- 帮助函数结果：`0x18529a850`、`0x1852a2110`、`0x1852a58d0` 除诊断编号外逻辑相同，均分配 `0x120` 字节事件对象，复制消息上下文和一个两指针共享对象，然后调用 `0x180388dc0` 投递 UI 事件。
- 成功路径：三个主回调都先解析 `app_ext_info` 和 `wcpay_info`，构造共享对象后进入各自帮助函数；帮助函数本身不暴露业务字段布局。
- 字符串邻域：同一 `.rdata` 区域明确包含 `/cgi-bin/mmpay-bin/receivewxhb`、`/openwxhb`、`/qrydetailwxhb`、`sendId`、`channelId`、`nativeUrl`、`openim hb not supported` 和 `cgi failed by bubble_msg_click`。
- 结论：`0x185297710`、`0x18529ed20`、`0x1852a2c80` 三组是微信红包接收、打开、详情或气泡交互相关模型，不是本项目需要的个人转账收款展示链路。
- 纠正：`WX-019` 至 `WX-032` 对其“支付消息模型”的表述应收窄为“支付相关的红包模型”；这些地址不进入后续收款观察器候选集。
- 安全影响：排除红包点击和打开流程后，可避免观察器误触发带业务动作的 UI 路径。
- 新目标：回到同一区域更早出现的被动转账字段 `trans_id`、`transfer_id`、`trans_status`、`payer_name`、`receiver_name`、`pay_time`、`status_desc`，区分其展示模型与主动 `CoAcceptPayTransfer`、`CoRefusePayTransfer` 操作。
- 下一步：对上述转账字段做精确字节搜索和交叉引用，优先选择只读取结构化状态并生成聊天气泡的函数，继续排除任何接受、拒绝或网络请求函数。

### WX-034 转账展示字段精确引用扫描脚本

- 脚本：`tools/ghidra/FindWechatTransferDisplayRefs.java`。
- 被动字段：`trans_id`、`transfer_id`、`trans_status`、`status_supplementary`、`payer_name`、`receiver_name`、`pay_time`、`status_desc`、`fee_type` 等转账结构字段。
- 展示标识：`transfer_msg_type` 和三种 `transfer_pay_style` 气泡样式。
- 主动操作排除标识：`CoAcceptPayTransfer`、`CoRefusePayTransfer`。
- 方法：先精确搜索以空字节结尾的 ASCII 字符串并输出现有引用，再分块扫描可执行段的 RIP 相对位移，用伪反汇编验证遗漏引用。
- 目标：从结构字段和气泡样式定位被动转账展示函数，同时明确隔离接受、拒绝和网络请求路径。
- 边界：离线只读扫描，不运行或附加微信。

### WX-035 转账展示字段精确引用扫描结果

- 执行结果：23 个搜索词得到 22 个精确字符串地址和 33 个有效 RIP 相对引用；项目保存成功，无扫描异常。
- `0x18527eaf0`：集中引用 `trans_status`、`status_supplementary`、`modify_time`、`refund_bank_type`、`fee_type`、`payer_name`、`status_desc`、`receiver_name`、`pay_time`、`delay_confirm_flag`，是完整转账状态结果的结构化解析候选。
- `0x18527bd40` 附近：引用 `bind_query_scene`、`trans_id`、`transfer_id`、`group_username`、`invalid_time`、`transfer_attach`，更像查询请求或参数构造路径。
- `0x1852814a0`：引用 `not_in_group_msg`，且已知包含主动 `CoAcceptPayTransfer` 流程，继续排除为观察点。
- `0x1853c9350`：唯一有效引用 `transfer_msg_type`，可能在聊天消息或气泡模型中识别转账消息子类型。
- `0x186c14990`：同一函数分别引用 `default_transfer_pay_style`、`inactive_transfer_pay_style`、`active_transfer_pay_style`，是当前最接近最终聊天气泡显示状态的静态候选。
- `active_transfer_pay_style` 在 `0x189b006da` 的额外命中是 `inactive_transfer_pay_style` 内部后缀重叠；独立字符串地址为 `0x189b006f8`。
- `0x1856eb360` 和 `0x1856f5170`：分别引用 `modify_time`，以及 `transfer_id`、`invalid_time`、`delay_confirm_flag`，可能属于持久化或模型转换路径，需反编译确认。
- 下一步：定点反编译 `0x18527eaf0`、`0x1853c9350`、`0x186c14990`、`0x1856eb360`、`0x1856f5170`，比较它们与 UI 对象、消息字段和数据库模型的关系；继续不分析或调用主动接受函数。

### WX-036 转账展示候选函数反编译脚本

- 脚本：`tools/ghidra/DecompileWechatTransferDisplayCandidates.java`。
- 目标：完整状态解析器 `0x18527eaf0`、消息类型读取函数 `0x1853c9350`、模型转换候选 `0x1856eb360` 和 `0x1856f5170`、气泡样式选择函数 `0x186c14990`。
- 限制：每个函数最多反编译 60 秒；不包含主动接受或拒绝转账函数。
- 输出：请求地址、实际函数入口、完成状态和 C 风格伪代码。
- 判定重点：是否读取聊天消息对象、是否构造 UI 属性、转账状态与样式的映射，以及结构化金额和交易标识在显示前是否仍可访问。

### WX-037 转账展示候选函数反编译结果

- 执行结果：5 个候选全部反编译成功；完整日志约 1,805 行、62 KB。
- `0x18527eaf0`：将 CGI 或查询结果解析到结构化转账状态对象，包含状态、补充说明、修改时间、退款银行类型、币种、付款人、收款人、状态描述、支付时间和延迟确认标志；该层仍偏业务结果解析，不作为首选 UI Hook。
- `0x1856eb360` 与 `0x1856f5170`：使用 `InitOnce` 注册字段名、类型和对象偏移，是反射、ORM 或持久化模型元数据初始化函数，不是逐笔消息事件路径。
- `0x186c14990`：按输入样式字符串的长度、哈希和精确比较选择 UI 资源；`default`、`inactive`、`active` 分别要求长度 `0x1a`、`0x1b`、`0x19`。该函数位于显示层，但只保留样式，不含金额或交易标识。
- `0x1853c9350`：纯读取已解包节点并填充约 `0x90` 字节输出结构，不发起网络请求、不写数据库，也不调用主动收款操作。
- `0x1853c9350` 输出布局：`+0x00` 为 `PayMsgType` 整数，`+0x08` 为 `fromusername` 字符串，`+0x28` 为 `tousername` 字符串，`+0x48` 为 `content` 字符串，`+0x68` 为 `transferid` 字符串，`+0x88` 为 `transfer_msg_type` 整数。
- 当前首选观察点：`0x1853c9350` 函数返回处。此时转账消息已经解包为结构化字段，后续尚未进入气泡样式和 UI 资源选择，符合“不 Hook 网络通信、观察显示前结构化数据”的约束。
- 待确认：金额和最终状态可能编码在 `content` 字符串中，需在受控运行时样本中读取并解析；还需追踪调用者确认该函数覆盖聊天气泡展示而非其他后台解析。
- 下一步：定位 `0x1853c9350` 的直接或表驱动调用者，并确定返回时 Windows x64 参数和输出对象的稳定读取方式，再实现最小只读观察器。

### WX-038 转账结构解析器调用者扫描脚本

- 脚本：`tools/ghidra/FindWechatTransferParserCallers.java`。
- 目标：首选结构解析器 `0x1853c9350` 和显示层样式解析器 `0x186c14990`。
- 方法：输出 Ghidra 已建引用、扫描模块内 64 位绝对函数指针，并分块验证全部 `CALL rel32` 与 `JMP rel32`。
- 用途：确认结构解析器由普通聊天路径直接调用还是由回调表间接进入，并将其与最终 UI 样式函数的调用层级对照。
- 边界：离线只读分析，不修改引用或函数边界。

### WX-039 转账结构解析器调用关系结果

- `0x1853c9350`：除 `.pdata` 元数据外只有一个普通调用，调用指令为 `0x1853c97bc`，所属函数入口 `0x1853c9760`；模块内没有发现其 64 位绝对函数指针。
- `0x186c14990`：没有普通直接调用，唯一绝对函数指针位于 `.rdata` `0x189b00590`，符合 UI 资源解析回调表成员特征。
- 结论：结构解析器不是全局注册回调，而是由单一上层函数直接调用，调用链可继续静态确认；样式解析器位于更晚的表驱动 UI 层。
- 观察点稳定性：`0x1853c9350` 的单一调用关系降低了误捕获其他业务消息的风险，但仍需确认上层函数的输入来源和后续用途。
- 下一步：反编译 `0x1853c9760`，检查其是否读取聊天消息内容、解析 XML 或应用消息节点，并确定解析器返回后输出结构的生命周期。

### WX-040 转账结构解析器上层函数反编译脚本

- 脚本：`tools/ghidra/DecompileWechatTransferParserCaller.java`。
- 目标函数：`0x1853c9760`，即 `0x1853c9350` 的唯一直接调用者。
- 限制：单函数反编译上限 60 秒；不扩展自动分析范围。
- 判定重点：输入是否来自聊天消息内容、是否创建 XML 或节点解析器、输出结构在调用后如何传递，以及函数是否包含业务动作。

### WX-041 转账结构解析器上层函数反编译结果

- `0x1853c9760` 首先调用 `0x1853c9350`，将输入节点解析到栈上的 `0x90` 字节转账结构。
- 随后调用 `0x1853c9ba0`，把基础转账结构转换为另一组显示字段；这一步发生在 ViewModel 分配之前，是金额、标题或状态文本进一步解析的候选位置。
- 函数接着分配 `0x108` 字节对象，安装表地址 `0x1897f1548`，把转换后的多个字符串、整数和调用参数复制到对象中。
- 最后通过通用描述构造 `0x18019a500` 和事件提交 `0x1802fb010` 投递对象，函数返回成功值 `1`。
- 安全结论：该调用链是“已解包节点 -> 结构化转账字段 -> 显示字段 -> UI 事件对象”，没有网络调用、数据库写入或接受/拒绝转账动作。
- 观察层选择：`0x1853c9350` 返回处可稳定取得发送方、接收方、交易号和原始内容；若 `0x1853c9ba0` 输出已包含金额和最终文案，则其返回处更适合直接输出收款事件。
- 下一步：反编译 `0x1853c9ba0`，确定输入输出字段布局以及金额、备注、状态和展示文本的来源。

### WX-042 转账显示字段转换函数反编译脚本

- 脚本：`tools/ghidra/DecompileWechatTransferDisplayTransform.java`。
- 目标函数：`0x1853c9ba0`，由 `0x1853c9760` 在基础转账结构解析完成后、UI ViewModel 分配前调用。
- 限制：单函数反编译上限 60 秒。
- 判定重点：是否解析 `content`、金额和备注，输出字段偏移，以及是否存在任何业务状态修改。

### WX-043 转账显示字段转换结果与运行时布局

- `0x1853c9ba0` 仅将 `0x1853c9350` 的输出做深拷贝：复制首个整数、4 个 MSVC `std::string` 和末尾整数；不解析 `content`，也不修改业务状态。
- 因此首选 Hook 保持为 `0x1853c9350` 返回处，返回值和第二参数均指向完整输出结构。
- 输出结构固定布局：`+0x00` `PayMsgType`，`+0x08` `fromusername`，`+0x28` `tousername`，`+0x48` `content`，`+0x68` `transferid`，`+0x88` `transfer_msg_type`。
- 字符串 ABI：每个字符串占 32 字节；长度位于字符串起点 `+0x10`，容量位于 `+0x18`；容量小于 `0x10` 时字符位于对象内联缓冲区，否则起点处保存堆指针。
- DLL 中与转账 `content` 相关的标签包括 `paysubtype`、`feedesc`、`transcationid`、`begintransfertime`、`pay_memo`、`receiver_username`，可用于把原始内容转换为金额、交易号、时间和备注。
- 最小运行时验证：仅附加到 `Weixin.dll + 0x53c9350`，保存第二参数，在函数返回时按上述布局只读输出 JSON；不调用微信函数、不修改返回值、不触碰网络或数据库。
- 风险控制：首次只做短时受控附加并立即可逆卸载；若微信出现异常、账号告警或完整性提示，停止该路径，不尝试绕过检测。
- 下一步：实现 Windows 只读观察器和本地事件输出，先验证一条现有或新到达的转账消息能否得到 `feedesc`、`transferid` 与状态字段。

### WX-044 Windows Frida 只读观察工具安装

- 安装脚本：`tools/install-windows-frida.ps1`，固定使用 `C:\Python314\python.exe`，可重复执行。
- 安装结果：`frida 17.19.0`、`frida-tools 14.10.4`；Python 版本为 `3.14.6`。
- 命令位置：`C:\Users\user\AppData\Roaming\Python\Python314\Scripts\frida.exe`，版本回读为 `17.19.0`。
- 环境说明：用户级 Python Scripts 目录当前不在 Windows `PATH`；后续宿主脚本直接使用 Python 模块，不依赖全局命令搜索。
- 验证方式：重复运行安装脚本，确认依赖均为已满足，并再次通过 Python 导入和 CLI 路径回读版本。
- 安全边界：本步骤只安装并验证工具，没有附加、停止、重启或注入微信进程，也没有访问微信网络通信或数据库。
- 下一步：实现仅 Hook `Weixin.dll + 0x53c9350` 的短时观察器，先做脚本静态检查，再由受控附加验证结构字段。

### WX-045 微信转账只读观察器实现与离线验证

- Frida 脚本：`tools/wechat-transfer-observer.js`，仅附加 `Weixin.dll + 0x53c9350`，进入函数时保存第二参数，返回时只读输出 `PayMsgType`、发送方、接收方、原始 `content`、`transferid` 和 `transfer_msg_type`。
- 字符串读取：按 MSVC x64 `std::string` 的 32 字节布局处理小字符串优化，限制单字段最大 64 KiB，并校验长度不超过容量；读取异常只输出错误事件，不修改进程数据。
- 地址保护：加载后确认目标地址属于可执行内存范围，否则拒绝安装 Interceptor。
- Python 宿主：`tools/wechat-transfer-observer.py`，使用标准库 XML 解析器提取 `paysubtype`、`feedesc`、`transcationid`、`begintransfertime`、`pay_memo` 和 `receiver_username`，事件以单行 JSON 输出。
- Windows 启动器：`tools/start-windows-wechat-transfer-observer.ps1`，自动选择加载 `Weixin.dll` 的微信进程，固定校验版本 `4.1.15.13`，默认 30 秒后自动卸载；版本不匹配时拒绝附加。
- 编码保护：启动器设置 `PYTHONIOENCODING=utf-8`，已确认金额符号和中文内容可从 Windows 原样回传到 WSL。
- 离线验证：`node --check` 通过；WSL Python 自检通过；Windows Python 3.14 经 `-CheckOnly` 自检通过，金额 `￥0.01`、交易号、时间、备注和收款账号全部正确解析。
- 安全边界：本步骤只执行语法、路径、编码和 XML 解析自检，没有附加、停止、重启或注入微信进程。
- 下一步：对当前主进程执行一次默认 30 秒受控附加，先打开已有转账聊天触发界面重建；若没有样本再决定是否进行小额测试。

### WX-046 微信转账观察器首次受控附加

- 目标进程：PID `4308`，模块 `D:\Program Files\Tencent\Weixin\4.1.15.13\Weixin.dll`，文件版本回读为 `4.1.15.13`。
- 运行时模块基址：`0x7ff977e00000`；观察地址：`0x7ff97d1c9350`，即模块偏移 `0x53c9350`。
- 内存保护：目标地址所在范围为 `r-x`，通过观察器的可执行地址检查。
- 执行结果：成功安装 Interceptor，持续 30 秒后自动卸载并断开会话，退出码为 `0`。
- 事件结果：收到 `observer_ready`，本次窗口内没有收到 `wechat_transfer` 或 `observer_read_error` 事件。
- 解释：当前微信界面在观察窗口内没有自然重建转账消息，不能据此否定 Hook 点；需要在下一次短时附加期间主动打开包含既有转账消息的聊天并轻微滚动，使气泡重新构建。
- 风险检查：运行期间未观察到微信退出、异常、完整性提示或账号告警；没有修改返回值、调用微信内部函数、触碰网络通信或数据库。
- 下一步：由用户在受控观察窗口内打开既有转账聊天并滚动，优先使用历史消息验证，不先产生新资金交易。

### WX-047 Frida 卸载导致微信崩溃并停用该路径

- 触发现象：准备第二次观察时，启动器未找到任何加载 `Weixin.dll` 的进程；随后确认所有微信进程均已退出。
- 检查工具：新增 `tools/check-windows-wechat-crash.ps1`，只读查询最近 Windows Application Error、Windows Error Reporting 和 .NET Runtime 事件。
- 崩溃时间：`2026-09-27T20:58:01+08:00`，与首次 30 秒观察器自动卸载时间一致。
- Application Error：故障应用为 `Weixin.exe 4.1.15.13`，故障模块为 `frida-agent.dll_unloaded`，异常代码 `0xc0000005`，错误偏移 `0x9c7e0`，进程 ID `0x10d4`，即十进制 PID `4308`。
- Windows Error Reporting：事件名 `BEX64`，再次指向 `frida-agent.dll_unloaded` 和 `0xc0000005`；报告 ID 为 `e1ba949d-f169-48a4-aede-5c9c093c056d`。
- 结论修正：`WX-046` 中“运行期间未观察到退出”只表示命令返回时未实时看到异常；系统事件日志证明卸载阶段实际导致微信崩溃。
- 安全处置：立即停止 Frida 运行时验证，不重启后再次附加，不研究或绕过微信完整性、防护或风控机制。
- 防误操作：Windows 启动器默认阻断运行时附加，仅允许 `-CheckOnly` 离线自检；保留显式风险参数只用于防止旧命令静默执行，不作为后续建议路径。
- 后续方向：重新评估不需要通用注入代理动态卸载的最小原生观察方案；在完成离线设计和卸载安全论证前，不再操作微信进程。

### WX-048 Windows 原生编译工具链盘点

- 检查脚本：`tools/check-windows-native-toolchain.ps1`，只读检查 MSVC、链接器、CMake、Ninja、MSBuild、Git、Visual Studio 安装信息和 Windows SDK 根目录。
- 检查结果：`cl.exe`、`link.exe`、`cmake.exe`、`ninja.exe` 和 `msbuild.exe` 均不存在或不在系统可发现路径。
- Visual Studio：`vswhere.exe` 存在，但查询全部产品返回空数组，确认没有已安装的 Visual Studio 或 Build Tools 实例。
- Windows SDK：注册表没有返回 `KitsRoot10`，当前未发现 Windows 10/11 SDK。
- 已有工具：Windows Git 位于 `D:\Program Files\Git\cmd\git.exe`。
- 安全边界：本步骤仅检查本机工具，不读取或操作微信进程。
- 下一步：安装最小 Visual Studio C++ Build Tools、Windows SDK 和 CMake，用于离线编译版本锁定的原生观察组件；安装完成前不执行任何注入。

### WX-049 Windows C++ Build Tools 安装器准备与首次取消

- 安装脚本：`tools/install-windows-cpp-build-tools.ps1`，下载微软官方 Visual Studio 2022 Build Tools 引导程序。
- 计划组件：`Microsoft.VisualStudio.Workload.VCTools` 及推荐组件，安装路径为 `C:\BuildTools`，静默等待、禁止自动重启并关闭安装缓存。
- 提权验证：脚本通过独立的管理员 PowerShell 执行安装，管理员进程必须写入 JSON 结果标记；外层随后使用 `vswhere`、MSVC 目录和 `cl.exe` 实际运行结果复核。
- 首次结果：Build Tools 引导程序下载完成，但 UAC 被用户取消，`Start-Process -Verb RunAs` 返回 `The operation was canceled by the user`。
- 系统影响：安装器未获得管理员权限，没有安装或修改 Visual Studio、MSVC 或 Windows SDK 组件。
- 安全边界：本步骤没有启动、重启或附加微信进程。
- 下一步：用户确认可接受 UAC 后重新执行同一安装脚本；安装完成并验证编译器前，不进入 DLL 编译或运行时测试。

### WX-050 WSL Windows x64 交叉编译环境安装

- 替代路径：由于 Windows Build Tools 的 UAC 未获批准，改用 WSL Ubuntu 官方仓库安装 `g++-mingw-w64-x86-64`，不需要 Windows 管理员权限。
- 软件来源：Ubuntu 20.04 `focal/universe`；安装包包括 `binutils-mingw-w64-x86-64`、`gcc/g++-mingw-w64-x86-64` 和对应 Windows 头文件、运行库。
- 安装版本：`x86_64-w64-mingw32-g++ 9.3-win32`；新增磁盘占用约 491 MB。
- 重复验证：新增 `tools/check-wsl-mingw-toolchain.sh`，从标准输入编译最小 Win32 程序并检查 PE 头、目标架构、子系统和 DLL 导入。
- 验证结果：成功生成 `PE32+ executable (GUI) x86-64`，`objdump` 目标为 `pei-x86-64`，子系统为 Windows GUI，导入 `KERNEL32.dll` 和 `msvcrt.dll`。
- 下载说明：曾尝试获取官方 `llvm-mingw 20250402` Ubuntu 20.04 归档，但当前 GitHub 大文件 TLS 链路持续失败；未得到完整归档，也未安装该来源文件。
- 安全边界：本步骤只安装编译工具并生成独立的最小测试程序，没有启动、读取、重启或附加微信进程。
- 下一步：用该工具链离线构建版本锁定的原生观察 DLL 和显式加载器，先完成单元测试与 PE 静态检查，不直接注入微信。

### WX-051 Hook 组件健康状态上报与管理端展示

- 数据库迁移：新增 `migrations/0002_monitor_component_status.sql`，以 `device_id + component` 为主键保存组件状态、版本、目标版本、进程号、Hook 偏移、诊断代码、脱敏说明、最后事件和最后上报时间。
- 状态集合：`STARTING`、`HEALTHY`、`DEGRADED`、`ERROR`、`STOPPED`、`UNSUPPORTED`；数据库使用 CHECK 约束拒绝其他值。
- 上报接口：新增签名接口 `POST /api/monitor/status`，继续使用现有 HMAC-SHA256、时间戳和 nonce 防重放认证。
- 微信组件：固定名称 `wechat_hook`；协议定义 `WAITING_PROCESS`、`HOOK_INSTALLING`、`HOOK_ACTIVE`、`PROCESS_NOT_FOUND`、`PROCESS_EXITED`、`VERSION_MISMATCH`、`SIGNATURE_MISMATCH`、`HOOK_INSTALL_FAILED`、`THREAD_COVERAGE_INCOMPLETE`、`EVENT_TIMEOUT`、`READ_FAILED` 和 `REPORT_RETRYING` 等诊断代码。
- 数据最小化：状态接口不上传聊天内容、内存地址、联系人或原始交易数据；只允许目标版本、进程号、模块偏移和最多 500 字符的脱敏诊断摘要。
- 管理端：PC 设备在线状态与组件健康状态分开显示；微信 Hook 超过 90 秒未上报时显示“上报超时”，页面每 30 秒自动刷新。
- 测试客户端：`tools/monitor-client.mjs status` 可上报微信或支付宝组件状态，用于部署前协议联调。
- 自动验证：TypeScript 类型检查通过；Vitest 共 3 个测试文件、7 项测试全部通过；前端 JavaScript 语法检查通过。
- 迁移验证：SQLite 3.31 从空库顺序执行 `0001` 和 `0002` 成功，主键冲突更新为 `HEALTHY/HOOK_ACTIVE` 成功，非法状态 `BROKEN` 被 CHECK 约束拒绝。
- Wrangler 限制：当前 WSL 20.04 glibc `2.31` 低于 `workerd` 要求的 `2.35`，因此 `wrangler d1 migrations apply --local` 无法启动；这是本机运行时限制，远程部署前仍需由 Wrangler 执行正式 D1 迁移。
- 网络环境：Windows Clash 正在 `127.0.0.1:7890` 监听，系统代理未全局启用；后续下载只按命令临时指定该代理，不修改 WSL 或 Windows 全局代理配置。
- 下一步：原生微信观察器把上述状态机接入其启动、版本校验、Hook 安装、事件读取、进程退出和 API 重试流程。

### WX-052 微信目标函数签名提取与原生观察器设计

- 提取工具：新增 `tools/extract-wechat-hook-signature.mjs`，直接解析 PE32+ 头和节表，把 RVA 转换为文件偏移，并验证 `CALL rel32` 的实际目标。
- 目标文件：`D:\Program Files\Tencent\Weixin\4.1.15.13\Weixin.dll`，大小 `201552944` 字节，SHA-256 为 `10f8e995453e2da46d4f2b5080cd6da1f13cc5147746adc119ceae38cb039de5`。
- 解析器：RVA `0x53c9350`，`.text` 文件偏移 `0x53c8750`，入口前 32 字节为 `55415741565657534883ec68488d6c246048c74500feffffff4c89c60f28053d`。
- 唯一调用点：RVA `0x53c97bc`，指令字节 `e88ffbffff`，工具按有符号相对位移计算后确认目标正是 `0x53c9350`。
- 设计文档：新增 `docs/wechat-native-observer.md`，确定 Windows 宿主、原生 DLL、命名管道、Cloudflare API 和管理端的职责边界。
- Hook 方案：使用每线程硬件执行断点，不改写微信代码页；入口命中保存 `RDX`，返回地址命中后只读结构，异常处理器仅写入预分配队列。
- 卸载策略：观察 DLL 不在微信运行期间动态卸载；停用时清除断点并保持惰性加载，随微信进程退出，避免重现 `frida-agent.dll_unloaded` 崩溃。
- 状态集成：设计明确把版本、签名、线程覆盖、事件超时、读取失败、上报重试和进程退出映射到 `wechat_hook` 状态接口。
- 安全门槛：先在自有测试进程验证断点与恢复，不直接对微信运行；任何版本或签名不匹配都拒绝安装。
- 下一步：实现通用的硬件断点测试 DLL、测试目标和状态输出，在自有进程完成验证后再编译微信专用观察器。

### WX-053 硬件断点观察器自有进程验证

- 原生项目：新增 `native/wechat-observer/`，使用 CMake 和 WSL MinGW 交叉编译 Windows x64 DLL 与测试目标。
- 观察方式：DLL 在目标函数入口使用每线程 `DR0`，从入口栈顶取得返回地址并临时设置 `DR1`；返回命中后按微信目标布局读取整数和 MSVC x64 `std::string`。
- 异常路径：向量异常处理器只执行固定大小读取和无锁环形队列入队；JSON 序列化、文件写入和状态输出由工作线程完成。
- 线程覆盖：工作线程每 250 毫秒枚举尚未登记的新线程，只对新线程补装 `DR0`；测试目标在 DLL 加载后创建新线程，成功捕获 `tx-thread` 事件。
- 停止策略：导出 `CfpayObserverStop`，通过独立清理线程恢复所有存活线程的 `DR0/DR1`、移除异常处理器并停止工作线程；DLL 不执行 `FreeLibrary`，保持惰性加载直至进程退出。
- 状态输出：成功安装输出 `HEALTHY / HOOK_ACTIVE`，成功停止输出 `STOPPED / HOOK_STOPPED`；线程覆盖失败会输出 `DEGRADED / THREAD_COVERAGE_INCOMPLETE`。
- 测试数据：主线程产生 `tx-1`、`tx-2` 两条事件，新线程产生 `tx-thread` 一条事件；停止后的 `tx-after-stop` 不得出现。
- 构建验证：DLL 和测试目标均为 `PE32+ x86-64`，DLL 导出仅包含 `CfpayObserverStop`，动态依赖仅为 `KERNEL32.dll` 和 `msvcrt.dll`。
- Windows 实测：单次验证通过；随后连续运行 20 次，每次退出码均为 `0`，每次均得到 1 条 `HOOK_ACTIVE`、3 条事件和 1 条 `HOOK_STOPPED`，没有额外或停止后事件。
- 安全边界：测试目标主动加载 DLL，未查找、启动、重启、附加或修改微信进程。
- 下一步：把测试 DLL 拆分为通用断点核心和微信专用配置，新增版本、SHA-256、入口签名和调用点校验，再实现显式宿主加载与 Cloudflare 状态上报。

### WX-054 微信目标校验器与受限 DLL 加载器

- 宿主程序：新增 `cfpay-wechat-host.exe`，作为后续 Windows 监控宿主的原生基础。
- 离线校验：`--inspect-weixin` 使用 Windows Version API、BCrypt SHA-256 和固定字节读取，验证微信版本、文件大小、完整哈希、解析器 32 字节入口签名、调用点指令及相对调用目标。
- 实际结果：Windows 侧校验当前 `Weixin.dll 4.1.15.13` 成功，输出 `STARTING / TARGET_VALIDATED`；未加载或附加微信进程。
- 受限加载：`--test-inject` 只接受进程映像名 `cfpay-hook-test-target.exe`，不存在任意进程名或通用注入命令。
- 地址计算：加载器枚举目标进程 `kernel32.dll`，用本地 `LoadLibraryW` 相对模块偏移计算远程线程入口，避免依赖跨进程基址相同假设。
- 注入验证：测试目标新增 `--wait-injection` 模式，由加载器远程写入 DLL 路径并显式调用 `LoadLibraryW`；完整 Hook、3 条事件、新线程覆盖和惰性停止流程均通过。
- 稳定性：连续 10 次受限远程加载测试全部退出码 `0`；调整远程 `kernel32.dll` 基址计算后再运行 3 次全部通过。
- 边界验证：用当前 PowerShell PID 调用受限加载器，程序在执行远程内存操作前以 `TEST_DLL_INJECTION_FAILED` 拒绝，运行脚本确认 `NON_TEST_PROCESS_REJECTED=1`。
- 构建产物：`cfpay-hook-observer.dll`、`cfpay-hook-test-target.exe`、`cfpay-wechat-host.exe` 均为 Windows x64 PE；宿主额外依赖系统 `bcrypt.dll` 和 `VERSION.dll`。
- 安全边界：本步骤只校验微信磁盘文件并注入仓库自有测试进程，没有启动、重启、附加或修改微信进程。
- 下一步：为观察 DLL 增加微信生产模式、内存签名复核和命名管道输出；宿主仍需在注入前再次执行全部磁盘校验，并把状态转交 Cloudflare 上报进程。

### WX-055 微信生产模式与命名管道传输

- DLL 模式：保留测试文件输出，新增测试命名管道模式；未设置测试环境变量时进入微信生产模式。
- 生产管道：DLL 按目标 PID 连接 `\\.\pipe\cfpay-wechat-observer-{pid}`，宿主在加载 DLL 前创建只入站服务端。
- 内存复核：生产 DLL 在目标进程内读取 `Weixin.dll + 0x53c9350` 的 32 字节入口签名和 `+0x53c97bc` 的 5 字节调用指令，并重新计算相对调用目标；不匹配时输出 `UNSUPPORTED / SIGNATURE_MISMATCH` 且不安装断点。
- 生产宿主：新增 `--run-weixin <pid> <observer.dll>`，只允许进程映像名 `Weixin.exe`，要求目标已加载 `Weixin.dll`，并在注入前再次执行版本、文件大小、SHA-256 和静态签名校验。
- DLL 限制：所有加载模式只接受固定文件名 `cfpay-hook-observer.dll`；生产模式若发现同名 DLL 已加载则拒绝重复操作。
- 状态心跳：管道模式每 30 秒输出 `HEALTHY / HOOK_ACTIVE`，用于上层服务持续刷新 Cloudflare 管理端状态。
- 断开恢复：命名管道写失败会触发异步停止线程，清除 `DR0/DR1`、移除异常处理器并进入惰性状态；并发调用 `CfpayObserverStop` 会等待同一清理结果。
- 正常管道验证：自有测试目标通过命名管道完整返回宿主加载状态、`HOOK_ACTIVE`、3 条事件和 `HOOK_STOPPED`；连续 10 次全部成功，后续代码调整后回归 3 次成功。
- 断开恢复验证：宿主读取第一条记录后主动关闭管道，连续 10 次目标均检测写失败、完成异步清理并以退出码 `0` 正常结束。
- 生产边界验证：以 PowerShell PID 调用 `--run-weixin`，宿主在远程操作前输出 `STOPPED / PROCESS_NOT_FOUND` 并确认 `NON_WEIXIN_PROCESS_REJECTED=1`。
- 安全边界：`--run-weixin` 已实现但本步骤没有执行该命令，没有启动、重启、附加或修改微信进程。
- 下一步：实现 Windows Node 监控服务，消费宿主 JSONL、解析转账 XML、持久化去重事件，并按 HMAC 协议上报心跳、Hook 状态和收款事件。

### WX-056 微信结构事件解析与候选收款校验

- XML 解析：新增 `fast-xml-parser` 依赖，只从 `content` 提取 `paysubtype`、`feedesc`、`transcationid`、`begintransfertime`、`pay_memo` 和 `receiver_username` 白名单字段。
- 金额处理：严格接受人民币符号、`CNY`、`RMB` 或纯十进制金额，使用字符串和整数分计算，不使用浮点数；零金额、三位以上小数和附加文本均拒绝。
- 方向校验：要求 `fromUsername`、`toUsername` 和 `receiver_username` 完整，发送方与接收方不同，且结构接收账号必须等于消息目标账号；可额外锁定本机配置的收款账号。
- 类型校验：默认只接受已定位样本中的 `payMsgType=49` 和 `transferMsgType=2`，允许值由本机配置显式覆盖。
- 时间窗口：要求 `begintransfertime` 有效且默认处于最近 10 分钟内，避免打开历史聊天时把旧转账误报为新收款。
- 去标识化：上报事件 ID 使用 `SHA-256(transcationid)`，摘要只包含金额，不保存原始交易号、联系人、备注或聊天 XML。
- 自动验证：新增 XML、CDATA、金额、时间、方向、类型和历史事件拒绝测试；本步骤只处理伪造记录，没有启动或附加微信。
- 下一步：实现本地持久化待上报队列、交易事件去重和 HMAC 客户端。

### WX-057 微信事件持久化队列与 HMAC 客户端

- 先落盘后上报：新增单进程 JSON 状态库，候选收款先写入 `pending`，再调用 Worker；进程中止不会丢失已经确认的候选事件。
- 幂等恢复：服务端成功但本地确认前崩溃时，事件会用同一 `eventId` 重试，由 Worker 已有的支付事件唯一键返回重复结果。
- 本地去重：待上报和已交付事件均参与去重；已交付 ID 保留 30 天且最多 10000 条，满足当前每日低于 100 单的规模。
- 退避状态：每个待上报事件持久化尝试次数、下次时间和截断错误摘要，按 1 秒到 5 分钟指数退避。
- 原子写入：状态先写同目录临时文件再重命名，文件权限在支持的平台设置为仅当前用户读写。
- 协议客户端：新增心跳、组件状态和收款事件的 HMAC-SHA256 请求客户端，签名原文与 `docs/monitor-protocol.md` 一致，并设置请求超时。
- 自动验证：新增队列重启恢复、重复抑制、退避、成功交付和 HMAC 请求签名测试。
- 下一步：实现 Windows 常驻服务，发现微信进程、监管原生宿主、消费 JSONL 并驱动队列上报。

### WX-058 Windows 微信监控常驻服务

- 配置边界：新增被 Git 忽略的本地配置模板，生产启动强制要求 Worker 地址、监控 ID、监控密钥、固定接收账号、宿主路径和观察 DLL 路径。
- 进程发现：新增 ASCII PowerShell 定位脚本，只列出 `Weixin.exe` PID；具体模块、版本、散列和签名仍由受限原生宿主二次校验。
- 宿主监管：Node 服务只使用 `cfpay-wechat-host.exe --run-weixin <pid> <observer.dll>`，逐行消费 JSONL；宿主退出后上报 `PROCESS_EXITED` 并回到等待状态。
- 状态上报：原生状态统一补充组件名、服务版本、目标版本、PID、Hook RVA、最后事件时间和观察时间，再调用 `/api/monitor/status`。
- 收款链路：通过全部结构校验的事件先进入本地持久化队列，由后台轮询调用 `/api/monitor/payment`；失败会落盘退避并在重启后继续。
- 并发与终态：待上报队列使用单实例刷新锁，宿主 JSONL 按行顺序处理；`UNSUPPORTED` 和 `ERROR` 终态不会被随后发生的宿主退出覆盖。
- 心跳：服务启动时立即上报设备心跳，之后按配置间隔持续刷新 `/api/monitor/heartbeat`。
- 脱敏日志：日志只记录稳定诊断代码、哈希事件 ID 和上报结果，不打印聊天 XML、联系人、备注、原始交易号或监控密钥。
- 启动工具：新增 PowerShell 启动脚本和 `--check` 配置校验模式；尚未对真实微信执行生产命令。
- 自动验证：新增状态协议补全、合法收款入队和非固定接收账号拒绝测试；Windows Node.js `v24.18.0` 已通过 `--check`，状态路径解析为当前用户的 `%LOCALAPPDATA%`。
- 定位验证：Windows 进程定位脚本独立执行返回空数组，全程没有启动、停止、附加或修改微信进程。
- 下一步：在 Windows 自有测试目标上运行完整 Node 服务链路，并用本地模拟 Worker 验证心跳、状态、事件上报和重试恢复。

### WX-059 Windows 自有目标端到端联调工具

- 测试数据：自有 C++ 目标改为生成当前 Unix 时间、固定接收账号和 `0.01`、`0.02`、`0.03` 三笔完整结构 XML，原有 Hook 行为与事件数量不变。
- 受限测试模式：Node CLI 新增单次 `--test-pipe-pid`，只调用原生宿主 `--test-pipe-run`；宿主继续硬编码拒绝除 `cfpay-hook-test-target.exe` 外的进程。
- 模拟 Worker：新增监听可配置局域网地址的 HTTP 服务，实际校验监控 ID、时间戳、nonce 防重放和 HMAC-SHA256 签名，并把已认证请求写入 JSONL。
- 自动验收：新增 Windows PowerShell 编排脚本和结果验证器，要求设备心跳、`HEALTHY / HOOK_ACTIVE`、三笔唯一收款事件以及本地队列 `pending=0`、`delivered=3`。
- 重试验收：模拟 Worker 首次收款请求可注入 HTTP 503；单次测试模式会等待持久化队列按退避重试并完全排空，超时则测试失败。
- 局域网入口：Windows 无法直接访问 WSL 虚拟网卡地址；曾尝试创建临时系统端口转发，但 UAC 被取消且 marker 未生成，确认没有修改系统网络配置。最终改用普通用户权限 TCP 中继监听 Windows 局域网地址，内部转到 WSL localhost 转发，进程退出即清理。
- 安全边界：该工具链只运行仓库自有测试目标，不查找、启动、停止、附加或修改微信进程。
- 原生回归：动态测试 XML 调整后，正常命名管道连续 3 次成功，目标退出码均为 `0`，非微信进程仍在远程操作前被拒绝。
- 端到端结果：Windows 通过局域网地址 `172.24.163.152:8799` 调用模拟 Worker；收到 1 次设备心跳、5 次组件状态、4 次收款尝试，其中首次注入 HTTP 503，随后 3 笔 `0.01`、`0.02`、`0.03` 全部成功。
- 持久化结果：Windows 状态文件最终为 `pending=0`、`delivered=3`；测试目标和 Node 服务退出码均为 `0`。
- 清理验证：测试结束后停止用户态 TCP 中继，再次访问局域网端口返回连接失败；没有遗留监听进程、端口转发或防火墙规则。

### ALI-012 后台证书配置存储基础

- D1 迁移：新增单行 `alipay_credential` 表，保存 App ID、三类证书序列号、AES-GCM 密文、随机 IV 和更新时间。
- 加密边界：使用 `SESSION_SECRET` 与固定上下文经 SHA-256 派生 AES-GCM 密钥；私钥和支付宝公钥只存在于认证后的上传请求、Worker 内存和 D1 密文中。
- 文件校验：支持支付宝导出的裸 PKCS#8 Base64 或 PEM 私钥，限制私钥和证书文件大小，并解析应用证书、支付宝公钥证书和多证书根证书。
- 配对校验：上传时使用应用私钥签名随机挑战，再用应用证书公钥验签；私钥与证书不匹配时拒绝保存。
- 运行时读取：支付宝账务查询优先解密 D1 动态配置，未上传时兼容现有 Worker Secrets；轮询器配置检查改为异步读取。
- 密钥轮换约束：更换 `SESSION_SECRET` 前必须重新上传支付宝凭据，否则现有 D1 密文无法解密并明确返回错误。
- 自动验证：新增 AES-GCM 往返、错误密钥拒绝和裸 PKCS#8 规范化测试。
- 真实文件验证：使用本地忽略目录中的四个实际文件执行上传解析，App ID、应用证书 SN、根证书 SN、支付宝证书 SN 和支付宝公钥均与现有 SDK 生成配置一致，私钥与应用证书配对验证通过。
- 下一步：增加管理后台 multipart 上传、状态展示、删除和账务查询验证操作。

### ALI-013 管理后台支付宝证书上传

- 上传接口：新增认证后的 `POST /admin-api/alipay/config`，接收 App ID、应用私钥、应用公钥证书、支付宝公钥证书和支付宝根证书 multipart 文件。
- 管理操作：新增动态配置状态、来源和更新时间展示，支持删除 D1 上传配置以及手动执行一次账务查询。
- 前端辅助：选择标准命名的应用证书时自动从 `appCertPublicKey_<AppId>.crt` 填入 App ID；文件上传不手工设置 `Content-Type`，由浏览器生成 multipart boundary。
- 回退语义：删除后台上传配置不会修改 Worker Secrets；若 Secrets 完整，运行时自动回退并在后台标明来源。
- 错误可见性：文件缺失、体积超限、App ID 不一致、证书无效或过期、私钥错配、密文无法解密均返回明确中文错误。
- 本地环境：WSL glibc 低于当前 Workerd 要求，未升级系统 libc；改用 `node:22-bookworm-slim` Docker 容器成功应用 `0001` 至 `0003` 三个本地迁移，并在 `0.0.0.0:8787` 启动 Worker。
- API 验证：后台登录成功，真实四文件上传返回 HTTP 200，状态接口返回 `configured=true`、`source=database` 和正确 App ID。
- 密文验证：D1 中 `encrypted_payload` 长度为 3276、IV Base64 长度为 16，检索不到 `PRIVATE KEY` 或原始私钥 Base64 前缀。
- 签名验证：本地集成测试从模拟 D1 行解密配置并生成支付宝 RSA2 请求，Authorization 中的 App ID、应用证书 SN 和根证书 SN 全部正确。
- 外网验证：Docker Workerd 因本机网络 TLS 根链返回“不信任签发者”，未能在容器内完成外呼；同一组真实文件通过官方支付宝 SDK 查询返回 HTTP 200、账务总数 0，证明凭据和支付宝接口有效。生产 Cloudflare 外呼仍需部署后复核。
- 前端验证空白：当前工具集中没有浏览器自动化或截图工具，本步骤只完成 HTML/JavaScript 语法、响应式 CSS 代码审查和 API 级验证，尚未做真实浏览器截图。
- 下一步：登录 Cloudflare、创建 D1、配置管理员 Secrets、部署 Worker，并在 HTTPS 域名后台复核上传界面和支付宝查询。

### WX-060 Windows 监控端安装包与登录自启动

- 单文件构建：新增 esbuild 打包流程，把 Node 服务、XML 解析和持久化逻辑打成 Node 20 ESM 文件，避免生产运行依赖 WSL 仓库的 `node_modules`。
- 安装内容：安装包同时包含受限原生宿主、观察 DLL 和微信进程定位脚本，目标目录默认为 `%LOCALAPPDATA%\\CFPay\\WeChatMonitor`。
- 配置门槛：安装前强制要求 HTTPS Worker 地址、合法监控 ID、至少 32 位监控密钥和具体 `wxid_` 接收账号，拒绝示例占位配置。
- 自启动：使用当前用户 Startup 文件夹的 Windows 快捷方式启动隐藏 PowerShell 运行脚本，不写 HKLM、不创建服务、不申请管理员权限。
- 可逆操作：支持 `-CheckOnly`、`-SkipStartup`、`-StartNow` 和 `-Uninstall`；持久化支付事件状态位于安装目录外，升级安装不清空队列。
- Cloudflare 授权：首次 `wrangler login` OAuth 会话等待 90 秒后超时，没有创建、修改或部署任何 Cloudflare 资源。
- 构建验证：安装包内 Node 服务为单文件 ESM，原生宿主和 DLL 均为 Windows x64 PE，定位脚本保持 ASCII；全量 21 个测试和类型检查通过。
- Windows 验证：测试配置通过 `-CheckOnly`，临时安装目录中的单文件服务通过 `--check`；随后成功创建 Startup 快捷方式并执行卸载，临时目录和快捷方式均被清除。
- 安全边界：安装验证没有启动监控服务、没有发现或附加微信，也没有写管理员级服务、计划任务、HKLM 或防火墙规则。
- 下一步：等待 Cloudflare OAuth 授权，创建生产 D1 和 HTTPS Worker；随后填写真实监控配置并执行正式安装。

### WX-061 微信监控端本机 Web 配置应用

- 配置入口：Windows 监控端新增 `--setup` 模式，默认只监听 `127.0.0.1:8791` 并自动打开浏览器；发行版本提升到 `0.2.0`。
- 配置字段：页面支持 Worker HTTPS 网关、监控端 ID、PC 监控端密钥、本人微信 `wxid`、`Weixin.exe` 路径、心跳间隔、进程轮询、队列发送和事件时效配置。
- 文件推导：管理员只选择微信主程序；服务优先推导同目录 `4.1.15.13\Weixin.dll`，也可扫描其他版本子目录，但最终必须通过受限宿主的版本、大小、SHA-256、解析函数入口和调用点签名校验。
- 进程锁定：配置保存 `weixinExecutablePath` 和推导出的 `weixinDllPath`；进程定位脚本新增绝对路径过滤，不再仅按 `Weixin.exe` 进程名选择目标。
- 密钥保护：读取配置只返回 `monitorSecretConfigured`，不回显密钥；密钥输入留空时保留原值，配置文件继续按原子临时文件加重命名方式写入。
- 本机接口保护：配置 API 要求启动时生成的 24 字节随机 Base64URL 会话令牌；页面载入后把令牌移入当前标签页 `sessionStorage` 并从地址栏移除，响应设置 CSP、`no-store`、`nosniff` 和 `no-referrer`。
- 连接测试：页面可使用当前表单向 Worker 发送真实 HMAC 心跳，验证网关、监控 ID 和密钥是否匹配；失败只显示截断后的接口错误，不输出密钥。
- 安装集成：发行包包含 Web 资源和 Windows 文件选择器；安装后在当前用户开始菜单创建配置入口，重装会替换 Web 目录，卸载会删除配置入口和安装目录。
- 自动验证：Node 语法检查、TypeScript 类型检查和 12 个测试文件共 35 项测试通过；新增 DLL 推导、密钥不泄露、会话令牌鉴权和精确进程路径传参测试。
- 发行验证：esbuild 成功生成 `0.2.0` 单文件服务，发行目录包含宿主、观察 DLL、两个 PowerShell 脚本和三项 Web 资源；打包后的 Linux HTTP 检查确认无令牌为 403、带令牌为 200 且无密钥字段。
- Windows 验证：Windows Node 直接运行发行包，页面与配置 API 均返回 HTTP 200，只监听 `127.0.0.1`；选择 `D:\Program Files\Tencent\Weixin\Weixin.exe` 后自动推导 `D:\Program Files\Tencent\Weixin\4.1.15.13\Weixin.dll` 并通过现有宿主静态校验。
- 安装回归：真实目标路径通过安装器 `-CheckOnly`；随后在临时目录执行 `-SkipStartup` 安装，Web 文件、开始菜单配置入口和已安装服务 `--check` 全部通过，再执行卸载并确认临时目录与快捷方式均已清理。
- 安全边界：本步骤没有启动、停止或附加微信，没有执行 `--run-weixin`，没有创建自启动监控；对微信仅执行磁盘文件的只读静态校验。

### WX-062 微信收款账号自动发现与下拉确认

- 配置语义：本人微信 `wxid` 不再要求首次安装时人工填写；空值表示发现模式，安装器和运行配置允许该状态，发行版本提升到 `0.3.0`。
- 候选来源：只接受已经通过消息类型、XML、收款方向、金额和时间窗口校验，且原生字段 `toUsername` 与 XML `receiver_username` 一致的 `wxid`；状态文件从版本 1 自动迁移到版本 2，最多保留五个候选并记录首次时间、最后时间和发现次数。
- 上报门槛：未确认候选时不写支付队列、不调用 `/api/monitor/payment`，只上报 `DEGRADED / RECEIVER_CONFIRMATION_REQUIRED`；管理员在本机配置页选择并保存后才启用后续收款事件上报。
- 下拉界面：配置页把候选改为账号下拉框，优先显示“昵称（wxid）”，昵称缺失时仅显示 `wxid`；已确认账号重新打开配置页时仍作为当前选项显示。
- 昵称边界：新增 ASCII PowerShell 脚本 `get-windows-weixin-profile.ps1`，仅通过 Windows UI Automation 读取带头像、账号或资料语义的控件名称，不点击微信界面，不读取通知、网络或数据库；读取失败、微信未运行或控件不可访问时返回空昵称，不阻断配置。
- 密钥保护：配置接口继续只返回 `monitorSecretConfigured`，候选响应只包含 `username`、可空 `nickname` 和本机计数，不返回监控端密钥。
- 发行集成：构建脚本和安装器均把昵称读取脚本列为发行文件；空 `expectedReceiverUsername` 可通过安装检查，格式错误的非空值仍被拒绝。
- 自动验证：监控运行时、服务和配置三组定向测试共 14 项通过，覆盖状态迁移、候选去重与上限、确认前不上报、昵称回退、配置响应不泄露密钥和 PowerShell 调用超时参数。
- 安全状态：本步骤没有启动、停止或附加微信，也没有执行生产 Hook；昵称读取脚本只在后续打开本机配置页且存在候选账号时按需运行。

### WX-063 本机微信账号读取、随机设备 ID 与文件选择修复

- 本机账号来源：确认微信 4.1.15.13 的当前用户数据位于 `%APPDATA%\Tencent\xwechat`，当前登录账号以 `login\wxid_*` 目录存在；无需读取聊天数据库，也无需等待首笔收款即可得到候选 `wxid`。
- 实机证据：只读检查在本机发现一个登录账号目录；标准旧版 `Documents\WeChat Files`、`Documents\Weixin Files` 和旧 AppData 路径均不存在，注册表只保存微信安装路径，不保存当前账号。
- 配置联动：选择并通过 `Weixin.exe` 静态校验后，配置接口立即读取当前 Windows 用户的登录账号目录并返回下拉选项；仅有一个账号时页面自动选中，仍需管理员点击保存才启用支付上报。
- 候选合并：已保存账号、本机登录目录和结构化收款事件候选按 `wxid` 去重；UI Automation 昵称只关联最近活动的本机登录账号，昵称不可用时继续只显示 `wxid`。
- 设备标识：新配置不再默认使用固定 `windows-main`；页面使用浏览器安全随机数自动生成 `wm_` 前缀的 24 位十六进制标识，并提供重新生成按钮。Worker 使用该值作为自动登记的设备主键，无需预创建。
- 文件选择：继续由本机 API 取得浏览器无法提供的真实绝对路径，但选择器改由 C++ 原生宿主直接调用 Windows `IFileOpenDialog`；Node 不再启动 PowerShell 文件选择脚本。
- 发行版本：Windows 微信监控端提升到 `0.3.1`；PowerShell 实机运行返回当前登录 `wxid`，未能可靠识别昵称时返回空昵称。
- 自动验证：TypeScript 类型检查、12 个测试文件共 42 项测试通过；Windows 打包后目标校验接口返回受支持的 `Weixin.dll` 和本机登录账号选项。

### WX-064 C++ 原生微信文件选择器

- 宿主命令：`cfpay-wechat-host.exe` 新增 `--select-weixin`，使用 COM 单线程单元创建 `IFileOpenDialog`，过滤器只显示 `Weixin.exe`，并启用文件存在、路径存在、文件系统路径和不改变工作目录约束。
- 输出协议：选择成功时宿主把 Windows UTF-16 路径转换为 UTF-8 后输出一行绝对路径；取消选择使用独立退出码，Node 配置服务继续映射为“未选择微信主程序”。
- 依赖调整：原生宿主新增系统 `ole32.dll` 依赖，静态链接 MinGW C++ 运行库；发行构建和安装包删除 `select-windows-weixin.ps1`，不再把 PowerShell 作为文件选择依赖。
- 控制器调整：`selectExecutable()` 直接执行现有受限宿主的 `--select-weixin`，选中后仍复用 DLL 路径推导、版本、大小、SHA-256、解析入口和调用点校验。
- 自动验证：Node 配置单元测试确认选择命令由原生宿主执行；MinGW Release 构建通过，Windows UI Automation 实测检测到可见的 `Select Weixin.exe` 窗口并只终止测试进程，没有选择文件或修改配置。
- 后续架构：原生选择器解决当前 GUI 调用问题；Windows 配置界面和常驻监控运行时将继续迁移为 Win32、WinHTTP 和 BCrypt，实现最终安装包不依赖 Node 或 PowerShell。

### WX-065 Win32 原生微信配置程序

- 原生界面：新增 `cfpay-wechat-config.exe` Windows GUI 子系统程序，使用 Win32 原生标签、输入框、下拉框和按钮，不依赖 Node 配置服务、浏览器或 PowerShell 绘制界面。
- 配置兼容：程序读取现有 `config.json`，兼容 Worker 网关、监控端 ID、监控端密钥、接收账号、微信主程序、DLL、宿主、观察 DLL和队列状态路径；保存使用临时文件刷新和原子替换。
- 标识生成：空监控端 ID或旧默认值 `windows-main` 自动替换为 `wm_` 前缀的 24 位十六进制随机值；界面提供重新生成按钮，优先使用 BCrypt，失败时回退到 Windows GUID。
- 账号来源纠正：未采用 `%APPDATA%\Tencent\xwechat\login\wxid_*` 目录名作为权威账号。实机数据根目录可被配置到其他磁盘，且账号数据库为加密 SQLCipher；本步骤只兼容回显已保存账号，新账号改由后续进程内 Hook 读取数据库解包后的当前账号资料结构。
- 文件选择：配置程序自身调用 `IFileOpenDialog` 选择 `Weixin.exe`，推导 `Weixin.dll` 后调用受限宿主静态校验；选择器创建或显示失败时展示 HRESULT，不再静默失败。
- 连接测试：程序使用 WinHTTP 调用 Worker 心跳接口，使用 BCrypt 完成 HMAC-SHA256，验证 Worker 网关、监控端 ID和监控端密钥组合。
- 安装调整：发行包加入原生配置程序并删除 `wechat-monitor-web`；开始菜单配置快捷方式直接启动 EXE，不再生成或调用 `setup.ps1`。常驻监控运行时本步骤仍使用 Node，后续单独迁移。
- 依赖固定：仓库内固定 `nlohmann/json` 单头文件 `3.11.3`，SHA-256 为 `9bea4c8066ef4a1c206b2be5a36302f8926f7fdc6087af5d20b417d0cf103ea6`。
- 自动验证：MinGW Release 构建生成 Windows x64 GUI 子系统程序且只依赖 Windows 系统 DLL；TypeScript 类型检查和 12 个测试文件共 42 项测试通过。
- Windows 验证：原生窗口、随机监控端 ID和微信目标校验通过；用户可见确认 C++ 文件选择窗口成功弹出。UI Automation 对 Shell 文件对话框的自动发现不稳定，因此该项记录为人工可见验证。
- 安装回归：临时安装目录包含原生配置程序，不包含 `setup.ps1` 或 `wechat-monitor-web`；开始菜单快捷方式直接指向 `cfpay-wechat-config.exe --config <config.json>`，卸载后临时目录和测试快捷方式均已清理。
- 权限边界：配置程序和安装器均不要求 UAC；测试期间发现并停止了旧 `cfpay-wechat-monitor.mjs --setup` Node 配置进程，没有提升权限或修改微信进程。

### WX-066 微信当前账号资料链路重新逆向

- 目标版本：继续使用已导入 Ghidra 项目的微信 `4.1.15.13` `Weixin.dll`，SHA-256 为 `10f8e995453e2da46d4f2b5080cd6da1f13cc5147746adc119ceae38cb039de5`；本步骤没有重新导入其他 DLL。
- 工具复用：Ghidra `12.1.4`、JDK `21.0.12.1`、项目 `/home/user/ghidra-projects/cfpay/Weixin_Options.gpr` 和此前支付链路脚本均可用；新增账号字符串引用、调用者、调用者反编译和虚表邻域脚本，不依赖公开仓库中的现成偏移。
- 字符串定位：`mars::app::AppManager::GetAccountInfo` 唯一引用落在 `Weixin.dll + 0xdc08d0`；`current_login_nick_name` 引用落在 `+0x96a2b0` 和 `+0x9aef50`，其中前者是大型 UI 初始化函数，后者是独立资料读取虚函数。
- 账号包装函数：`Weixin.dll + 0xdc08d0` 从管理器对象 `+0x10` 取得账号对象，再调用其虚表 `+0x18` 把当前账号资料写入调用方缓冲区；对象不存在时返回全空结构，不执行登录、切号或网络操作。
- 结构布局：结合空结构初始化、22 个真实代码调用者和清理辅助函数，确认输出大小至少 `0x29` 字节：`+0x00` 为 8 字节数值账号标识，`+0x08` 为 32 字节 MSVC 小字符串优化对象，`+0x28` 为状态字节。字符串容量字段位于 `+0x20`，容量大于 15 时数据指针位于 `+0x08`，否则字符内联在 `+0x08`。
- wxid 观察点：后续原生观察 DLL 可在 `Weixin.dll + 0xdc08d0` 返回处只读复制上述结构；只接受长度合理、UTF-8 可解码且符合微信用户名格式的字符串，并在退出回调前完成复制，不保留微信进程内指针。
- 调用证据：账号包装函数存在 22 个直接代码调用点，涵盖账号切换、初始化、状态序列化和业务管理器路径；多个调用者把 `+0x00` 数值字段传给后续组件，另有调用者复制前 32 字节或检查状态，证明该函数不是仅用于日志的孤立候选。
- 昵称虚表：`Weixin.dll + 0x9aef50` 位于虚表 `0x188eb9598` 的 `+0x58` 槽位；构造函数 `+0x9ae980` 写入该虚表并注册资料变化回调，清理函数 `+0x9aec40` 恢复父类虚表并释放订阅资源。
- 昵称回退：`+0x9aef50` 优先读取键 `current_login_nick_name`，缺失时调用 `+0xb61a0` 取得当前资料消息中的对应字符串字段；该链路与账号包装函数分离，后续需要在运行时分别验证触发频率和输出对象布局。
- 排除项：`account_username` 的三个字符串引用主要构造埋点或状态映射，不作为 Hook 点；没有采用登录目录名、离线 SQLCipher 解密、通知、OCR 或网络通信 Hook。
- 分析产物：完整 Ghidra 日志保存在工作区外的 `/home/user/ghidra-projects/cfpay/`，仓库只提交可复现 Java 脚本和本追溯结论，避免把大型临时日志、微信二进制或本机数据加入 Git。
- 安全边界：本步骤完全是离线静态分析，没有启动、停止、附加、注入或修改微信进程，也没有触发收款、登录、切号或资料更新行为。

### WX-067 微信当前账号只读硬件断点观察器

- 双 Hook 设计：保留 `DR0/DR1` 观察 `Weixin.dll + 0x53c9350` 收款结构，新增 `DR2/DR3` 观察 `Weixin.dll + 0xdc08d0` 账号资料函数；两条链路均在函数返回后只读复制输出，不修改微信代码页、参数或返回值。
- 生产签名：宿主和观察 DLL 同时校验账号函数入口前 32 字节、调用点 `+0x2914d6f` 的 5 字节 `CALL rel32` 及解析目标；本机真实 `Weixin.dll 4.1.15.13` 的版本、大小、SHA-256、收款签名和账号签名全部通过。
- 账号结构：读取 `+0x00` 的 8 字节数值账号标识、`+0x08` 的 MSVC 小字符串优化对象和 `+0x28` 的登录状态字节；字符串在回调内完成边界检查与复制，不向异步线程传递微信内存指针。
- 重复抑制：相同 `uin + wxid` 使用无动态分配的 64 位 FNV-1a 指纹去重，避免账号函数高频调用挤占固定事件队列；账号变化或新线程首次命中仍会输出新记录。
- 本机状态：监控服务接受 `account` JSONL 记录，仅允许 `wxid_` 格式并写入既有 `receiverCandidates`；未确认账号时上报 `RECEIVER_CONFIRMATION_REQUIRED`，当前账号与已确认账号不一致时上报 `DEGRADED / ACCOUNT_MISMATCH`。
- 原生配置：Win32 配置程序读取 `wechat-monitor-state.json` 中的 Hook 账号候选，与已保存账号去重后填充下拉框；只有一个候选时自动选中，仍需管理员点击保存确认。
- 版本：Windows 微信监控端版本提升到 `0.4.0`；管理端组件仍以 `wechat_hook` 展示，主 Hook 偏移字段保留收款观察点 `0x53c9350`，账号观察点记录在状态说明和本文档。
- 构建验证：MinGW Release 构建通过，观察 DLL、测试目标、受限宿主和原生配置程序均生成 Windows x64 文件；观察 DLL 只依赖 `KERNEL32.dll` 和 `msvcrt.dll`。
- 自动测试：监控服务和状态存储 2 个测试文件共 12 项通过，覆盖账号候选记录、非法账号拒绝和账号不一致状态上报。
- Windows 自有目标：自加载模式输出 3 条收款事件和 2 条去重账号记录，新线程同时命中两类观察点，`CfpayObserverStop` 后两类调用均不再输出；命名管道注入连续 3 次通过，并继续拒绝非 `Weixin.exe` 生产目标。
- 运行边界：本步骤没有把观察 DLL 注入真实微信进程；对真实微信只执行磁盘文件的只读签名校验。生产短时验证仍需单独记录并在异常、告警或退出时立即停用。

### WX-068 微信监控端可选本地调试日志

- 配置开关：Win32 原生配置程序新增“写入本地调试日志”复选框，默认关闭；保存为 `debugLogging` 布尔值，不要求管理员手工填写日志路径。
- 日志位置：配置程序固定生成 `%LOCALAPPDATA%\CFPay\wechat-monitor.log` 并写入 `logPath`；监控服务继续使用环境变量展开和绝对路径解析，避免依赖当前工作目录。
- 写入范围：开启后记录服务启动、进程发现、宿主标准错误、Hook 状态处理、账号候选、支付队列和上报重试；关闭时复用原控制台 logger，不创建或追加本地日志文件。
- 轮转策略：单文件上限 2 MiB，超过后把当前文件替换为 `.1` 备份再创建新文件，只保留一代历史，避免长期运行无限增长。
- 敏感边界：日志接口只接收程序内部生成的诊断消息，不记录配置 JSON、监控端密钥、HMAC 请求头、支付宝证书或私钥；日志写入失败只输出本地错误，不中断 Hook 和支付队列。
- 配置兼容：示例配置新增 `debugLogging=false` 和默认 `logPath`；旧配置没有字段时保持关闭，字段类型错误时原生配置页也按关闭处理。
- 自动验证：新增日志写入和轮转单元测试，验证控制台仍收到消息、旧日志进入 `.1`、新日志继续写入；原生配置程序窗口高度同步增加，避免新增复选框与操作按钮重叠。

### ADM-005 易支付商户 ID 随机生成

- 管理后台“易支付商户 ID”改为输入框与生成按钮组合，点击后使用浏览器 `crypto.getRandomValues` 生成 `m_` 前缀的 24 位十六进制标识。
- 生成动作只填入表单并显示成功提示，不立即写入 D1；管理员仍需点击“保存设置”，避免误触直接更换现有商户身份。
- 兼容约束：生成值长度为 26，只包含字母、数字和下划线，符合服务端 `1-32` 位格式校验及易支付 `pid` 参数要求。
- 缓存处理：管理后台脚本和样式查询版本更新为 `v=20260928.3`，避免浏览器继续使用没有生成按钮的旧资源。

### OPS-005 微信 0.01 元收款未捕获核查

- 用户现象：微信已收到 `0.01` 元，同时本机配置页成功调用服务端并显示文件选择对话框；后者只证明配置 API 可用，不代表 Hook 监控已启动。
- Windows 状态：只发现 `cfpay-wechat-monitor.mjs --setup` 配置服务进程，没有常驻监控进程；默认安装目录不存在监控日志和事件状态文件。
- 配置状态：当前被忽略的本地配置仍为旧局域网测试网关和占位接收账号，尚未保存生产网关、真实目标路径与可运行配置。
- Cloudflare 证据：生产 D1 的微信 `payment_event` 和微信 `payment_order` 查询均为 0 行；监控设备表只有此前生产连通性检查设备，没有本次 Hook 心跳或组件事件。
- 结论：该笔收款未被监控端捕获，也无法事后补抓；必须先保存有效配置并启动常驻监控服务，再进行下一笔收款测试。

### DEP-001 Cloudflare 授权与生产 D1

- OAuth 授权：第二次 `wrangler login` 成功，获得当前账户的 Workers、D1、路由和 Secrets 写权限；凭据由 Wrangler 保存在用户配置目录，不进入仓库。
- 资源审计：账户原有 `cffk_db` 未修改；新建独立生产数据库 `cfpay_db`，区域为 WNAM。
- Worker 绑定：`wrangler.jsonc` 已写入 `DB` 绑定的实际数据库 ID `00000000-0000-0000-0000-000000000000`。
- 后台入口：生成随机后台路径和非默认管理员账号并写入非敏感 Worker vars。
- 敏感配置：后台密码、`SESSION_SECRET`、易支付密钥和监控端密钥只写入被 Git 忽略的 `config/production.local.json`；Worker Secrets 使用独立忽略文件。
- 下一步：上传 Worker Secrets、执行远端 D1 迁移并部署 Worker，然后通过 HTTPS 完成后台初始化。

### DEP-002 生产后台密码轮换与登录验证

- 登录语义：`ADMIN_PASSWORD` 是 Worker Secret 中保存的明文比较值，服务端使用恒定时间比较，不是可回填到登录框的哈希或密文；旧值只是随机格式看起来类似 Base64。
- 问题复核：使用本地忽略配置中的旧账号密码直接请求生产 `POST /admin-api/login`，返回 HTTP 200 和有效管理会话 Cookie，证明原生产 Secret 与本地记录一致。
- 密码轮换：为避免复制时遗漏 `+`、`=` 等特殊字符，重新生成仅含小写十六进制字符的 36 位强密码，并同步更新本地忽略配置与 Cloudflare Worker Secret `ADMIN_PASSWORD`。
- 反向验证：轮换后使用旧密码请求生产登录接口返回 HTTP 401 和“账号或密码错误”，证明旧凭据已失效。
- 正向验证：使用新密码请求同一生产登录接口返回 HTTP 200 和 `{"ok":true}`，证明新凭据已经在生产环境生效。
- 敏感信息：新密码只保存在被 Git 忽略的 `config/production.local.json` 和 `config/worker-secrets.local.json`，本文及 Git 历史均不记录密码内容。

### OPS-001 临时 Clash 中继清理

- 使用范围：生产登录验证期间只为单次 WSL 命令设置 `http_proxy` 和 `https_proxy`，没有写入 shell 全局环境或项目配置。
- 清理结果：停止此前监听 Windows 局域网地址 `172.24.163.152:17890` 的普通用户 TCP 中继。
- 关闭验证：清理后从 WSL 对 `172.24.163.152:17890` 发起 TCP 连接，在两秒超时内无法建立连接，确认局域网中继已关闭。
- 保留服务：Windows 本机 Clash 的回环监听 `127.0.0.1:7890` 未修改。

### DEP-003 生产自定义域名与 Windows 直连

- 故障定位：Windows 将原 `workers.dev` 域名解析到 `65.49.26.98`，直连健康检查在 12 秒内超时；Clash `127.0.0.1:7890` 正常，经代理访问原 Worker 健康检查返回 HTTP 200。
- 浏览器状态：Windows WinINET 系统代理为关闭状态，因此浏览器访问原 `workers.dev` 地址没有进入 Clash，这就是后台网页打不开的直接原因。
- 域名审计：Cloudflare 账户当前唯一活动 Zone 为 `example.com`；已有 `cffk.example.com` 绑定到其他 Worker，未修改该记录。
- 新域名：在 Wrangler 配置中新增 `pay.example.com` 自定义域名并显式关闭 `workers_dev` 与预览地址，避免继续依赖受限的 `workers.dev` 入口。
- 部署结果：Worker 生产版本 `37f099b9-0511-4b57-8d9d-4b8501dab39f` 发布成功，Cloudflare 域名记录确认 `pay.example.com` 绑定到生产服务 `cfpay`。
- 直连验证：Windows 在系统代理关闭状态下解析到 Cloudflare 地址 `104.21.69.50`、`172.67.204.177`，健康检查、后台 HTML 和真实后台登录均返回 HTTP 200。
- 本地记录：被 Git 忽略的生产配置新增 `baseUrl=https://pay.example.com`；密码和其他 Secret 仍未进入 Git。
- 网络清理：部署期间使用的普通用户临时中继已停止，对 `172.26.128.1:17890` 的 TCP 连接验证失败，确认没有遗留监听。

### SEC-001 追溯文档敏感信息处置

- 发现方式：自定义域名步骤的提交后检查发现，追溯文档中存在一组不应进入版本库的生产登录凭据。
- 远端边界：问题提交仅存在于本地，期间没有执行 `git push`；记录的远端跟踪分支不包含该提交。
- 历史修复：删除文档中的账号密码明文并修正本地提交，随后过期全部相关 reflog 并立即清理不可达对象；旧提交对象已无法读取。
- 凭据轮换：即使问题提交未推送，仍按已暴露处理，重新生成后台密码并更新本地忽略配置与 Cloudflare Worker Secret。
- 失效验证：使用被替换密码请求 `https://pay.example.com/admin-api/login` 返回 HTTP 401。
- 生效验证：使用新密码请求同一接口返回 HTTP 200 和 `{"ok":true}`。
- 全历史扫描：对当前全部可达 Git 历史执行旧密码精确检索，未发现匹配；新密码也从未写入受 Git 管理文件。
- 网络清理：轮换期间的临时 Clash 中继已停止，端口连接验证确认关闭。

### QR-001 收款码解析与重新生成

- 方案纠正：未采用二维码图片直接写入 D1 BLOB 的初始方案；该方案在提交前撤销，仓库和迁移历史中没有图片 BLOB 表。
- 与 `cffk` 的边界：`cffk` 媒体文件使用 S3 兼容对象存储并在 D1 保存元数据；本项目的收款码不需要保存原图，因此不写 OSS，只保存解析出的二维码内容。
- 浏览器解析：管理后台优先使用浏览器原生 `BarcodeDetector`，不可用或未识别时回退到随静态资源发布的 Apache-2.0 `jsQR`；图片不离开管理员浏览器。
- 服务端校验：二维码内容限制为 3 至 2048 字符、必须具有 URI scheme、禁止控制字符，并拒绝可明确识别的微信与支付宝渠道错配。
- 数据模型：新增 `payment_qr_code` 表，按渠道保存二维码内容、SHA-256 摘要和更新时间；不保存原图、Base64 或图片二进制。
- 重新生成：公开路由使用 `qrcode` 按内容生成 480 像素标准 SVG，响应包含 ETag、短期公共缓存、`nosniff` 和限制 SVG 内容的 CSP。
- 设置联动：保存内容时原子更新对应渠道的收款码地址为带摘要版本号的站内路径；删除时只清除由系统管理的站内地址，不覆盖人工填写的外部 URL。
- 自动验证：TypeScript 类型检查通过，9 个测试文件共 25 项测试通过；Wrangler dry-run 成功打包 D1、静态资源和 Durable Object。
- D1 验证：Docker Workerd 成功应用 `0001` 至 `0004` 四个本地迁移。
- HTTP 验证：局域网绑定的本地 Worker 完成登录、保存、列表、设置联动、SVG 获取、ETag 304、渠道错配 400、删除和删除后 404 全链路；生成 SVG 不包含原始二维码内容明文。

### ADM-001 动态管理员配置与易支付网关

- 管理配置表：新增单行 `admin_setting`，保存管理员账号、隐藏入口、密码哈希参数、会话版本和更新时间；没有记录时继续使用 Wrangler 变量与 Worker Secret 作为首次部署回退。
- 密码存储：新密码使用 PBKDF2-SHA256、随机 16 字节盐、310,000 次迭代和 256 位输出；D1 只保存 Base64URL 哈希、盐和迭代次数。
- 会话失效：会话载荷新增版本号；每次保存管理员配置都会递增版本、清除浏览器 Cookie，旧会话立即返回 HTTP 401。
- 动态入口：后台入口按 D1 配置实时匹配，修改后旧路径返回 HTTP 404，新路径直接返回后台 HTML；固定 `/admin` 与 `/admin.html` 被显式拦截为 404，避免 Static Assets 暴露固定入口。
- 易支付网关：系统设置接口返回当前请求 Origin，后台以只读字段展示并提供复制按钮，同时明确列出 `/submit.php`、`/mapi.php` 和 `/query.php`。
- 商户配置：易支付商户 ID 和密钥由管理员自行填写；新增浏览器 `crypto.getRandomValues` 生成 32 字节、43 位 Base64URL 密钥的按钮。
- 保存确认：随机密钥自动填入表单并打开模态对话框，显示完整密钥、复制按钮和“我已保存”确认；关闭对话框不会自动提交，仍需点击“保存设置”。
- 自动验证：TypeScript 类型检查通过，10 个测试文件共 27 项测试通过；新增 PBKDF2 正确密码、错误密码、管理员账号和入口校验测试。
- 本地迁移：Docker Workerd 成功应用 `0005_admin_setting.sql`。
- HTTP 验证：环境回退账号登录 200；保存新账号、密码和入口 200；旧会话 401、旧账号密码 401、新账号密码 200；旧入口 404、新入口 200 且标题为 CFPay 管理后台。
- 密文检查：本地 D1 中密码哈希长度 43、盐长度 22、迭代次数 310,000，明文检索结果为 0；验证后已删除本地测试管理员记录并停止测试容器。

### OSS-001 支付宝凭据加密对象存储

- 参考实现：复用 `cffk` 已验证的 `aws4fetch`、S3 兼容 Endpoint、Region、Bucket、对象前缀和 Path-style 配置模型，不读取或复制 `cffk` 的访问凭据。
- 数据迁移：新增 `0006_object_storage.sql`，无损扩展 `alipay_credential` 为 D1 密文和 OSS 加密对象双后端，并新增单行 `object_storage_setting`。
- 双层保护：支付宝私钥和证书组合继续使用 `SESSION_SECRET` 派生的 AES-GCM 密钥加密；OSS 中只保存该密文，D1 保存对象键、SHA-256、IV 和证书序列号。
- 访问密钥：OSS AccessKey ID 和 SecretKey 使用独立 AES-GCM 上下文加密后保存到 D1，后台状态接口只返回“是否已配置”，不回显明文。
- 管理流程：后台支持填写、测试并保存 S3 兼容配置；保存配置前强制执行 PUT、GET、DELETE 探测，测试按钮验证当前表单而不是旧配置。
- 位置保护：支付宝凭据已经迁移到 OSS 后，禁止直接修改 Endpoint、Region、Bucket、对象前缀和地址模式，避免原对象因配置切换失联；访问密钥仍可在原位置轮换。
- 迁移语义：迁移先上传新对象，再原子更新 D1 元数据并清除 `encrypted_payload`；上传或 D1 写入失败时清理新对象，替换成功后尽力清理旧对象。
- 完整性校验：每次从 OSS 读取支付宝凭据时先计算 SHA-256 并进行恒定时间比较，摘要不一致、对象缺失或解密失败都会阻止账务 API 使用该配置。
- 自动验证：TypeScript 类型检查通过，11 个测试文件共 30 项测试通过；对象配置校验、URL 生成、访问密钥加解密和错误密钥拒绝均有测试覆盖。
- D1 验证：Docker Workerd 成功应用第 6 个迁移；真实支付宝四文件上传后状态为 `source=database`，密文长度 3276，明文 `PRIVATE KEY` 检索为 0。
- SigV4 验证：同一源码模块对本地模拟 S3 完成带 AWS4 签名的 PUT、GET、DELETE；迁移后模拟 D1 的 `encrypted_payload` 为 `NULL`、对象摘要长度 64，并在清空模块缓存后从 OSS 重新读取和解密成功。
- 本地限制：项目启用 `global_fetch_strictly_public`，Workerd 按生产安全策略拒绝访问私网模拟 S3；没有修改正式兼容标志，改由 Node 运行同一存储模块验证私网 mock。
- 环境恢复：验证后删除本地测试 `object_storage_setting`，停止 Worker、模拟 S3 和测试容器；本地真实支付宝凭据保留为 D1 加密后端。

### EPAY-001 易支付网关与标准查询入口

- 网关定义：后台明确把当前 Origin 标为“易支付网关基础地址”，并展示 `/submit.php`、`/mapi.php`、`/api.php?act=order` 和 `/query.php` 的完整 URL 与请求方法。
- 标准查询：新增 `/api.php` 订单查询别名，支持易支付常见的 `act=order&pid=...&key=...&out_trade_no=...`；原 `/query.php` 保留兼容。
- 动作限制：`/api.php` 只接受空动作或 `act=order`，其他动作返回 HTTP 400，避免把未实现的商户资料、余额等接口误判为可用。
- 对接文档：新增 `docs/epay-api.md`，写明基础网关填写方式、完整接口、表单编码、支付参数、MD5 签名、创建和查询返回、异步通知字段及重试规则。
- HTTP 验证：局域网地址上的 `/api.php?act=order` 已进入签名校验并对无效凭据返回 HTTP 401；未知动作返回 HTTP 400，后台 HTML 包含标准查询完整地址。

### ADM-002 收款码入口精简与监控密钥生成

- 入口精简：删除后台“微信收款码图片地址”和“支付宝收款码图片地址”输入框，管理员统一在收款码区域上传图片、浏览器解析内容并由系统重新生成二维码。
- 兼容边界：后端字段和 API 参数继续保留；前端加载后原样回传已有地址，因此升级和保存其他设置不会覆盖旧的外部地址配置。
- 监控密钥：PC 监控端密钥新增浏览器随机生成按钮，使用与易支付商户密钥相同的 32 字节 `crypto.getRandomValues` 和 Base64URL 输出。
- 保存确认：商户密钥和监控端密钥复用动态标题的确认对话框，支持复制完整密钥；生成只填入表单，仍需点击“保存设置”才写入 D1。
- 前端验证：`admin.js` 语法检查通过；局域网后台 HTML 返回 HTTP 200，包含 `generateMonitorSecret`，且不再包含两个手工图片地址输入。

### ADM-003 管理操作结果提示

- 统一反馈：管理后台新增右上角 Toast 栈，成功使用绿色提示、失败使用红色提示；提示支持手动关闭，成功提示约 3.5 秒后消失，失败提示约 7 秒后消失。
- 可访问性：Toast 容器使用 `aria-live=polite`，失败消息使用 `alert` 角色；移动端限制在视口左右边距内，不会横向溢出。
- 操作覆盖：登录、退出、系统设置、管理员设置、网关与密钥复制、收款码上传和删除、OSS 保存和测试及迁移、支付宝证书上传和删除及账务测试、人工补单、订单与监控端手动刷新均显示成功或失败结果。
- 静默边界：后台初始化加载和每 30 秒监控状态轮询继续静默执行，避免网络波动时反复弹出提示；登录错误仍同步显示在登录表单内。
- 网络错误：请求封装区分浏览器离线和服务器不可达，分别提示检查网络连接或稍后重试；接口返回的业务错误继续优先展示服务端消息。
- 入口变更：保存管理员账号、密码或后台入口后使用 Toast 显示新入口，并延迟 900 毫秒跳转，替代阻塞式浏览器对话框。
- 自动验证：`admin.js` 语法检查、TypeScript 类型检查和 11 个测试文件共 31 项测试通过；Wrangler dry-run 成功打包 Worker 与 7 个静态资源。
- HTTP 验证：只读静态 HTTP 服务确认后台 HTML 返回 Toast 容器，并能正常获取包含提示逻辑的 `admin.js` 和样式；当前 WSL 的 glibc 低于新版 `workerd` 所需版本，因此完整本地 Worker 启动检查未执行，已由 dry-run 和自动测试覆盖构建与业务回归。

### ADM-004 后台空白加载恢复

- 故障现象：生产健康检查、动态后台 HTML、`admin.js`、CSS 和未登录会话接口均在约 0.2 至 1.1 秒内返回，但用户浏览器一直显示加载状态。
- 根因定位：后台 HTML 默认同时隐藏登录视图和管理视图，必须等待模块脚本成功执行并完成会话请求后才显示登录页；脚本缓存、浏览器拦截或请求挂起都会造成永久空白。
- 渐进显示：登录视图改为 HTML 默认可见，管理视图继续默认隐藏；即使 JavaScript 完全未执行，管理员也能看到登录表单和页面结构。
- 请求超时：管理端所有 API 请求统一增加 15 秒 `AbortController` 超时，超时后显示“请求超时，请检查网络后重试”，不再无限等待浏览器网络栈。
- 初始化隔离：未登录会话失败只切回登录视图；已登录后某个设置、订单或监控接口失败时保留管理后台框架并显示失败 Toast，不再把整个页面退回隐藏状态。
- 登录隔离：登录成功后立即显示管理视图和成功提示；后续并行数据加载失败只显示对应错误，不会把成功登录误报为登录失败。
- 缓存处置：动态后台 HTML 和 `admin.js` 响应强制设置 `no-store, no-cache, must-revalidate`、`pragma: no-cache` 和过期时间 0，避免浏览器继续复用旧的隐藏登录页实现。
- 资源换代：后台 HTML 为当前 `admin.js` 和 `styles.css` 增加版本查询参数，使已取得新 HTML 的浏览器立即请求新脚本和样式。

### DEP-004 网关、后台和对象存储能力生产发布

- 迁移审计：生产 D1 待执行项仅为 `0004_payment_qr_code.sql`、`0005_admin_setting.sql` 和 `0006_object_storage.sql`；应用前支付宝密文长度 3276、IV 长度 16，订单数为 0。
- 迁移结果：三个迁移全部成功；支付宝凭据保持 `storage_backend=database`、密文长度 3276、对象键为空，证明表重建没有丢失现有凭据。
- 默认回退：生产 `object_storage_setting` 为 0 行，发布后不会自动迁移或外传支付宝凭据；只有管理员保存并验证 OSS 配置后才可手动迁移。
- 发布版本：Cloudflare Worker 版本 `64ce9c74-940e-4cf2-9e92-7634cd93f4e7` 已绑定 `pay.example.com`，Cron 和 Durable Object 绑定保持不变。
- 健康验证：`GET /api/health` 返回 HTTP 200；动态后台入口和真实账号密码登录返回 HTTP 200，并成功签发管理会话 Cookie。
- 后台验证：生产 HTML 包含 PC 监控端密钥生成按钮和 `/api.php?act=order`，不再包含微信或支付宝手工图片地址输入。
- 配置验证：系统设置返回网关 `https://pay.example.com`，商户密钥和监控端密钥均显示已配置；OSS 状态明确为未配置。
- 支付宝验证：凭据状态为 `configured=true`、`source=database`；真实账务查询返回 HTTP 200、有效 trace ID 和账务总数 0。
- 易支付验证：使用生产商户 ID 和密钥调用 `POST /api.php` 查询随机不存在订单，通过鉴权后返回 HTTP 404 和“订单不存在”，证明标准查询入口已生效。
- 网络清理：部署只按命令使用普通用户 TCP 中继连接 Windows Clash；完成后停止中继，Windows 监听检查与 WSL TCP 连接均确认 `172.26.128.1:17890` 已关闭。

### DEP-005 管理操作提示生产发布

- 发布范围：本次没有 D1 迁移和 Worker Secret 变更，只发布管理后台 Toast 容器、请求错误转换、主动操作反馈和对应静态样式；微信监控端本机 Web 应用位于工具发行包，不进入 Worker 静态资源。
- 首次链路：Windows Clash 正常监听 `127.0.0.1:7890`，临时中继监听 `172.26.128.1:17890`；Wrangler 已完成账户 API 请求，但在创建静态资源上传会话时等待约 93 秒后因代理 TLS 链路 `fetch failed` 退出，没有返回新版本号。
- 直连恢复：随后 WSL 直连 Cloudflare API 返回正常 HTTP 400 缺少鉴权头响应，证明网络可达；移除全部代理环境变量后重新部署成功。
- 资源结果：Wrangler 检测并上传 `/admin.html`、`/admin.js` 和 `/styles.css` 三个变更资源，其余三个已有资源复用；Worker 启动时间为 11 毫秒。
- 发布版本：Cloudflare Worker 版本 `92f1cafe-6acc-405a-9f8e-575920e29e0a` 已绑定 `pay.example.com`，每分钟 Cron、D1、Durable Object 和 Static Assets 绑定保持不变。
- 生产验证：`GET /api/health` 返回 HTTP 200；使用被 Git 忽略的生产账号密码登录返回 HTTP 200，动态后台入口返回 HTTP 200。
- 前端验证：生产后台 HTML 包含 `toastStack`，生产 `admin.js` 包含 `showToast` 和失败处理逻辑，生产 CSS 包含成功与失败 Toast 样式。
- 敏感边界：生产账号、密码、商户密钥和监控端密钥只从 `config/production.local.json` 读取，部署输出、本文和 Git 历史均未记录其值。
- 网络清理：部署后停止普通用户临时中继；Windows 监听检查返回 `CLOSED=YES`，WSL 对 `172.26.128.1:17890` 的连接在两秒内无法建立，确认没有遗留端口。

### WX-069 微信真实进程受控停止与冒烟验证工具

- 工具确认：现有 Ghidra 项目、Windows 原生宿主、观察 DLL、测试目标、MinGW 构建链路和硬件断点测试均仍可用；主微信进程 PID `1744` 已加载目标版本 `Weixin.dll`，检查时尚未加载观察 DLL。
- 停止命令：`cfpay-wechat-host.exe` 新增 `--stop-weixin <pid>`，只接受进程名 `Weixin.exe`、目标版本和四处静态签名均匹配且已加载固定文件名观察 DLL的进程。
- 清理方式：停止命令按本地映射导出偏移计算远程 `CfpayObserverStop`，等待 DLL 清除 `DR0/DR1/DR2/DR3`、移除异常处理器和停止工作线程；DLL 不动态卸载，避免在微信进程内执行不稳定的卸载流程。
- 冒烟脚本：新增 `tools/run-windows-weixin-observer-smoke.ps1`，等待 `HOOK_ACTIVE` 后定时只读观察，随后请求优雅停止并确认 `HOOK_STOPPED` 和微信进程仍存活。
- 隐私边界：冒烟脚本的持久化结果只记录状态与事件计数，不保存 `wxid`、账号数值标识、转账双方或转账内容；原始命名管道输出在生成脱敏结果后删除。
- 离线验证：MinGW Windows Release 构建通过；自有测试目标命名管道连续 3 次通过，每次均包含 3 条收款事件、2 条去重账号事件、`HOOK_ACTIVE` 和 `HOOK_STOPPED`；生产加载与停止命令均在远程操作前拒绝非微信进程。
- 脚本边界：真实进程冒烟脚本以 Windows 系统进程执行入口测试，参数解析和产物复制正常，并在注入前因目标不是 `Weixin.exe` 明确退出。
- 当前边界：本步骤先实现和离线验证受控停止能力，真实微信注入结果将在下一条追溯记录中单独登记。

### WX-070 微信真实进程首次冒烟启动修复

- 首次执行：对主微信 PID `1744` 启动 10 秒冒烟脚本时，PowerShell 在轮询 `Start-Process` 重定向输出文件时遇到文件独占错误，脚本立即进入清理分支并结束宿主。
- 状态核对：主微信进程继续存活且仍只加载 `Weixin.exe` 与目标 `Weixin.dll`；未发现 `cfpay-hook-observer.dll`，原始输出和错误输出均为空，说明本次在宿主完成注入和输出 `HOOK_ACTIVE` 前中止。
- 脚本修复：轮询改用 `FileStream` 配合 `FileShare.ReadWrite` 读取重定向文件，不再要求独占读取；正式结果解析仍在宿主退出后执行。
- 安全边界：本次没有观察事件、没有留下硬件断点或观察 DLL，也没有触发微信支付、登录、切号和界面操作。

### WX-071 微信真实进程双 Hook 短时验证

- 验证目标：主微信 PID `1744`，程序版本 `4.1.15.13`，目标 `Weixin.dll` SHA-256 与离线逆向样本一致；执行前未加载观察 DLL。
- 验证时长：观察器在真实微信进程中只读运行 10 秒，全程没有自动点击界面，没有主动调用支付、登录、切号或账号资料业务函数。
- 状态序列：宿主依次收到 `TARGET_VALIDATED`、`HOOK_INSTALLING`、`HEALTHY / HOOK_ACTIVE` 和 `STOPPED / HOOK_STOPPED`，证明目标复核、显式加载、`DR0/DR1/DR2/DR3` 安装和受控清理均完成。
- 事件结果：10 秒静默窗口内收款事件计数为 0、账号事件计数为 0，符合没有新收款且没有打开账号资料相关界面的运行条件；不能据此否定账号 Hook 地址，只能说明该函数没有自然执行。
- 清理结果：`CfpayObserverStop` 成功清除全部硬件断点并移除异常处理器；微信 PID `1744` 继续存活，观察 DLL按设计保持惰性加载，宿主进程退出且没有残留。
- 稳定性检查：Windows 最近 30 分钟应用事件日志中与 `Weixin` 或 `WeChat` 相关的应用错误、Windows 错误报告和 .NET 运行时事件计数均为 0。
- 脱敏产物：本机结果只保留 4 条状态和事件计数摘要，没有保存 `wxid`、账号数值标识、转账双方或转账内容；原始管道输出已删除。
- 后续限制：同一微信进程已加载惰性观察 DLL，宿主会拒绝重复注入；如需验证账号资料自然触发，必须先正常退出并重启微信，再由用户手动打开设置或个人资料页，不能由工具自动点击或主动调用业务函数。

### WX-072 微信观察 DLL 原地恢复

- 生产缺口：原观察 DLL 停止后保持惰性加载，宿主发现同名 DLL 时直接拒绝，导致监控服务、宿主或电脑网络异常重启后必须重启微信才能恢复收款监控。
- 发行版本：Windows 微信监控端提升到 `0.5.0`，Node 服务上报版本、原生配置连接测试请求体和 WinHTTP User-Agent 同步更新。
- 生命周期：观察 DLL 新增 `STOPPED / STARTING / RUNNING / STOPPING` 四态管理和 `CfpayObserverStart` 导出；停止后关闭工作线程、队列事件和旧管道，清空线程断点状态与事件队列，再次启动时重新连接管道、复核微信内存签名并安装 `DR0/DR1/DR2/DR3`。
- 宿主恢复：`--run-weixin` 和测试管道模式发现观察 DLL 已加载时，先比较已加载 DLL 文件与当前发行包 DLL 的 SHA-256，再直接解析目标进程内 PE 导出并校验运行时 API 版本 `0x00050000`，全部一致后才通过受限远程线程调用 `CfpayObserverStart`；哈希、内存版本不一致、实例仍在运行或导出调用失败时拒绝恢复。
- 并发边界：`CfpayObserverStart` 不抢占已经处于 `RUNNING` 的实例；`CfpayObserverStop` 会等待启动或已有清理完成，并保证最终进入 `STOPPED`，避免两个宿主同时持有同一微信进程的观察状态。
- 测试目标：新增 `--wait-pipe-reactivation`，同一进程和同一 DLL 映像连续执行两轮完整观察，每轮均重新连接命名管道、安装断点、捕获 3 条收款事件和 2 条去重账号事件，再输出 `HOOK_STOPPED`。
- 编译器约束：测试导出函数增加 `noclone`，防止 GCC 生成 `constprop` 克隆导致测试调用绕过导出地址上的硬件断点；反汇编确认构建产物只保留真实 `CfpayTestParser` 和 `CfpayTestAccount` 入口。
- 回归结果：MinGW Windows Release 构建通过；普通命名管道连续 3 次、原地恢复连续 2 轮、文件输出连续 3 次、显式注入连续 3 次均通过；生产加载、生产停止和测试注入继续在远程操作前拒绝非目标进程。
- 应用回归：TypeScript 类型检查通过，12 个测试文件共 46 项测试通过，Windows 监控端 `0.5.0` 发行目录重新生成成功。
- 真实边界：当前微信 PID `1744` 仍加载上一版已停止观察 DLL，新发行包哈希不同，宿主会按设计拒绝原地恢复；升级版真实恢复需在正常重启微信并加载新 DLL 后验证。

### WX-073 微信真实恢复验证脚本升级

- 模式扩展：`tools/run-windows-weixin-observer-smoke.ps1` 不再把已加载观察 DLL视为固定错误，允许宿主按哈希和运行时 API 版本判断能否原地恢复。
- 结果区分：脱敏摘要和控制台新增 `observerInitiallyLoaded`，首次注入应为 `false`，同一微信进程第二轮恢复应为 `true`；状态序列仍必须包含 `HOOK_ACTIVE` 和 `HOOK_STOPPED`。
- 安全边界：脚本自身不跳过宿主校验；旧 DLL、哈希不一致、运行时 API 版本不一致和正在运行的实例仍由受限宿主拒绝，不会强制覆盖或卸载目标进程模块。

### WX-074 微信线程覆盖瞬时误报修复

- 真实现象：微信重启后首次加载 `0.5.0` 观察器的 10 秒验证成功完成 `HOOK_ACTIVE` 和 `HOOK_STOPPED`，但中途出现一次 `THREAD_COVERAGE_INCOMPLETE`；同一进程第二轮原地恢复没有该状态，微信始终存活且无崩溃日志。
- 根因范围：线程枚举与 `OpenThread`、`SuspendThread` 之间存在正常退出竞态，短生命周期线程可能在快照后消失；旧逻辑把单次瞬时失败立即上报为持续降级。
- 退出识别：`OpenThread` 返回 `ERROR_INVALID_PARAMETER`，或已打开线程在挂起前确认不再为 `STILL_ACTIVE` 时，清除预登记线程槽并视为正常退出，不计入覆盖失败。
- 降级防抖：只有连续 3 次、约 750 毫秒仍无法覆盖存活线程时才上报 `THREAD_COVERAGE_INCOMPLETE`；后续覆盖恢复时主动上报 `HEALTHY / THREAD_COVERAGE_RESTORED`。
- 回归约束：普通命名管道和原地恢复测试新增断言，任何 `THREAD_COVERAGE_INCOMPLETE` 都会使回归失败。
- 验证结果：MinGW Windows Release 构建通过；普通命名管道连续 3 次和同进程原地恢复连续 2 轮均为零覆盖告警；TypeScript 类型检查、12 个测试文件共 46 项测试和 Windows `0.5.0` 发行目录生成均通过。

### WX-075 微信真实进程原地恢复验证

- 升级保护：PID `1744` 中加载的旧观察 DLL面对 `0.5.0` 宿主时返回 `HOOK_INSTALL_FAILED / loaded observer DLL does not match the current package`，宿主没有调用旧运行时导出或覆盖进程模块。
- 首轮验证：正常重启微信并得到 PID `1352` 后，首次 10 秒观察状态为 `TARGET_VALIDATED → HOOK_INSTALLING → HOOK_ACTIVE → HOOK_STOPPED`，`observerInitiallyLoaded=false`。
- 恢复验证：同一 PID 和同一 DLL 映像第二次 10 秒观察状态为 `TARGET_VALIDATED → HOOK_REACTIVATING → HOOK_ACTIVE → HOOK_STOPPED`，`observerInitiallyLoaded=true`，证明没有重复 `LoadLibraryW`。
- 覆盖结果：两轮均未出现 `THREAD_COVERAGE_INCOMPLETE`，收款和账号事件计数均为 0，符合静默窗口内没有新收款且没有打开资料界面的条件。
- 稳定性：两轮停止后微信 PID `1352` 继续存活，窗口标题为“微信”，观察 DLL保持惰性加载；Windows 最近 30 分钟相关崩溃事件计数为 0。
- 操作边界：重启只结束并重新启动主微信进程，没有自动点击、登录、切号或调用支付与账号资料业务函数；临时重启脚本执行后已删除。

### WX-076 微信监控端合并为单 EXE

- 运行形态：`cfpay-wechat-monitor.exe` 合并托盘常驻、状态窗口、配置窗口、进程发现、支付队列、Worker HMAC 上报和受限宿主逻辑；注入所需 `cfpay-hook-observer.dll` 继续作为唯一外部运行文件。
- 内部命令：监控端通过自身子进程的 `--internal-host` 模式执行目标校验、观察启动和受控停止；配置入口通过同一 EXE 的 `--config-window` 模式显示，不再依赖独立宿主或配置 EXE。
- Node 清理：生产发行目录不再包含 `.mjs`、`package.json`、Node 服务、PowerShell 定位脚本或 `run.ps1`；安装器不检测 Node，登录启动和开始菜单配置快捷方式均直接指向同一个 EXE。
- 配置迁移：原生配置保存和安装器都会删除旧版 `hostPath`、`locatorScriptPath`，只保留观察 DLL路径、微信目标和监控协议配置；示例配置同步清理旧字段。
- 原生能力：C++ 监控端使用 WinHTTP、BCrypt、Toolhelp、动态加载的系统 `xmllite.dll` 和持久化 JSON 状态完成原 Node 常驻服务职责；调试日志、账号候选、支付去重与失败重试保持在本机。
- 编译约束：交叉编译切换为 MinGW POSIX 线程模型并静态链接运行库，最终 EXE 只依赖 Windows 系统 DLL；开发构建仍保留独立测试目标，但不会复制到生产发行目录。
- Windows 验证：发行目录严格只有 `cfpay-wechat-monitor.exe` 与 `cfpay-hook-observer.dll`；原生自测、同一 EXE 内置宿主对真实 `Weixin.dll 4.1.15.13` 的 `TARGET_VALIDATED`、状态窗口、配置窗口、四项托盘右键菜单和菜单退出流程全部通过。
- 安全边界：本步骤没有启动真实观察、没有重新安装硬件断点、没有读取收款数据，也没有自动操作微信；目标校验只读取磁盘上的已知版本 DLL。

### WX-077 单 EXE 生产安装与 Hook 上线

- 配置迁移：从被 Git 忽略的生产配置读取 HTTPS 网关和现有监控密钥，生成随机监控端 ID `wm_000000000000000000000000`；新本地配置删除 `hostPath` 和 `locatorScriptPath`，接收账号暂留空以进入候选确认模式。
- 网关预检：安装前使用新监控端 ID 和生产密钥发送 HMAC 心跳，生产 Worker 返回 HTTP 200，证明网关、时间戳、nonce 和签名配置有效。
- 安装结果：`%LOCALAPPDATA%\CFPay\WeChatMonitor` 只包含 `cfpay-wechat-monitor.exe`、`cfpay-hook-observer.dll` 和 `config.json`；登录启动与开始菜单配置快捷方式均指向同一 EXE，未安装或启动 Node 常驻服务。
- 升级保护：重启前微信 PID `1352` 加载的旧惰性观察 DLL SHA-256 为 `38159918924390d08431358f58780dc2f2dcadf96b4bbcfb2821a541bef33765`，新发行包为 `de56ddba31dd2a840623e02550c35a0bd3ec1deb1fd359102b21eb54942a8625`；没有尝试覆盖或调用旧 DLL。
- 受控重启：新增严格路径校验脚本，只结束路径为 `D:\Program Files\Tencent\Weixin\Weixin.exe` 的进程并通过 CIM 重新启动；首次执行遇到辅助进程自然退出竞态后，脚本改为忽略已消失 PID，再次执行成功。
- 运行状态：已安装托盘进程 PID `19020`，同一 EXE 的内部宿主 PID `24760`；新微信 PID `516` 已加载安装目录中的观察 DLL，磁盘 SHA-256 与发行包完全一致。
- 账号候选：账号 Hook 在微信启动后自然命中，状态文件记录 1 个接收账号候选；追溯日志和命令输出不记录候选 `wxid`。配置仍保持未确认状态，确认前支付事件不会进入上传队列。
- 生产验证：管理 API 返回设备版本 `0.6.0`、平台 `windows`、组件 `wechat_hook` 状态 `HEALTHY / HOOK_ACTIVE`、目标版本 `Weixin 4.1.15.13` 和进程 PID `516`；该设备未匹配事件数为 0。
- 运维工具：新增脱敏审计、已安装监控端 CIM 启动和受限微信重启脚本，分别用于状态核对、持久启动和版本升级；所有脚本均不输出监控密钥或账号候选值。
- 下一步：管理员在已打开的原生配置窗口确认唯一账号候选并保存，然后创建微信测试订单并完成一笔小额真实收款，验证支付事件、订单状态和易支付查询接口闭环。

### WX-078 已安装监控端优雅重启

- 启动脚本：`tools/start-installed-windows-wechat-monitor.ps1` 增加 `-Restart` 参数，只匹配安装目录下带 `--tray` 参数的托盘主进程。
- 退出方式：脚本按进程 ID 查找 `CFPayWeChatMonitorWindow` 窗口并发送现有退出菜单命令，等待托盘进程和内部宿主正常收尾，不使用强制结束进程。
- 失败保护：找不到目标窗口或 30 秒内未正常退出时终止重启并返回错误，避免重复启动两个监控实例。

### WX-079 原生配置窗口日志入口

- 界面精简：移除配置窗口顶部 `CFPAY WINDOWS MONITOR` 英文行，主标题和表单整体上移；`自动推导的 Weixin.dll` 简化为 `Weixin.dll`。
- 日志入口：在“写入本地调试日志”开关右侧增加“打开日志”按钮，读取当前配置的 `logPath`，未配置时使用 `%LOCALAPPDATA%\CFPay\wechat-monitor.log`。
- 文件处理：日志目录或文件不存在时先创建，再通过 `notepad.exe` 打开；目录创建、文件创建或记事本启动失败时均在窗口状态区和对话框中提示。

### WX-080 04:05 微信收款漏报与诊断增强

- 漏报确认：用户在 `2026-09-28 04:05` 完成一笔 `0.01` 元微信收款；本地状态文件的待发送与已发送数量均为 0，日志在该时段没有支付 Hook 事件，确认未捕获且无法事后补抓。
- 覆盖结论：当前生产断点 `Weixin.dll + 0x53c9350` 是转账展示结构解析器，只能证明转账消息链路，不覆盖收款码到账/收款助手消息；后续必须定位并验证独立的收款码到账展示函数。
- 观察计数：观察 DLL 的 `HOOK_ACTIVE` 健康消息增加支付入口、支付返回、账号入口、账号返回、读取失败、队列丢弃和已覆盖线程计数，可区分未命中、返回未完成、读取失败和队列溢出。
- 本地日志：版本提升到 `0.6.1`，日志改为本地可读时间并带进程/线程 ID，记录进程发现、宿主启动退出、Hook 状态详情、事件类型与长度、规则拒绝原因、队列结果、HTTP 状态码及 WinHTTP 错误码。
- 隐私边界：增强日志不记录监控密钥、HMAC 请求头、wxid、昵称、交易号或原始消息正文；收款事件只记录金额分值、类型、内容字节数和时间。
- 回归结果：MinGW Windows Release 构建通过，命名管道自有目标回归通过；真实微信需升级 DLL并重启后才能使用新增观察计数。

### WX-081 监控状态与配置合并为单窗口

- 问题确认：旧版虽然只有一个发行 EXE，但“打开配置”通过 `--config-window` 启动第二个进程和顶层窗口；托盘主进程退出后，配置窗口仍会独立存在。
- 单窗口结构：生产主窗口顶部显示运行与上报状态，下方直接嵌入完整配置表单；“打开配置”按钮和托盘重复菜单项删除，只保留“显示窗口”“隐藏到托盘”和“退出”。
- 单实例行为：开始菜单保留 `--config-window` 参数以兼容现有快捷方式；未运行时启动完整主窗口，已运行时第二个进程立即退出并还原现有窗口，不创建第二个 EXE 窗口。
- 开发边界：独立 `cfpay-wechat-config.exe` 继续作为不进入发行包的开发构建目标；生产发行目录仍只有主 EXE 与观察 DLL。
- 验证更新：单 EXE UI 验证改为在主窗口同时检查状态与配置控件，并断言第二次 `--config-window` 调用只经过单实例还原路径。

### WX-082 已安装监控端正常停止工具

- 停止脚本：新增 `tools/stop-installed-windows-wechat-monitor.ps1`，按安装路径筛选主 EXE，向新版主窗口发送现有托盘退出命令。
- 旧版兼容：升级期间若仍存在旧版 `CFPayWeChatConfigWindow` 独立窗口，脚本同时发送正常关闭消息，避免配置进程阻止文件替换或继续显示。
- 失败保护：脚本等待最多 30 秒确认主进程、内部宿主和旧配置进程全部退出；超时返回错误，不调用 `Stop-Process -Force`。

### WX-083 单窗口 DPI 裁切修复

- 现象复核：生产窗口在当前 Windows DPI 环境下外框宽高被缩放，但固定坐标子控件仍保持原尺寸，导致右侧输入框和底部表单被裁切，Hook 健康详情也占用过多纵向空间。
- 自适应尺寸：主窗口创建全部状态与配置控件后，枚举子控件实际边界并按右侧 28 像素、底部 24 像素留白重新计算外框尺寸，不依赖固定窗口宽高或 DPI 比例猜测。
- 状态摘要：`HOOK_ACTIVE` 主窗口详情简化为“微信 Hook 正常运行”；入口、返回、读取失败、队列丢弃和线程计数继续完整写入调试日志与 Worker 状态详情。
- 布局调整：运行状态区增加高度，分隔线和配置表单整体下移，避免状态换行覆盖配置标题。
- 回归结果：MinGW Windows Release 构建、命名管道自有目标连续 3 次、单 EXE 自测、真实 `Weixin.dll 4.1.15.13` 校验、合并窗口 UI、TypeScript 类型检查和 12 个测试文件共 46 项测试均通过。

### ADM-005 监控端与组件状态合并展示

- 页面结构：后台“监控端”页删除独立的“设备”和“组件状态”两张表，合并为一张“监控状态”表。
- 状态规则：设备心跳超过 90 秒时统一显示“离线”；设备在线但组件超过 90 秒未上报时显示“上报超时”；两者正常时直接显示组件的 Hook 状态。
- 数据兼容：Worker API 继续返回 `devices` 与 `components` 两组数据，前端按设备 ID 合并，兼容尚未上报组件的设备和缺少设备行的历史组件记录，无需数据库迁移。
- 缓存更新：后台静态资源版本参数提升到 `20260928.4`，避免浏览器继续使用分表版脚本。

### DEP-006 后台空白加载与缓存修复发布

- 首次修复发布：登录视图默认可见、15 秒请求超时和初始化错误隔离发布为 Worker 版本 `bd2e6b85-c761-477e-b14b-f9bc96319dba`；Wrangler 上传 `/admin.html` 和 `/admin.js` 两个静态资源。
- 服务端状态：发布前后生产 `/api/health`、动态后台 HTML、`admin.js`、CSS 和未登录会话接口均曾成功返回；Windows 真实账号登录最终返回 HTTP 200，证明后台服务与凭据正常。
- 网络诊断：Windows 对 `104.21.69.50` 和 `172.67.204.177` 两个 Cloudflare IPv4 分别连续测试，同一 IP 既有 HTTP 200 也有 5 秒连接超时；直连请求还出现连接重置，说明“网页一直加载”同时包含客户端到 Cloudflare 的间歇性链路故障。
- Clash 对比：Windows Clash `127.0.0.1:7890` 连续五次访问生产健康接口全部连接超时或 TLS 握手失败，未启用为系统代理，也没有用于用户后台访问。
- 缓存修复：动态后台 HTML 与 `admin.js` 增加 `no-store, no-cache, must-revalidate`、`pragma: no-cache` 和过期时间 0；后台 HTML 引用带 `v=20260928.2` 的脚本和样式。
- OAuth 续期：第二次发布前 Wrangler OAuth 令牌到期；浏览器已返回授权码，但首次令牌交换因直连 `dash.cloudflare.com` 超时失败。重新打开同一轮授权页面后成功登录，没有创建 API Token 或把令牌写入仓库。
- 临时重试代理：为令牌交换和发布在 WSL 回环 `127.0.0.1:17891` 启动仅支持 CONNECT 的临时代理，上游建连失败最多重试六次，TLS 内容保持端到端加密；发布和验证完成后已停止该进程。
- 最终版本：缓存修复发布为 Worker 版本 `48886535-f0ea-46ea-9dc7-c956ea9c3831`，自定义域名、Cron、D1、Durable Object 和 Static Assets 绑定保持不变。
- 生产验证：动态后台 HTML 和 `admin.js` 均返回预期禁用缓存响应头；HTML 中登录视图不再包含 `hidden`，并引用版本化脚本和样式；生产脚本包含 `AbortController` 与中文请求超时提示。
- 用户入口：Windows 默认浏览器已重新打开带当前版本查询参数的动态后台地址，避免原标签页继续展示旧文档。

### WX-084 登录后微信收款码到账复测

- 登录证据：微信从登录页进入主页后，账号观察入口和返回计数由 `1` 增长到 `358`，读取失败和队列丢弃均为 `0`，证明微信业务线程已实际运行且观察 DLL 正常命中其他结构化界面链路。
- 收款复测：用户随后完成一笔 `0.02` 元微信收款；截至 `2026-09-28 04:31:12`，支付观察入口和返回计数仍均为 `0`，本地状态文件待发送与已发送事件也均为 `0`。
- 结论收敛：本次排除了“微信停留登录页导致未触发”的干扰因素，确认 `Weixin.dll + 0x53c9350` 不属于当前版本收款码到账展示路径，不能继续作为微信收款监控点。
- 后续方向：保留现有安全边界，继续从收款助手到账界面的结构化展示路径定位新函数；不读取通知、不 OCR、不 Hook 网络、不解密数据库，也不自动操作微信业务界面。

### WX-085 已加载观察 DLL 的同版本升级

- 故障现象：监控端正常停止后，观察 DLL 仍按设计惰性加载在微信进程中；安装器重新复制内容完全相同的 DLL 时被 Windows 文件映射锁拒绝，导致仅更新主 EXE 的升级中断。
- 安装修复：安装器复制每个发行文件前比较源文件与目标文件 SHA-256；内容相同直接保留现有文件，内容不同仍执行替换并在文件被占用时明确失败。
- 配置保护：升级源配置未填写当前微信账号时，安装器保留已安装配置中格式有效的已确认账号，避免使用开发侧空值覆盖生产确认结果。
- 安全边界：该逻辑不绕过不同 DLL 版本的占用保护；观察 DLL 实际发生变化时仍需正常重启微信后升级，避免磁盘映像和进程内映像版本不一致。
- 实机恢复：首次升级因同哈希 DLL 被微信映射占用而失败；修复后安装成功，监控主进程 PID `21880`、内部宿主 PID `3468`，状态依次进入 `TARGET_VALIDATED`、`HOOK_REACTIVATING` 和 `HOOK_ACTIVE`，进程加载 DLL 与发行包 SHA-256 一致。
- 配置纠正：本次源配置原为空账号，首次成功安装曾覆盖已确认值；随后从状态文件唯一候选恢复已确认账号并同步本地忽略配置，全程未输出账号或昵称，最终审计为 `CONFIG_RECEIVER_CONFIGURED=True`。
- 窗口核验：生产窗口客户区为 `505x426`，26 个子控件最大边界为 `487x410`，越界控件数为 `0`，确认当前 DPI 下输入框、按钮和底部状态均未被裁切。

### WX-086 服务号收款助手链路重新定位

- 界面归属：用户确认收款到账消息被收纳在“服务号”聚合页内的“微信收款助手”，不经过普通聊天转账气泡；因此旧 `transfer_msg_type` 解析器零命中符合实际界面路径。
- 静态字段：目标 DLL 中存在连续的 `paysubtype`、`feedesc`、`transcationid`、`begintransfertime`、`pay_memo` 和 `receiver_username` 字符串，正好覆盖收款子类型、金额、交易标识、支付时间、备注和收款账号。
- 分析调整：扩展现有 Ghidra RIP 相对引用扫描器，同时查找上述到账字段及 `brandservicesessionholder`、`service_officialaccounts`、`BizMessageStorage` 服务号链路标识，优先定位结构化字段进入服务号消息 ViewModel 前的函数。
- 安全边界：本步骤只读扫描磁盘 DLL 和既有 Ghidra 项目，不附加微信、不读取消息数据库、不观察网络，也不操作服务号界面。
- 中止状态：首次扩展扫描尚未执行到新增到账字段时，因微信界面异常退出报告立即中止；本步骤没有形成新的函数地址结论。

### WX-087 微信异常退回登录态与保护停用

- 触发前操作：`2026-09-28 04:37` 为恢复已确认账号配置而正常重启监控端，观察 DLL 在原微信 PID `19580` 内执行 `HOOK_REACTIVATING`；随后启动的 Ghidra 任务只读取磁盘 DLL 和 Linux 侧项目，没有附加或控制 Windows 微信进程。
- 现场状态：用户报告微信自行退出后，Windows 应用错误、错误报告和 .NET 事件均为 `0`；微信进程由 5 个减少为 1 个，但 PID `19580` 仍存活，存在 1 个可见且未挂起的窗口，更符合退回登录态或辅助进程退出，而不是完整崩溃。
- 风险判断：无法证明具体原因，但原地恢复后的观察 DLL、硬件断点或微信自身防护均属于可疑因素；在完成隔离验证前不再把当前观察器视为可安全常驻。
- 立即处置：已中止 Ghidra 扫描并正常停止 CFPay 监控端，没有结束、重启、登录或点击微信；新增启动项开关工具并禁用监控端开机自启，避免系统下次登录后自动重新 Hook。
- 登录后复核：用户手动重新登录后微信恢复为 5 个进程，最近 10 分钟仍无崩溃事件；CFPay 主进程和内部宿主均未运行，活动启动快捷方式为空，禁用标记已保留。
- 惰性 DLL：原 PID `19580` 未完全退出，因此观察 DLL 仍加载但已经执行停止流程，不再安装硬件断点；本步骤没有为卸载 DLL 而结束微信，后续干净验证必须由管理员先完整退出并重新打开微信。
- 后续门槛：下一轮只能先在自有测试目标和不含真实账号的隔离环境验证观察器生命周期；真实微信侧重新启用前需要明确告知管理员，并从完全退出后重新启动的干净进程开始，禁止继续原地恢复旧 DLL。

### WX-088 微信登录态纳入 Hook 健康判定

- 判定原则：微信进程存在和硬件断点安装成功不再等同于健康；每次监控端启动、宿主重启或目标 PID 变化均重置为未知状态，必须在本次进程观察到 `loggedIn=true`、非零账号标识和有效 `wxid` 才允许上报健康。
- 状态序列：断点安装后先输出 `STARTING / WEIXIN_LOGIN_CHECKING`；明确登出或 15 秒内没有登录证据时输出 `DEGRADED / WEIXIN_LOGIN_REQUIRED`；登录账号持续 90 秒未再次观察到也按未登录降级。
- 状态防护：原生监控端和旧 Node 服务均拦截观察器的任何 `HEALTHY` 状态；账号证据缺失、过期、未确认或与配置不一致时分别转换为登录要求、账号确认要求或账号不匹配，避免后续心跳覆盖降级状态。
- 界面提示：主窗口对登录检测、需要登录和健康状态显示中文摘要；版本提升到 `0.6.2`，配置连接测试和 WinHTTP User-Agent 同步更新。
- 自有目标：测试目标新增显式登出结构，随后登录并切换到新线程账号；普通命名管道连续 3 轮、同进程恢复 2 轮均严格得到登录要求后再健康，停止后不再产生事件。
- 应用回归：MinGW Windows Release 构建通过，TypeScript 类型检查通过，12 个测试文件共 49 项测试通过；真实微信监控、自启和 Ghidra 扫描继续保持停止，本步骤没有附加或操作真实微信。

### WX-089 微信监控端调试日志中文化

- 文本范围：本机调试日志中的启动、进程发现、宿主生命周期、账号状态、收款事件、队列、心跳和状态上报说明统一改为中文；日期、PID/TID、函数或组件名及固定状态码继续保留。
- 诊断字段：`detail`、`reason`、`processId`、`amountFen`、`capturedAt`、`httpStatus`、`winError`、Hook 计数和线程数保持原格式，便于精确筛选和自动化分析。
- 状态映射：监控端按 `detailCode` 生成中文说明，不再把观察器或宿主的原始英文句子直接写入日志；健康消息仅保留分号后的登录态、命中计数、读取失败、队列丢弃和线程数。
- 隐私边界：中文化没有扩大日志内容，仍不记录密钥、HMAC 请求头、wxid、昵称、交易号、二维码登录内容或原始消息正文。
- 验证结果：MinGW Windows Release 构建、原生自测、发行目录约束、真实 `Weixin.dll 4.1.15.13` 静态校验和合并窗口 UI 验证均通过；真实微信监控继续停用。

### WX-090 调试日志说明字段完整中文化

- 表达范围：将落盘日志中的 `ok/failed`、`true/false`、`logged_in/logged_out/unknown` 及 Hook 命中计数名统一改为中文说明。
- 诊断保留：日期时间、PID/TID、Hook 与 Worker 接口名、固定状态码、`reason`、`processId`、`amountFen`、`capturedAt`、`httpStatus` 和 `winError` 保持不变，便于检索和排查。
- 仅限本地：只调整 `%LOCALAPPDATA%\CFPay\wechat-monitor.log` 的呈现，不改观察 DLL 与宿主的 JSON 协议，不改 Worker 上报结构。
- 版本记录：Windows 原生监控端版本提升到 `0.6.3`，配置连接测试和 User-Agent 同步更新。
- 验证结果：MinGW Windows Release 重新编译通过，单 EXE 发行目录约束与原生自测通过；TypeScript 类型检查通过，12 个测试文件共 49 项测试通过。本步骤未启动或附加真实微信。

### WX-091 Worker 微信登录会话与二维码通道

- 签名接口：新增 `POST /api/monitor/wechat-login`，复用监控端 HMAC、时间戳和 nonce 防重放认证；只接受 `LOGIN_REQUIRED`、`LOGIN_CLICKED`、`QR_READY`、`LOGGED_IN`、`RESTARTING` 五种通用状态。
- 数据最小化：接口不接受失败原因、账号、昵称、窗口截图或任意扩展字段；`QR_READY` 只上传本机解码后的二维码内容，其他状态禁止携带二维码。
- 加密与时效：新增 `wechat_login_session` 表，二维码内容使用 `SESSION_SECRET` 派生的 AES-GCM 密钥加密，监控端 ID 绑定为附加认证数据；二维码 2 分钟过期，其他状态 3 分钟过期，登录成功和状态切换会立即覆盖并清除二维码密文。
- 管理端：现有“监控端”页面新增微信登录区域，显示每台设备的通用登录状态；仅在 `QR_READY` 时通过管理员会话鉴权接口解密并重新生成 SVG 二维码，响应设置 `no-store`，不向页面返回二维码原文。
- 清理维护：定时任务删除过期登录会话；后台状态列表只查询尚未过期的记录，二维码过期、登录成功或状态变化后原地址立即失效。
- 自动验证：新增 3 项登录会话测试，覆盖状态白名单、拒绝失败原因字段、二维码格式、AES-GCM 设备绑定和登录成功清除密文；TypeScript 类型检查通过，13 个测试文件共 52 项测试通过。
- 迁移验证：当前 WSL 的 glibc 低于 Wrangler 内置 workerd 要求，`wrangler d1 migrations apply --local` 无法启动；改用系统 SQLite 按编号顺序执行全部 7 份迁移并运行完整性检查，结果为 `ok`。远程发布仍由部署命令先执行 D1 迁移。
- 运行边界：本步骤只完成 Worker、D1 和管理端协议，未启动、附加或操作真实微信；原生端二维码识别、按钮点击和受限重启留在后续独立步骤。

### WX-092 原生微信登录辅助与受限重启

- 状态机：Windows 原生监控端版本提升到 `0.7.0`，新增独立登录辅助线程；每次启动从未知状态重新判断，只在 Hook 明确未登录或目标进程尚未加载 `Weixin.dll` 时检查登录窗口，避免在正常主界面盲目操作。
- 二维码路径：使用 GDI 只截取同一微信进程的可见顶层窗口，在本机内存中通过 `quirc` 解码；不使用 OCR，不保存或上传截图，日志仅记录二维码字节数，不记录内容。
- 按钮路径：优先识别标准 Win32 登录按钮，兼容 Windows Active Accessibility 按钮和链接默认动作；标准按钮调用设置 3 秒超时，同一登录过程只调用一次。
- 重启限制：按钮调用 20 秒后仍没有新鲜登录证据时，先停止活动 Hook 宿主，再只结束绝对路径与配置完全一致且文件名为 `Weixin.exe` 的进程，并从同一路径启动一次；成功登录前禁止第二次自动重启。
- 上报边界：登录接口只发送五种通用状态、时间戳和按条件出现的二维码内容，不上传按钮缺失、调用失败或重启错误原因；详细诊断继续只写本机中文日志。
- 日志表达：登录状态码和登录界面诊断码继续保留用于精确检索，每条相关落盘记录同时追加中文“说明”；日期时间、PID/TID、函数或组件名、HTTP 状态码和 Windows 错误码保持原格式。
- 第三方依赖：引入 `quirc` 固定提交 `927d680904dc95fdff4cd9d022eb374b438ff8f2`，保留 ISC 许可证；许可证与六个源码/头文件的排序哈希清单摘要为 `be7c8af1296d8d07a7777f1f10000a9e38aeffb4f7893b730d2ab7575a69b3c4`。
- 隔离验证：自有 Windows 测试窗口的登录按钮调用、二维码窗口捕获解码和临时绝对路径 `Weixin.exe` 受限重启均通过；硬件断点普通模式 3 轮、命名管道 3 轮、同进程恢复 2 轮、断管恢复 2 轮均通过。
- 应用回归：MinGW Windows Release 构建无警告，单 EXE 发行目录、自测和合并窗口 UI 验证通过；TypeScript 类型检查、13 个测试文件共 52 项测试及 Worker dry-run 打包通过。本步骤未启动、附加、结束或点击真实微信，也未安装新版本。

### DEP-007 微信登录会话通道生产发布

- 发布前检查：Cloudflare OAuth 仍绑定既有账户，远程 D1 仅有 `0007_wechat_login_session.sql` 待执行；TypeScript 类型检查和 13 个测试文件共 52 项测试重新通过。
- 数据库迁移：生产 `cfpay_db` 已成功应用 `0007_wechat_login_session.sql`，迁移后再次查询显示无待执行项。
- Worker 发布：自定义域名、每分钟 Cron、D1、Durable Object 和静态资源绑定保持不变；当前生产版本为 `11683050-e0d0-45fe-ac29-4acbb469db2f`。
- 签名验证：使用临时监控端 ID 完成 `QR_READY` 和 `LOGGED_IN` 两次 HMAC 请求，生产接口均返回 HTTP 200；请求过程不输出监控密钥或二维码内容。
- 管理端验证：管理员鉴权后的监控列表可见临时二维码会话，受保护二维码接口返回 SVG 和 `no-store, no-cache, must-revalidate`；状态切换为已登录后旧二维码地址返回 HTTP 404。
- 密文验证：二维码就绪阶段只通过受保护接口重新生成图像；切换为已登录后，远程 D1 回读确认 `encrypted_qr_payload` 与 `encryption_iv` 均已清空。
- 运行边界：本次仅发布 Worker 和迁移数据库，没有启动、安装、附加、点击、结束或重启真实微信；原生监控端 `0.7.0` 仍未安装到生产电脑。

### WX-093 收款助手 `wcpayinfo` 结构解析链路定位

- 完整扫描：恢复此前中止的扩展字符串扫描，32 个目标词全部完成；共命中 32 个字符串和 55 个可执行段 RIP 相对引用，任务正常结束且未取消。
- 字段解析器：`0x182292ce0` 负责序列化、`0x182293980` 负责反序列化同一支付结构；两者直接处理 `paysubtype`、`feedesc`、`transcationid`、`begintransfertime`、`pay_memo`、`receiver_username` 等到账字段。
- 唯一调用者：序列化器只由 `0x180aabed0` 直接调用，反序列化器只由 `0x180aae3f0` 直接调用；全可执行段扫描未发现其他直接调用、跳转或函数指针表项。
- 分支条件：`0x180aae3f0` 在消息子类型为 `2000` 或 `2001` 时读取外层 `wcpayinfo` 节点，分配约 `0x408` 字节的输出结构并调用 `0x182293980`；该函数返回时到账字段已经结构化。
- 结构偏移：候选返回结构中 `paysubtype` 位于 `+0x1c8`，`feedesc` 位于 `+0x1d0`，`transcationid` 位于 `+0x1f0`，`begintransfertime` 位于 `+0x230`，`pay_memo` 位于 `+0x238`，`receiver_username` 位于 `+0x258`；字符串继续使用当前版本的 MSVC 32 字节布局。
- 服务号线索：`brandservicesessionholder` 和 `service_officialaccounts` 命中的是聚合容器、注册或会话管理函数；反编译结果没有直接调用 `wcpayinfo` 解析器，不能单独作为收款事件 Hook 点。
- 候选 Hook：下一步优先校验 `Weixin.dll + 0x2293980` 的入口签名和唯一调用点，在函数返回处只读上述结构并按 `paysubtype`、金额、交易标识、时间和收款账号过滤；旧 `+0x53c9350` 转账气泡 Hook 保留为独立兼容路径。
- 安全边界：本步骤只读取既有 Ghidra 项目和磁盘 DLL 分析数据，没有启动、安装、附加、点击、结束或重启真实微信，也没有读取数据库、通知或网络通信。

### WX-094 收款助手解析器磁盘签名

- 工具扩展：`tools/extract-wechat-hook-signature.mjs` 增加 `transfer` 和 `receipt` 两种配置；未指定时继续使用旧转账配置，保持既有调用兼容。
- 目标文件：仍为 `Weixin.dll 4.1.15.13`，大小 `201552944` 字节，SHA-256 为 `10f8e995453e2da46d4f2b5080cd6da1f13cc5147746adc119ceae38cb039de5`，映像基址为 `0x180000000`。
- 解析器入口：收款结构反序列化函数 RVA 为 `0x2293980`，位于 `.text`，文件偏移为 `0x2292d80`；入口前 32 字节为 `41565657534883ec384889d74889ce4c8d050ac4fb06488d5c24304889d14889`。
- 唯一调用点：调用指令 RVA 为 `0x0ab207e`，文件偏移为 `0x0ab147e`，字节为 `e8fd187e01`；按 `CALL rel32` 计算的目标严格等于 `0x2293980`。
- 兼容复核：同一工具重新校验旧转账入口 `0x53c9350` 和调用点 `0x53c97bc`，签名与既有常量完全一致。
- 安全边界：签名提取只读取磁盘 DLL，没有启动、安装、附加、点击、结束或重启真实微信。

### WX-095 本机调试日志中文说明补全

- 表达规则：本机落盘日志继续保留时间、PID/TID、函数或组件名、状态码、`reason`、HTTP 状态码和 Windows 错误码等技术字段；面向管理员阅读的说明统一使用中文。
- 拒绝原因：为账号记录和收款记录的固定拒绝码补充中文映射，日志在保留 `reason` 原值后追加“说明”，覆盖记录结构、消息类型、解析、收付方向、收款账号、交易标识、金额和时间校验。
- 状态原因：补充微信版本不匹配、目标文件不可用、Hook 未活动和账号读取失败等状态的中文说明；未知拒绝码使用中文兜底，不直接落盘宿主返回的英文句子。
- 隐私边界：本步骤没有增加业务敏感字段，日志仍不记录密钥、HMAC 请求头、wxid、昵称、交易号、消息正文、二维码内容或截图。
- 版本记录：Windows 原生监控端版本提升到 `0.7.1`，配置连接测试和 WinHTTP User-Agent 同步更新；真实微信监控继续保持停用。
- 验证结果：MinGW Windows Release 构建无警告，单 EXE 发行目录约束和原生自测通过；TypeScript 类型检查及微信监控核心、安装相关 2 个测试文件共 12 项测试通过。

### WX-096 微信收款助手结构 Hook 替换旧转账 Hook

- Hook 替换：四个硬件断点槽位继续用于“收款入口/返回 + 账号入口/返回”，不增加代码补丁；收款入口由旧转账气泡解析器 `0x53c9350` 替换为 `wcpayinfo` 反序列化器 `0x2293980`，调用点为 `0x0ab207e`。
- 参数与结构：入口命中时从第一参数 `RCX` 保存输出地址；返回时只读 `paysubtype +0x1c8`、`feedesc +0x1d0`、`transcationid +0x1f0`、`begintransfertime +0x230`、`pay_memo +0x238` 和 `receiver_username +0x258`。
- 类型核实：Ghidra 反编译确认 `paysubtype`、`begintransfertime` 为 32 位整数，其余四项为 32 字节 MSVC 字符串；测试目标按真实偏移构造结构，并同时覆盖内联字符串和堆字符串读取。
- 事件协议：观察 DLL 直接输出六个结构化字段，不再输出旧 `payMsgType`、`transferMsgType`、双方账号、原始 XML `content` 或旧 `transferId`；监控端不再加载 `xmllite.dll` 或解析消息正文。
- 安全校验：支付候选继续严格校验已确认收款账号、交易标识、金额和支付时间窗口；未确认账号时只记录本机候选并禁止支付上报，事件 ID 继续使用交易标识 SHA-256。
- 配置兼容：旧 `allowedPayMsgTypes` 和 `allowedTransferMsgTypes` 字段仍可存在于旧配置而不导致读取失败，原生配置窗口和旧安装配置生成器下次保存时会移除这两个已弃用字段。
- 工具默认：签名提取工具保留 `transfer` 历史配置用于复核，但未指定模式时默认提取当前生产使用的 `receipt` 解析器签名。
- 状态升级：Hook 状态偏移更新为 `0x2293980`，观察 DLL API 提升到 `0x00060000`，Windows 原生监控端版本提升到 `0.8.0`，防止仍加载旧观察 DLL 的进程被原地复用。
- 隐私边界：本地日志只记录子类型、时间和字段字节数，不记录 wxid、昵称、交易号、备注或消息正文；支付上报仍只发送金额、支付时间和脱敏摘要。
- 静态验证：真实 `Weixin.dll 4.1.15.13` 的文件大小、SHA-256、新解析器入口 32 字节、调用指令和相对目标均通过单 EXE 内嵌宿主校验。
- 自有目标：普通受限注入 3 轮、命名管道 3 轮、同进程恢复 2 轮和断管恢复 2 轮通过；输出严格为六个新字段，旧 XML 字段未出现。
- 端到端验证：模拟 Worker 收到 `HEALTHY / HOOK_ACTIVE`，三笔 `0.01/0.02/0.03` 收款全部交付；首笔注入 HTTP 503 后由本地持久化队列重试成功，三个事件 ID 均由交易标识哈希生成且互不重复。
- 应用回归：MinGW Windows Release 构建无警告，单 EXE 包约束、原生自测和真实 DLL 内嵌宿主静态校验通过；TypeScript 类型检查及 13 个测试文件共 51 项测试通过。
- 运行边界：本步骤未启动、附加、点击、结束或重启真实微信，也未安装 `0.8.0`；真实验证必须先完整退出旧微信进程，再从干净进程启动。

### WX-097 微信监控端干净升级保护

- 现场审计：Windows 当前没有运行监控端，开机启动项保持禁用；微信有 5 个进程，旧观察 DLL 仍加载在主进程 PID `19580`，已加载 DLL 哈希与 `0.8.0` 发行包哈希不同。
- 风险结论：在该状态下覆盖安装无法替换进程内旧代码，且可能造成磁盘文件与已加载 DLL 不一致，因此继续禁止原地升级、原地恢复或真实收款验证。
- 安装保护：安装器在写文件前比较发行包与安装目录中的 EXE、DLL SHA-256；安装目录内监控端仍运行时拒绝覆盖，二进制需要升级且任一 `Weixin.exe` 仍运行时拒绝安装。
- 只读检查：`-CheckOnly` 新增 `RUNNING_MONITOR_COUNT`、`WEIXIN_PROCESS_COUNT`、`BINARY_UPGRADE_REQUIRED` 和 `CLEAN_UPGRADE_READY`，只报告条件，不结束进程、不复制文件、不修改配置或启动项。
- 操作门槛：只有管理员完整退出全部微信进程后，才能安装 `0.8.0`；安装完成后必须从干净微信进程启动并进行真实小额收款验证。
- 现场验证：使用 `0.8.0` 包检查现有安装得到 `RUNNING_MONITOR_COUNT=0`、`WEIXIN_PROCESS_COUNT=5`、`BINARY_UPGRADE_REQUIRED=1`、`CLEAN_UPGRADE_READY=0`；实际安装入口以固定码 `CLEAN_WEIXIN_EXIT_REQUIRED` 在复制前拒绝。
- 对照验证：把当前安装目录自身作为同版本包检查时得到 `BINARY_UPGRADE_REQUIRED=0`、`CLEAN_UPGRADE_READY=1`，证明微信运行状态只阻止二进制替换，不影响同版本只读检查。
- 不变性复核：拒绝安装后旧 DLL 仍只加载于 PID `19580`，安装目录文件、配置、状态队列、开始菜单入口和禁用启动项均未变化；本步骤没有结束、重启、附加或点击微信。

### WX-098 本机调试日志中文落盘约束

- 规则确认：时间、PID/TID、函数或组件名、状态码、字段名、HTTP 状态码和 Windows 错误码等技术信息保持原格式；其余面向排障人员的说明统一以中文落盘。
- 现状审计：原生监控端全部 `appendLog(...)` 调用均包含中文说明；Hook 状态、登录状态、拒绝原因和计数指标通过固定映射输出，不直接写入宿主返回的英文状态句。
- 回归保护：新增源码级测试扫描全部 `appendLog(...)` 调用，缺少中文 Unicode 文本时立即失败，并单独校验状态、登录状态和拒绝原因使用中文说明映射。
- 隐私边界：本步骤只约束日志语言，没有新增日志字段；密钥、wxid、昵称、交易号、备注、消息正文、二维码内容和截图仍不得落盘。
- 验证结果：专项日志语言测试 2 项通过；TypeScript 类型检查及 14 个测试文件共 53 项测试通过。
- 运行边界：本步骤没有启动、安装、附加、点击、结束或重启真实微信。

### OPS-011 生产收款能力只读审计工具

- 审计范围：新增 Windows PowerShell 工具，使用本机忽略的生产配置依次验证 Worker 健康页、管理员入口和会话、系统设置、两种收款码、支付宝配置与真实账务查询、监控端状态、订单概况和易支付鉴权查询。
- 只读边界：工具不创建订单、不修改系统设置、不上传证书、不迁移对象存储、不标记订单已支付，也不启动或操作微信；唯一 POST 请求为管理员登录和支付宝只读账务查询。
- 敏感保护：输出只包含布尔状态、HTTP 状态码和数量，不输出管理员账号密码、会话 Cookie、易支付商户 ID/密钥、监控端密钥、支付宝凭据、二维码内容、订单号或交易明细。
- 异常保护：易支付鉴权查询使用表单 POST，密钥不进入 URL；HTTP 错误只记录去掉查询参数后的接口路径，禁止把 `key`、订单号或其他查询参数拼入异常文本，鉴权失败只输出 HTTP 状态和布尔结果。
- 网络兼容：支持显式 `-ProxyUrl`，用于 Windows Clash 仅监听 `127.0.0.1`、WSL 无法直连代理的现场环境；代理地址不写入系统或仓库全局配置。
- 密钥轮换：新增生产易支付商户密钥轮换工具，使用 Windows 加密随机数生成 32 字节 Base64URL 密钥，经管理员会话更新后台后同步本机忽略配置，控制台只输出 SHA-256 短指纹。
- 首轮发现：本机生产配置中的易支付商户信息落后于后台当前设置；同步远程 D1 后首次鉴权失败异常曾把旧密钥放入本机工具输出，因此立即将该旧密钥视为暴露并停止继续使用。
- 立即处置：已通过认证后台生成并写入新的 32 字节易支付商户密钥，本机忽略配置同步更新；随后把鉴权请求改为表单 POST，密钥不再进入 URL，并增加自动化测试禁止查询串和异常文本输出密钥。
- 生产结果：轮换后只读审计全部通过；健康页、管理员入口和会话、网关、商户密钥、监控密钥、微信与支付宝收款码均有效，支付宝配置来源为 D1，真实账务查询 HTTP 200、账务总数 0。
- 网关证据：使用轮换后的商户密钥查询随机不存在订单返回 HTTP 404，证明 `/api.php` 易支付鉴权有效；生产订单概况为待支付 0、已支付 1，监控设备 3、组件状态 3。
- 微信边界：本步骤没有启动或附加真实微信；现场仍有 5 个微信进程且旧观察 DLL 仍加载，`0.8.0` 干净升级和真实收款闭环继续等待管理员完整退出微信。
- 验证结果：两个 PowerShell 工具均为 ASCII，生产审计实机通过；TypeScript 类型检查及 15 个测试文件共 55 项测试通过。

### OPS-012 支付宝生产闭环证据固化

- D1 订单证据：生产库按渠道汇总得到支付宝 `PAID=1`，微信 `EXPIRED=4`；当前没有待支付订单，微信历史记录均为未完成的测试订单。
- 到账证据：生产 `payment_event` 按渠道汇总得到支付宝 `MATCHED=1`，说明真实支付宝账务记录已被轮询器识别并匹配到订单。
- 回调证据：生产 `callback_job` 汇总得到 `SUCCESS=1`，证明该支付宝订单完成了“创建订单、到账识别、订单匹配、商户异步通知成功”的完整闭环。
- 审计增强：生产只读工具新增支付宝与微信已支付/已过期订单数、通知成功订单数和 `alipayClosedLoopEvidence`，后续无需直接查询交易字段即可复核渠道闭环状态。
- 敏感边界：汇总查询和审计输出不包含订单号、金额、交易号、回调地址、二维码内容或任何密钥。
- 微信缺口：当前仍没有微信 `PAID` 或微信到账事件；真实微信闭环必须在旧 DLL 完整退出并安装 `0.8.0` 后验证，不能用支付宝证据替代。
- 验证结果：增强后的生产只读审计返回 `alipayClosedLoopEvidence=true`、`wechatPaidOrderCount=0`；TypeScript 类型检查及 15 个测试文件共 55 项测试通过。

### WX-099 微信监控端 0.8.0 干净升级

- 前置复核：管理员完成微信退出后，Windows 现场 `WEIXIN_PROCESS_COUNT=0`、`OBSERVER_LOADED_COUNT=0`；此前加载旧观察 DLL 的 PID `19580` 已不再存在。
- 安装动作：使用已安装的生产配置作为输入，保留当前 Worker 网关、监控端 ID、密钥和已确认收款账号；复制 `0.8.0` 的单 EXE 与观察 DLL，启动项继续保持禁用，未启动监控端或微信。
- 版本校验：安装目录观察 DLL SHA-256 与发行包一致；配置没有旧 `hostPath` 或 `locatorScriptPath`，安装目录仅保留 EXE、DLL 和配置文件。
- 回归验证：安装器只读检查返回 `BINARY_UPGRADE_REQUIRED=0`、`CLEAN_UPGRADE_READY=1`；单 EXE 发行目录、自测、真实 `Weixin.dll 4.1.15.13` 内嵌宿主静态校验和安装器检查全部通过。
- 安全边界：本步骤没有启动微信、注入观察器、点击登录或触发收款；开机自启仍禁用，下一步先手动启动监控端观察状态。

### WX-100 0.8.0 监控端首次启动

- 启动方式：通过 Windows 原生进程创建启动安装目录中的 `cfpay-wechat-monitor.exe --tray`，没有使用 Node、PowerShell Hook 或开机启动项。
- 运行结果：监控端 PID `25264` 持续运行，安装目录配置、生产网关、监控端密钥和已确认收款账号均通过脱敏审计；微信进程数为 `0`，观察 DLL 加载数为 `0`。
- Worker 结果：生产只读审计仍返回健康页、支付宝闭环、易支付鉴权全部通过；微信渠道仍为已支付 `0`，符合微信尚未启动的预期。
- 安全边界：本步骤未启动或附加微信，未点击登录，未触发收款；下一步从该监控端运行状态下启动微信主程序。

### WX-101 登录按钮实际点击兜底工具

- 现象：`0.8.0` 监控端通过 MSAA 对“进入微信”执行默认动作并收到成功返回，但微信仍停留在登录窗口；Worker 当前设备状态为 `LOGIN_CLICKED`，随后按策略完成一次受限重启。
- 工具：新增 ASCII PowerShell 工具，仅接受指定 `Weixin.exe` 主窗口，要求 UI Automation 找到唯一的“进入微信”按钮，校验可见矩形后执行一次实际鼠标点击。
- 限制：工具不点击二维码、不输入账号密码、不操作其他窗口、不结束进程；仅用于本次登录态验证，点击前将微信窗口置前台。
- 当前状态：本步骤已完成工具实现，实际点击和结果验证作为下一步单独记录。

### WX-102 微信干净进程登录与 Hook 健康

- 进程启动：从监控端已运行的状态启动 `Weixin.exe`，首次进程 PID `13632`，登录页显示已保存账号和“进入微信”按钮；监控端按策略调用按钮后发现未进入主界面，完成一次受限重启，随后进程 PID 为 `7448`。
- 实际登录：对 PID `7448` 的唯一“进入微信”按钮执行一次受限实际点击，不输入账号密码、不读取通知或网络；微信随后切换为 `mmui::MainWindow` 主界面。
- Worker 证据：当前设备 `wm_000000000000000000000000` 已上报 `HEALTHY / HOOK_ACTIVE`，目标版本 `Weixin 4.1.15.13`，新收款 Hook 偏移 `0x2293980`，登录会话为 `LOGGED_IN`，二维码密文已清理。
- 运行验证：观察 DLL 已加载于 PID `7448`，SHA-256 与 `0.8.0` 发行包一致；监控端、微信主进程和 Hook 宿主均保持运行，开机自启仍禁用。
- 安全边界：本步骤没有读取微信通知、OCR、网络通信或数据库，没有自动进入服务号页面，也没有触发支付；下一步只创建一笔专用小额测试订单并等待管理员实际付款。

### WX-103 微信真实收款测试订单工具

- 工具：新增 ASCII PowerShell 订单工具，使用生产易支付 `/mapi.php` 创建指定渠道的小额测试订单，采用本机配置中的商户密钥计算 MD5 签名。
- 输出边界：工具只输出订单号、订单金额、尾差金额和收银台地址，不输出商户 ID、商户密钥或签名；可通过 Windows Clash `-ProxyUrl` 访问生产网关，并可自动打开收银台。
- 支付边界：本步骤只实现订单创建工具，未自动付款；真实付款必须由管理员在收银台确认金额后手动完成。

### WX-104 微信真实收款测试订单已创建

- 订单创建：生产 `/mapi.php` 返回成功，渠道为微信，订单金额 `0.01` 元，尾差后实际付款金额 `0.01` 元；收银台已由 Windows 默认浏览器打开。
- 脚本修复：订单创建成功后发现 PowerShell 5.1 仅在输出 `SwitchParameter` 时类型转换失败；该错误发生在业务请求完成之后，已改为显式输出 `0/1`，不重复创建订单。
- 当前状态：订单仍等待管理员手动付款；付款前不进行补单、不标记订单已支付、不重复生成测试订单。

### WX-105 修复登录证据 90 秒后误降级

- 现场问题：微信已进入 `mmui::MainWindow` 主界面且 Hook 正常运行，但账号记录只在登录瞬间产生；约 90 秒后监控端按旧超时规则把健康状态错误改为 `WEIXIN_LOGIN_REQUIRED`。
- 根因修复：原生端和 Node 测试服务改为按微信进程 PID 维护登录证据；同一 PID 的有效登录记录持续有效，微信进程退出、PID 变化或收到明确 `loggedIn=false` 记录时才清除。
- 测试覆盖：保留“健康状态必须先观察到本次登录”的测试，新增同一进程长期证据保持健康和进程变化后重新要求登录两项测试。
- 安全边界：修复不绕过登录校验；没有登录证据时仍禁止上报健康和支付事件。本步骤尚未安装新构建，当前运行进程仍使用上一版二进制。

### WX-106 修复观察 DLL 自身登录超时

- 深层根因：实机长稳验证仍在 90 秒后降级，进一步确认观察 DLL `hardware_observer.cpp` 自身的登录新鲜度函数会输出 `WEIXIN_LOGIN_REQUIRED`，上层监控端无法覆盖该状态。
- 修复内容：删除观察 DLL 的 90 秒登录超时；同一观察 DLL 生命周期内保持有效登录状态，只有明确捕获 `loggedIn=false`、账号字段无效、观察器停止或微信进程重启才清除状态；不再因时间经过重复入队账号记录。
- 回归保护：源码测试禁止重新引入 `kLoginObservationTimeoutMs`，同时要求保留明确未登录状态的降级路径。
- 当前边界：当前 Windows 进程仍加载旧观察 DLL；必须再次完整停止监控、重启微信、重新构建并安装后才能验证本修复。

### WX-107 安装登录超时修复版并复测

- 干净替换：停止监控端并重启微信后确认旧观察 DLL 加载数为 `0`，安装器检查通过；随后安装包含 `hardware_observer.cpp` 修复的新版单 EXE 包，启动项继续禁用。
- 登录复测：新版监控端启动后识别登录页，Worker 收到 `LOGIN_REQUIRED`/`LOGIN_CLICKED`；对当前 PID `2000` 的唯一“进入微信”按钮执行两次受限实际点击，微信仍保持 `mmui::LoginWindow`，未进入主界面。
- 安全结论：问题发生在微信登录按钮实际生效环节，尚未进入 Hook 登录健康阶段；没有继续重启微信、没有输入凭据、没有创建第二笔订单，也没有触发收款。
- 当前状态：新版观察 DLL 已加载且状态为 `WEIXIN_LOGIN_REQUIRED`，待管理员手动点击“进入微信”后再验证登录健康和 90 秒稳定性。

### WX-108 修复账号 Hook 瞬态未登录误判

- 现场证据：调试日志确认微信曾在 `07:57:30` 成功上报 `LOGGED_IN` 和 `HEALTHY / HOOK_ACTIVE`，约 90 秒后账号 Hook 的瞬态未登录记录又使原生观察器持续上报 `WEIXIN_LOGIN_REQUIRED`；此时微信窗口仍为主界面。
- 根因：观察 DLL 将单次 `loggedIn=false` 账号结构立即视为登出，清除有效登录证据；账号函数在正常运行期间可能出现瞬态返回，导致监控端误降级。
- 修复：只有在已经观察到登录账号后连续收到三次未登录账号结构，才确认登出并清除登录证据；登录账号再次出现时立即清零未登录计数。首次启动或明确未登录状态仍会立即保持降级，满足“未登录不能报健康”。
- 诊断工具：新增 `tools/inspect-installed-wechat-log.ps1` 读取安装端中文调试日志，新增 `tools/stop-windows-weixin-for-monitor.ps1` 执行不自动重启的干净升级清理。
- 回归结果：源码测试 9 项通过；MinGW Windows Release 构建无警告；Hook Windows Harness 连续 3 轮通过；修复版单 EXE 与观察 DLL 已完成干净安装，启动项继续禁用。
- 当前边界：修复版已加载到新微信进程，但微信重启后停在“进入微信”页面，受限自动点击及一次实际按钮点击均未进入主界面；尚未重新取得登录证据，也未触发真实微信收款验证。

### WX-109 增加登录登出防回归 Harness

- 测试场景：自有 Windows 测试目标在成功产生登录账号记录后连续产生三次未登录账号结构，验证观察 DLL 只输出一次确认后的登出记录，不因前两次瞬态记录清除健康状态。
- 校验内容：Harness 仍要求三条收款事件、健康状态、初始未登录记录和停止状态；账号记录数调整为四条，并明确校验登出记录恰好两条。
- 验证边界：该回归只使用自有测试目标，不连接真实微信、不读取通知、不触发网络收款；真实微信登录和支付闭环仍等待管理员完成登录。

### WX-110 诊断微信登录按钮未进入主界面

- 只读检查：微信登录窗口类为 `mmui::LoginWindow`，登录按钮可见、启用，矩形有效，并提供标准 Windows `InvokePattern`。
- 交互复测：鼠标坐标点击和标准 `InvokePattern` 各执行一次，调用均返回成功，但窗口仍保持登录页；监控端未因此伪报健康，也未重复强制重启。
- 结论：当前阻塞点不是控件缺失或禁用，而是微信自身登录流程没有完成；必须由管理员在本机完成一次有效登录后，才能继续验证 Hook 稳定性和真实收款。
- 工具：新增 `tools/inspect-wechat-login-control.ps1`，支持只读检查和显式 `-Invoke` 诊断，默认不触发任何交互。

### WX-111 限制登录按钮自动重试

- 现场发现：微信未完成登录时，监控端在同一进程上每约 30 秒重复调用“进入微信”，即使已经完成一次受限重启仍会继续调用。
- 修复：持续扫描微信登录窗口；发现按钮就调用，发现二维码就上报 Worker；按钮未进入主界面时执行受限重启，重启成功后继续扫描新进程。登录证据出现后立即保持 Hook 宿主并上报健康状态。
- 安全边界：该调整减少重复登录交互，不读取账号密码、不点击二维码、不改变微信网络配置。
- 现场验证：修复版已重新干净安装；初始微信进程出现 `LOGIN_CLICKED` 和 `RESTARTING`，重启后的新进程继续进入登录检测流程。
- 当前边界：仍需管理员完成微信登录后验证 `HEALTHY / HOOK_ACTIVE` 和收款 Hook；当前未创建新订单，原 `0.01` 元订单保持待支付。

### WX-112 改为 C++ 直接发送登录窗口消息

- 实现调整：移除登录页对 Windows Active Accessibility、MSAA 和 `accDoDefaultAction` 的依赖；标准 Win32 `Button` 继续使用 `BM_CLICK`，微信 `mmui::LoginWindow` 使用 `SendMessageTimeoutW` 发送窗口消息。
- 后台行为：按钮操作不要求微信窗口置前、不调用无障碍树、不使用 PowerShell 鼠标脚本；消息发送设置 3 秒超时，避免登录窗口无响应时监控线程永久等待。
- 回归结果：自有窗口 Harness 的直接按钮调用、二维码隔离测试和受限重启测试全部通过；原生中文日志、监控服务和配置流程专项测试共 27 项通过，MinGW Release 构建成功。
- 安全边界：本步骤没有在真实微信上继续点击或输入凭据；二维码当前仍沿用旧实现，下一步只在静态确认可靠函数签名后改为进程内 Hook。

### WX-113 微信当前昵称只读 Hook

- 逆向依据：`Weixin.dll 4.1.15.13` 的 `+0x9aef50` 是当前资料昵称虚表槽位，函数签名近似 `FUN(this, outputString, mode)`；返回对象为 32 字节 MSVC 字符串，输出地址取 `RDX`。
- 原生实现：保留收款 `DR0/DR1`，账号观察改为 `DR2` 同槽位入口/返回切换，昵称使用 `DR3` 同槽位入口/返回切换；异常处理器只复制字符串，不修改微信代码页、参数或返回值。
- 事件协议：观察 DLL 新增 `nickname` JSONL 记录；`account` 记录同时兼容可选 `nickname` 字段。昵称记录晚于账号记录到达时，由 C++ 监控端按当前进程账号更新 `receiverCandidates[].nickname`。
- 配置界面：原生下拉框显示 `昵称（wxid）`，保存时仍写入真实 `expectedReceiverUsername`，不把展示文本写入配置。
- 测试结果：MinGW Release 构建通过；Windows 自有 Harness 验证昵称入口、返回、线程覆盖和停止清理；Node 监控、状态存储和配置专项测试 26 项通过。
- 安全边界：昵称 Hook 只读当前资料返回值，不读取聊天数据库、不解密账号文件、不监听网络通信；真实微信仍需在旧 DLL 完整退出、干净升级后再验证。

### WX-114 登录二维码进程内 Hook 逆向复核

- 分析范围：继续使用 `Weixin.dll 4.1.15.13` 的 Ghidra 项目，新增二维码字段字符串、字段邻域、候选函数反编译和机器码相对 `CALL` 扫描脚本。
- 排除结果：`login_qr_image_size`、`login_qr_size` 和 `login_qr_view_style` 位于登录界面配置解析函数 `+0x1fcc9e0`，不是二维码内容；`micromsg.LoginQRCode*` 和 `micromsg.PushLoginURL*` 当前仅存在 protobuf 类型元数据引用，`.text` 中没有直接 `E8 rel32` 调用者。
- 当前结论：尚未找到能稳定返回二维码 payload 或结构化图片对象的真实函数入口，不能把上述构造函数、配置解析函数或截图解码替换为生产 Hook 点。
- 代码状态：本步骤只提交可复现的离线 Ghidra 分析脚本和日志结论，没有修改生产二维码路径；现有 `PrintWindow + quirc` 仍保留，直到找到并验证真实进程内返回对象。
- 安全边界：分析只读取静态 DLL 和 Ghidra 项目，没有启动、附加、登录、停止或修改真实微信，也没有记录二维码内容。

### WX-115 单 EXE 构建与真实登录前联调

- 构建结果：重新执行 `npm run typecheck`、`npm test -- --run` 和 `npm run wechat:monitor:build`；TypeScript 检查通过，15 个测试文件共 60 项通过，发行目录只包含 `cfpay-wechat-monitor.exe` 与 `cfpay-hook-observer.dll`。
- 发行验证：单 EXE 验证脚本通过自测、内嵌宿主校验、合并窗口、单实例还原和托盘菜单检查；目标 `Weixin.dll 4.1.15.13` 的 SHA-256 仍为 `10f8e995453e2da46d4f2b5080cd6da1f13cc5147746adc119ceae38cb039de5`。
- 真实启动：使用已安装配置启动监控端并启动指定路径的 `Weixin.exe`；观察 DLL 成功加载，Worker 心跳、目标校验和登录状态请求均返回 HTTP 200，安装目录仍只有单 EXE、观察 DLL 和配置文件。
- 未登录保护：微信停留在 `mmui::LoginWindow` 时，监控端持续上报 `DEGRADED / WEIXIN_LOGIN_REQUIRED`，没有上报健康或支付事件；登录按钮调用未进入主界面后，按既定策略执行受限重启并重新安装观察器。
- 当前等待：二维码登录需要管理员在 Windows 微信窗口完成扫码；扫码前不创建新的支付订单、不修改微信网络配置、不读取通知、网络或数据库。扫码完成后继续验证当前 `wxid`、昵称、健康状态和真实小额收款事件。

### WX-116 修复 Qt 登录按钮并去重状态上报

- 现场根因：UI Automation 显示登录窗口类为 `mmui::LoginWindow`，但 Win32 原生类实际为 `Qt51514QWindowIcon`；旧 C++ 类名校验失败，因此日志中的 `LOGIN_REQUIRED` 并不代表已经调用登录按钮。
- 登录修复：固定版本的 Qt 原生窗口加入 C++ 消息路径；补充窗口激活、焦点、回车和鼠标消息，仍不使用无障碍执行。对真实微信 PID 的 C++ 登录自测返回成功，随后真实进程上报 `LOGGED_IN` 和 `HEALTHY / HOOK_ACTIVE`。
- 状态分析：`LOGGED_IN` 属于微信登录会话接口，`HEALTHY / HOOK_ACTIVE` 属于组件状态接口，成对出现是协议设计；同一账号记录和观察器健康记录还会重复触发相同组件状态，属于冗余上报。
- 去重修复：组件状态和登录状态增加进程内指纹，按状态、详情、进程、二维码摘要和最后收款时间去重；相同记录 30 秒内不重复提交，状态变化、进程变化和新收款事件不受影响。
- 验证结果：TypeScript 15 个测试文件共 61 项通过；MinGW 原生构建通过；登录按钮、二维码、受限重启和 Hook Harness 两轮均通过。新版去重二进制已生成，待下一次受控安装后验证真实日志降噪。

### WX-117 二维码登录人工协助停点

- 测试隔离：停止监控端自动动作，强制结束已登录微信并单独启动 `Weixin.exe`；使用 C++ 直消息点击“切换账号”，没有使用无障碍执行，也没有点击二维码。
- 页面确认：原生窗口仍为 `Qt51514QWindowIcon`，UI 自动化只读检查确认页面出现“二维码”和“扫码登录”，登录按钮数量为 0。
- Worker 证据：随后启动监控端进行二维码解析，真实日志记录 `status=QR_READY`、二维码字节数 `43`、上报结果成功且 HTTP 200；没有落盘二维码明文或截图。
- 当前停点：微信保持在扫码登录页，监控端只等待扫码，不自动点击、不自动重启；待管理员扫描 Worker 管理端重新生成的二维码后，继续验证登录状态、当前 `wxid`、昵称和 Hook 健康。收款测试尚未开始，需另行等待管理员手动付款。

### WX-118 修复二维码失效后的自动点击

- 现场问题：二维码首次上报成功后，二维码识别短暂失败，登录辅助线程把同一进程误判为普通登录按钮页，自动发送了登录消息；该行为没有发生人工扫码，不作为扫码登录证据。
- 修复：同一微信 PID 记录 `QR_READY` 后设置二维码保护；在该 PID 生命周期内禁止再次自动点击登录按钮，只有真实登录证据出现或微信 PID 变化后清除保护。
- 处置：已立即停止监控端和微信进程，避免继续自动操作；修复后重新进入二维码页，再次等待管理员扫码。后续扫码证据只接受 Worker 登录会话状态变化，不接受自动按钮登录。

### WX-119 重新建立扫码人工协助停点

- 现场校正：前一轮日志中的 `LOGGED_IN` 与 `HEALTHY / HOOK_ACTIVE` 不能证明管理员完成扫码；本轮先停止旧监控和指定路径的全部微信进程，清除旧 Hook 会话影响。
- 登录页准备：重新启动固定路径的 `Weixin.exe` 后，使用 C++ 直消息完成“切换账号”，只为进入二维码页；只读检查确认原生类为 `Qt51514QWindowIcon`、二维码控件存在、登录按钮数量为 0。
- Worker 证据：重新启动已安装监控端，当前进程上报 `QR_READY`，本机解析得到二维码字节数 `43`，请求返回 HTTP 200；日志未记录二维码明文、截图、wxid 或昵称。
- 当前停点：微信进程保持在“扫码登录”页，监控端 PID 与 Hook 宿主已运行但未执行登录按钮操作；等待管理员扫描 Worker 页面重新生成的二维码。未经管理员明确确认，不继续检查登录成功或进入收款测试。

### WX-120 校正昵称 Hook 输出对象并完成扫码复测

- 人工证据：管理员确认此前两轮 `QR_READY` 后的登录均由本人扫码完成；本轮扫码后日志出现 `LOGGED_IN` 和 `HEALTHY / HOOK_ACTIVE`，微信窗口进入 `mmui::MainWindow`，不再把登录成功归因于自动恢复。
- 根因修复：`Weixin.dll +0x9aef50` 的 `RDX` 输出不是 32 字节 `std::string`，而是指向微信引用计数字符串对象的指针；对象 `+0x04` 为字节长度，`+0x10` 为相对数据偏移，观察器已按该布局边界检查并复制。
- Harness 结果：自有 Windows Hook Harness 通过，输出 `events=3`、`accounts=4`、`nicknames=3`，所有昵称记录正确，`readFailures=0`；生产发行包已重新构建并安装。
- 现场结果：当前生产监控端通过 C++ 直接发送登录按钮消息进入主界面，Hook 健康上报成功，`wxid` 已捕获并保存在本机候选状态；登录后的昵称函数没有自然再次触发，状态文件暂未出现昵称，不能宣称昵称链路已完成。
- 后续边界：已确认登录后昵称位于联系人资料结构化字段链路，继续寻找稳定只读观察点；不读取数据库、不监听网络、不使用 OCR，也不进入收款测试，直到账号昵称显示和 Hook 状态证据完整。

### WX-121 切换登录后昵称结构化 Hook

- 逆向定位：追踪 `mmkv_key_nick_name` 得到登录后资料写入函数 `Weixin.dll +0x04b690`，唯一代码调用点为 `+0x45ec92`；函数入口 `RDX` 指向资料结构，昵称是 `RDX + 0x28` 的 32 字节 MSVC `std::string`。
- 生产修改：昵称硬件断点从登录前 `current_login_nick_name` 链路 `+0x9aef50` 切换到 `+0x04b690`；入口保存昵称字段地址，返回点使用统一的 `readMsvcString()` 边界检查读取，不使用界面文本、数据库、通知、OCR 或网络。
- 版本保护：注入前宿主和注入后观察 DLL 均新增昵称入口前 32 字节静态签名校验；固定版本签名不一致时拒绝注入或安装断点，避免微信升级后按旧偏移读取。
- 测试同步：自有 Hook Harness 的昵称目标改为模拟 `RDX` 资料结构和 `+0x28` 昵称字段，保证测试 ABI 与生产入口一致；MinGW 全量原生构建通过，Windows Harness 连续两轮均得到 `events=3`、`accounts=4`、`nicknames=3`、`readFailures=0`。后续需完成干净安装和真实登录触发验证。
- 追溯工具：保留昵称键引用、候选函数反编译和调用者追踪 Ghidra 脚本；脚本只记录固定版本代码地址和结构结论，不包含账号、昵称、二维码或密钥。

### WX-122 完成昵称 Hook 实机部署验证

- 干净升级：发行包仍仅包含 `cfpay-wechat-monitor.exe` 和 `cfpay-hook-observer.dll`；先正常停止旧监控，再结束固定路径下的微信进程，安装新版后保持原有开机启动禁用状态。
- 自动登录：监控端启动后从固定配置路径启动微信，C++ 登录按钮路径完成登录；日志依次出现 `LOGIN_CLICKED`、`LOGGED_IN` 和 `HEALTHY / HOOK_ACTIVE`，不需要扫码协助。
- 昵称证据：登录后 `Weixin.dll +0x04b690` 昵称入口命中并成功返回，监控日志记录“昵称Hook已更新当前微信昵称”；本机状态文件有且仅有一个接收候选，该候选同时具有非空 `username` 和非空 `nickname`。
- 发行一致性：目标微信仅主进程加载观察 DLL，加载路径位于正式安装目录；已加载 DLL 的 SHA-256 与发行包完全一致，安装目录没有 Node、脚本或其它生产可执行文件。
- 敏感边界：验证只统计昵称字段是否存在，没有输出、落文档或提交真实 `wxid`、昵称、监控密钥和二维码内容。

### WX-123 修复配置页昵称显示与手动刷新

- 现场问题：昵称 Hook 已写入本机候选状态，但单 EXE 配置面板只在创建时读取一次状态；昵称晚于窗口创建到达时，下拉框继续显示旧账号文本，且右侧没有用户要求的重新获取入口。
- 界面修复：“当前微信账号”下拉框缩短，在右侧新增“重新获取”按钮；点击后重新读取 Hook 生成的候选状态，显示 `昵称（wxid）` 并保留用户当前选择。
- 反馈处理：重新获取到昵称时弹出成功提示；只有账号但没有昵称时明确提示 Hook 尚未捕获昵称，避免点击后无反馈。
- 数据边界：按钮只读取原生观察器写入的本机 Hook 候选状态，不读取微信数据库、通知、界面文本、OCR 或网络响应。
- 诊断日志：每次刷新记录状态文件是否读取成功、候选数、昵称数、下拉选项数和是否存在昵称选项，不记录真实 `wxid` 或昵称。
- Windows 验证：验证程序先复制到 `%LOCALAPPDATA%` 后启动，避免从 WSL UNC 路径运行 GUI EXE 引发 Windows 安全/UAC提示；实测下拉框只有一个选项，文本符合 `昵称（wxid）` 格式，按钮存在且点击后成功显示结果对话框。

### WX-124 部署昵称界面并发现登录误触

- 正式部署：重新构建只含主 EXE 与观察 DLL 的发行包，在无微信进程状态下完成干净安装；已安装版再次通过账号下拉框昵称格式、重新获取按钮和成功对话框验证。
- UAC规避：Windows GUI 验证统一先复制到 `%LOCALAPPDATA%`，不再直接执行 WSL UNC 路径中的 EXE，现场没有依赖管理员授权窗口。
- 现场校正：首次 C++ 登录调用后实际进入“网络代理设置”页，不是普通登录超时；旧实现先向 Qt 登录窗发送 `Enter`，只要消息成功投递就误判为已调用登录按钮，而当前键盘焦点可能位于代理设置。
- 后续结果：监控端按限制重启微信后，第二次调用进入主界面并产生 `LOGGED_IN`、`HEALTHY / HOOK_ACTIVE`、账号和昵称 Hook 记录；由于首次存在误触，本轮不能作为普通按钮逻辑通过证据。

### WX-125 移除登录键盘路径并防止代理设置误触

- 点击修复：Qt 登录窗不再发送 `Enter`；仅接受固定尺寸范围内的竖向登录窗口，并在只读检查得到的“进入微信”按钮中心比例发送鼠标移动、按下和抬起消息。
- 现场几何：固定版本登录窗客户端为 `280 x 380`，“进入微信”按钮位于客户端 `x=56..224`、`y=277..313`；“网络代理设置”位于窗口顶部，新的点击点不与其重叠。
- 回归目标：原生 Harness 增加仿真 `Qt51514QWindowIcon`，同时布置顶部代理区域和底部登录区域；测试必须观察到登录事件且代理事件保持未触发。
- Harness 结果：标准 Win32 登录按钮、Qt 定点登录、二维码识别和受限重启四项测试全部通过；Qt 场景确认登录事件触发且代理区域事件未触发。
- 真实单击：在监控端停止、无自动重启循环的条件下，对真实登录窗执行一次新版 C++ 定点点击，窗口从 `mmui::LoginWindow` 进入 `mmui::MainWindow`，没有再次进入代理设置。

### WX-126 完成修复版普通登录全链路复测

- 干净部署：停止微信后重新构建并安装修复版双文件发行包，启动正式监控端，再从固定路径启动微信。
- 登录结果：正式监控端只上报一次 `LOGIN_CLICKED`，约 5 秒后即收到 `LOGGED_IN` 和 `HEALTHY / HOOK_ACTIVE`；本轮没有 `RESTARTING`，也没有进入网络代理设置。
- Hook 结果：同一微信进程继续产出有效账号记录和昵称记录，读取失败数为 0；普通登录按钮、登录状态判断和 Hook 安装已经形成完整实机证据。

### WX-127 修复切换账号主窗口假成功

- 现场问题：扫码准备阶段在微信主界面调用“切换账号”自测时返回成功，但窗口仍是 `mmui::MainWindow`；原因是旧函数只校验 Qt 类名，向主窗口坐标投递鼠标消息成功就误判为已切换。
- 尺寸保护：切换账号路径与登录路径统一只接受固定尺寸范围内的竖向登录窗，横向主窗口不再执行坐标消息，也不会返回成功。
- Harness 扩展：仿真 Qt 登录窗增加真实比例的“切换账号”区域和独立事件；登录辅助 Harness 新增切换测试，必须命中切换区域后才通过。
- 诊断收敛：微信只读检查工具新增摘要模式，只输出窗口类和登录、切换、扫码、二维码控件数量，不再默认输出与登录判断无关的界面内容。
- 验证结果：五项登录辅助 Harness 全部通过；真实主窗口切换自测按预期返回失败，重启到普通登录窗后切换成功，页面摘要变为登录按钮 0、扫码登录 1、二维码 1。

### WX-128 建立修复版扫码登录协助停点

- 生产升级：停止微信后安装包含登录和切换窗口保护的最新版双文件发行包，保持开机启动禁用。
- 页面证据：微信启动后保持 `mmui::LoginWindow` 扫码页，只读摘要为登录按钮 0、切换账号 0、扫码登录 1、二维码 1。
- Worker 证据：启动正式监控端后解析得到 43 字节二维码内容，`QR_READY` 上报成功且 HTTP 200；日志和文档均未记录二维码明文。
- 扫码结果：管理员确认已扫描 Worker 页面重新生成的二维码；随后同一微信进程依次上报 `LOGGED_IN` 和 `HEALTHY / HOOK_ACTIVE`，账号 Hook 与昵称 Hook 均成功写入，没有自动点击或重启记录。

### WX-129 建立真实收款协助停点

- 运行前提：扫码登录后的微信主进程、正式监控端和 Hook 宿主均保持运行，观察 DLL 加载路径与发行包哈希一致，组件状态健康。
- 基线状态：收款测试前本机队列 `pending=0`、`delivered=0`，当前收款助手入口和返回命中数均为 0，便于区分下一笔真实测试付款。
- 当前停点：等待管理员发起一笔新的微信小额付款；付款完成后由监控端日志、状态队列和 Worker 订单状态共同确认，不读取通知、数据库、OCR 或网络通信。

### WX-130 0.03 元真实收款漏报分层定位

- 付款证据：管理员确认已完成新的 `0.03` 元微信付款；付款后连续检查安装端日志、本地状态文件和生产 Worker。
- 本机结果：微信主进程仍为 `LOGGED_IN / HOOK_ACTIVE`，观察 DLL 仍加载且读取失败为 `0`；收款助手入口和返回命中数均为 `0`，本地 `pending=0`、`delivered=0`。
- Worker 结果：生产审计仍为微信已支付订单 `0`、微信过期订单 `5`，未出现新的微信匹配事件；因本地未形成收款候选，故障位于金额解析、账号校验和 HTTP 上报之前。
- 进程分层：当前微信除主进程外运行 `--type=wxpublic` 子进程并加载 `wxpublic.dll`，同时有多个 `WeChatAppEx.exe` 渲染进程；现有观察器只注入加载 `Weixin.dll` 的主进程。
- 定位结论：现有 `Weixin.dll + 0x2293980` 只覆盖主进程中消息类型 `2000/2001` 的 `wcpayinfo` 解析分支；这笔收款被收纳在“服务号 -> 微信收款助手”，需继续验证实际展示是由主进程延迟解析，还是由 `wxpublic` / `WeChatAppEx` 链路承载。
- 安全边界：本步骤只读取进程、模块、脱敏日志和 Worker 管理汇总；没有重启微信、重新注入、读取通知或数据库、OCR 或 Hook 网络通信。

### WX-131 打开收款助手后仍零命中

- 人工动作：管理员在 PC 微信中手动打开“服务号 -> 微信收款助手”的到账页面，没有再次付款。
- 实时结果：页面打开后监控端仍为 `LOGGED_IN / HOOK_ACTIVE`，收款入口和返回命中数仍均为 `0`，本地支付队列仍为空。
- 结论：排除“主进程只在打开页面时延迟调用 `Weixin.dll + 0x2293980`”；当前到账展示使用另一条结构化链路。
- 诊断工具：新增脱敏的 Windows 微信进程树检查脚本，只输出 PID、父进程、启动时间、进程角色、关键模块和观察 DLL 加载状态，不输出账号、昵称、页面文字或消息内容。
- 下一步：在页面保持打开时对比 `wxpublic` 和 `WeChatAppEx` 进程树，再对实际承载显示的模块做离线签名和结构化函数定位。

### WX-132 收款助手窗口归属脱敏诊断

- 工具：新增 Windows 窗口归属检查脚本，枚举微信主窗口和其子窗口的句柄、PID、窗口类、可见性和几何信息。
- 隐私边界：脚本不调用 `GetWindowText`、UI Automation 名称属性或截图，不输出页面文字、消息内容、账号或昵称。
- 用途：在管理员保持收款助手页面打开时，识别可见绘制窗口实际属于 `Weixin.exe`、`wxpublic` 还是 `WeChatAppEx.exe`，为后续 Hook 目标选择提供证据。

### WX-133 应用消息总解析器调用者追踪脚本

- 分析目标：已知 `Weixin.dll + 0x0aae3f0` 是包含 `wcpayinfo` 分支的应用消息总解析器，当前需要从该函数继续向上追踪消息存储和服务号展示路径。
- 脚本：新增 Ghidra 调用者检查，同时输出既有交叉引用、目标函数指针和可执行段中的 `CALL/JMP rel32`，避免只依赖 Ghidra 尚未完成的引用创建。
- 边界：该分析只读磁盘 DLL 和已有 Ghidra 工程，不附加、暂停、修改或操作当前微信进程。

### WX-134 总解析器上游路径排除

- 调用者结果：`Weixin.dll + 0x0aae3f0` 只有 `+0x22c1410`、`+0x22c1ab0` 和 `+0x22c1ce0` 三个直接调用者，另有一个只读段函数指针。
- 反编译结果：三个调用者均在消息对象类型为 `0x31` 时构造约 `0x9c8` 字节对象，调用总解析器后执行复制、销毁或字符串提取；它们属于通用对象序列化包装，不是收款助手页面回调。
- 服务号候选：`brandservicesessionholder` 相关函数会分类会话、刷新列表和处理选中项，`service_officialaccounts` 函数用于注册服务号类型；当前反编译中未直接出现金额、交易号或到账时间字段。
- 结论：不将通用 `0x31` 解析包装替换为生产 Hook 点；下一步对服务号会话刷新和选中函数做只计数的受控动态命中测试，再沿真实命中路径继续定位结构字段。

### WX-135 服务号会话候选只计数诊断观察器

- 诊断点：开发构建使用四个硬件断点统计 `Weixin.dll + 0x48f0e70`、`+0x48f7090`、`+0x48fbd50` 和 `+0x4910560` 的入口命中次数。
- 读取边界：诊断异常处理器不读取函数参数、返回值、字符串或业务对象，不生成支付候选，不访问 Worker 或任何网络。
- 版本防护：四个候选入口均校验固定版本前 `32` 字节签名；任一签名不匹配时拒绝安装断点。
- 发行隔离：诊断 DLL 使用独立 CMake 目标 `cfpay-hook-diagnostic`，正式目标 `cfpay-hook-observer` 的宏定义、偏移和事件协议不变；生产发行脚本仍只允许主 EXE 和正式观察 DLL。
- 构建验证：MinGW Windows Release 构建无告警；正式 Hook Harness 连续 `3` 轮通过，发行自检和单 EXE 双文件约束均通过。

### WX-136 补齐诊断观察器线程覆盖

- 现场复核：诊断构建首次注入时会给已有线程安装四个候选硬件断点，但原周期性线程覆盖逻辑仅在正式构建启用；微信打开服务号页面时新建的工作线程可能因此漏记命中。
- 诊断修复：诊断工作线程每 `250ms` 重新枚举目标进程线程并补齐硬件断点，继续只统计四个候选入口，不读取参数、返回值或业务对象。
- 失败边界：周期补齐沿用已有线程状态表和断点安装函数；单次线程暂态失败不改变正式 Hook 状态协议，也不进入生产发行包。
- 回归验证：MinGW Windows Release 全量原生构建无告警；正式 Hook Harness 连续 `3` 轮通过，确认正式收款、账号、昵称和停止清理协议未受诊断改动影响。

### WX-137 增加服务号候选受控诊断工具

- 启动隔离：新增开发脚本，将 `cfpay-wechat-host.exe` 和诊断 DLL 复制到 `%LOCALAPPDATA%\CFPay\WeChatServiceDiagnostic`；诊断 DLL 在临时目录重命名为固定加载名，不覆盖正式安装目录。
- 启动保护：目标必须是指定 PID 的 `Weixin.exe`，且进程内不得已加载同名正式观察 DLL；脚本等待 `DIAGNOSTIC_ACTIVE` 后才报告成功。
- 日志输出：宿主标准输出独立写入诊断 JSONL，标准错误写入单独文件；文件只包含状态和四个候选累计命中数，不包含账号、昵称、二维码、交易号或消息正文。
- 停止清理：新增配套停止脚本，通过受限宿主的 `--stop-weixin` 调用观察器清理硬件断点和异常处理器，不动态卸载 DLL。
- 发行边界：两个脚本仅存在于开发工具目录，正式双文件发行脚本不复制诊断 DLL、开发宿主或 PowerShell 工具。

### WX-138 建立服务号候选实机诊断基线

- 正式停机：优雅停止正式监控端和正式 Hook 宿主，确认退出后结束固定路径下的微信进程，避免正式四个硬件断点与诊断断点并存。
- 登录恢复：从配置中的固定 `Weixin.exe` 路径重启微信，使用修复后的 C++ 定点点击完成普通登录；主窗口进入已登录状态，没有循环点击或循环重启。
- 诊断加载：开发宿主从 `%LOCALAPPDATA%\CFPay\WeChatServiceDiagnostic` 向主微信进程加载临时重命名的诊断 DLL；进程内仅有该临时观察 DLL，正式安装目录未修改。
- 基线结果：宿主持久在线，诊断 JSONL 持续输出 `DIAGNOSTIC_ACTIVE`；打开收款助手前候选 A、B、C、D 的累计命中数均为 `0`。
- 当前停点：等待管理员重新进入“服务号 -> 微信收款助手”到账页面；本阶段不需要付款，仅比较页面打开前后的候选命中增量。

### WX-139 确认服务号页面候选 A/B 动态命中

- 人工动作：管理员在诊断运行期间重新进入“服务号 -> 微信收款助手”到账页面，没有发起新付款。
- 命中结果：候选 A `Weixin.dll + 0x48f0e70` 从 `0` 增至 `3`，候选 B `+0x48f7090` 从 `0` 增至 `3`；候选 C `+0x48fbd50` 和候选 D `+0x4910560` 均保持 `0`。
- 稳定观察：页面打开后继续观察多个诊断周期，A/B 计数稳定在 `3/3`，不是每秒健康输出自身造成的递增。
- 现场清理：取得增量后立即通过受限停止入口清除目标进程全部诊断硬件断点和异常处理器；微信主进程保持运行，诊断 DLL仅惰性驻留。
- 定位结论：A/B 属于当前固定微信版本的服务号会话打开链路，C/D 可从本轮页面入口候选中排除；下一步离线追踪 A/B 的调用者、参数对象和展示前结构化字段。

### WX-140 服务号候选 A/B 调用者静态扫描

- 可复现工具：新增 Ghidra 脚本，对动态确认的 A/B 入口同时枚举既有交叉引用、固定地址函数指针和可执行段中的 `CALL/JMP rel32`，避免只依赖不完整分析索引。
- 候选 A：`Weixin.dll + 0x48f0e70` 有 `11` 个直接代码调用点和一条 `.pdata` 元数据引用，没有固定地址函数指针。
- 候选 B：`Weixin.dll + 0x48f7090` 有 `7` 个直接代码调用点和一条 `.pdata` 元数据引用，没有固定地址函数指针。
- 共同上游：`Weixin.dll + 0x4904710` 同时直接调用 A 和 B，是页面打开动作的首个高优先级共同调用者；仅凭静态结果仍不能证明本次三次动态命中全部来自该函数。
- 下一步：诊断观察器在 A/B 入口只读取栈顶返回地址并按已知调用点 RVA 计数，不读取函数参数、对象字段或页面内容，以动态确定真实调用者。

### WX-141 增加服务号 A/B 调用点计数

- 诊断扩展：A/B 入口命中时只从当前线程栈顶读取函数返回地址，减去 `Weixin.dll` 基址得到调用点返回 RVA，并按静态确认的 `11` 个 A 调用点和 `7` 个 B 调用点分别累计。
- 输出格式：诊断健康行新增 `callersA` 和 `callersB`，仅输出非零调用点 RVA及次数；未知地址单独累计为 `unknown`，不输出绝对地址、线程栈或函数参数。
- 隔离保证：调用点数组、计数器、栈顶读取和输出字段均位于 `CFPAY_DIAGNOSTIC_BUILD` 条件编译路径，正式观察 DLL 的 Hook 地址和事件协议不变。
- 回归验证：MinGW Windows Release 全量构建无告警；正式 Hook Harness 连续 `3` 轮通过，收款、账号、昵称、新线程覆盖和停止清理均保持原行为。
- 复测要求：旧诊断 DLL 已在当前微信进程中惰性驻留，必须重启固定路径微信后加载新版本，再比较重新进入收款助手页面前后的调用点计数。

### WX-142 收敛收款助手页面真实调用者

- 登录校正：微信重启后实际仍在普通登录页；使用已验证的 C++ 定点点击一次，窗口类从登录页切换为 `mmui::MainWindow`，没有扫码、循环点击或自动重启。
- 登录基线：登录过程 A 保持 `0`，B 累计 `8`；B 调用点为 `+0x48f0cd6(4)`、`+0x49049ba(2)`、`+0x490b4c7(1)`、`+0x490b827(1)`。
- 页面增量：管理员进入“微信收款助手”后，A 从 `0` 增至 `3`，具体为 `+0x5f258ca(2)` 和 `+0x48f6e1d(1)`；B 从 `8` 增至 `9`，仅 `+0x48f0cd6` 新增一次。计数随后稳定。
- 排除结果：共同静态上游 `+0x4904710` 在页面进入时没有新增命中，不是本次收款助手入口的直接来源。
- 调用者反编译：`+0x5f25500` 按列表索引取得服务号会话项并调用 A；`+0x48f6b60` 在当前选择键变化时以索引 `-1` 再次调用 A完成清空/重选；`+0x48f0cb0` 是调用 B 的刷新包装。
- 定位结论：A/B 及本轮三个真实调用者属于服务号列表选择和分类刷新层，不直接承载到账金额与交易字段；下一步对 A 函数体内的直接调用指令建立地址清单，并动态确认选中收款助手后实际进入的展示分支。
- 现场清理：取得稳定增量后通过受限停止入口清理诊断硬件断点和异常处理器，微信主进程保持运行。

### WX-143 建立服务号选择函数内部调用清单

- 可复现工具：新增 Ghidra 脚本，枚举 A `+0x48f0e70` 和外层列表选择处理器 `+0x5f25500` 函数体内每条直接或间接调用指令，输出调用地址、静态目标和函数名。
- 外层确认：`+0x5f25500` 的实际 A 调用指令为 `+0x5f258c5`，与动态返回 RVA `+0x5f258ca` 完全对应，证明调用点归一化计数正确。
- A 内部分支：服务号选择后可能进入 `+0x48f14d8 -> +0x06f1a30`、`+0x48f15c2 -> +0x1941840`、`+0x48f1668 -> +0x0222e80` 或 `+0x48f1969 -> +0x06f1a30` 等互斥展示路径。
- 下一步：将四个诊断硬件断点从 A/B 入口切换到上述四条调用指令，只计数是否执行；重新进入收款助手即可确定实际展示分支，不读取调用参数或对象字段。

### WX-144 切换到服务号内部展示分支诊断

- 诊断点：四个硬件断点切换为 A 函数体内 `+0x48f14d8`、`+0x48f15c2`、`+0x48f1668` 和 `+0x48f1969` 四条直接调用指令。
- 指标命名：诊断健康行分别统计 `genericPrimary`、`specialMessage`、`specialContent` 和 `genericSecondary`；异常处理器只递增计数，不读取寄存器参数、对象字段或返回值。
- 版本防护：从固定版本 `Weixin.dll` 的 PE `.text` 节提取每个调用点连续 `32` 字节签名，任一不匹配时拒绝安装诊断断点。
- 阶段替换：移除上一阶段 A/B 返回地址归一化计数代码；历史调用者结论已写入 WX-142，当前诊断目标仅用于确定收款助手实际展示分支。
- 回归验证：MinGW Windows Release 全量构建无告警；正式 Hook Harness 连续 `3` 轮通过，确认生产观察 DLL 和协议未受影响。
- 协助停点：部署分支诊断后，由管理员一次性进入收款助手、点击详情并打开“收款小账本”小程序，同时比较主微信与 WMPF 子进程变化。

### WX-145 建立收款小账本操作前诊断基线

- 干净重启：结束含上一阶段惰性诊断 DLL 的固定路径微信进程后重新启动，使用 C++ 定点点击完成普通登录；只读窗口检查确认为 `mmui::MainWindow`。
- 分支诊断：新微信主进程从临时目录加载四分支诊断 DLL，宿主持久在线；`genericPrimary`、`specialMessage`、`specialContent` 和 `genericSecondary` 基线均为 `0`。
- WMPF 基线：存在一个 WMPF 主进程、常驻 utility/gpu/render 子进程和一个预加载应用角色，尚未出现由本次收款小账本操作新建的目标应用进程。
- 当前停点：等待管理员一次性进入收款助手、点击详情并打开“收款小账本”小程序；完成后比较四分支计数和 WMPF 新增进程角色。

### WX-146 确认收款小账本落在 WMPF 渲染链路

- 人工动作：管理员进入收款助手、点击详情并打开“收款小账本”小程序。
- 主进程结果：`genericPrimary`、`specialMessage`、`specialContent` 和 `genericSecondary` 全程保持 `0`，排除 A 内四条候选展示调用作为小程序启动或到账详情渲染入口。
- WMPF 增量：操作后新增 `WeChatAppEx.exe` renderer 进程，启动参数包含 `--wmpf-render-type=4`、`--wmpf-appid=preload-10` 和新的 renderer client ID；操作前不存在该进程。
- 窗口归属：新增可见 `Chrome_WidgetWin_0` 小程序窗口属于 WMPF 主进程，不属于微信主进程或 renderer；renderer 加载 `flue.dll`、`owl.dll`、`xweb_elf.dll` 等 WMPF/Chromium 运行模块。
- 结论：收款小账本到账详情的最终展示位于 WMPF 浏览器/renderer 进程链路；后续诊断目标转向 WMPF 中网络解包完成后的应用消息、脚本对象或 DOM 提交前结构，不 Hook 网络通信，不读取通知、数据库或 OCR。
- 现场清理：取得证据后立即清除主微信进程四个无效分支断点和异常处理器，小程序窗口与微信进程保持运行。

### WX-147 定位 WMPF 显示前结构化候选函数

- 固定版本：分析对象为 `RadiumWMPF/25715` 的 `flue.dll`，SHA-256 为 `5baf4a84a41036ce5b2ef897d85029bde9ee9965b4a69d4f5b594672821cad56`；本阶段只读磁盘模块和 Ghidra 工程，不附加或暂停运行中的微信/WMPF 进程。
- 字符串引用：新增脚本扫描小程序调用、回调和 `SetData` 日志字符串，并在未执行全 DLL 分析时直接恢复 RIP 相对引用；11 个引用归并到 9 个 `.pdata` 函数边界。
- JS API 分发：`flue.dll + 0x2f1fc60` 对第 4 参数执行字符串比较和日志输出，确认该参数为 API 名；第 5 参数按字符串对象复制，空值补为 `{}`，随后分派到同步或异步处理器。
- 服务回调：`+0x2f07920` 的第二参数按字符串输出，第三参数按整数输出，第四参数按最长 400 字节的字符串处理，并最终调用内部服务回调；该点适合区分应用服务回调是否进入 renderer。
- XWeb 回调：`+0x3749e80` 的第二参数为 API 名，第三参数为回调编号，函数先取得并截断最多 200 字节的回调内容，再投递到渲染运行时。
- SetData 提交：`+0x37eab90` 的第 5 参数按标准字符串对象读取、复制、分块或解压，成功后构造异步任务并提交到渲染执行器；它比通用 JS API 分发更接近 DOM/界面更新，是到账字段动态诊断的最高优先级候选。
- 动态策略：下一阶段先在目标 `WeChatAppEx.exe` renderer 中对 `+0x2f07920`、`+0x3749e80` 和 `+0x37eab90` 做只计数硬件断点，不读取参数；确认真实页面刷新命中路径后再逐步验证 API 名或结构字段。
- 隐私边界：分析日志不保存真实账号、昵称、二维码、交易号、金额或页面正文；正式监控端仍保持停止，当前小程序 renderer PID `20840` 保持运行。

### WX-148 增加 WMPF renderer 只计数诊断能力

- 开发构建：新增 `cfpay-hook-wmpf-diagnostic.dll`，复用既有硬件断点和 VEH 清理框架，但仅在同时定义 `CFPAY_DIAGNOSTIC_BUILD` 与 `CFPAY_WMPF_DIAGNOSTIC_BUILD` 时生成；正式 `cfpay-hook-observer.dll` 代码路径和事件协议不变。
- 四个观察点：固定版本断点为 `flue.dll + 0x2f07920` 服务回调、`+0x2f1fc60` JS API 分发、`+0x3749e80` XWeb 回调和 `+0x37eab90` SetData 提交；异常处理器只累计命中数，不读取寄存器参数、字符串或页面数据。
- 版本防护：宿主要求目标进程名为 `WeChatAppEx.exe`，加载模块必须是大小 `237162032` 字节且 SHA-256 固定的 `flue.dll`，并再次校验四个候选入口各 `32` 字节签名。
- 角色防护：启动脚本只接受命令行同时包含 `--wmpf-render-type=4` 与 `--wmpf-appid=preload-10` 的目标 renderer，开发 DLL复制到 `%LOCALAPPDATA%` 后以受限固定名加载。
- 生命周期：宿主新增内部 `--run-wmpf` 和 `--stop-wmpf` 命令，停止时调用 DLL导出清除全部硬件断点和异常处理器，DLL仅惰性驻留；该命令不连接 Worker 或业务接口。
- 回归验证：MinGW Windows Release 全量构建无告警；正式 Hook Harness 连续 `3` 轮通过；正式发行目录自检仍只包含 `cfpay-wechat-monitor.exe` 与 `cfpay-hook-observer.dll`，主 EXE自检通过。
- 下一步：向当前收款小账本 renderer 注入开发诊断，记录页面已经打开后的稳定基线；取得基线后再由管理员执行一次页面刷新或详情切换，比较四项增量。

### WX-149 建立 WMPF 主进程显示链路诊断基线

- Renderer 结果：目标 PID `20840` 通过进程角色、`flue.dll` 大小、SHA-256 和四处入口签名校验，但远程 `LoadLibraryW` 返回空模块；宿主记录失败阶段为 `LoadLibraryW`，未安装断点，也未强行绕过 Chromium renderer 沙箱。
- 主进程依据：收款小账本可见 `Chrome_WidgetWin_0` 窗口属于 WMPF 主进程 PID `20176`，该进程同样加载固定版本 `flue.dll`，可观察浏览器侧服务回调和向 renderer 的投递边界。
- 主进程加载：开发诊断 DLL成功加载到 PID `20176`，四个入口签名再次在进程内校验通过，硬件断点和 VEH 安装完成；正式微信主进程与正式监控端状态未改变。
- 稳定基线：页面已经打开后连续观察超过 `60` 秒，`serviceCallback`、`jsApi`、`xwebCallback` 和 `setData` 均保持 `0`，健康输出本身不会增加计数。
- 启动修复：WMPF 诊断脚本支持显式 `Renderer`/`Main` 角色，并改用 `Win32_Process.Create` 脱离启动宿主，标准输出和错误继续重定向到 `%LOCALAPPDATA%`，避免 WSL 调用会话被常驻宿主占用。
- 故障可见性：受限注入函数新增 `OpenProcess`、`VirtualAllocEx`、`WriteProcessMemory`、`CreateRemoteThread`、`LoadLibraryW` 等失败阶段和 Win32 错误码，不改变允许加载的进程名与 DLL固定文件名限制。
- 当前停点：WMPF 主进程只计数诊断保持运行，等待管理员在收款小账本内执行一次页面刷新、详情切换或重新打开页面；本轮不需要付款。

### WX-150 定位 WMPF 浏览器侧结构化投递函数

- 字符串扫描：扩展固定版本 `flue.dll` 扫描项，新增 `SendAppletEvent`、`SessionSendDataToMiniProgram`、`NotifyEvent`、`dispatchEvent` 和两种 `SendDataToMiniProgram` 完整函数签名；9 个引用归并到 8 个 `.pdata` 函数边界。
- 会话投递：`flue.dll + 0x2d22100` 以第三参数字符串匹配当前小程序会话，匹配后将第二参数持有对象、第四参数事件字符串和第五参数 `base::Value` 交给 `+0x2d63490`，属于宿主向指定小程序会话投递结构化数据的入口。
- Applet 事件：`+0x2d179f0` 接收事件名相关字符串和回调对象，根据回调是否存在分派到两个下游发送函数；它位于 `AppletRuntimeManager::SendAppletEvent` 浏览器侧路径。
- 容器通知：`+0x2d4bac0` 的第二、第三参数均按标准字符串读取并记录，随后原样传给 `+0x2d38b40`；该函数对应 `AppletIndexContainer::NotifyEvent(event, data)`，最接近“事件名 + JSON 字符串”进入 renderer Mojo 接口前的边界。
- 管理器分发：`+0x2d5a400` 接收一个整数和两个字符串，直接调用 `+0x2d656f0`；对应 `AppletManager::dispatchEvent`，适合确认主进程是否通过通用事件分发器推送页面数据。
- H5 分支：`+0x2d62e70` 从输入对象解析事件和数据，最终以 `onH5Data` 调用同一 `+0x2d656f0` 下游；该路径偏向 H5 通信，不作为收款小账本第一候选。
- 动态优先级：下一轮四个只计数断点切换为 `+0x2d179f0`、`+0x2d22100`、`+0x2d4bac0` 和 `+0x2d5a400`；只有动态命中后才逐步读取事件名，不直接记录 JSON 正文或支付字段。

### WX-151 切换 WMPF 浏览器侧结构化投递诊断

- 四个观察点：WMPF 开发诊断断点切换为 `flue.dll + 0x2d179f0` `SendAppletEvent`、`+0x2d22100` `SessionSendDataToMiniProgram`、`+0x2d4bac0` `NotifyEvent(event, data)` 和 `+0x2d5a400` `dispatchEvent`。
- 版本防护：同步更新四个固定文件偏移和各入口连续 `32` 字节签名；任一入口与固定 `RadiumWMPF/25715` 模块不匹配时拒绝安装诊断断点。
- 指标命名：诊断健康行改为 `sendAppletEvent`、`sessionSendData`、`notifyEvent` 和 `dispatchEvent`；当前阶段仍只累计命中次数，不读取事件名、JSON 正文或页面字段。
- 构建回归：MinGW Windows Release 全量构建通过；正式 Hook Harness 连续 `3` 轮通过，正式观察 DLL、事件协议和发行目录未改变。
- 登录恢复：现场只读检查发现微信仍停在普通“进入微信”按钮页；将最新单 EXE 复制到本机安装目录并校验 SHA-256 一致后，使用内置原生 C++ 定点点击一次，窗口进入 `mmui::MainWindow`，没有循环点击、自动重启或 UI Automation 操作。
- 当前基线：微信主进程未加载正式或诊断观察 DLL；WMPF 主进程 PID `552` 和常驻预加载 renderer 已建立，尚未出现收款小账本可见窗口。下一步由管理员打开“微信收款助手 -> 详情 -> 收款小账本”，再向新的 WMPF 主进程加载本轮四点诊断。

### WX-152 建立 WMPF 浏览器结构化投递零点基线

- 诊断加载：向本轮 WMPF 主进程 PID `552` 加载四点开发诊断，固定 `flue.dll` 校验、四处入口签名校验、DLL加载、VEH 和硬件断点安装均成功；正式微信主进程仍未加载观察 DLL。
- 打开结果：管理员打开收款小账本后，可见 `Chrome_WidgetWin_0` 小程序窗口出现，同时新增 `--wmpf-appid=preload-11` 应用进程，确认本次页面确实进入 WMPF 链路。
- 计数基线：`sendAppletEvent`、`sessionSendData`、`notifyEvent` 和 `dispatchEvent` 连续多个健康周期均为 `0`，健康输出自身不会产生误命中。
- 时序限制：本次小程序进程创建时间与诊断安装时间重叠，不能证明四个候选不参与首次页面投递；诊断保持运行，下一步在断点已稳定安装的条件下关闭并重新打开一次收款小账本，再比较增量。

### WX-153 确认收款小账本命中浏览器结构化投递链路

- 人工动作：管理员关闭并从“微信收款助手 -> 详情”重新打开收款小账本；操作前四项计数已稳定为 `0`，操作后新增 `--wmpf-appid=preload-12` 应用进程和新的可见小程序窗口。
- 命中增量：`SendAppletEvent` 从 `0` 增至 `3`，`NotifyEvent(event, data)` 从 `0` 增至 `17`，`dispatchEvent` 从 `0` 增至 `2`；`SessionSendDataToMiniProgram` 保持 `0`。
- 稳定结果：页面打开完成后四项计数稳定在 `3/0/17/2`，不是健康上报或后台空闲造成的持续增长。
- 路径结论：固定版本收款小账本的浏览器侧页面投递明确经过 `SendAppletEvent`、`NotifyEvent` 和 `dispatchEvent`，其中 `NotifyEvent` 是当前最密集、且参数形态最接近“事件名 + JSON”的观察边界。
- 现场清理：通过受限停止入口清除 WMPF 主进程全部诊断硬件断点和异常处理器；微信主进程、小程序窗口和 WMPF 子进程保持存活，开发 DLL仅惰性驻留。
- 下一步：诊断扩展仅从 `NotifyEvent` 第二参数读取事件名并输出，明确禁止读取或落盘第三参数 JSON；重新打开页面后按事件名进一步收敛到账展示路径。

### WX-154 增加 NotifyEvent 事件名只读诊断

- 读取边界：WMPF 开发诊断仅在 `NotifyEvent` 入口读取 `RDX` 指向的第二参数标准字符串；`R8` 指向的第三参数数据/JSON没有读取、复制、解析或输出路径。
- 内容保护：事件名最大 `127` 字节，只接受字母、数字及 `_-. : / $ @` 中不含空格的标识符字符；空值、超长值、非 ASCII 文本或疑似 JSON 内容直接清空并拒绝落盘。
- 输出协议：有效值以独立 `diagnostic_event_name` 记录输出，固定标记来源为 `NotifyEvent`；健康行新增 `eventNameReadFailures`，用于区分参数布局错误而不暴露被拒绝内容。
- 线程模型：异常处理器只执行受限字符串复制、字符校验和既有无锁队列入队，JSON拼接与文件写入仍由观察器工作线程完成。
- 构建回归：MinGW Windows Release 全量构建通过；正式 Hook Harness 连续 `3` 轮通过，生产收款、账号、昵称、登录健康和断点停止行为保持不变。
- 实机前置：旧版开发 DLL已在当前 WMPF 主进程中惰性驻留，必须重启固定路径微信后才能加载本轮新 DLL；普通按钮登录可由内置原生 C++ 助手自行完成。

### WX-155 重启并回归普通按钮登录

- 干净重启：结束固定路径下包含旧惰性开发 DLL的微信进程树，并从 `D:\Program Files\Tencent\Weixin\Weixin.exe` 重新启动；新微信主进程和 WMPF 主进程均未加载观察 DLL。
- 页面确认：启动后只读检查为 `mmui::LoginWindow`，存在“进入微信”按钮和当前账号提示，属于无需扫码的普通按钮登录场景。
- 原生操作：单 EXE内置 C++ 登录助手以 `direct` 模式定点点击一次，没有使用 UI Automation、PowerShell 控件调用、循环点击或自动重启。
- 登录结果：点击入口立即返回成功，微信初始化约 `11` 秒后窗口类切换为 `mmui::MainWindow`；本轮说明按钮动作与主界面建立之间可能存在数秒延迟，健康判断必须以窗口状态为准而不能只看点击返回值。
- 当前状态：微信主进程 PID `19228` 已登录，WMPF 主进程 PID `22176` 可用于加载新版事件名诊断。

### WX-156 识别 flue.dll 字符串 ABI

- 实机结果：管理员打开收款小账本后，浏览器侧计数为 `SendAppletEvent=2`、`NotifyEvent=17`、`dispatchEvent=2`、`SessionSendData=0`；17 次 `NotifyEvent` 事件名读取全部被保护逻辑拒绝。
- 隐私确认：本轮没有生成任何 `diagnostic_event_name` 记录，第三参数数据/JSON仍未读取；失败记录只包含累计数 `eventNameReadFailures=17`。
- 根因：原读取器按 MSVC `std::string` 的长度 `+0x10`、容量 `+0x18` 布局解释参数，但固定版本 `flue.dll` 使用 24 字节 libc++ 风格字符串对象。
- 指令证据：`NotifyEvent` 反编译在 `param_2 + 0x17` 读取有符号标记；非负时长度即该字节、数据内联于对象起始地址，负值时数据指针位于 `+0x00`、长度位于 `+0x08`。`param_3` 使用完全相同布局，但本阶段禁止读取。
- 现场清理：确认失败计数稳定后，通过受限停止入口清除 WMPF 主进程断点和 VEH；微信及小程序保持运行，开发 DLL惰性驻留。
- 下一步：新增固定版 flue 24 字节字符串读取器，仅用于 `NotifyEvent` 第二参数；继续保留短长度、字符白名单和疑似 JSON拒绝保护。

### WX-157 实现固定版 flue 字符串读取器

- 读取实现：新增仅在 `CFPAY_WMPF_DIAGNOSTIC_BUILD` 编译的 24 字节字符串读取器，一次读取对象头并按 `+0x17` 有符号标记区分内联短字符串与堆上长字符串。
- 短字符串：标记非负时以该字节为长度，内容直接取对象起始地址；长字符串：标记为负时从 `+0x00` 取得数据指针、从 `+0x08` 取得长度。
- 安全限制：两种布局均要求长度小于输出缓冲区且不超过全局字符串上限，长字符串指针必须非空；随后仍执行事件名 ASCII 白名单，任何失败只累计计数、不输出原始字节。
- 隔离范围：新读取器只由 WMPF `NotifyEvent` 第二参数事件名诊断调用；正式观察 DLL代码路径继续使用既有 MSVC 字符串读取器，第三参数 JSON没有新增访问。
- 构建回归：MinGW Windows Release 全量构建通过；正式 Hook Harness 连续 `3` 轮通过。
- 实机前置：当前 WMPF 主进程仍惰性加载上一版诊断 DLL，需再次重启固定路径微信后加载新版本。

### WX-158 验证 NotifyEvent 事件名读取

- 干净部署：再次重启固定路径微信，使用原生 C++ 定点点击完成普通按钮登录，并向新 WMPF 主进程加载修正版诊断；打开小账本前基线为 `0/0/0/0` 且读取失败数为 `0`。
- 打开结果：管理员打开收款小账本后，计数稳定为 `SendAppletEvent=2`、`SessionSendData=0`、`NotifyEvent=17`、`dispatchEvent=2`，新增目标 WMPF 应用进程。
- ABI 验证：17 次 `NotifyEvent` 第二参数全部成功解析，`eventNameReadFailures` 保持 `0`，证明固定版 flue 24 字节字符串布局正确。
- 事件集合：本轮只记录到 `titleBarDragArea`、`currentPageInfo`、`setCapsuleStyle`、`appReady`、`hideInitLoading`、`pageStart`、`pageReady`、`engineStart` 和 `engineReady` 等容器/页面生命周期事件。
- 隐私边界：输出仅包含上述短事件名和时间戳，没有访问或落盘第三参数 JSON、页面正文、账号、昵称、交易号或金额。
- 当前停点：诊断保持运行且计数稳定，下一步在管理员执行一次真实小额收款后比较事件名和计数增量，确认到账更新是否仍经过该浏览器侧边界。

### WX-159 排除到账主动推送经过 NotifyEvent

- 收款动作：管理员在收款小账本保持打开、四项计数稳定的条件下完成一次真实小额收款；文档和诊断日志不记录金额、交易号或付款方信息。
- 支付前基线：`SendAppletEvent=2`、`SessionSendData=0`、`NotifyEvent=17`、`dispatchEvent=2`，事件名读取失败数为 `0`。
- 支付后结果：持续观察超过 `15` 秒，四项计数、事件名集合和失败数均无任何变化；WMPF 主进程、目标应用进程和诊断宿主保持存活。
- 排除结论：到账发生时，小账本不会通过当前固定版 `SendAppletEvent`、`SessionSendDataToMiniProgram`、`NotifyEvent` 或 `dispatchEvent` 浏览器入口主动推送页面更新；这些入口目前只覆盖页面启动和容器生命周期。
- 下一步：保持诊断运行，由管理员关闭并重新打开小账本触发页面重新查询；比较重开时事件名增量，并进一步检查目标 WMPF 应用/renderer 内的数据提交路径。

### WX-160 定位微信 AppReader 消息卡片候选

- 现场确认：管理员确认真实小额收款后，微信服务号侧已经出现到账消息，而收款小账本没有收到主动投递；分析目标因此转回微信主界面的 `ChatAppReaderItemView` 消息卡片绑定链路。
- 可复现工具：新增 Ghidra 脚本扫描 `ChatAppReaderItemView`、AppReader 模板/分类视图、点击信息和样式名称，并在不依赖完整引用索引时恢复 RIP 相对代码引用。
- 元对象入口：`mmui::ChatAppReaderItemView` 字符串位于 `Weixin.dll` 数据区，直接代码引用归属 `+0x0889e40`；`AppReaderCateItemView` 和 `AppReaderTemplateView` 同时存在注册引用与元对象引用。
- 构造候选：`AppReaderCateSecondaryItemView` 的直接引用位于 `+0x46444b0`，`AppReaderCateCoverItemView` 位于 `+0x46458d0`；两者是收款助手卡片内部分类/封面项目的高优先级构造候选。
- 样式候选：`style::ChatAppReaderViewStyle` 有两条直接引用，分别归属 `+0x3d2fde0` 和 `+0x3d2fec0`；`AppReaderCateView` 引用归属 `+0x3d21d00`。
- 安全边界：本阶段只读固定版磁盘 DLL 和已有 Ghidra 项目，没有附加微信、读取服务号正文或访问通知/数据库；WMPF 诊断断点已清除。
- 下一步：反编译上述候选，区分 Qt 元对象注册、样式查询、消息模型绑定和点击处理，选择只计数动态入口。

### WX-161 排除 AppReader 元对象与纯构造候选

- 可复现工具：新增脚本反编译 AppReader 类型工厂、三个 Qt 元对象函数、分类视图/样式函数及两个卡片子视图构造函数。
- 类型工厂：`+0x017beb0` 按类名创建 UI对象；匹配 `AppReaderCateItemView` 或 `AppReaderTemplateView` 时统一分配对象并调用 `+0x09ab800`，该下游是后续构造链高优先级入口。
- 元对象排除：`+0x0888070`、`+0x0889000` 和 `+0x0889e40` 均执行 Qt 类型比较和基类指针偏移调整，属于 `qt_metacast` 类逻辑，不接收消息模型。
- 样式排除：`+0x3d2fde0/+0x3d2fec0` 只按 `ChatAppReaderViewStyle` 名称缓存和查询样式编号；`+0x3d21d00` 只初始化分类视图和布局容器。
- 子视图构造：`+0x46444b0` 与 `+0x46458d0` 分别初始化 secondary/cover 卡片对象、字符串成员、布局和子控件，未读取业务消息字段；它们可能由更上层绑定函数创建，但自身不是最终 Hook 点。
- 下一步：枚举 `+0x09ab800`、`+0x46444b0`、`+0x46458d0` 和 `+0x0889e40` 的直接调用者、存储函数指针及代码分支，继续向真正的消息绑定方法收敛。

### WX-162 收敛 AppReader 卡片共同构造上游

- 可复现工具：新增调用者扫描脚本，对四个 AppReader 候选同时枚举 Ghidra引用、固定地址函数指针和可执行段 `CALL/JMP rel32`。
- 工厂下游：`+0x09ab800` 只有类型工厂 `+0x017beb0` 一条直接调用，没有存储函数指针，确认它是按类型创建对象的构造包装，不是运行期消息更新入口。
- 共同上游：secondary 构造 `+0x46444b0` 和 cover 构造 `+0x46458d0` 均只由 `+0x208e670` 直接调用；该函数同时组装两种卡片子视图，是完整 AppReader 消息卡片构建的高优先级上游。
- 函数表线索：`ChatAppReaderItemView` 的 `qt_metacast` `+0x0889e40` 没有直接代码调用，但其地址存储在只读区 `0x189179a50`，说明可从相邻函数表项恢复该类的虚方法集合。
- 下一步：反编译 `+0x208e670` 和 `+0x09ab800`，并解析 `0x189179a50` 邻域的可执行函数指针，寻找接收消息模型并更新卡片文本/详情动作的虚方法。

### WX-163 确认 AppReader 消息模型绑定入口

- 现场复核：管理员再次确认真实收款后，收款小账本没有收到主动投递，但微信服务号侧已经出现对应消息；继续排查服务号消息卡片显示前链路，不再把小账本生命周期事件作为到账入口。
- 共同上游：`+0x208e670` 根据卡片类型创建 cover 或 secondary 子视图，并把上游消息参数继续传给子视图绑定函数；该函数本身主要负责子视图惰性创建、显示切换和布局组装。
- cover 绑定：cover 分支创建 `+0x46458d0` 后调用 `+0x4646e50(view, param2, param3)`，其中 `param2/param3` 直接来自共同上游，是当前最高优先级的完整卡片消息绑定入口。
- secondary 绑定：secondary 分支创建 `+0x46444b0` 后调用 `+0x4644ae0(view, param2)`，同样直接接收上游消息模型，是并列高优先级绑定入口。
- 构造包装排除：`+0x09ab800` 只调用基类构造 `+0x208e350` 并设置虚表，不读取消息模型，继续从动态 Hook 候选中排除。
- 函数表恢复：从 `ChatAppReaderItemView` 元对象邻域恢复出 `+0x3d1fe50`、`+0x3d20600`、`+0x3d20780`、`+0x3d20800`、`+0x3d209a0`、`+0x3d20b20`、`+0x3d20f50`、`+0x3d21000` 和 `+0x3d21a40` 等虚方法候选。
- 下一步：优先反编译 `+0x4646e50` 与 `+0x4644ae0`，再与上述虚方法比较消息模型读取、字符串复制、文本设置和详情动作构造行为；动态阶段最多选择四个入口做只计数诊断。

### WX-164 切换到 AppReader 消息绑定诊断

- 绑定结构：`+0x4646e50` 与 `+0x4644ae0` 都读取消息对象 `+0xec` 的整数类型码并进入 `switch` 分支，确认两者不是纯布局函数，而是 cover/secondary 卡片的实际消息模型绑定入口。
- 字段证据：不同类型分支把消息对象 `+0x40`、`+0xa8`、`+0x108`、`+0x128`、`+0x150` 和 `+0x170` 等标准字符串成员交给文本控件更新函数；本阶段只记录字段偏移，不读取或落盘真实正文。
- 虚方法排除：`+0x3d20800` 至 `+0x3d20b20` 主要处理几何、鼠标和选择状态，`+0x3d20f50`、`+0x3d21000` 与 `+0x3d21a40` 处理尺寸、事件和虚调用转发，不是消息正文绑定入口。
- 四个断点：主微信开发诊断切换为 cover 绑定 `+0x4646e50`、secondary 绑定 `+0x4644ae0`、cover 通用下游 `+0x4645fb0` 和 secondary 通用下游 `+0x4644910`，每个入口保留固定版本连续 `32` 字节签名校验。
- 类型诊断：cover/secondary 绑定入口只从 `RDX+0xec` 读取整数类型码并按 `0..31` 建立计数直方图；不读取任何字符串字段、交易号、金额、账号或消息正文，越界或不可读只累计失败数。
- 回归验证：MinGW Windows Release 全量构建无警告；正式 Hook Harness 连续 `3` 轮和同进程重新激活 `2` 轮通过，正式收款、账号、昵称、登录健康和停止清理协议未改变。
- 下一步：向当前已登录微信加载开发诊断并建立零点基线，再由管理员重新进入“微信收款助手”会话触发已有消息卡片重绑定，本轮不需要再次付款。

### WX-165 建立 AppReader 绑定诊断零点基线

- 现场状态：微信主进程 PID `9260` 保持运行，正式监控端未启动且进程内没有既有观察 DLL；本步骤没有点击、重启或结束微信。
- 加载结果：开发诊断 DLL已加载，常驻宿主通过命名管道持续输出健康行；启动脚本的外层等待未自行退出，但只读检查确认宿主和断点均正常运行。
- 稳定基线：`coverBind=0`、`secondaryBind=0`、`coverCommon=0`、`secondaryCommon=0`，cover/secondary 类型直方图均为空，类型读取失败为 `0`。
- 排查工具：新增只读 Windows 检查脚本，统一输出目标微信是否加载诊断 DLL、常驻宿主命令行及诊断输出/错误文件尾部，便于区分注入失败与外层脚本等待问题。
- 当前停点：等待管理员手动重新进入“服务号 -> 微信收款助手”会话，使已有到账消息卡片重新绑定；不需要再次付款，也不需要打开收款小账本。

### WX-166 确认收款助手 AppReader 类型零路径

- 人工动作：管理员从“微信收款助手”切换到另一个普通会话，再切回收款助手，使诊断加载前已经渲染的消息卡片重新绑定；没有再次付款，也没有打开收款小账本。
- 动态命中：cover 绑定 `+0x4646e50` 命中 `1` 次，secondary 绑定 `+0x4644ae0` 命中 `4` 次；对应 cover/secondary 通用下游分别命中 `1/4` 次。
- 类型结果：cover 类型直方图为 `0:1`，secondary 类型直方图为 `0:4`，类型读取失败为 `0`；本次收款助手可见卡片全部走消息类型 `0` 分支。
- 路径结论：类型 `0` 分支中的 `+0x4645fb0` 和 `+0x4644910` 是到账服务号卡片显示前的已验证共同路径；后续应继续向这两个下游中的文本控件更新调用收敛，而不是观察其它 AppReader 类型分支。
- 现场清理：取得稳定计数后立即调用受限停止入口，已清除微信主进程内四个硬件断点和异常处理器；微信保持运行，开发 DLL仅惰性驻留。
- 下一步：离线比较 `+0x4645fb0` 与 `+0x4644910` 对消息类型 `0` 的字段读取和文本写入，选择最多四个更靠近正文提交的下游入口做只计数诊断。

### WX-167 定位类型零文本提交调用点

- 边界修正：编译器把 AppReader 类型分支拆成非连续代码块，按 Ghidra函数体枚举会漏掉分支内调用；新增固定地址区间 `CALL rel32` 扫描脚本恢复真实 UI提交点。
- secondary 标题：`+0x4645204` 调用 styled-text 提交函数 `+0x3d3d540`，对应已验证 secondary 类型 `0` 分支的 `param2+0x40` 文本字段。
- cover 标题：`+0x4646506` 与 `+0x4646591` 是 cover 类型 `0` 在两种展示状态下互斥执行的 styled-text 提交点，均来自消息对象 `param2+0x40`。
- cover 副标题：`+0x4646548` 调用 plain-text 提交函数 `+0x1e84570`，输入为可选消息字段 `param2+0x60`；只有该字段存在时才执行。
- 参数形态：styled-text 调用的 `RDX+0x08` 是临时 MSVC字符串对象，plain-text 调用的 `RDX` 直接是 MSVC字符串对象；四个点均位于显示前且已完成消息模型解包。
- 版本防护：已为四个调用指令分别提取连续 `32` 字节签名；下一轮诊断仍需全部匹配固定版本后才能安装硬件断点。
- 隐私策略：动态诊断只输出调用次数、字符串字节数、固定到账关键词是否存在及是否匹配十进制金额格式，不输出原文、金额值、付款方或交易号。

### WX-168 增加 AppReader 文本形态脱敏诊断

- 四个观察点：开发诊断断点切换为 secondary 标题 `+0x4645204`、cover 标题 A `+0x4646506`、cover 副标题 `+0x4646548` 和 cover 标题 B `+0x4646591`，全部在 UI提交调用执行前命中。
- 字符串读取：三个 styled-text 调用从 `RDX+0x08` 读取临时 MSVC字符串，plain-text 调用从 `RDX` 读取 MSVC字符串；最大读取长度限制为 `1023` 字节，异常或越界只累计失败数。
- 脱敏指标：每个槽位只累计命中数、最近字符串字节数，以及是否包含“收款/到账”“支付”“金额/元/货币符号”和十进制金额格式；读取缓冲区使用后立即丢弃，不写原文或数值。
- 版本保护：四个调用指令均使用固定版本连续 `32` 字节签名校验，任一不匹配时拒绝安装诊断断点。
- 回归验证：MinGW Windows Release 全量构建无警告；正式 Hook Harness 连续 `3` 轮和同进程重新激活 `2` 轮通过，生产观察 DLL和事件协议保持不变。
- 部署要求：上一版开发 DLL已在当前微信进程中惰性驻留，加载本版前必须受控重启微信并恢复普通按钮登录；不需要扫码或付款。

### WX-169 重启并建立文本诊断零点基线

- 受控重启：新增开发诊断专用 Windows 脚本，结束旧微信进程、确认完全退出后通过 `Win32_Process.Create` 脱离启动固定路径 `Weixin.exe`；新主进程 PID为 `16220`。
- 按钮登录：使用单 EXE内已验证的原生 C++ `direct` 模式调用普通登录按钮一次，约 `12` 秒后只读确认窗口类为 `mmui::MainWindow`，登录页按钮、扫码标签和二维码标签均为 `0`。
- 启动修复：服务号诊断启动脚本由 `Start-Process` 重定向改为 `Win32_Process.Create + cmd` 脱离运行，通过输出文件确认 `DIAGNOSTIC_ACTIVE`，解决常驻宿主导致外层 PowerShell不退出的问题。
- 干净加载：新版开发 DLL成功加载到新微信进程，四个文本提交调用点签名校验通过，常驻宿主和命名管道输出正常。
- 稳定基线：四个调用点命中均为 `0`，四组文本形态指标均为 `0`，`textReadFailures=0`；当前未读取或写入任何消息正文。
- 当前停点：等待管理员进入“服务号 -> 微信收款助手”会话，使已有消息卡片重新渲染；本轮不需要付款或打开收款小账本。

### WX-170 排除类型零外层标题字段

- 人工动作：管理员先进入收款助手，确认因微信重启自动恢复页面而没有触发；随后切换到其它会话再切回收款助手，使卡片重新提交。
- 动态结果：仅 cover 标题 B `+0x4646591` 命中 `1` 次，其余 secondary 标题、cover 标题 A和 cover 副标题均为 `0`；命中字段可读且长度为 `36` 字节，读取失败为 `0`。
- 脱敏形态：该字段不包含“收款/到账”“支付”“金额/元/货币符号”，也不匹配十进制金额格式；日志没有保存原文或数值。
- 排除结论：消息类型 `0` 的外层 `param2+0x40/+0x60` 字段不是本次到账正文或金额来源；真正可见内容应继续位于 cover 通用下游处理的三项数据提交链。
- 现场清理：取得稳定结果后已通过受限停止入口清除四个硬件断点和异常处理器，微信继续运行，开发 DLL仅惰性驻留。
- 下一步：离线反编译 cover 通用下游调用的 `+0x08e4ef0` 及其三次 `+0x08e4180` 提交，确认对象布局和精确调用点后再做脱敏动态验证。

### WX-171 验证登录前注入与会话自动恢复

- 时序修正：按管理员建议调整为“启动微信登录页 -> 加载诊断 -> 确认零点 -> C++ direct 点击登录 -> 自动恢复主界面”，避免诊断加载晚于会话恢复。
- 登录前状态：新微信主进程 PID `19928` 停留在 `mmui::LoginWindow`，诊断 DLL已加载且四项计数和读取失败均为 `0`；登录按钮只读检查可见且可用。
- 登录结果：原生 C++按钮调用一次成功，约 `15` 秒后窗口进入 `mmui::MainWindow`，登录页按钮、扫码和二维码标签均消失。
- 自动恢复结果：从登录点击到主界面稳定期间，四个外层文本提交点始终保持 `0`，没有读取失败；自动恢复没有经过本轮 `+0x4645204/+0x4646506/+0x4646548/+0x4646591` 四个 setter。
- 结论：自动恢复确实是更完整的首次渲染时序，但当前四个观察点仍过于外层或仅用于会话手动重选分支；下一阶段继续下移到 cover 通用下游 `+0x08e4ef0` 及内部三项提交。
- 现场清理：已清除本轮硬件断点和异常处理器，微信主界面保持运行，开发 DLL仅惰性驻留。

### WX-172 排除 AppReader 三项媒体资源链

- 清理函数：`+0x08e4ef0` 只重置资源加载标志、引用对象、缓存字符串和回调状态，不接收消息正文；`+0x08e4a50` 在资源已加载时执行同类清理。
- 资源提交：`+0x08e4180` 接收 MSVC字符串，复制到对象缓存后调用 `+0x0a0f450` 解析资源描述，并通过异步回调加载对象；失败时走通用资源获取链。
- 三项含义：cover 类型 `0` 下游在固定长度为三项时调用 `+0x08e4180`，因此这三项是图片/媒体资源标识或地址，不是到账金额、付款方或正文文本。
- 排除结论：外层 `param2+0x40/+0x60` 没有到账形态，内部三项又属于媒体资源；当前 `ChatAppReaderItemView` cover 模型不足以解释可见到账正文，可能只对应会话中的其它卡片或到账卡片的媒体外壳。
- 下一步：扩大静态类型扫描，定位服务号会话中其它 `ChatApp*ItemView`、模板消息或支付卡片类，并在动态阶段先识别实际到账消息项类型，再选择结构化绑定入口。

### WX-173 定位客服模板消息结构化绑定入口

- 类型定位：新增字符串与引用扫描后确认 `mmui::ChatKfTemplateMsgItemView` 对应服务号客服模板消息视图；其完整控件构造函数 `+0x4647980` 会创建顶部键值区、详情标题和详情键值行。
- 虚表修正：构造包装 `+0x46477d0` 实际写入 `0x1896817a8` 等七组虚表；此前从 `qt_metacast` 邻域恢复的 `0x18992d4a0` 属于元对象函数表，不是主视图真实虚表。
- 事件排除：真实虚表中的类专属覆盖主要是析构和通用 Qt 事件处理；`+0x3ce8720/+0x3ce8b70/+0x3ce8800/+0x3cf0360/+0x3cf0670` 分别处理鼠标、拖拽和输入事件，不是消息数据绑定入口。
- 唯一上游：模板键值行维护函数 `+0x3d40a00` 只有一个直接调用者 `+0x1be3e20`，因此无需继续从虚表猜测消息绑定函数。
- 结构化证据：`+0x1be3e20(this, model)` 直接读取模型 `+0x510/+0x530/+0x550/+0x588/+0x670` 等字符串字段，并调用文本控件提交函数；随后按 `model+0x570..+0x578` 的向量范围计算行数。
- 键值行证据：向量元素固定为 `0x40` 字节，每行把 `+0x00` 和 `+0x20` 两个 MSVC字符串分别写入键控件和值控件；该位置已经完成网络解包和模板模型构造，且发生在界面显示前。
- 自动恢复结论：微信重启后自动恢复会经过模板消息视图的首次构造和 `+0x1be3e20` 数据绑定；此前零命中是因为观察了不对应到账正文的 AppReader 卡片，不是自动恢复时序无效。
- 下一步：在固定版本签名校验后，对 `+0x1be3e20` 及键值行两个文本提交调用点做只计数、长度和固定关键词形态诊断；先用登录前注入加自动恢复验证已有消息，无需真实付款。

### WX-174 增加客服模板消息脱敏诊断

- 四个观察点：开发诊断切换为模板绑定入口 `+0x1be3e20`、键值行准备调用 `+0x1be41f9`、键文本提交 `+0x1be43d8` 和值文本提交 `+0x1be43e4`。
- 版本保护：四个地址均从固定版 `Weixin.dll` 提取连续 `32` 字节签名，任一不匹配时拒绝安装硬件断点。
- 结构指标：绑定入口只记录命中数、模型键值行数和 `model+0x550` 文本形态；行准备点只记录请求行数；键和值提交点只记录命中数、字节数及固定关键词/金额格式形态。
- 隐私边界：诊断不输出原始键名、原始值、金额值、付款方、交易号、账号或昵称；读取失败和异常范围只累计计数。
- 回归验证：MinGW Windows Release 全量构建无警告；正式 Hook Harness 连续 `3` 轮通过，生产观察 DLL的收款、账号、昵称和登录健康协议未改变。

### WX-175 验证自动恢复未重绑客服模板消息

- 干净时序：结束全部旧微信进程后启动固定路径微信，确认停留在 `mmui::LoginWindow`；先加载新版客服模板诊断并建立四项均为 `0` 的零点。
- 登录执行：直接运行单 EXE内原生 C++ `direct` 登录路径，退出码为 `0`；约 `15` 秒后只读确认窗口进入 `mmui::MainWindow`，登录页按钮、扫码标签和二维码标签均消失。
- 自动恢复结果：从点击登录到主窗口稳定期间，`templateBind/rowPrepare/rowKey/rowValue` 始终为 `0`，模型行数、准备行数和读取失败也均为 `0`。
- 结论修正：重启登录确实覆盖主窗口恢复，但本次恢复没有重新实例化可见的 `ChatKfTemplateMsgItemView`；因此不能仅凭自动恢复替代目标会话重选，零命中不否定 `+0x1be3e20` 的静态绑定证据。
- 当前停点：新版诊断和四个硬件断点保持运行，等待重新选择一次“微信收款助手”会话触发已有客服模板消息项重绑；不需要再次付款。

### WX-176 排除客服模板消息并切换全局会话摘要链

- 人工复核：管理员从其它会话切回“微信收款助手”后，`templateBind/rowPrepare/rowKey/rowValue` 四项计数仍全部为 `0`，`textReadFailures=0`；不是自动恢复没有重绑导致的单次假阴性。
- 排除结论：`ChatKfTemplateMsgItemView +0x1be3e20` 虽然是客服模板详情项的结构化绑定入口，但不承载当前微信收款到账消息；本轮不再围绕会话详情页继续下钻。
- 行为依据：到账金额在未打开“微信收款助手”会话时也会出现在微信侧，真实目标应位于消息到达后生成会话列表摘要、未读预览或服务号会话摘要的全局链路。
- 现场清理：通过受限停止入口成功清除当前微信进程内四个硬件断点和异常处理器，常驻诊断宿主已退出；微信主进程继续运行，开发 DLL仅惰性驻留。
- 工具路由：安装并采用 `zhaoxuya520/reverse-skill`，主路由为 `R22 / ghidra-reverse`；离线 case 位于 `work/wechat-receipt-preview/`，授权、角色、禁止项和固定样本均已落盘。
- 分诊证据：固定样本 SHA-256 为 `10f8e995453e2da46d4f2b5080cd6da1f13cc5147746adc119ceae38cb039de5`，类型为 x64 PE DLL；静态导入表仅有 `KERNEL32.dll` 的 `333` 个符号，同时包含 `LoadLibrary/GetProcAddress`，按流程标记为动态加载嫌疑，不能据此否定模块能力；命名导出共 `117` 个。
- 新静态锚点：样本包含 `mmui::ChatSessionCell`、`mmui::ChatSessionList`、`mmui::ChatSessionCellViewModel`、`mmui::ServiceSessionCell`、`unread_preview_msg_text_style`、`OnSubscriptionSessionListUpdate` 和订阅号摘要读写字符串。
- 下一步：使用现有 Ghidra headless 工程从上述字符串交叉引用定位会话摘要 ViewModel 到文本 setter 的显示前结构化绑定函数，再选择最多四个只计数观察点验证。

### WX-177 定位全局会话摘要结构化绑定入口

- 会话单元格：`mmui::ChatSessionCell` 的类工厂位于 `+0x17beb0`，实际构造函数为 `+0x09ac0f0`；构造函数写入主虚表 `0x188eb85c8` 和副虚表 `0x188eb8678`。
- 虚表排除：与基类虚表逐槽比较后仅有四个派生覆盖，反编译确认分别处理行高、状态位、样式和交互分发，不复制消息文本，不是摘要绑定点。
- ViewModel 恢复：通过 `ChatSessionCellViewModel` 类型转换函数的数据表引用恢复真实方法表 `0x189187238`，构造函数为 `+0x1c43510`，Qt 元调用分发为 `+0x0892500/+0x0892840`。
- 绑定入口：`+0x1c43ef0(this, sessionModel)` 在显示前把会话来源模型复制到响应式 ViewModel；该函数同时更新会话标识、显示文本、摘要、时间、未读数和多项状态标志。
- 摘要字段：来源 `sessionModel+0x1b8` 写入第二个文本属性；当 `sessionModel+0x1d9 == 1` 时改写为空字符串，符合隐藏预览策略。时间字段 `sessionModel+0x8c` 由独立函数 `+0x1c45a80` 格式化，因此 `+0x1b8` 不是时间，属于当前最高置信度的会话摘要正文。
- 全局证据：绑定入口存在八条直接调用路径，调用者覆盖 `+0x1c3d540/+0x1c3db90` 和 `+0x48f8e90/+0x48fa020/+0x48fae90/+0x49006f0/+0x4900d50` 等会话列表家族；它不依赖打开聊天详情页。
- 失败分支：这些类名不是标准 MSVC `TypeDescriptor`，按 Complete Object Locator 恢复虚表零命中；随后改用 Qt 自定义类型转换函数和注册表指针成功恢复，未把失败路径当作否定证据。
- 动态方案：下一轮只在 `+0x1c43ef0` 入口读取候选字符串长度、固定到账关键词形态、十进制金额形态、隐藏标志和调用者桶；不输出原文、金额值、付款方、交易号、账号或昵称。先登录前加载并自动恢复已有会话验证，不需要新付款。

### WX-178 增加会话摘要诊断并纠正 Windows 执行路径

- 诊断实现：开发诊断断点切换为全局 ViewModel 绑定入口 `+0x1c43ef0` 及三个会话列表家族调用点；绑定入口只读取来源模型 `+0x160/+0x1b8` 的脱敏文本形态和 `+0x1d9` 隐藏标志，不保存原文、金额值、付款方、交易号、账号或昵称。
- 指标调整：状态输出改为 `viewModelBind/updateWrapper/serviceFamilyBind/globalFamilyBind/hiddenPreview`，并分别记录两个候选字段的可读次数、最近字节数、固定关键词和十进制金额形态；读取异常只累计失败数。
- 离线验证：MinGW Windows Release 构建无警告，正式 Hook Harness 连续三轮通过；生产观察 DLL、事件协议和正式双文件发行内容未改变。
- 执行纠正：本轮曾由 WSL 直接发起 Windows 重启、注入和登录操作，这与 `d259934`、`b102c07` 已记录的执行约束冲突，存在触发 Windows/微信安全验证并污染现场状态的风险。
- 证据降级：该执行路径得到的动态命中和文本形态只能作为排查线索，不得作为 `+0x160/+0x1b8` 字段语义、登录稳定性或 Hook 可用性的验证证据，也不据此修改生产收款逻辑。
- 后续硬规则：禁止从 WSL 调用 `powershell.exe`、`pwsh.exe`、`psrun.sh` 或 Windows EXE 来启动、停止、登录、注入、检查微信；实机诊断必须由已安装的 Windows 原生监控端发起，或由管理员在 Windows 本机手动启动已复制到 `%LOCALAPPDATA%` 的受限测试入口。
- 当前状态：硬件断点和异常处理器已经清除，后续先完成 Windows 原生测试入口设计与人工启动说明，再重做登录前注入和会话恢复验证；在此之前不进行扫码或真实付款测试。

### WX-179 增加 Windows 原生现场测试状态机

- 单 EXE入口：Windows 原生监控端主窗口新增“登录测试”“扫码测试”“收款测试”三个按钮和独立结果栏；测试请求由监控端现有登录线程消费，不启动 PowerShell、Node、浏览器控制器或第二个 GUI 进程。
- 自动序列：从 `WeChatFieldTest-*` Windows 本地目录启动或显式使用 `--field-test-sequence` 时，单 EXE 自动按“按钮登录 -> 扫码登录 -> 收款匹配”推进；只有二维码扫描和真实付款需要管理员参与，任一阶段失败立即停止。
- 普通登录：按钮测试先正常停止 Hook 宿主，再由同一 EXE受限重启固定路径微信；只调用已验证的 C++窗口消息登录路径，并以账号匹配且 `HOOK_ACTIVE` 为通过条件。测试等待 20 秒未健康时直接失败，不用循环重启掩盖失败。
- 扫码登录：现有固定位置“切换账号”动作提取为原生公共函数；扫码测试重启微信后调用一次切换账号，解析二维码并上报 Worker 后保持暂停，等待管理员扫码。二维码未出现或切换失败时禁止回退为普通按钮自动登录。
- 收款确认：WinHTTP 客户端按需读取 JSON响应；收款测试只接受开始时间后的新结构化事件，并且仅在 `/api/monitor/payment` 返回 `matched=true` 时通过。HTTP 200 但未匹配订单明确判定失败。
- 本地运行：便携 EXE目录没有配置时，只读回退到 `%LOCALAPPDATA%\CFPay\WeChatMonitor\config.json`；该机制用于管理员从 Windows 本机启动测试包，不把密钥复制到仓库，也不允许 WSL 发起 Windows 进程。
- 版本与验证：原生监控端版本提升到 `0.8.1`；MinGW Windows x64 全量构建通过，中文日志和原生现场测试定向测试共 `7` 项通过。当前尚未安装或运行新版 Windows 包，真实按钮、扫码和付款仍待 Windows 本机启动后验证。

### WX-180 放置 Windows 本地便携测试包

- 本地目录：便携包已通过文件复制放置到 `%LOCALAPPDATA%\CFPay\WeChatFieldTest-0.8.1`，目录严格只有 `cfpay-wechat-monitor.exe` 与 `cfpay-hook-observer.dll`；未复制 `config.json`、监控密钥、账号、昵称或其它状态文件。
- 完整性：自动序列版主 EXE SHA-256 为 `380c8c3971009b709b6b021ed289140fc79ee299d2b3c6ecaa3a246d899d4a0c`，观察 DLL SHA-256 为 `cd83fb3f69875afdf841e75b4fc224506d6b815c46c20d372582842e75bd084a`；仓库发行文件与 Windows 本地副本逐项一致。
- 配置来源：Windows 本机启动时，包内没有配置文件，程序会只读复用已存在的 `%LOCALAPPDATA%\CFPay\WeChatMonitor\config.json`；内部宿主仍从便携目录的同一 EXE启动，观察 DLL路径继续按已保存配置校验。
- 执行边界：本步骤只从 WSL 向 Windows 文件系统复制静态文件并计算哈希，没有调用 Windows EXE、PowerShell、`cmd.exe`、WMI/CIM、计划任务或快捷方式，也没有启动、停止、登录、附加或注入微信。
- 当前停点：需要管理员先在 Windows 侧退出正在运行的旧监控端，然后从上述本地目录双击 `cfpay-wechat-monitor.exe`。程序会自动开始普通按钮登录测试；按钮测试通过后自动进入二维码阶段并等待管理员扫码。

### WX-181 实机通过按钮登录并建立扫码停点

- 启动方式：按管理员最新许可，从 WSL 直接执行 Windows 本地 `%LOCALAPPDATA%\CFPay\WeChatFieldTest-0.8.1\cfpay-wechat-monitor.exe --field-test-sequence`；没有调用 `pwsh.exe`、`powershell.exe`、PowerShell脚本、`cmd.exe`、WMI/CIM 或 WSL UNC 路径中的程序。
- 按钮登录：原生测试序列受限重启微信后调用一次 C++登录按钮路径；日志依次出现 `LOGIN_CLICKED`、`LOGGED_IN`、`HEALTHY / HOOK_ACTIVE`，并明确记录“按钮登录后 Hook 已健康”，按钮登录现场测试通过。
- 进程切换：重启前旧 Hook 宿主先正常停止并清理断点；新微信进程重新通过目标文件、入口签名和线程覆盖校验。旧进程退出期间产生的 `PROCESS_NOT_FOUND/HOOK_INSTALL_FAILED` 属于 PID切换窗口，随后已由新进程健康状态覆盖。
- 扫码准备：自动序列再次正常停止 Hook、重启微信并调用一次固定位置“切换账号”；二维码在本机内存中解析成功，日志只记录字节数 `43`，`QR_READY` 上报 Worker 返回 HTTP `200`，未落盘二维码内容或截图。
- 当前停点：微信保持扫码登录页，观察器明确上报 `WEIXIN_LOGIN_REQUIRED`，自动按钮点击被二维码保护禁止；等待管理员只扫描 Worker 重新生成的二维码。扫码成功后程序会自动验证 `LOGGED_IN/HOOK_ACTIVE` 并进入收款测试。

### WX-182 实机通过扫码登录并创建收款测试订单

- 扫码结果：管理员扫描 Worker 重新生成的二维码后，同一微信进程依次上报 `LOGGED_IN` 和 `HEALTHY / HOOK_ACTIVE`；自动序列明确记录“扫码登录后 Hook 已健康”，扫码登录现场测试通过。
- 账号证据：登录后账号 Hook 保存了已确认接收账号，昵称 Hook随后更新当前微信昵称；日志和文档没有记录真实 `wxid`、昵称或二维码内容。
- 收款停点：自动序列已进入收款测试并记录“等待新的结构化收款事件”；该阶段只接受测试开始后的新事件，并要求 Worker返回 `matched=true`。
- 订单创建：使用 WSL Node 直接请求生产 HTTPS易支付网关创建新的微信小额测试订单；签名密钥只在内存中使用，没有调用 PowerShell。随后直接执行 Windows本地 `explorer.exe` 打开支付页，没有经过 `pwsh.exe`、`powershell.exe` 或 `cmd.exe`。
- 敏感清理：订单创建命令产生的临时 JSON已立即删除；追溯不保存真实金额、订单号、交易号、付款方或支付二维码内容。
- 当前停点：支付页面已经在 Windows 默认浏览器打开，等待管理员完成本次真实付款；付款后由监控端自行捕获结构化事件、上报 Worker并验证订单匹配。

### WX-183 实机付款排除现有收款解析入口

- 付款结果：管理员完成新订单付款后，微信客户端已经显示对应到账消息，说明本机微信接收和界面展示链路正常。
- Hook 结果：监控端持续保持 `LOGGED_IN / HOOK_ACTIVE`，线程覆盖和账号、昵称 Hook 正常；但收款入口和返回命中数仍均为 `0`，没有产生本地收款事件、队列记录或 Worker 上报。
- 匹配结果：本轮没有收到 `/api/monitor/payment` 响应，更没有 `matched=true`；现场收款测试未通过，不能以微信界面已显示或 HTTP 健康心跳代替收款链路成功。
- 排除结论：当前生产观察器使用的 `Weixin.dll + 0x2293980` 解析入口没有参与这笔到账消息的主界面显示路径，继续等待同一入口不会增加证据。
- 转向决策：保留固定样本和既有隐私边界，下一轮改用已经静态定位的全局会话摘要绑定入口 `Weixin.dll + 0x1c43ef0` 做脱敏动态验证；只记录调用计数、字段可读性和关键词/金额形态，不保存消息原文、金额值、付款方或交易标识。
- 当前状态：原生监控端和微信进程暂不强制终止；先生成并验证独立诊断构建，再在不并存硬件断点的前提下切换现场观察器。

### WX-184 增加受限会话摘要诊断宿主模式

- 新增命令：单 EXE 内部宿主增加 `--run-weixin-diagnostic <pid> <diagnostic.dll>` 和 `--stop-weixin-diagnostic <pid>`，用于在已登录微信进程中独立启动和停止会话摘要诊断。
- 安全限制：诊断模式只接受进程名为 `Weixin.exe`、包含固定版本且哈希匹配的 `Weixin.dll`，并且只允许加载文件名为 `cfpay-hook-diagnostic.dll` 的模块；不提供任意进程或任意 DLL 注入入口。
- 生命周期隔离：生产观察器和诊断观察器按各自模块名检查文件哈希、运行时 API 版本并调用启动/停止导出；切换前必须先清理生产硬件断点，禁止两套断点同时运行。
- 数据边界：诊断 DLL继续只输出调用计数、候选字段可读次数、长度、固定关键词/金额形态、隐藏标志和读取失败数，不输出消息正文或业务标识。
- 验证结果：MinGW Windows x64 全量构建通过；微信原生日志、监控核心、服务和配置定向测试共 `4` 个测试文件、`33` 项通过。
- 下一步：把新宿主 EXE和诊断 DLL放入 Windows 本地诊断目录，停止当前生产 Hook后对现有会话列表执行恢复验证；该步骤不需要新付款。

### WX-185 放置 Windows 本地会话摘要诊断包

- 本地目录：`%LOCALAPPDATA%\CFPay\WeChatSessionDiagnostic-0.8.1`。
- 文件范围：目录仅包含 `cfpay-wechat-monitor.exe` 和 `cfpay-hook-diagnostic.dll`，没有配置文件、监控密钥、账号、昵称、消息内容或支付数据。
- 完整性：本地 EXE 与仓库构建产物 SHA-256 一致；本地诊断 DLL 与仓库构建产物 SHA-256 一致。
- 执行状态：本步骤只复制静态构建产物并校验哈希，尚未停止生产 Hook、启动诊断或改动微信进程。
- 下一步：调用现有原生宿主停止生产观察器，确认断点已清理后结束旧监控端，再对同一已登录微信进程启动独立会话摘要诊断。

### WX-186 登录前动态确认全局会话绑定并修正字段判断

- 切换结果：生产观察器通过原生停止导出确认清理硬件断点，旧监控端退出；微信先在不重启的状态下加载诊断，因会话模型没有重绑而保持零命中。
- 前置验证：随后正常停止诊断、由原生 C++受限重启微信，在登录按钮调用前重新安装诊断断点；登录按钮调用返回成功，不需要管理员操作。
- 动态命中：主界面恢复后 `viewModelBind=204`、`serviceFamilyBind=8`，`updateWrapper=0`、`globalFamilyBind=0`，文本读取失败为 `0`。
- 字段形态：`sessionModel+0x160` 在全部绑定中可读，并累计出现收款、支付和金额标记；`sessionModel+0x1b8` 虽可读但全部为空。
- 判断修正：`+0x1c43ef0` 已动态确认属于登录恢复时的全局会话 ViewModel绑定路径；此前把 `+0x1b8` 作为最强摘要候选的静态判断被动态证据修正，当前应优先跟踪 `+0x160`。
- 证据边界：本轮只保留总次数、可读性和布尔形态分类，没有保存会话名称、消息正文、金额值、付款方、账号或交易标识。
- 待确认项：已有会话恢复只能证明该字段包含支付/收款相关显示文本，尚不能区分服务名称与新到账摘要；需要在诊断保持运行时再触发一笔新付款，比较增量命中和金额形态。

### WX-187 创建会话摘要增量验证订单

- 诊断状态：会话摘要诊断继续运行在已登录微信主进程中，基线计数已经稳定，不与生产收款观察器并存。
- 订单创建：使用 WSL Node 直接调用生产 HTTPS易支付 `/mapi.php` 创建新的微信小额测试订单；商户密钥仅在进程内读取和参与签名，没有写入临时文件或输出。
- 页面打开：直接调用 Windows 本地 `explorer.exe` 打开 Worker 返回的付款页，没有调用 PowerShell、`cmd.exe`、WMI/CIM 或浏览器自动化。
- 敏感边界：文档、Git 和控制台不记录真实订单号、系统交易号、实际付款金额、付款方或二维码内容。
- 当前停点：等待管理员完成这一笔真实付款；付款后比较 `viewModelBind`、服务号家族、`+0x160` 文本形态和读取失败数的增量。

### WX-188 验证新到账触发会话更新但未暴露金额摘要

- 付款确认：管理员确认在 `16:13` 完成新订单付款；诊断和微信主进程在观察期间保持运行，没有重装断点或切换生产观察器。
- 首个明确增量：约 `16:13:53`，`viewModelBind` 从 `254` 增至 `258`，`serviceFamilyBind` 从 `24` 增至 `25`，收款关键词分类从 `1` 增至 `2`。
- 未增长指标：支付关键词、金额标记和十进制金额分类均未随该笔到账增长；`sessionModel+0x1b8` 继续为空，文本读取失败保持 `0`。
- 动态结论：`Weixin.dll+0x1c43ef0` 确实参与新到账后的服务号会话更新，但当前读取的 `sessionModel+0x160` 更符合会话名称或服务名称，不能作为到账金额正文来源。
- 静态回查：该绑定函数先调用 `FUN_181949b30(param_1+0x160, param_2)` 构造嵌套显示状态，再把 `param_2+0x160` 写入另一个响应式属性；下一步应追踪 `FUN_181949b30` 的输出对象或其最终文本提交点。
- 隐私边界：本步骤仅记录时间窗口、聚合计数和布尔文本形态，没有保存实际金额、消息正文、付款方、订单号、交易号、账号或昵称。

### WX-189 恢复会话显示模型并扩展富文本形态诊断

- 界面事实：管理员确认微信会话列表更新为一条同时包含支付、收款、金额和场景说明的完整摘要；该事实只用于判断数据结构，不记录原句或实际金额。
- 静态修正：`FUN_181949b30` 不是摘要生成函数，而是约 `0x290` 字节会话显示模型的深拷贝函数；它复制多个 MSVC字符串、标志字段和一个富文本分段向量。
- 模型布局：静态确认字符串槽位位于 `+0x000/+0x048/+0x068/+0x0a8/+0x0c8/+0x100/+0x120/+0x140/+0x160/+0x198/+0x1b8/+0x208/+0x228/+0x248/+0x270`；`+0x1e8..+0x1f0` 是元素大小为 `0x28` 的向量范围，每个元素以一个 MSVC字符串开头并携带附加样式数据。
- 诊断扩展：继续复用 `+0x1c43ef0` 单个入口断点，在同一次模型绑定中统计全部字符串槽位和富文本分段的可读次数、最近长度、收款/支付/金额标记和十进制金额形态；不新增进程断点。
- 隐私边界：富文本分段只在栈上短暂读取并立即转换为布尔形态和字节计数，不拼接、不输出、不写盘原文；分段数量限制为 `64`，异常布局只累计读取失败。
- 验证结果：MinGW Windows x64 全量构建通过；微信原生日志、监控核心、监控服务和登录逻辑共 `4` 个测试文件、`28` 项通过；窄范围 Ghidra 脚本在只读工程中成功反编译目标函数及其直接调用者。
- 下一步：停止旧诊断、替换 Windows 本地诊断 DLL，并在不需要新付款的登录恢复中确定哪个槽位或富文本分段包含金额形态。

### WX-190 登录恢复定位会话摘要金额字段

- 干净切换：旧诊断先通过原生停止导出清除全部硬件断点；因旧 DLL仍惰性驻留，使用监控端内置 C++受限重启固定路径微信后替换新版 DLL，仓库构建产物与 Windows 本地副本 SHA-256 一致。
- 前置时序：新版诊断在登录页计数为零时安装，随后由原生 C++直消息登录路径点击一次登录按钮；没有调用 PowerShell、`cmd.exe`、UI Automation、通知读取、OCR或网络 Hook。
- 恢复结果：主界面恢复后 `viewModelBind` 达到 `214`，`serviceFamilyBind` 达到 `11`，全部 `15` 个字符串槽位和富文本向量读取失败为 `0`。
- 金额候选：`sessionModel+0x48` 是唯一同时出现收款关键词、支付关键词、金额标记和两位小数金额形态的字符串槽位；累计形态为收款 `4`、支付 `4`、金额标记 `6`、十进制金额 `2`。
- 对照排除：`sessionModel+0x160` 虽有收款、支付和金额标记，但十进制金额始终为 `0`；`+0x1e8` 富文本向量在全部恢复绑定中元素数均为 `0`，不是当前会话列表摘要载体。
- 当前判断：`sessionModel+0x48` 是会话列表完整摘要正文的最高置信度字段，与管理员看到的一句话到账摘要形态一致；仍需在服务号调用点单独采样，确认金额形态来自收款助手而非其它支付会话。
- 隐私边界：诊断只保留槽位偏移、累计次数、最近字节长度和布尔形态，没有保存摘要原文、实际金额、付款方、订单号、交易号、账号或昵称。

### WX-191 增加服务号调用点摘要专用采样

- 复用断点：继续使用现有服务号家族调用点 `Weixin.dll+0x48f9667`，不增加第五个硬件断点；该地址是调用 `+0x1c43ef0` 前的直接 `call` 指令，`RDX` 仍指向来源会话模型。
- 专用字段：在该调用点只读取 `sessionModel+0x48`，独立累计可读次数、最近字节长度、收款/支付/金额标记和十进制金额形态，状态中新增 `serviceSummary`。
- 目的：把全局恢复中的 `+0x48` 金额候选收窄到服务号会话家族，排除普通支付聊天或其它会话对全局计数的贡献。
- 隐私与稳定性：实现不输出摘要原文或金额值，读取异常只进入既有失败计数；正式观察 DLL、生产收款事件和正式宿主协议均未改变。
- 验证结果：MinGW Windows x64 全量构建通过；微信原生日志、监控核心、监控服务和登录逻辑共 `4` 个测试文件、`28` 项通过。

### WX-192 排除服务号家族调用点直接承载金额摘要

- 干净部署：停止上一版诊断后再次受限重启固定路径微信，替换新版诊断 DLL并校验 SHA-256一致；诊断在登录页零点安装后由原生 C++路径完成一次按钮登录。
- 恢复计数：主界面恢复后 `viewModelBind=214`、服务号家族调用点 `11` 次，全部模型读取失败为 `0`。
- 专用结果：`serviceSummary` 的最近长度始终为 `3` 字节，收款、支付、金额标记和十进制金额分类全部为 `0`；它不是管理员看到的完整到账摘要正文。
- 对照结果：同一恢复过程中全局 `sessionModel+0x48` 仍累计出现收款 `4`、支付 `4`、金额标记 `6`、十进制金额 `2`，证明金额候选来自 `+0x1c43ef0` 的其它调用路径。
- 判断修正：`+0x48f9667` 虽在新到账时参与刷新，但不能作为金额正文读取点；下一步在 `+0x1c43ef0` 入口读取栈顶返回地址，按全部直接调用点分别统计 `+0x48` 形态。
- 隐私边界：本轮仍未保存任何摘要原文或金额值。

### WX-193 增加会话绑定入口调用者分桶

- 直接调用关系：静态确认 `+0x1c43ef0` 有六个直接 `CALL` 返回地址 `+0x1c3dd84/+0x1c43924/+0x48f966c/+0x48fa6f6/+0x48fb566/+0x4900fc9`，另有两个尾调用 `JMP`。
- 动态分桶：入口断点从栈顶只读返回地址，把 `sessionModel+0x48` 的可读次数、最近长度和四类文本形态分别累计到六个固定桶及一个其它桶；尾调用和未识别来源归入其它桶。
- 安全边界：分桶不保存返回地址之外的栈内容，不输出摘要原文或实际金额；仍只使用原有四个硬件断点。
- 验证结果：MinGW Windows x64 全量构建通过；微信原生日志、监控核心、监控服务和登录逻辑共 `4` 个测试文件、`28` 项通过。

### WX-194 定位金额摘要绑定调用路径

- 干净验证：调用者分桶版诊断在登录前安装，原生 C++按钮登录完成后恢复 `210` 次全局绑定，读取失败为 `0`。
- 唯一金额桶：返回地址 `Weixin.dll+0x1c43924` 累计 `171` 次命中，`sessionModel+0x48` 出现收款 `4`、支付 `4`、金额标记 `6`、十进制金额 `2`；四类形态与全局 `+0x48` 计数完全一致。
- 其它固定桶：`+0x48f966c` 和 `+0x48fb566` 分别命中服务刷新数据但没有收款或金额形态；其余固定 `CALL` 桶未命中，尾调用/其它桶也没有四类形态。
- 静态对应：`+0x1c43924` 是 `FUN_181c43510` 内 `CALL +0x1c43ef0` 的返回地址，说明完整摘要模型在该路径进入全局 ViewModel绑定函数。
- 当前结论：收款会话列表摘要的最高置信度结构化入口为 `Weixin.dll+0x1c43ef0`，限定调用返回地址 `+0x1c43924`，金额正文位于来源模型 `+0x48`。
- 下一验证：保持当前诊断运行，触发一笔新付款；若该桶的收款、支付、金额和十进制分类同步增加，即可转入生产解析实现。
- 隐私边界：本轮不保存摘要原文或金额值，只记录调用来源和聚合形态。

### WX-195 跟踪到账摘要 ViewModel 实时变化

- 显示形态确认：管理员确认微信会话列表更新为一条完整的“微信支付收款 x.xx 元”摘要，并附带场景说明；该形态同时包含支付、收款、金额单位和两位小数，与 `sessionModel+0x48` 的脱敏分类一致。
- 静态落点：`+0x1c43ef0` 首先通过 `FUN_181949b30(this+0x160, sessionModel)` 深拷贝完整会话显示模型，因此来源摘要 `sessionModel+0x48` 在长期存活的 `ChatSessionCellViewModel` 中对应 `this+0x1a8`。
- 诊断实现：仅在返回地址 `+0x1c43924` 且摘要同时满足收款、支付、金额单位和两位小数形态时登记 ViewModel；后台每 `250ms` 读取 `ViewModel+0x1a8`，只比较进程内哈希、长度和形态变化计数，不输出原文或金额。
- 生命周期修正：开发诊断停止流程在清除硬件断点和 VEH 后调用远程 `FreeLibrary` 卸载诊断 DLL，解决旧实现“已停止但文件仍被占用”导致无法热更新的问题；正式收款观察 DLL 的停止行为不变。
- 离线验证：MinGW Windows x64 全量构建通过；微信原生日志、监控核心、监控服务和登录逻辑共 `4` 个测试文件、`28` 项通过。
- 现场验证：诊断在登录前安装，原生 C++直接点击登录按钮后恢复 `205` 次会话绑定；目标返回地址桶仍为 `171` 次，成功登记 `2` 个摘要 ViewModel，连续读取无失败，基线变化计数为 `0`。
- 下一验证：保持当前诊断运行，触发一笔新付款；若 `trackedViewModels.changes` 与 `qualified` 同步增加，即证明无需停留在收款助手页面也能从显示模型实时取得到账摘要。
- 隐私边界：登记表只保留进程内地址和不可输出的瞬时哈希，状态仅报告数量、长度和形态计数；不落盘真实摘要、金额、付款方、账号、昵称或交易标识。

### WX-196 验证实时到账摘要变化

- 测试前基线：已登记 `2` 个完整到账摘要 ViewModel，`changes=0`、`qualified=0`、`readFailures=0`。
- 真实付款结果：管理员完成一笔新的小额付款后，`trackedViewModels.changes` 从 `0` 增至 `2`，`qualified` 同步从 `0` 增至 `2`，读取失败保持 `0`。
- 入口对照：构造绑定桶 `c1c43924` 保持 `171` 次不变，说明实时更新不是重新构造会话 ViewModel；全局绑定总数仅由其它会话刷新路径增加。
- 字段对照：全局 `sessionModel+0x48` 的收款、支付、金额单位和两位小数分类均同步增加，变化后的持久摘要长度仍处于受控范围。
- 当前结论：`ChatSessionCellViewModel+0x1a8` 是可持续读取的实时到账摘要字段，用户无需停留在微信收款助手页面；生产实现应从该结构化显示模型解析金额，并对同一笔更新产生的多次变化做去重。
- 隐私边界：测试证据只记录计数变化，不保存测试金额、摘要原文、付款方、账号、昵称或交易标识。

### WX-197 复核后续到账的绑定来源

- 现场时点：管理员在约 `16:59` 完成另一笔小额付款；诊断保持原进程、原 DLL 和原跟踪表连续运行，没有重启微信或重新登记基线。
- 入口增量：约 `17:00`，全局绑定从 `215` 增至 `231`，服务号家族绑定从 `11` 增至 `13`；`sessionModel+0x48` 的收款、支付、金额单位和两位小数形态各新增一次。
- 调用来源：固定构造桶 `c1c43924` 保持 `171` 次不变，新增的完整金额摘要落入 `cOther`；该桶的四类形态同步各新增一次，证明后续到账还存在未归入六个固定 `CALL` 返回地址的绑定来源。
- 长期对象结果：原先登记的两个 ViewModel 继续可读且读取失败为 `0`，但 `changes/qualified` 保持 `2/2`，没有覆盖这次更新。
- 判断修正：`ViewModel+0x1a8` 字段本身仍有效，但生产实现不能只登记来自 `+0x1c43924` 的对象；需要覆盖所有满足到账摘要形态的 `+0x1c43ef0` 绑定，并建立能区分历史恢复与实时新绑定的去重状态。
- 隐私边界：本轮仍只记录时间窗口、计数和调用来源分类，不保存实际金额、摘要原文、付款方、账号、昵称或交易标识。

### WX-198 向上追踪到账摘要的数据来源

- 目标修正：正式 Hook 不停留在最终 ViewModel 轮询，而是继续定位网络响应完成解包、转换为业务结构后进入会话模型的入口；仍不截获网络通信、不读取通知、不做 OCR或数据库解密。
- 可复现工具：新增 `TraceWechatReceiptSummaryUpstream.java`，从两个尾调用包装器、摘要模型构造器和四个订阅会话更新锚点出发，自动输出两层直接调用边并反编译所选函数。
- 尾调用来源：`+0x1c3d6aa` 和 `+0x490085d` 都直接尾调用 `+0x1c43ef0`，因此入口断点看到的是包装器上层返回地址并归入 `cOther`；这解释了后续到账没有进入六个固定 `CALL` 桶。
- 服务会话链：`FUN_1849032c0` 在 `+0x4903674` 调用 `FUN_1849006f0`，后者构造会话显示模型并从 `+0x490085d` 尾调用绑定入口；其上层 `FUN_184903110` 负责条件触发批量刷新。
- 构造链：`FUN_181c43510` 除原固定构造路径外，还被多个普通会话和服务会话刷新函数调用，进一步证明最终 ViewModel入口是汇聚点而不是唯一数据源。
- 当前断点：服务会话刷新主函数 `FUN_1848f7090` 通过函数表或回调注册进入，直接静态调用引用不足；下一步需要解析该函数指针所在表及注册者，再向上定位解包后回调和业务对象字段。
- 验证：Ghidra `12.1.4` 在既有只读分析工程中成功处理 `27` 个函数，脚本完成且项目保存成功。

### WX-199 排除网络句柄与会话刷新链

- 目标校正：上游入口必须位于网络响应完成解包之后、结构化业务对象刚生成并准备写入服务会话模型的位置；数据库恢复、网络传输句柄、通用会话刷新和最终 ViewModel 均不能作为正式收款入口。
- 已排除分派器：`FUN_1848f0e70` 是通用会话刷新分派器，内部调用 `FUN_1848fd550`；它汇聚多类会话更新，不是网络响应解包回调。
- 已排除状态写入：`FUN_184904ae0` 只把共享对象保存到状态对象 `+0x250/+0x258`，没有解析到账业务字段。
- 已排除网络句柄回调：`FUN_18490b260/FUN_18490b5c0/FUN_18490b920` 的参数属于 `mmcronet::TransferHandle` 或 Engine 关联对象，只负责切换传输句柄和触发刷新，不携带已解包的到账业务对象。
- 已排除坐标切换：`FUN_185f26070` 根据坐标和当前对象切换状态，虽然调用批次开始、结束包装器，但不是网络解包投递器。
- 服务会话容器：`FUN_1849032c0` 消费 `服务会话对象+0x138..+0x140` 的记录向量，单条记录大小为 `0x300`；`FUN_184902f70` 将 `+0x130` 置为批量更新中，`FUN_184903110` 清除标志并刷新模型。
- 下一定位点：动态命中 `FUN_184902f70` 时仅取得服务会话对象地址，随后对该对象 `+0x140` 安装 8 字节写入硬件断点；命中后只记录写入指令的模块相对地址、线程、顺序和次数，再从真实写入者静态向上追到解包回调。
- 隐私边界：动态诊断不读取 `0x300` 记录正文，不保存金额、摘要、账号、昵称、付款方、订单号、交易号或二维码内容，也不截获原始网络通信。

### WX-200 实现服务会话向量写入溯源诊断

- 独立构建：新增 `cfpay-hook-network-diagnostic.dll`，不修改正式观察 DLL 和现有会话摘要诊断的断点语义；宿主新增受限启动、停止命令，目标仅允许固定版本 `Weixin.exe/Weixin.dll` 和固定诊断文件名。
- 两阶段断点：槽 0 在 `Weixin.dll+0x4902f70` 命中时只读取 `RCX` 作为服务会话对象地址；槽 1 随后监控 `对象+0x140` 的 8 字节写入。每次对象变化立即向已有线程同步一次，其余每 `250ms` 只做线程覆盖巡检，避免高频挂起全部微信线程。
- 输出边界：写入事件只记录递增序号、批次计数、线程 ID、目标模块归属和 `postWriteRva`；不读取被写入值，也不读取向量中的 `0x300` 记录。
- 地址语义：x64 数据硬件断点在写指令执行后触发，`postWriteRva` 指向下一条待执行指令，不直接冒充写入指令；现场命中后必须静态反汇编其前序指令，确认真实写入者。
- 并存保护：网络溯源宿主拒绝在正式 `cfpay-hook-observer.dll` 或会话摘要 `cfpay-hook-diagnostic.dll` 仍加载时启动；停止流程清理断点和 VEH 后卸载独立诊断 DLL，命名管道异常断开也会触发同一异步清理路径。
- 自有进程验证：测试目标新增同布局 `+0x140` 写入函数；一次批次开始后连续两次写入均命中，事件顺序为批次 `1`、写入 `2/3`，停止状态为 `HOOK_STOPPED`。
- 反汇编校验：测试目标写指令位于模块 `+0x24d7`，硬件断点报告 `postWriteRva=+0x24de`，准确验证“执行后触发”的地址语义。
- 构建结果：MinGW Windows x64 全量构建通过；诊断 DLL 仅依赖 Windows `KERNEL32.dll` 和静态运行库。

### WX-201 排除批次开始函数并改用向量消费者取址

- 现场切换：旧会话摘要诊断已通过受限宿主清理断点并卸载；网络溯源诊断在原微信进程和一次受限重启后的新进程中均完成签名校验和安装。
- 登录验证：网络诊断在登录按钮点击前安装，原生 C++ 直接点击登录后微信恢复主界面并加载多个子进程，但 `FUN_184902f70` 的命中数始终为 `0`。
- 结论修正：`FUN_184902f70` 不能作为可靠的服务会话对象捕获点，更不能据此推断网络解包批次；该地址从动态候选中排除。
- 新取址点：改用已确认读取 `服务会话对象+0x138..+0x140` 且按 `0x300` 步长消费记录的 `FUN_1849032c0`。它只提供对象地址，网络来源仍必须由随后命中的 `+0x140` 写入者证明。
- 隐私边界：消费者入口不读取记录正文，仍只把 `RCX+0x140` 配置为数据写断点；输出字段由批次计数改为对象捕获计数。

### WX-202 动态定位结构化记录追加函数

- 登录恢复结果：消费者取址版诊断在原生登录恢复后累计捕获服务会话对象 `15` 次、`+0x140` 写入 `31` 次，队列丢弃为 `0`；所有写入都在同一微信工作线程内发生。
- 写入分型：`postWriteRva=+0x4903d2e` 对应前序指令 `+0x4903d27`，作用是把向量结束指针重置为起始指针，属于消费完成后的清空。
- 追加入口：`postWriteRva=+0x490d62a` 对应前序指令 `+0x490d61f`，作用是把 `服务会话对象+0x140` 增加 `0x300`，属于一条结构化记录追加完成。
- 函数语义：真实追加函数为 `FUN_18490d5c0(param_1, param_2, ...)`；`param_1+8` 指向服务会话对象，`param_2` 指向待追加的完整 `0x300` 记录。函数先构造记录主体、复制 `+0x290` 子对象和 `+0x2f8` 状态字段，再推进向量结束指针。
- 表误判修正：`FUN_18490d5c0` 的唯一静态数据引用 `18bd3d668` 位于 PE `.pdata`，内容是三元组“函数开始 RVA、函数结束 RVA、展开信息 RVA”，不是回调注册表。
- 下一定位点：直接在 `FUN_18490d5c0` 入口设置执行断点，从栈顶读取返回地址并按模块 RVA 记录真实间接调用者；仍不读取 `param_2` 的记录正文。

### WX-203 改为直接观察结构化记录追加入口

- 诊断收敛：移除消费者取址和 `+0x140` 数据写断点，仅保留 `Weixin.dll+0x490d5c0` 一个执行断点，降低线程调试寄存器占用和运行时干扰。
- 调用者取证：函数入口尚未执行序言，`RSP` 顶部是实际返回地址；诊断只读取该指针并转换为 `callerRva`，不访问 `RDX` 指向的 `0x300` 记录。
- 输出字段：事件类型改为 `decoded_record_append`，仅包含序号、追加次数、线程 ID、模块归属和调用者 RVA；健康状态报告追加次数、返回地址读取失败和队列丢弃。
- 版本保护：入口使用固定版 `FUN_18490d5c0` 前 `32` 字节签名，任一字节不匹配时拒绝安装。

### WX-204 排除一层通用订阅广播器

- 现场命中：原生登录恢复期间捕获两次结构化记录追加，栈顶返回地址均为 `Weixin.dll+0x0000d23f`；对应调用指令位于 `Weixin.dll+0x0000d23c`，形式为 `call *0x10(%rax)`。
- 静态确认：Ghidra 将调用点归入 `FUN_18000d0a0`。该函数遍历订阅者容器，并通过订阅对象虚表 `+0x10` 广播同一个结构化参数，属于通用信号分派器，不是网络响应解包函数。
- 调用者限制：该分派器存在大量直接尾调用包装器，单层返回地址无法区分数据库恢复、状态刷新或网络响应投递来源。
- 下一定位点：从 `FUN_18490d5c0` 入口异常上下文执行受限的 Windows x64 栈展开，只记录 `Weixin.dll` 模块相对返回地址链，用首个非通用上游函数继续定位网络解包后的业务回调。
- 隐私边界：不读取 `RDX` 指向的记录正文，不转储原始栈内存，不记录金额、摘要、账号、昵称、付款方、订单号或交易号。

### WX-205 实现受限的 Windows x64 调用栈展开

- 展开方式：结构化记录追加入口命中后，从异常 `CONTEXT` 调用 `RtlLookupFunctionEntry/RtlVirtualUnwind`；无运行时函数表的叶函数仅从当前受控栈顶读取一个返回地址。
- 输出限制：最多遍历 `32` 层，最多保留 `16` 个位于目标模块内的返回 RVA；事件新增 `callChainRvas`、`callChainDepth`、`walkedFrames` 和 `unwindComplete`，不输出模块外地址或原始栈字节。
- 兼容字段：`callerRva` 保持为调用链第一项，现有现场读取逻辑无需立即调整；健康状态新增 `unwindFailures` 计数。
- 自测调用链：测试目标新增“网络响应分派 -> 解包后投递 -> 结构化记录追加”两层不可内联包装。两次事件分别得到 `+0x24dc,+0x250c,+0x2784...` 和 `+0x24dc,+0x250c,+0x278e...`，与反汇编调用后地址一致。
- 自测结果：两次事件均为 `callChainDepth=5`、`walkedFrames=8`、`unwindComplete=true`，停止状态为 `HOOK_STOPPED`；Windows x64 全量构建通过。
- 下一定位点：现场替换独立诊断 DLL，捕获 `+0xd23f` 以上的模块内返回 RVA 链，再反编译首个非通用订阅包装器，确认其是否属于网络响应解析或反序列化回调。

### WX-206 捕获结构化记录追加的完整模块内调用链

- 现场切换：旧诊断先通过受限宿主清理断点并卸载；新 DLL 哈希与构建产物一致后加载到原 PID `8932`，安装状态为 `NETWORK_PROVENANCE_ACTIVE`。
- 自然命中：无需付款或界面操作，后台刷新产生一次结构化记录追加；直接返回地址仍为通用分派器后的 `+0x0000d23f`。
- 调用链：新增上游 RVA 为 `+0x00057772 -> +0x0194834b -> +0x019746d0 -> +0x001d1ed4 -> +0x001d1c70 -> +0x00f4cbd0 -> +0x003892f8 -> +0x00389289 -> +0x0063b583 -> +0x01618218 -> +0x01617d02 -> +0x006978a3 -> +0x0062e75f -> +0x0062e60b -> +0x0062e20f`。
- 运行质量：保留模块内帧 `16` 个，遍历 `32` 层后正常完成；返回地址读取失败、展开失败和队列丢弃均为 `0`。
- 结论边界：该链证明结构化记录追加的具体发布路径可被恢复，但尚未证明其中哪个函数负责网络响应反序列化；下一步必须逐层反编译并用网络/协议对象交叉引用确认。

### WX-207 排除未证明网络来源的后台追加链

- 记录构造：`FUN_181948200` 从主对象、`+0x290` 子对象和 `+0x2f8` 状态构造统一 `0x300` 记录，再经通用包装器发布。
- 批量入口：`FUN_181974590` 遍历记录指针向量，对每个非空记录调用 `FUN_181948200`；它位于 `0x189106cd8` 的回调操作表，由 `FUN_181946cd0` 构造，并由大型服务构造函数 `FUN_18195b500` 持有。
- 上游分型：更高调用帧属于通用异步任务、定时器与 Windows 窗口消息分发，最终经过 `DefWindowProcW`；当前静态锚点没有出现网络响应、传输解码、Protobuf 或反序列化符号。
- 结论修正：自然后台刷新只证明“已有一批结构化源记录被统一转换并发布”，不能证明来源是网络解包。该链仅作为后续真实付款调用链的对照基线，禁止直接选为正式收款 Hook。
- 下一验证：保持诊断运行，暂停自动推进并等待管理员完成一笔新付款；付款到达后比较 `callChainRvas`，优先追踪仅在实时到账出现的上游分支。

### WX-208 用真实付款排除异步批量发布链

- 付款前基线：独立网络溯源诊断报告 `recordAppends=53`，返回地址读取失败、调用栈展开失败和队列丢弃均为 `0`。
- 付款后结果：管理员确认付款后，约 `18:37:46` 追加计数增至 `55`；两次新增事件的十六层 `callChainRvas` 与后台基线完全一致。
- 结论：实时到账确实进入 `FUN_181974590 -> FUN_181948200 -> 通用订阅广播 -> FUN_18490d5c0` 链，但网络解析调用栈已在异步投递处断开。
- 正式排除：该链只能作为“已存在业务记录被批量转换和发布”的证据，不能作为网络解包后入口，也不能用于证明记录来自实时网络而非本地恢复。
- 隐私边界：仍只记录计数、线程和模块相对返回地址，不读取或保存记录正文、金额、摘要、账号、昵称、付款方或交易标识。

### WX-209 锚定 NewSync 网络与 Protobuf 路径

- 网络锚点：只读 Ghidra 扫描确认 `/cgi-bin/micromsg-bin/newsync` 被 `FUN_18067fee0`、`FUN_182e2fbb0` 和 `FUN_182e324a0` 直接引用。
- Protobuf 锚点：`micromsg.NewSyncResponse` 被 `FUN_181070420` 引用；该函数只构造类型名。`NewSyncStart` 被 `FUN_182849c30` 引用，用于性能或阶段记录。
- 初步分型：`FUN_18067fee0` 属于字符串和元数据注册，`FUN_182e324a0` 构造带 NewSync URI 的请求；`FUN_182e2fbb0` 包含请求执行、重试、错误处理和对 `FUN_182e30aa0` 的响应处理调用，是当前最强的解包后候选上游。
- 排除项：资源字符串 `id-it-subscriptionResponse` 存在于 `.rdata`，但没有任何代码引用，不能作为订阅协议入口。
- 工具记录：新增三个可复现 Ghidra 脚本，分别批量反编译服务回调、验证订阅资源引用和追踪 NewSync 网络锚点；NewSync 扫描共访问 `5` 个锚点并成功反编译 `5` 个函数。
- 下一步：展开 `FUN_182e2fbb0/FUN_182e30aa0/FUN_18106e570/FUN_18106e990` 的调用者、被调用者和数据流，定位 `NewSyncResponse` 解析完成后、`CmdList/AddMsg` 分发前的结构化对象入口。

### WX-210 定位 NewSyncResponse Protobuf 解析器

- 响应对象：`FUN_18106e570` 构造约 `0x50` 字节的 `micromsg.NewSyncResponse` 对象，虚表为 `0x188fc0cc8`；`FUN_18106e990` 进入析构/清理路径，`FUN_18106f690` 合并或复制字段。
- 解析入口：虚表槽 `7`、偏移 `+0x38` 指向 `FUN_18106eb90`，模块 RVA 为 `+0x106eb90`。函数按 Protobuf wire tag 读取字段 `1..9`，并直接更新对象字段和 `+0x48` 存在位图。
- 相邻语义：`FUN_18106f480` 计算序列化长度，`FUN_18106f300` 写出字段，反向印证 `FUN_18106eb90` 是反序列化入口而非普通业务复制函数。
- 请求链修正：`FUN_182e2fbb0` 创建响应对象并发起请求；`FUN_182e30aa0` 负责异步状态和完成结果提取，不是网络 wire parser。
- 工具修正：生成函数表实际由 32 位 image-relative RVA 组成；专用脚本按 4 字节解析，避免把相邻 RVA 错拼为 64 位地址。脚本共识别并反编译 `39` 个可执行目标。
- 下一验证：停止旧的结构化记录追加诊断，将唯一执行断点切换到 `Weixin.dll+0x106eb90`，仅记录命中计数和受限模块调用链；先等待自然 NewSync 流量，无需管理员付款。

### WX-211 实现 NewSyncResponse 解析入口诊断

- 固定入口：独立网络诊断的唯一硬件执行断点从 `+0x490d5c0` 切换到 `Weixin.dll+0x106eb90`，并使用 Ghidra 导出的前 `32` 字节签名拒绝不匹配版本。
- 输出语义：事件改为 `newsync_response_parse`，计数改为 `parseCount`；仍只输出线程、模块相对调用链、展开层数和完成状态，不读取 `RCX` 响应对象或 `RDX` 解析流。
- 状态语义：健康码改为 `NEWSYNC_PARSER_ACTIVE`，说明文字使用中文；周期状态从每 `1` 秒调整为每 `5` 秒，降低连续重复上报。
- 自测入口：自有 Windows 测试目标新增 `CfpayTestNewSyncResponseParse` 和两层不可内联调用包装，保持调用链可核对。
- 自测结果：两次解析命中均得到 `5` 个目标模块 RVA、遍历 `8` 层且 `unwindComplete=true`；停止后输出 `HOOK_STOPPED`，未出现读取失败、展开失败或队列丢弃。
- 构建结果：MinGW Windows x64 全量构建通过；网络诊断 DLL 只依赖系统 `KERNEL32.dll` 和 `msvcrt.dll`。
- 下一步：停止 PID `8932` 中仍运行的旧诊断，替换为本构建并等待自然 NewSync 命中；命中后静态反编译第一层通用解析调用者。

### WX-212 修正登录后新建网络线程的断点覆盖竞态

- 首轮现场：解析探针在登录前安装且原生登录按钮点击成功，微信主进程和四个子进程均恢复，但 `parseCount` 保持 `0`。
- 竞态判断：登录触发的 NewSync 网络线程可能在点击后新建，并在原 `250ms` 线程巡检首次覆盖前完成第一次响应解析；已有线程均在点击前完成断点配置。
- 实现调整：首次解析命中前把线程覆盖巡检缩短到 `25ms`；一旦 `parseCount>0` 自动恢复 `250ms`，限制持续挂起线程的频率和运行时干扰。
- 回归结果：自有测试目标仍捕获两次解析，调用链深度 `5`、遍历 `8` 层、展开完整，停止清理成功。
- 下一步：用更新后的 DLL 重启到登录页，在点击前安装探针并执行第二轮原生登录验证。

### WX-213 修正 NewSync 响应类型并增加 AddMsg 结构诊断

- 旧结论纠正：后续虚表表项、函数签名和现场调用约定确认，`NewSyncRequest` 与 `NewSyncResponse` 使用相邻但不同的生成表；旧记录中的 `+0x106eb90` 属于请求侧路径，不能作为响应解析入口。
- 当前响应入口：`NewSyncResponse.vtable=0x188fc0d48`，解析器为 `Weixin.dll+0x106fb10`，响应解析返回采样点为 `Weixin.dll+0x680fcc`；对象约 `0x38` 字节，存在位图位于 `+0x30`。
- 命令列表：响应对象的命令列表包含数量和命令对象指针；现场静态表明 `CmdID=5` 对应 `micromsg.AddMsg`。AddMsg 虚表为 `0x188fc0f38`，解析入口为 `Weixin.dll+0x1072ab0`；当前只读取 `messageType` 与 `presentMask`。
- 隐私边界：诊断不读取原始网络缓冲区、消息正文、金额、付款方、昵称、账号、订单号或交易号；只记录解析状态、命令 ID、消息类型、存在位图和受限模块相对调用链。
- 自测结果：`./tools/build-wechat-hook-harness.sh` 构建成功；Windows 自有测试目标连续产生 `newsync_response_parse` 两次和 `addmsg_parse` 两次，命令 ID 为 `[5,17]`，AddMsg 类型为 `49`，停止状态为 `HOOK_STOPPED`。
- 现场结果：新 DLL 哈希为 `889087c59607b053557f4816c45cf55d7504c7a2f8134ac1d6e12f9815d91721`，已加载到主微信 PID `4740`，状态为 `NEWSYNC_PARSER_ACTIVE`；停止前累计 `parseCount=15`，其中多次响应包含 `CmdID=5`，`callerReadFailures=0`、`unwindFailures=0`、`queueDrops=0`，但 `addmsg_parse` 命中数为 `0`。因此管理员刚才确认的付款仍不能归因于具体 AddMsg 事件，必须继续做付款前后对照。
- 结论边界：当前已定位到网络解包后的 NewSync/AddMsg 解析层，但尚未证明 `messageType=49` 对应收款消息，也未将该诊断改造成正式收款队列入口。

### WX-214 修订旧 NewSync 证据记录

- 文档修订：同步修正 `docs/wechat-native-observer.md`、`work/wechat-receipt-preview/evidence/E-static-session-preview.md` 和本日志中旧的解析器地址、对象布局与下一步描述。
- 追溯原则：不改写历史 Git 提交；通过新增本条修订记录保留错误结论的来源、纠正依据和当前可复现证据。

### WX-215 付款后补抓未命中 AddMsg

- 现场动作：管理员再次确认已支付后，立即在未重启微信的主进程 `PID 4740` 上重新安装网络诊断 DLL，观察后通过受限宿主正常停止。
- 补抓结果：本窗口捕获 `3` 次 `NewSyncResponse`，其中 `1` 次包含 `CmdID=5`；调用链均为完整的 `16` 个模块内 RVA，读取失败、展开失败和队列丢弃均为 `0`。
- 关键结果：本窗口 `addmsg_parse` 命中数仍为 `0`，因此不能把本次付款和 `AddMsg` 解析事件建立因果关联；后续同步可能只返回空命令或后台状态批次。
- 本机投递：`%LOCALAPPDATA%/CFPay/wechat-monitor-state.json` 显示 `pending=0`、`delivered=0`；当前没有发现对应的生产观察器收款投递记录。
- 清理结果：诊断断点和 DLL 已清理，微信进程仍存活；没有修改微信、数据库或网络通信。
- 下一步：必须在诊断已安装并输出 `NEWSYNC_PARSER_ACTIVE` 后，由管理员发起一笔新的小额付款，再比较付款前后 `NewSync`、`CmdID=5` 和 `AddMsg` 三组事件。

### WX-216 AddMsg 调用者扫描与网络路径边界

- 静态工具：新增 `FindWechatAddMsgCallers.java`，统一枚举 AddMsg 构造、解析、析构函数、虚表和生成表引用，并尝试息直接 `CALL/JMP` 调用者。
- 追踪扩展：`TraceWechatNewSyncResponseDispatch.java` 补充 `0x182e7d480`、`0x183a93ef0`、`0x181072a30` 和 `0x181072ab0`。
- 结论：AddMsg 解析器和构造器只发现虚表、生成表和数据引用，没有找到可直接作为业务分发器的普通调用者；`CmdID=5` 的后续分型仍可能经过间接函数表。
- 生产决策：不将当前 AddMsg 解析器误作已验证的收款入口；已经用真实收款验证的会话摘要绑定路径作为下一个生产实现候选。
- 隐私边界：本轮只做静态 Ghidra 分析，不启动、停止或附加实际微信，不读取网络原文、数据库或消息正文。

### WX-217 生产收款 Hook 切换到会话摘要链

- 入口替换：生产观察 DLL 的收款槽位由 `Weixin.dll + 0x2293980`（`wcpayinfo` 反序列化器，已多次真实收款零命中）改为已经真实收款验证的全局会话摘要绑定入口 `Weixin.dll + 0x1c43ef0`。
- 字段映射：入口 `RDX` 为来源会话模型，读取 `sessionModel + 0x48` 摘要文本；入口 `RCX` 为长期存活的 `ChatSessionCellViewModel`，其 `+0x1a8` 是同一摘要的复制字段，用于后续轮询变化。
- 双路径捕获：绑定入口命中即视为一次到账信号，摘要同时满足“收款/支付/金额单位/两位小数”四类形态时直接生成收款事件；同时登记 ViewModel 并每 250 毫秒轮询 `+0x1a8`，摘要哈希变化且形态合格时补发事件。这样既覆盖“已有摘要被原地改写”，也覆盖“新绑定携带到账摘要”，避免第二笔相同金额因文本未变而漏报。
- 启动抑制：观察器启动后 30 秒内的绑定只登记基线、不生成事件，避免登录恢复期批量历史会话摘要被误判为新收款。
- 去重窗口：同一摘要哈希在 8 秒内只生成一个事件；事件标识仍为 `session-summary-<摘要哈希>-<秒级时间>` 的 SHA-256，交由本机持久化队列和 Worker 二次去重。
- 账号补齐：会话摘要不含收款账号，监控端在 `source=session_summary` 时用本机已确认账号补齐 `receiverUsername`；未确认账号时上报 `DEGRADED / RECEIVER_CONFIRMATION_REQUIRED`，不入支付队列。
- 状态口径：Hook 状态偏移上报改为 `0x1c43ef0`，观察 DLL 运行时 API 版本提升到 `0x00070000`，健康指标新增 `summaryChanges` 与 `summaryPayments`；账号与昵称观察点保持不变。
- 测试兼容：自有测试目标仍走原结构化收款入口/返回路径（`g_testMode` 分支），既有命名管道回归的三条收款事件语义不变。
- 构建与验证：MinGW Windows x64 全量构建通过；`tests/monitor-status.test.ts`、`tests/wechat-monitor-service.test.mjs`、`tests/wechat-monitor-core.test.mjs` 共 21 项通过。
- 现场部署：新 `cfpay-wechat-monitor.exe` 与 `cfpay-hook-observer.dll` 已复制到 `%LOCALAPPDATA%\CFPay\WeChatMonitor`，旧二进制备份在 `backup-0.8.1-parser`；监控端从 Windows 本机路径启动，未通过 WSL 调用 PowerShell。
- 现场状态：观察 DLL 已加载到微信主进程 `PID 4740`，Worker 收到 `HEALTHY / HOOK_ACTIVE`，登录状态 `LOGGED_IN`，已确认收款账号生效；等待一笔真实小额收款验证端到端匹配。
- 隐私边界：不读取通知、不做 OCR、不解密数据库、不截获原始网络密文；调试日志只记录金额分值、事件来源和字节数，不记录摘要原文、付款方或交易号。

### WX-218 会话摘要生产链路实机收款验证成功

- 部署：新观察 DLL（API `0x00070000`）与监控端复制到 `%LOCALAPPDATA%\CFPay\WeChatMonitor`，旧二进制备份在 `backup-0.8.1-parser`；因禁止原地换版，先完整退出微信再启动新进程。
- 第一笔（无存活订单）：`20:54:27` 到账，`20:54:27.440` 捕获、`20:54:27.445` 入队，捕获到入队仅 5 毫秒；来源 `source=session_summary`，金额 `0.01` 元。因当时没有存活订单，Worker 记为未匹配收款，符合预期。
- 第二笔（端到端）：新建易支付订单 `wx-live-1790600355-1612`（`0.01` 元），`21:01:29.572` 捕获、`21:01:29.574` 入队、`21:01:32.289` 上报 `HTTP 200`；管理端订单状态 `PAID`、`paid_at=1790600489000`，易支付查询接口返回 `status=1`，闭环成立。
- 组件状态：Worker 侧 `wechat_hook` 为 `HEALTHY / HOOK_ACTIVE`，`hook_offset=0x1c43ef0`，`process_id` 与本机一致，`last_event_at` 等于到账时间。
- 网络异常：`20:54` 与 `21:14` 前后出现多次 `winError=12002`（WinHTTP 超时），期间心跳时好时坏；本机持久化队列保留事件并在网络恢复后自动补投，`21:23` 起全部上报恢复 `HTTP 200`。该时段 Windows 直连与代理访问 Cloudflare 均失败、访问百度正常，判定为出口网络抖动而非本项目缺陷。
- 发现的缺陷：同一笔 `21:01` 收款在 `21:03:00` 又产生一条 `0.01` 事件并上报成功，因无存活订单而落入未匹配收款。重复事件可能在未来误匹配同金额订单，必须收敛。

### WX-219 收款事件来源路径与脱敏摘要骨架

- 事件字段：会话摘要收款事件新增 `path`（`binding` 表示绑定入口直接捕获，`poll` 表示 ViewModel 轮询变化捕获）和 `summaryShape`（摘要骨架）。
- 脱敏方式：骨架把连续数字整体替换为 `#`，保留中文与标点，最长 96 字节；只用于判断摘要结构（是否含时间、是否含条数），不记录金额原文、付款方、账号或交易号。
- 基线转储：观察器启动抑制期结束后，对已登记且形态合格的 ViewModel 各输出一条 `summary_debug` 记录，监控端只写本机中文日志，不上报 Worker。
- 自适应去重：骨架含时间标记（`#:#`、`#-#`、今天、昨天、星期、月）时去重窗口为 8 秒；不含时间标记时摘要文本在同金额重复收款场景下完全相同，去重窗口提升到 120 秒，用于压制 `WX-218` 中 91 秒后的重复投递。
- 日志增强：`HOOK_ACTIVE` 时把观察 DLL 原生指标写入本机调试日志（`receiptEntries`、`summaryChanges`、`summaryPayments`、`accountEntries`、`readFailures`、`queueDrops`、`threads`），上报给 Worker 的 `message` 保持固定文案以免计数器变化破坏状态去重。

### WX-220 修复登录失败时的微信无限重启循环

- 现象：`21:15`、`21:16`、`21:17`、`21:18` 连续四次“微信受限重启已完成”，每约 50 秒强杀并重启微信一次，与文档中“一次监控端运行期间最多自动重启一次”不符。
- 根因：登录辅助线程在重启成功后立即把 `restartAttempted` 复位为 `false`，下一轮又满足“登录页 + 未点击 + 未重启”条件，于是点击登录、等待 20 秒、再次重启，形成闭环。
- 修复：重启成功后保持 `restartAttempted` 闭锁；新增运行内计数 `autoRestartCount` 与上限 `kMaxAutoRestartPerRun = 1`，达到上限只上报 `LOGIN_REQUIRED` 并写中文日志，不再点击或重启；只有观察到有效登录证据时才同时清除闭锁与计数。
- 验证：修复版启动后本轮自动重启次数为 `0`，一次登录按钮调用即进入 `LOGGED_IN` 与 `HEALTHY / HOOK_ACTIVE`，微信主进程 `PID 23756` 在随后两分钟内保持稳定，未再被结束。

### WX-221 去重修复后的实机收款复验

- 摘要骨架：脱敏骨架首次落地为 `微信支付收款#.#元(老顾客到店[朋友])`，数字已整体掩码；骨架不含时间标记，因此自适应去重窗口取 120 秒分支。
- 本轮事件：`21:24:42` 捕获 `0.02` 元、`21:25:31` 捕获 `0.01` 元，均为 `source=session_summary`、`path=binding`；捕获到入队分别 4 毫秒和 11 毫秒，上报分别 0.9 秒和 1.5 秒完成，均 `HTTP 200`。
- 订单匹配：`0.01` 元事件匹配订单 `wx-live-1790601896-1221`，管理端状态 `PAID`、`paid_at=1790601931000`，易支付查询接口返回 `status=1`；`0.02` 元事件因无同金额存活订单，正确落入未匹配收款。
- 重复抑制：`21:25:31` 之后持续观察 2.7 分钟，未再出现新的捕获记录，`summaryPayments` 保持 `2`、`summaryChanges` 保持 `4`；对比 `WX-218` 中 91 秒后的重复投递，重复事件已消除。轮询变化次数多于发事件次数，说明去重窗口按预期吸收了同一次到账的多次界面刷新。
- 组件状态：Worker 侧 `wechat_hook` 为 `HEALTHY / HOOK_ACTIVE`，组件版本 `0.8.1`，`hook_offset=0x1c43ef0`，`process_id=23756`，`last_event_at` 与到账时间一致。
- 登录稳定：修复后本轮自动重启次数为 `0`，微信主进程 `PID 23756` 全程未被结束。
- 遗留边界：摘要文本不含时间，因此同金额收款在 120 秒去重窗口内只会产生一个事件；观察器启动后 30 秒抑制期内由绑定入口捕获的到账不发事件，但轮询路径仍可在摘要变化时补发。未匹配收款只在事件写入时尝试匹配，不会在后续被历史事件追溯匹配。
- 网络说明：本轮验证期间 Cloudflare 出口恢复正常；`winError=12002` 时段的事件由本机持久化队列保留并在恢复后自动补投，未出现丢失。

### WX-222 停用展示文案匹配并新增结构化记录槽位转储诊断

- 需求修正：Hook 服务端只承担 Hook 与结构化字段读取，收款判定不得依赖 `微信支付收款#.#元` 这类展示文案；文案匹配只是过渡手段。
- 生产收敛：观察 DLL 增加编译期开关 `kEnableSummaryTextPayment = false`，绑定入口与轮询路径都不再基于关键词和金额形态生成收款事件；`summaryChanges` 计数保留用于观察，`summaryPayments` 恒为 `0`。
- 新诊断目标：新增 `native/wechat-observer/record_dump_observer.cpp` 与构建目标 `cfpay-hook-record-diagnostic.dll`，唯一硬件执行断点位于已被真实付款验证会命中的结构化记录追加函数 `Weixin.dll + 0x490d5c0`，入口 `RDX` 指向待追加的 `0x300` 记录。
- 转储内容：按 8 字节步长尝试以 MSVC `std::string` 解释记录内槽位，只输出偏移、字节数和掩码骨架（数字转 `#`、ASCII 字母转 `a`，保留中文与标点，最长 96 字节）；同时读取 `+0x290` 的嵌套显示模型并做同样扫描，标记 `nested=true`。
- 输出边界：转储只写本机 JSONL 文件，不进入 Worker，不记录账号、交易号、金额或摘要原文；上限为 400 次命中和 20000 条槽位记录，超限只计数。
- 宿主模式：单 EXE 内部宿主新增 `--run-weixin-record-diagnostic <pid> <dll>` 与 `--stop-weixin-record-diagnostic <pid>`，沿用固定版本 `Weixin.exe/Weixin.dll` 校验、固定 DLL 文件名白名单、与正式观察器及其它诊断互斥、停止时清理断点并卸载 DLL 的既有约束。
- 稳定性修正：初版把首次命中前的线程巡检设为 25 毫秒，现场出现工作线程停止输出（3 分钟无状态行）；判断为高频挂起全部线程与堆锁互等，改为固定 250 毫秒后恢复正常输出。
- 会话摘要诊断复用问题：旧会话摘要诊断在同一进程停止时返回 `HOOK_STOP_FAILED` 且 DLL 驻留，其按地址清理 `DR0` 的行为会连带清掉同地址的其它诊断断点（曾导致 `viewModelBind` 恒为 0）；因此每轮诊断都从干净微信进程开始。
- 现场状态：结构化记录转储诊断已安装在微信主进程 `PID 23868`，登录由原生单次点击完成，`RECORD_DUMP_ACTIVE`、覆盖 77 个线程；空闲状态下 `recordAppends=0`，说明该函数只在服务号会话真实刷新时调用，需要一笔真实到账触发。

### WX-223 修复记录转储输出链路

- 阻塞定位：首版转储经命名管道交由宿主转发到 WSL 挂载文件，登录完成后工作线程停止输出（3 分钟无状态行），判定为转发链路阻塞导致 DLL 写入卡住。
- 输出改造：诊断 DLL 改为直接写本机文件并逐行 `FlushFileBuffers`，命名管道只保留注入握手与失败原因回报；宿主转发链路不再是转储的必经路径。
- 路径缺陷：`swprintf(L"%s...")` 在本工程宽字符调用中把 `%s` 当窄字符串处理，路径被截断为 `C`，转储文件被写到进程当前目录；改为 `wcslen + memcpy` 拼接 DLL 自身目录与固定文件名后路径正确。
- 失败可观测：配置阶段每一步失败都通过管道回报固定码与 `winError`，覆盖 `PROCESS_NOT_FOUND`、`SIGNATURE_MISMATCH`、`LOCALAPPDATA` 解析失败、DLL 自身路径解析失败、路径过长、转储文件创建失败和断点安装失败。
- 重试增强：命名管道连接重试从 100 次提升到 300 次，避免注入与宿主建管道的时序竞争导致误报未连接。
- 现场状态：转储文件 `C:\Users\user\AppData\Local\CFPay\WeChatSlotDump\record-dump.jsonl` 持续输出，微信主进程 `PID 18000` 已登录，覆盖 111 个线程；空闲状态 `recordAppends=0`，确认记录追加函数只在服务号会话真实刷新时调用，需一笔真实到账触发。
- 遗留清理：路径缺陷期间在微信进程当前目录生成的 `Crecord-dump.jsonl` 属无害空文件，后续人工清理，不影响微信运行。

### WX-224 结构化记录槽位与字段类型发现

- 触发条件：真实到账后记录追加函数 `Weixin.dll + 0x490d5c0` 命中 11 次，转出 58 条字符串槽位记录，读取失败与队列丢弃均为 0。
- 会话记录结构（`0x300`）：`+0x000` 为会话内部标识，`+0x048` 为会话摘要文案，`+0x0a8` 为会话账号（服务号形如 `gh_` 加 12 位字符，共 15 字符），`+0x0c8` 为会话显示名，`+0x140` 为跳转链接，`+0x160` 为会话类型标签或显示名。
- 收款助手识别：到账记录中 `+0x0a8` 与另一条资料记录的 `+0x000` 是同一个 15 字符服务号账号，`+0x0c8`/`+0x160` 为“微信收款助手”，`+0x160` 同时出现“服务号”类型标签；因此收款会话可由结构化账号标识判定，不依赖摘要文案。
- 现存缺口：摘要文案 `微信支付收款#.##元(老顾客到店[朋友])` 位于 `+0x048`，本轮字符串扫描没有发现独立的金额或交易号字符串槽位，需继续确认数值字段。
- 诊断扩展：记录转储新增数值字段发现，按 4 字节步长输出时间戳形态（秒级或毫秒级且落在当前时间前后窗口内）与 1 到 1000 万之间的 32 位整数候选，单条记录最多 60 项，仍只写本机文件。
- 现场状态：新诊断已装入原微信主进程 `PID 18000`，旧诊断按受限宿主命令清理断点并卸载后再注入，未重启微信。

### WX-225 生产收款链路改为结构化会话记录捕获

- 结论前提：`0x300` 服务会话记录中不存在金额数值、交易号或时间戳字段；全部 `i32` 候选都是 `std::string` 的长度与容量元数据（例如摘要槽 `+0x048` 的长度位于 `+0x058=50`、容量位于 `+0x060=63`）。业务字段只有会话账号、显示名、类型标签、摘要文案和跳转链接。
- Hook 定位：生产观察器 `DR0` 由会话摘要绑定入口 `0x1c43ef0` 改为已被两次真实到账验证的结构化记录追加入口 `Weixin.dll + 0x490d5c0`；磁盘文件偏移 `0x490c9c0`，入口前 32 字节签名与固定 DLL 的 SHA-256 一并校验，观察 DLL API 版本提升到 `0x000a0000`。
- 职责边界：观察 DLL 只做 Hook 与结构化字段读取，原样转发 `primaryId(+0x000)`、`altId(+0x0a8)`、`nameA(+0x0c8)`、`nameB(+0x160)`、`summary(+0x048)`；关键词判断、金额解析、去重、账号校验和上报全部移到监控端，DLL 内不再存在任何文案匹配逻辑。
- 触发条件：监控端只在 `primaryId` 或 `altId` 等于配置的 `wechatReceiptServiceId` 时才处理记录；本机实测收款助手服务号账号为 `gh_f0a92aa7146c`，会话类型标签为“服务号”，显示名为“微信收款助手”。
- 金额来源：仅按数值形态从摘要中提取（整数 1-8 位、小数 1-2 位、后面不得紧跟数字），不检查“收款/支付/元”等关键词；无小数金额则忽略该记录。
- 事件标识：`wechat:` 加 `服务号账号|摘要|120 秒窗口序号` 的 SHA-256；同一笔到账在窗口内产生的多次记录追加会自然折叠为一个事件，由本机持久化队列与 Worker 双层去重。
- 未配置服务号时：只把发现的候选写入本机状态文件 `receiptServiceCandidates`（最多 10 条，按最近出现排序），并上报 `DEGRADED / RECEIPT_SERVICE_CONFIRMATION_REQUIRED`，不进入支付队列。
- 链接槽验证：`+0x140` 的 85 字节跳转链接在 `0.01` 元与 `0.37` 元两次到账中掩码形态完全一致，证明与金额无关；对掩码占位字符做累积 XOR 得到的数值不构成金额编码，含金额的摘要槽 XOR 为 `40` 而非分值 `37`。
- 时间来源：记录内无结构化时间字段，事件时间使用捕获时刻，误差在本机观察到到账的秒级范围内。

### WX-226 结构化收款链路 0.41 元端到端验证

- 触发证据：`23:05:33.445` 捕获服务会话记录，`altIdBytes=15` 命中配置服务号 `gh_f0a92aa7146c`，`summaryBytes=50`；监控端按数值解析得到 `amountFen=41`，`23:05:33.449` 入队。
- 去重证据：同一笔到账在 `23:05:33.502` 触发第二条记录（`primaryIdBytes=15`），事件标识落在同一 120 秒窗口桶，日志记录“服务会话收款已去重”，最终只上报一条事件。
- 上报结果：`HTTP 200`，本机队列 `pending=0`，`delivered` 由 5 增至 6。
- Worker 结果：订单 `wx-live-1790607886-1394` 状态 `PAID`、`paid_at=1790607933445`；易支付查询接口返回 `status=1`、`money=0.41`；未匹配收款仍为历史 3 条，没有新增重复事件。
- 组件状态：`wechat_hook` 为 `HEALTHY / HOOK_ACTIVE`，`hookOffset=0x490d5c0`，进程与本机一致。
- 语义修正：`lastEventAt` 只在事件真正入队时更新，去重记录不再刷新最后收款时间。
- 代码边界复核：生产观察 DLL 中已不存在关键词或文案匹配代码，`containsText` 与形态分类只编译进 `CFPAY_DIAGNOSTIC_BUILD` 研究诊断；已删除失效的 `kEnableSummaryTextPayment` 常量。
- 干净重部署：替换观察 DLL 与监控端后完整退出并重启微信，新微信进程 `PID 8032` 由原生按钮登录自动进入主界面，`HEALTHY / HOOK_ACTIVE` 恢复，登录恢复期即捕获 7 条服务会话记录，本轮自动重启次数为 0。
- 候选发现：未配置服务号时，本机状态文件已记录 5 条候选（含群号、`wxid`、`brandsessionholder`、`gh_` 服务号），可用于管理员确认后写入 `wechatReceiptServiceId`。

### WX-227 停用文本拆金额并转向网络解包层定位结构化字段

- 需求确认：生产链路禁止从展示文案拆取金额；`parseSummaryAmount` 及全部关键词/数值文本解析代码已从监控端删除，命中收款服务号时只记录 `STRUCTURED_AMOUNT_SOURCE_REQUIRED`，不再生成收款事件。
- 当前能力边界：结构化触发（服务号账号 `gh_f0a92aa7146c`）保留并验证有效；在找到结构化金额来源前，微信收款不会上报，这是刻意的功能收敛而非缺陷。
- 新诊断：新增 `native/wechat-observer/sync_command_observer.cpp` 与构建目标 `cfpay-hook-sync-diagnostic.dll`，唯一执行断点在 `NewSyncResponse` 解析入口 `Weixin.dll + 0x106fb10`，按线程在真实返回地址安装第二个断点，返回后读取已解包响应对象。
- 转储内容：响应头（`retCode`、`continueFlag`、`status`、`serverTime`）、命令列表逐项 `CmdID`、命令对象虚表 RVA、载荷指针与归属（module/heap/null）；对堆载荷对象继续扫描 `0x140` 字节，输出字符串槽位掩码骨架、时间戳形态（秒/毫秒）与 1 到 1000 万的 32 位整数候选。
- 宿主模式：单 EXE 内部宿主新增 `--run-weixin-sync-diagnostic <pid> <dll>` 与 `--stop-weixin-sync-diagnostic <pid>`，沿用固定目标校验、DLL 文件名白名单、与正式观察器及其它诊断互斥、停止时清理断点并卸载的约束。
- 输出方式：转储直接写诊断 DLL 同目录的 `sync-dump.jsonl` 并逐行落盘，命名管道只用于握手与失败原因回报，避免转发链路阻塞工作线程。
- 基线结果：登录后自然流量已捕获 `8` 个响应、`11` 条命令，`CmdID` 覆盖 `1/5/23/44`，命令对象与载荷对象虚表同为 `module+0x8e3d058`，读取失败与队列丢弃均为 `0`，覆盖 `220` 个线程。
- 关键线索：`CmdID=5` 的某个堆载荷在 `+0x60` 处存在 `282` 字节字符串，掩码骨架呈现 XML 标签结构，说明命令载荷携带的是已解包的消息协议体，可作为结构化字段的来源；`CmdID=5` 此前 AddMsg 解析器零命中的现象与“载荷对象由通用包装类持有、按命令类型延迟解析”的结构一致。
- 隐私处理：为确认协议体是否存在命名字段，新增 `payload_text` 记录，仅写本机文件、不上报 Worker，字段确认完成后删除转储文件；掩码骨架与全文转储都不进入生产观察器。

### WX-228 定位 paymsg 结构化支付通知并接入生产链路

- 载荷布局还原：Ghidra 反编译确认命令对象类型为 `micromsg.CmdItem`（`0x20` 字节：虚表、`+0x08` 载荷对象、`+0x10` cmdid、`+0x18` 存在位），载荷对象 `+0x08` 指向 `std::string`，字符串对象 `+0x10` 为长度、`+0x18` 为容量，容量不小于 16 时 `+0x00` 才是数据指针；空串哨兵为 `Weixin.dll + 0xb916de0`。上一版少解一层引用，因此转储到的是字符串对象头而非协议字节。
- 协议字段：真实到账时 `CmdID=5` 载荷解出的消息 protobuf 为 `field1` 客户端消息号、`field2/3` 收付双方、`field4=10002`（sysmsg）、`field5.1` 内容体、`field9` 创建时间（Unix 秒）、`field12` 服务端消息号、`field13` 序列号。
- 内容体结构：`<sysmsg type="paymsg">` 内含 `PayMsgType=9`、`transid`（唯一交易号）、`username`（付款方 wxid）、`displayname`、`timestamp`（秒级）、`fee`（**整数分**，实测 0.49 元对应 `49`）、`feetype=1`、`type=wx_f2f`（收款码）、`scene`、`status=1`（成功）、`outtradeno`、`voice_content`。
- 生产 Hook 切换：观察器 `DR0` 改为 `NewSyncResponse` 解析入口 `Weixin.dll + 0x106fb10`（磁盘偏移 `0x106ef10`，签名唯一性以固定偏移校验），`DR1` 按线程在真实返回地址采样；返回后校验响应虚表 `+0x8fc0d48`、命令对象虚表 `+0x93d3c78`、载荷对象虚表 `+0x8e3d058`，只处理 `CmdID=5` 且 `field4=10002` 的消息。
- 职责边界：DLL 只按固定布局读取结构化字段并转发 `msgType`、`createTime`、`serverMsgId`、`fromUsername`、`toUsername` 与内容体；业务判定全部在监控端，DLL 内没有任何文案匹配或金额拆解代码。
- 监控端判定：按协议标签取值，要求 `status=1`、`fee>0`、`feetype=1`、`type` 等于配置的 `wechatPayTypeFilter`（默认 `wx_f2f`）、`toUsername` 等于本机已确认收款账号；事件标识为 `wechat:` 加 `transid` 的 SHA-256，天然唯一，去重不再依赖时间窗口。
- 完整上报：事件 `raw` 字段携带完整结构化 JSON（`transid`、`outtradeno`、`PayMsgType`、`fee`、`feetype`、`status`、`scene`、`type`、`timestamp`、`msgType`、`createTime`、`serverMsgId`、收付双方、付款方昵称、内容体原文与字节数）；Worker 将 `raw_excerpt` 截断上限从 500 提升到 8000，管理端未匹配收款列表改为显示关键字段摘要并注明完整数据长度。
- 诊断结论修正：`Weixin.dll + 0x2293980`（`wcpayinfo` 反序列化器）只覆盖转账类消息，收款码到账走 `paymsg` 系统消息，因此此前零命中符合实际；`+0x490d5c0` 记录追加链只承载会话展示数据，无金额、交易号与时间字段，已从生产链路移除，仅保留为独立诊断。

### WX-229 面板提示语义修正与启动自动校验

- 冗余按钮：窗口关闭（`WM_CLOSE`）本就是隐藏到托盘，托盘菜单已提供“显示窗口/状态/退出”，因此移除面板上的“隐藏到托盘”按钮及其 `kHideWindow` 常量与命令分支；释放的横向空间用于把状态标题与详情标签加宽到 702 像素，避免右侧留白和控件挤压。
- 启动自检：新增 `RunWechatConfigPanelAutoValidate()`，由窗口创建后投递的 `WM_APP + 20` 消息触发，避免在 `WM_CREATE` 内阻塞绘制。程序自行完成微信目标校验，不再要求管理员手动点“校验目标”。
- 提示语义：删除“已读取保存的微信目标，请重新校验”这类未操作却像出错的提示；状态栏初始文本由“等待配置”改为空；校验失败时给出具体原因（未找到有效 `Weixin.exe`、未在版本目录找到 `Weixin.dll`、版本/SHA-256/函数签名不匹配且仅支持 4.1.15.13），成功时只显示“配置已加载，微信目标自动校验通过”。
- 静默校验：手动点击“校验目标”才显示“正在校验微信版本、SHA-256 和函数签名...”并弹窗报错；启动自动校验与选择文件后的校验都只写状态栏，不弹窗。
- 陈旧状态修复：记录 `validatedExecutablePath`，保存配置时若主程序路径被手工改动，即使此前标记为已校验也会自动重新校验，避免保存无效目标。
- 弹窗收敛：“重新获取”当前微信账号成功与失败都只写状态栏（失败为错误样式）；“测试连接”成功只写状态栏，失败才弹窗，符合“程序自行校验、出错再报”的要求。
- 输出健壮性：观察器新增 UTF-8 规范化，内容体按 2048 字节截断时回退到码点边界并把非法字节替换为 `?`，修复此前宿主侧 `Hook宿主输出了无效JSON` 的两条报错。
- 现场状态：新版 EXE 原地替换后由监控端复用已加载观察 DLL（哈希与运行时 API 版本一致），未重启微信即恢复 `HEALTHY / HOOK_ACTIVE`，`receiptEntries` 与 `receiptReturns` 正常增长，无无效 JSON 报错。

### WX-230 paymsg 结构化收款端到端验证与内容偏移修正

- 验证结果：`00:11:19` 先收到 `PayMsgType=9`、`fee=0`、`status` 缺失的预通知，被 `STATUS_NOT_SUCCESS` 正确忽略；`00:11:26` 收到 `status=1`、`fee=53`、`feetype=1`、`type=wx_f2f`、`transid` 93 字节的到账通知，`paidAt` 直接取协议 `timestamp=1790611886`，`00:11:27` 上报 `HTTP 200`。
- 订单闭环：订单 `wx-live-1790611845-8801` 状态 `PAID`、`paid_at=1790611886000`；易支付查询接口返回 `status=1`、`money=0.53`；组件状态 `HEALTHY / HOOK_ACTIVE`、`hookOffset=0x106fb10`。
- 完整数据落库：`payment_event.raw_excerpt` 长度 1366，`status=MATCHED`，事件标识由 `transid` 的 SHA-256 生成；Worker 截断上限提升到 8000 后完整结构化 JSON 已入库。
- 缺陷修正：内容体提取把嵌套字段偏移当作相对载荷起点的偏移使用，少加了外层字段偏移，导致 `content` 前混入二进制与收付双方字符串（金额、交易号、时间等字段仍正确，因此匹配未受影响）。修正为外层字段偏移与内层字段偏移相加。
- 自证方式：修正后自然到达的 `msgType=10002` 系统消息在日志中输出 `contentHead=<sysmsg type="dynacfg">`，首字符即为干净 XML，无需再触发一笔收款。
- 可观测性：监控端收款通知日志新增 `contentHead`（前 24 字符）用于自证内容边界；`fee`、`status`、`type`、`transidBytes`、`timestamp`、`toUsernameBytes` 全部按结构化字段记录，不打印交易号与内容原文。
- 输出健壮性：UTF-8 规范化后本轮未再出现 `Hook宿主输出了无效JSON`。
- 隐私清理：字段定位阶段产生的本机转储文件（记录槽位与 NewSync 载荷全文）在结论确认后删除，诊断 DLL 与宿主命令保留以便复核。

### ADM-003 管理端信息架构拆分与设备按心跳显示

- 标签拆分：原“系统设置”一页堆叠五块内容，现拆为七个标签页——支付网关、收款码、支付宝证书、对象存储、监控端、订单列表、管理员安全；PC 监控端密钥移到“监控端”标签并独立成表单。
- 接口配合：`PUT /admin-api/settings` 改为按字段部分更新，只处理本次提交的键（缺省字段保持原值），因此多个表单可以各自保存而不会互相清空商户号、收款码地址或订单参数。
- 设备不持久化：`GET /admin-api/monitors` 只返回 `last_heartbeat_at` 在 90 秒内的设备，组件状态与微信登录会话都按存活设备过滤，并回传 `onlineWindowMs`；新增 `cleanupStaleMonitorDevices()` 由每分钟定时任务调用，删除超过 10 分钟无心跳的设备及其组件状态，刷新后台不会看到历史设备。
- 视图合并：微信登录与监控状态不再分两个区块，改为按设备聚合成卡片，卡片内包含设备信息（名称、ID、平台、版本、地址、心跳秒数）、微信登录状态与组件状态表（状态、诊断码、目标版本、Hook 偏移、进程、最后收款、上报时间）。

### ADM-004 登录二维码按需弹窗

- 默认不渲染二维码：只有在设备上报 `QR_READY` 且未过期时才显示“显示登录二维码（剩余 mm:ss）”按钮，按钮每秒刷新剩余时间，过期后自动禁用并标注“已过期”。
- 弹窗行为：点击按钮才打开弹窗加载二维码图片；上报 `LOGGED_IN`、二维码失效或过期时自动关闭弹窗并提示；收到新的二维码（`updatedAt` 变化）时在弹窗内自动刷新图片并提示“已收到新的登录二维码”。
- 隐私效果：不点击就永远不请求二维码图片接口，管理端页面轮询也不会加载二维码内容。

### ADM-005 订单与收款事件的原始数据详情

- 数据口径修正：监控端上报的 `raw` 只包含解析后的结构化 JSON（`transid`、`outtradeno`、`PayMsgType`、`fee`、`feetype`、`status`、`scene`、`type`、`timestamp`、`msgType`、`createTime`、`serverMsgId`、收付双方、付款方账号与昵称、`contentBytes`），不再上传微信 paymsg 原文 XML；原文只在监控端本机日志保留前 24 字符用于排查。
- 新增接口：`GET /admin-api/orders/:id` 返回订单、关联收款事件、解析后的结构化字段、上报 JSON 原文、回调任务与最近 10 次尝试；`GET /admin-api/payment-events/:id` 返回未匹配事件与其匹配订单。
- 列表瘦身：未匹配收款列表不再回传整段 `raw_excerpt`，改为返回 `raw_length` 与前 120 字符摘要，避免 50 条 × 8000 字符的响应体。
- 界面：订单列表与未匹配收款都新增“详情”按钮，弹窗先展示结构化字段表，再展示上报 JSON 原文（可一键复制），并显示商户回调任务状态、尝试次数、下次尝试时间、最后错误与每次尝试的 HTTP 状态与响应摘要。

### OPS-012 商户回调链路验证

- 验证设施：新增迁移 `0008_callback_sink.sql` 与 `GET /api/callback-sink/:token`（随机 token 校验、`fail` 前缀返回 500、成功返回易支付约定的 `success` 文本、记录保留 7 天），以及管理端 `GET /admin-api/callback-sink` 查询最近记录。
- 触发方式：用管理端补单接口 `POST /admin-api/orders/:id/mark-paid` 触发回调任务，无需真实付款即可验证成功与重试两条路径。
- 成功路径：订单 `wx-cb-ok-1790613813` 的 `notify_status=SUCCESS`，回调任务 `SUCCESS`，第 1 次尝试 `HTTP 200`、响应体 `success`；接收端记录到 `trade_no`、`out_trade_no`、`type=wxpay`、`money=0.01`、`trade_status=TRADE_SUCCESS` 与 `sign`。
- 失败重试：订单 `wx-cb-fail-1790613814` 的 `notify_status=RETRYING`，任务 `PENDING`、尝试 `1/6`、`last_error=HTTP_500:sink failure`，下次尝试按 60 秒退避排期，证明重试链路生效。
- 部署记录：迁移已应用到远端 D1，Worker 版本 `6399f78a-74ef-42a7-9b2e-880d5201f652`；新版监控端 EXE 与观察 DLL 已安装，微信主进程 `PID 13420` 上报 `HEALTHY / HOOK_ACTIVE`，本轮自动重启 0 次、无无效 JSON。

### WX-231 监控端单 EXE 分发（内嵌观察 DLL）

- 嵌入方式：新增 `native/wechat-observer/embedded_observer.h`，用 `.incbin` 直接把 `cfpay-hook-observer.dll` 构建产物放入 EXE 只读数据段，配合 `add_dependencies` 与生成器表达式 `CFPAY_OBSERVER_DLL_FILE=$<TARGET_FILE:cfpay-hook-observer>`，避免生成数 MB 的 C 数组源文件，也保证每次构建嵌入的都是最新 DLL。
- 释放策略：启动时把内嵌映像释放到 EXE 同目录的 `cfpay-hook-observer.dll`；先按字节整体比对，已存在且完全一致时不重写；需要替换时先写 `.tmp` 再 `MoveFileExW` 原子替换；文件被运行中的微信占用且内容不同时返回 `OBSERVER_DLL_LOCKED_BY_RUNNING_WEIXIN`，明确要求先完整退出微信，不做原地覆盖升级。
- 触发时机：任何运行模式（GUI、`--self-test`、`--internal-host`）在取得程序目录后先尽力释放，配置加载完成后再执行权威校验并写日志，因此配置缺失也不影响自检释放。
- 可观测性：日志输出 `state`（`embedded observer dll extracted` 或 `already present and identical`）、`bytes`、`sha256` 与目标路径；失败时以 `OBSERVER_DLL_NOT_EMBEDDED`、`OBSERVER_DLL_WRITE_FAILED`、`OBSERVER_DLL_REPLACE_FAILED`、`OBSERVER_DLL_LOCKED_BY_RUNNING_WEIXIN` 区分原因，并在界面显示 `OBSERVER_DLL_UNAVAILABLE`。
- 配置放宽：`loadConfig` 不再要求 `observerDllPath` 指向的文件预先存在（由内嵌映像释放），仍要求微信主程序存在。
- 打包与脚本：`tools/build-windows-wechat-monitor.mjs` 只产出单个 `cfpay-wechat-monitor.exe`（5,282,878 字节，较未内嵌版本增加约 1.03 MB，与 DLL 体积一致）；安装脚本的必需文件只剩 EXE；单 EXE 校验脚本改为断言自检运行后 DLL 已被释放到 EXE 同目录。
- 现场验证一：在只放置 EXE 与测试配置的空目录启动，日志为 `state=embedded observer dll extracted bytes=1062126 sha256=a00b06685d71bdb245f77b27229fb57e33328019f11f28aeaad7bfe09aaac44c`，释放出的 DLL 与生产 DLL 哈希一致，随后 `HEALTHY / HOOK_ACTIVE`、上报 `HTTP 200`。
- 现场验证二：删除生产目录的独立 DLL 后只启动 EXE，DLL 被自动释放，微信 `PID 17804` 恢复 `HOOK_ACTIVE`；再次部署新 EXE 时日志为 `already present and identical`，不重写文件、不重启微信即复用已加载观察器。
- 设计边界：注入仍走 `LoadLibraryW` 路径注入，因此运行目录会出现释放出的 DLL 文件；这是刻意选择，避免手写内存映射注入带来的稳定性与风控不确定性。若需要“磁盘不落 DLL”，需要改为反射式注入并重写已加载模块哈希校验逻辑。
- 环境清理：删除历史路径缺陷遗留的 `%LOCALAPPDATA%` 错误目录，生产目录现为 `cfpay-wechat-monitor.exe`、`cfpay-hook-observer.dll`（运行时释放）、`config.json` 与备份目录。

### OPS-013 单 EXE 一键编译脚本与项目 README 重写

- 新增脚本：`tools/build-single-exe.sh`（`npm run wechat:monitor:single`）一次完成工具链检测、CMake 配置、观察 DLL 构建、内嵌 EXE 构建、产物复制与校验；支持 `--build-dir`、`--output`、`--clean`、`--jobs`、`--self-test`。
- 工具链处理：优先 `x86_64-w64-mingw32-g++-posix`，回退到 `x86_64-w64-mingw32-g++`；有 `ninja` 用 Ninja，否则用 Unix Makefiles；缺少 `cmake`、编译器或 `sha256sum` 时以固定退出码报错。
- 内嵌校验：脚本读取观察 DLL 中段 4096 字节并在 EXE 映像中检索，命中才算内嵌成功（本次输出 `内嵌校验: yes`），避免“构建成功但没嵌进去”的静默失败。
- 产物指纹：输出 EXE 与内嵌 DLL 的字节数、SHA-256、`file` 类型与导入表；实测 EXE 只导入 `bcrypt/comctl32/gdi32/kernel32/msvcrt/ole32/shell32/user32/version/winhttp`，无 MinGW 运行时依赖。
- 复现性结论：同一份源码在两个构建目录产出的观察 DLL 大小相同（1,062,126 字节）但仅 6 字节不同，全部位于 PE 时间戳字段，因此构建不可逐字节复现；每次新构建的 DLL 哈希都会变化，升级观察器必须先完整退出微信。
- 发行包：`dist/single-exe/cfpay-wechat-monitor.exe` 为唯一分发文件；`tools/build-windows-wechat-monitor.mjs` 与 `tools/install-windows-wechat-monitor.ps1` 已同步为只处理 EXE，安装脚本的必需文件、升级判定与复制逻辑都基于同一列表。
- 文档：重写根 `README.md`，覆盖功能特性、架构与数据流、目录结构、本地与云端部署、七个后台标签页职责、易支付接口与签名、支付宝与微信两条通道的实现细节（含对象布局、paymsg 字段表、判定条件、事件标识）、监控端单 EXE 与内嵌释放策略、配置字段表、编译与诊断产物、安装升级规则、安全与隐私、测试与真实收款验证步骤、故障排查错误码表与已知边界。

### OPS-014 版本化工件与 GitHub Release 自动发布

- 版本升级：监控端版本 `0.8.1 → 0.9.0`，同步 `native/wechat-observer/wechat_monitor.cpp`（`kMonitorVersion`、`kMonitorUserAgent`）、`native/wechat-observer/wechat_config.cpp`（心跳 body 与 WinHTTP UA）、`tools/lib/wechat-monitor-constants.mjs`（`MONITOR_VERSION`）与 `docs/wechat-native-observer.md` 的现状描述；Worker 侧不校验监控端版本，`npm test` 15 个文件 62 项全部通过。
- 工件命名：`tools/build-single-exe.sh` 新增发布工件 `dist/single-exe/cfpay-wechat-monitor-<监控端版本>-weixin-<微信版本>-win64.exe`（本次为 `cfpay-wechat-monitor-0.9.0-weixin-4.1.15.13-win64.exe`），与固定名 `cfpay-wechat-monitor.exe` 字节完全一致，后者继续供安装脚本使用。
- 版本来源：脚本不再硬编码版本，改为从 `wechat_monitor.cpp` 的 `kMonitorVersion` 与 `wechat_host.cpp` 的 `kExpectedVersion` 解析，并做格式校验（`^[0-9]+\.[0-9]+\.[0-9]+` 与 `^[0-9]+(\.[0-9]+)*$`），解析失败以退出码 `3` 中断；`--version` / `--weixin-version` 可临时覆盖。
- 构建指纹：新增 `dist/single-exe/BUILD_INFO`，记录 `MONITOR_VERSION`、`WEIXIN_VERSION`、`OBSERVER_API_VERSION`（本次 `0x000a0000`）、EXE 与内嵌 DLL 的大小与 SHA-256、编译器、UTC 构建时间、`GIT_COMMIT`、`GIT_DIRTY` 与 `GITHUB_RUN_ID`；同时产出 `SHA256SUMS` 覆盖两个 EXE。
- 本次构建结果：`--clean --jobs 4` 全量构建 12 个目标通过，EXE `5,282,878` 字节，SHA-256 `35806cbcfaab3e4ae62a79d28d6f9255938c2c10a4cbebb8b3eb468589600136`，内嵌观察 DLL `1,062,126` 字节，SHA-256 `e99a71a9eef631051bc78ac4d90921bf93f75c589aa4fff83e1604c510fc1e0d`，内嵌校验 `yes`，导入表只有 Windows 系统 DLL；`sha256sum -c SHA256SUMS` 两个文件均 `OK`。同一份源码的前一次构建 EXE 哈希为 `d576a09823b83a861c30d445fcd9f64bd5d36a5cf6a7a3eee1b7869a5d12b67d`，再次印证 PE 时间戳导致构建不可逐字节复现，Release 工件的权威哈希以 CI 产出的 `BUILD_INFO` 与 `SHA256SUMS` 为准。
- CI 发布：新增 `.github/workflows/release.yml`，`push: tags: v*` 触发（另支持 `workflow_dispatch` 与预发布开关），`permissions: contents: write`，按 `github.ref` 做并发互斥；步骤为安装工具链、`--clean --jobs 4` 全量构建、校验 `SHA256SUMS`、校验标签等于 `v<监控端版本>`、生成发布说明、用 `gh` + `GITHUB_TOKEN` 创建 Release 并上传版本化 EXE、`SHA256SUMS`、`BUILD_INFO`，Release 已存在时 `--clobber` 覆盖。
- 发布说明内容：版本信息表（监控端版本、支持微信版本、平台、提交、大小、SHA-256）、工件说明、三步部署指引，以及上一标签到本标签之间的提交列表；无历史标签时回退最近 20 条提交。
- 约束记录：仓库为私有且本机无 GitHub API token 与 `gh` CLI，因此只能靠 tag 触发的 Actions 用内置 token 发布；代码推送使用 `~/.ssh/deploy_key` 部署密钥（`GIT_SSH_COMMAND="ssh -i ~/.ssh/deploy_key -o IdentitiesOnly=yes -o BatchMode=yes"`），该密钥具备写权限。
- 文档：`README.md` 第 10 章补充产物表与 `10.1 发布到 GitHub Release` 流程，`docs/wechat-native-observer.md` 新增 `版本化发布工件` 章节。
- 运维注意：Release 工件由 CI 重新编译，PE 时间戳导致它与本机已部署的 `cfpay-wechat-monitor.exe` 字节不同（功能一致）；把 Release 工件覆盖到现场目录时，内嵌观察 DLL 会被判定为“内容不同”，若微信仍在运行会报 `OBSERVER_DLL_LOCKED_BY_RUNNING_WEIXIN`，必须先完整退出微信再启动新 EXE。
- 验证边界：本机无 GitHub API token 与 `gh` CLI，仓库私有（匿名 API 返回 `404`），因此 Release 是否生成只能在 `https://github.com/OWNER/cfpay/actions` 与 `releases/tag/v0.9.0` 页面确认；本地能验证的部分（`--clean` 全量构建、版本解析、内嵌校验、`sha256sum -c`、workflow YAML 解析与每个 `run` 块的 `bash -n`）均已通过。

### OPS-015 管理端「订单列表」刷新按钮失效修复

- 现象：管理员反馈控制台 `https://pay.example.com/admin-change-me` 有些按钮点了没反应。
- 排除部署陈旧：线上 `/admin.js`、`/styles.css`、`/vendor/jsQR.js` 与仓库文件 md5 完全一致，后台入口 HTML 也一致，因此问题在代码本身。
- 定位：静态比对 `public/admin.html` 中 18 个带 `id` 的按钮与 `public/admin.js` 的监听绑定，只有 `refreshOrders` 完全没有绑定；`git log -S refreshOrders` 指向 `07c504c`（拆分设置页），该提交把 `document.querySelector('#refreshOrders').addEventListener(...)` 整块替换成了 `monitorSecretForm` 的 submit 监听，属于重构误删。同提交的监听目标差集只有这一项；`admin.html` 中消失的 `monitorsBody`、`wechatLoginSessions` 是有意合并为 `monitorDevices`。
- 接口核对：`admin.js` 调用的 19 个 `/admin-api/*` 路径与 `src/index.ts` 注册的路由一一对应，不存在因 404 导致的“点击无反应”。
- 修复一：恢复 `refreshOrders` 监听，行为与 `refreshMonitors` 对齐（`loadOrders()` + 成功/失败 toast）。
- 修复二：新增 `tabRefreshers`，切到收款码、支付宝证书、监控端、订单列表这四个只读数据页时重新拉取数据。原因是 `bootstrap()` 用 `Promise.all` 并发加载，任一请求失败只弹一次错误，该页会一直空白且没有重试入口。表单页（支付网关、对象存储、管理员安全）刻意不在切换时刷新，避免清空已输入但尚未保存的商户密钥、监控端密钥、OSS 密钥与管理员新密码。
- 修复三：订单列表页可见时每 15 秒轮询一次（监控端页维持 5 秒），付款到账后无需手动刷新。
- 修复四：`secretDialogDescription` 原文案要求“点击系统设置中的保存设置”，但设置页已拆分，实际按钮是「保存网关设置」与「保存监控端密钥」；改为由 `showGeneratedSecret` 传入准确的保存位置提示，静态 HTML 默认文案同步更正。
- 防回归：新增 `tests/admin-console-wiring.test.mjs`，断言每个带 `id` 的静态按钮都有监听绑定、`refreshOrders` 与 `refreshMonitors` 已绑定、`admin.js` 引用的 `#id` 在 HTML 中都存在（防止 null 选择器抛错中断模块、导致其后所有监听失效）、每个 `data-tab` 都有对应 `#tab-*` 面板且四个只读页都在 `tabRefreshers` 中。`npm test` 16 个文件 66 项全部通过。
- 缓存与生效：后台入口 HTML 与 `/admin.js` 都经 `src/index.ts` 的 `noStore()` 返回 `no-store`，部署后立即生效；仍把两处 `?v=` 提升到 `20260929.1`。

### OPS-016 商户回调超时排查（订单 USR1NOHnpMjJ1790617154）

- 现象：管理端订单详情里该订单的回调错误显示 `The operation was aborted due to timeout`。
- 报错来源：`src/callbacks.ts:84` 的 `AbortSignal.timeout(15_000)`。`fetch` 被该信号中断后抛出的就是这条消息，由 `markFailedAttempt` 原样写入 `callback_attempt.error` 与 `callback_job.last_error`，因此它描述的是“商户回调地址 15 秒内没响应”，不是 CFPay 内部超时。
- 数据核对（远端 D1）：订单 `id=33de1077605c40598c50557b704ea535`、`merchant_order_no=USR1NOHnpMjJ1790617154`、`provider=wxpay`、`amount_fen=100`、`notify_url=https://gateway.example.com/api/user/epay/notify`；当前 `status=PAID`、`notify_status=SUCCESS`。
- 时间线（Asia/Shanghai）：`01:39:14.884` 下单 → `01:39:43.000` 支付成功 → `01:39:45.653` 创建回调任务 job 6 → `01:39:47.084` 第 1 次尝试发起，15 秒无响应，约 `01:40:02` 记为超时 → 按 `RETRY_DELAYS_MS[1]` 排期 `01:40:47` → cron 每分钟触发，实际 `01:41:03.175` 第 2 次尝试返回 `HTTP 200` 且响应体 `success` → 任务转 `SUCCESS`、`last_error` 清空、订单 `notify_status=SUCCESS`。
- 结论：重试链路按设计工作，回调最终成功，无需人工补单。首次失败的原因是商户端 `gateway.example.com` 对该请求 15 秒内未返回（典型冷启动或首次处理慢）。排查后本机连续 3 次请求该 notify 端点，TTFB 分别 `0.24s / 0.26s / 0.31s`、HTTP 200，说明当时是瞬时慢而非持续不可用。
- 全局核对：`callback_attempt` 中 `The operation was aborted due to timeout` 只有这 1 条；另有 5 条 `HTTP_500:sink failure` 全部属于回调验证设施用的 sink 订单 `wx-cb-fail-1790613814`（job 3 仍 `PENDING`、`attempt 5/6`，是刻意保留的失败重试用例）。`callback_job` 共 7 条：6 条 `SUCCESS` + 1 条该测试 `PENDING`，没有真实商户回调处于失败终态。
- 风险提示：超时不等于未送达。第 1 次请求可能已经到达商户并被处理，只是响应慢；商户随后又收到第 2 次通知，所以 notify 处理必须按 `out_trade_no` 幂等（已支付直接返回 `success`），否则会重复加款。
- 可选调整：若商户端首包稳定超过 15 秒，可把 `AbortSignal.timeout` 放宽到 20–30 秒。当前证据不支持这个必要性，本次不改动。

### OPS-017 移除 S3 兼容对象存储，支付宝私钥固定只存 D1

- 决策：甲方确认支付宝私钥继续留在 D1，OSS 整套移除。
- 删除前现状核查（远端 D1）：`object_storage_setting` 有 1 行（Backblaze B2 `s3.us-east-005.backblazeb2.com`、bucket `sc-cffk`、前缀 `cfpay/private`，配置于 `2026-09-28 23:42:11`），但 `alipay_credential.storage_backend='database'`、`object_key=NULL`，凭据保存于 `2026-09-27 23:10:57`，即 OSS 从未真正存过任何对象。
- 为什么必须现在删：`isObjectStorageConfigured()` 为真时，`saveAlipayCredentials` 会在**下次任何人重新上传支付宝证书时**自动把密文写进 B2，并把 D1 的 `encrypted_payload` 置为 `NULL`；而管理端只有 `status/config/test/migrate-alipay` 四条路由，**没有停用或删除 OSS 配置的入口**。凭据仍在 D1 的当下是唯一安全的删除窗口，一旦自动迁移发生，删代码就等于把私钥丢在 B2 里取不回来。
- 迁移：新增 `migrations/0009_remove_object_storage.sql`，`DROP TABLE object_storage_setting`（其中加密保存的 B2 AccessKey/SecretKey 一并删除，将来需要重新配置），并把 `alipay_credential` 收回 database-only 形态。`INSERT ... SELECT` 刻意不加 `WHERE` 过滤：若仍有行 `encrypted_payload` 为 `NULL`（凭据在 OSS 上），`NOT NULL` 约束会让迁移直接失败，避免静默丢掉支付宝私钥。
- 服务端代码：删除 `src/object-storage.ts`（395 行、13 个导出）与 `tests/object-storage.test.ts`（109 行、4 项测试）。`src/alipay-config.ts` 去掉 `storage_backend` / `object_key` / `object_sha256` 字段、OSS 读写与 SHA-256 完整性校验分支、`migrateAlipayCredentialsToObjectStorage`，以及只服务于对象哈希的 `sha256Hex` / `constantTimeHex` 局部函数；`AlipayCredentialStatus.source` 收敛为 `database | environment | null`。`src/index.ts` 删除 4 条 `/admin-api/storage/*` 路由和 15 条 `OBJECT_STORAGE_*` / `ALIPAY_CREDENTIAL_OBJECT_INVALID` 错误码中文映射。`package.json` 与 `package-lock.json` 移除 `aws4fetch`（全项目唯一使用点）。
- 前端：删除「对象存储」标签页与相关脚本（11 个 storage 常量、`loadStorageStatus`、`storagePayload`、表单提交、测试连接、迁移凭据监听），`bootstrap` 的 `Promise.all` 去掉 `loadStorageStatus`；支付宝证书页的来源文案只剩「D1 加密存储 / Worker Secrets / 未配置」。标签页由七个收敛为六个，标签与面板一一对应。
- 文档：`README.md` 第 1、3、6、12 章去掉对象存储描述；`docs/architecture.md` 改为“密文只保存在 D1 的 `alipay_credential`”，并记录 0009 迁移移除了哪些结构。
- 验证：`tsc --noEmit` 通过；全仓 grep 确认 `object_storage` / `objectStorage` / `aws4fetch` / `admin-api/storage` 在 `src/`、`public/`、`tools/`、`package.json`、`wrangler.jsonc` 中零残留（仅测试自身的断言正则命中）。

### WX-232 微信通道不可用时收银台隐藏收款码并提示改用支付宝

- 需求：没有微信 Hook 客户端时，微信收款页提示微信不可用，让付款人改用支付宝重新下单。
- 判定实现：`src/monitor.ts` 新增 `getWechatChannelStatus(database, now, freshWindowMs = 90_000)`，联表 `monitor_component_status` 与 `monitor_device` 取最新一条 `wechat_hook` 记录，返回 `available` 与 `reason`。组件上报或设备心跳任一超过 90 秒判 `STALE`；新鲜但状态不是 `HEALTHY` 判 `UNHEALTHY`；无记录判 `NO_MONITOR`。`no such table` 按 `NO_MONITOR` 处理（沿用 `getCredentialRow` 的容错写法），避免收银台在新库上直接 500。
- 收银台渲染：`src/pages.ts` 的 `renderPaymentPage` 新增第三参数 `PaymentPageOptions`。微信订单且 `wechatAvailable === false` 时**隐藏收款码和尾差提示**，改为红色提示块，按 reason 显示「当前没有接入微信收款监控端 / 微信收款监控端已离线 / 微信收款监控端未就绪（微信未登录或 Hook 未激活）」，并统一给出「请返回商户页面，改用支付宝重新下单支付」。倒计时、订单号与状态轮询保留。
- 状态边界：守卫只作用于 `status='PENDING'` 的微信订单，`/pay/:id` 也只在 PENDING 时才查一次可用性。已支付或已失效的订单继续原样渲染，否则付款人付完款回来会看到「微信支付暂不可用」，属于误导。
- 为什么必须隐藏收款码：Hook 不在线时付款人扫码付的钱系统完全看不到，订单会一直停在 `PENDING` 直到过期，连「未匹配收款」都不会留下记录，只能靠人工对账退款。继续展示收款码等于让付款人把钱付进黑洞。
- 自动恢复：`GET /api/orders/:id/status` 对 `provider='wxpay'` 且 `status='PENDING'` 的订单追加 `wechatAvailable` 布尔值；收银台每秒轮询该接口，发现布尔值翻转即 `location.reload()`，监控端恢复后付款人无需手动刷新就能看到收款码。非 PENDING 或非微信订单返回 `null`，页面的 `typeof === 'boolean'` 守卫确保不会误刷新。
- 刻意不做原地改渠道：过程中实现过 `switchOrderProvider`（把 PENDING 订单原地改成支付宝，并按 `(provider, payable_amount_fen)` 部分唯一索引重新分配尾差），甲方明确要求只做提示、由付款人回商户页重新下支付宝单，已回退。该决定也站得住：`merchant_order_no` 是唯一键，同一商户单号换渠道会打乱商户对账口径，而且回调的 `type` 会与商户下单时声明的不一致。
- 影响范围：支付宝订单走账务 API 轮询（Durable Object），不依赖 PC 监控端，因此永不隐藏收款码。
- 测试：新增 `tests/wechat-channel-availability.test.ts`（10 项），覆盖四种判定、`no such table` 容错、五种非 `HEALTHY` 状态、微信不可用与可用两种渲染、支付宝订单不受影响、`channelAvailable` 注入。渲染结果另用 esbuild 打包后在 Node 中实机打印核对两种页面片段。
- 前端复核（`npm test` 16 文件 74 项通过）：16 个静态按钮全部有监听、5 个表单全部绑定 submit、6 个标签页与面板一一对应、`admin.js` 引用的 id 全部存在于 HTML、前端调用的 `/admin-api/*` 路径与 `src/index.ts` 注册路由完全对齐（新增 `tests/admin-console-wiring.test.mjs` 的“API 面对齐”与“无 OSS 残留”两项断言长期守护）。

### OPS-018 对象存储移除与微信通道守卫的生产部署验证

- 部署前置检查：再次确认远端 `alipay_credential` 为 `storage_backend='database'`、`encrypted_payload` 非空、`object_key=NULL`，满足 0009 迁移的安全前提。
- 迁移执行：`wrangler d1 migrations apply DB --remote` 应用 `0009_remove_object_storage.sql`，6 条语句 2.5ms 完成，状态 ✅。
- 数据核对：`sqlite_master` 中已无任何 `%storage%` 表；`alipay_credential` 保留 1 行，`app_id=2021000000000000`、`encrypted_payload` 长度 3276、`encryption_iv` 长度 16、`created_at` 与 `updated_at` 均为 `1790521857690`，与迁移前完全一致，凭据未受影响。
- Worker 发布：`wrangler deploy` 上传 2 个变更静态资源（`/admin.html`、`/admin.js`），版本 `73786e05-71db-4d12-9bcd-96f61e44516a`；自定义域名 `pay.example.com` 与每分钟 Cron、D1、Durable Object、Assets 绑定保持不变。
- 前端线上核对：`https://pay.example.com/admin.js` 与后台入口 HTML 的 md5 与本地文件逐字节一致；线上 `admin.js` 中 `storage` / `OSS` 关键字命中数为 0。`/admin-api/storage/status` 返回 401，与 `/admin-api/definitely-not-a-route`、`/admin-api/settings` 一致——`/admin-api/*` 全量先过鉴权中间件，因此路由是否删除无法从外部区分，由 `tests/admin-console-wiring.test.mjs` 的“无 OSS 残留”断言与 `tsc --noEmit` 在代码层守护。
- 通道判定线上核对：把 `getWechatChannelStatus` 的原句直接跑在生产 D1 上，返回 `device_id=wm_000000000000000000000000`、`status=HEALTHY`、`detail_code=HOOK_ACTIVE`、`reported_at=1790619796258`、`last_heartbeat_at=1790619787987`，相对当时时间 `1790619803216` 均在 15 秒内，判定为可用。
- 收银台线上核对：已支付微信订单 `33de1077605c40598c50557b704ea535` 的收款页正常显示收款码、`let channelAvailable=null`（非 PENDING 不探测），状态接口返回 `wechatAvailable:null`；不存在的订单返回 404。
- 端到端验证：临时插入一条 PENDING 微信订单（金额 ¥9999.01 避免与真实尾差撞槽、有效期 120 秒），收款页显示尾差提示与收款码、`let channelAvailable=true`，`/api/orders/:id/status` 返回 `{"status":"PENDING","notifyStatus":"NONE","returnUrl":null,"wechatAvailable":true}`；验证后立即 `DELETE` 并复查 `remaining=0`，生产库无残留。
- 未做的验证：不可用分支（`NO_MONITOR` / `STALE` / `UNHEALTHY`）需要监控端离线才能在线上复现，本次没有为了验证而中断正在运行的微信监控端；该分支由 `tests/wechat-channel-availability.test.ts` 与本地 esbuild 实机渲染核对覆盖。

### OPS-019 放开后台入口与管理员账号的格式限制

- 需求：甲方要求后台入口路径与管理员账号不做限制。
- 原限制：`validateAdminUsername` 要求 `^[A-Za-z0-9_.-]{3,64}$`，`validateAdminPath` 要求 `^[A-Za-z0-9_-]{8,96}$` 且不在保留字集合内；`src/index.ts` 的兜底路由还额外用 `^\/[A-Za-z0-9_-]{8,96}$` 预筛，两处必须同时放开，否则后台能保存新入口但永远访问不到。
- 现在的规则：账号与入口都只要求 trim 后非空，字符集与长度不限（可用中文、邮箱、空格、多级路径）。错误码由 `INVALID_ADMIN_USERNAME` / `INVALID_ADMIN_PATH` 改为 `ADMIN_USERNAME_REQUIRED` / `ADMIN_PATH_REQUIRED` / `ADMIN_PATH_RESERVED`，中文提示同步更新；密码仍保留 12–128 位限制（甲方未要求放开）。
- 保留首段：入口首段仍拒绝 `admin`、`admin.html`、`admin.js`、`admin-api`、`api`、`api.php`、`index.html`、`mapi.php`、`pay`、`payment-assets`、`query.php`、`styles.css`、`submit.php`、`vendor`。这不是使用偏好限制而是功能必需——它们是 Worker 已注册的路由前缀或 `public/` 下真实存在的静态资源，占用后管理台会顶掉接口与资源，或反过来永远无法访问；匹配按首段做、大小写不敏感，多级入口只校验第一段。
- 路由匹配：`normalizeAdminPath` 改为纯归一化（trim + 去首尾斜杠），空值回退 `admin-change-me` 移到 `getAdminConfig` 的 env 分支。新增并导出 `decodeAdminPathname`（先 `decodeURIComponent`，畸形编码如孤立 `%` 退回原值，再去尾斜杠、根路径归一为 `/`）与 `matchesAdminEntry`（配置为空直接不匹配，`/admin` 与 `/admin.html` 固定不放行，其余与 `/` + 配置值逐字比较）。兜底路由改为调用这两个函数，非 ASCII 入口因此可用；`tools/audit-production-readiness.ps1` 本来就用 `EscapeDataString` 拼路径，与服务端解码一致，多级入口编码成 `%2F` 也能命中。
- 代价：兜底路由现在对每个未命中请求都会读一次 `admin_setting`（原来被字符集正则挡住）。单行主键读，按本项目量级（每日低于 100 单）可忽略；正确性优先，未引入缓存以免入口修改后出现生效延迟。
- 前端：`public/admin.html` 去掉 `#adminPath` 的 `pattern="[A-Za-z0-9_-]{8,96}"`，两个输入框改用 placeholder 说明“不限字符与长度”；`admin.js` 本来就没有客户端格式校验，无需改动。
- 测试：`tests/auth.test.ts` 用 5 个用例替换原来 1 个，覆盖任意账号（含中文、邮箱、空格、300 字符）、任意入口（含中文多级路径、300 字符）、空值与非字符串报错、14 个保留首段的大小写与多级变体、入口精确匹配（尾斜杠、多级、百分号编码的中文、不匹配、空配置、`/admin` 与 `/admin.html` 不放行）、畸形编码回退。`npm test` 16 文件 78 项通过，`tsc --noEmit` 通过。
- 兼容性：线上 `admin_setting` 现值 `username=admin`、`admin_path=admin-change-me`、`password_hash` 为空（回退 Worker Secret `ADMIN_PASSWORD`）、`session_version=2`，本次不修改任何已存值，现有入口与登录不受影响。

### OPS-020 放开 admin_setting 的数据库约束并让失败原因可见

- 现象：OPS-019 放开应用层校验后，甲方在后台修改账号/入口仍失败，前端只显示「请求处理失败」，没有任何原因。实际响应体为 `{"code":-1,"msg":"请求处理失败"}`。
- 根因：`migrations/0005_admin_setting.sql` 建表时写了 `username TEXT NOT NULL CHECK (length(username) BETWEEN 3 AND 64)` 与 `admin_path TEXT NOT NULL CHECK (length(admin_path) BETWEEN 8 AND 96)`。OPS-019 只放开了 `src/auth.ts` 的应用层校验，数据库层的 CHECK 仍在，写入被拒；D1 抛出的错误文本不在 `publicErrorMessage` 的映射表里，于是落到兜底文案。
- 取证：在远端 D1 建了一张同约束的临时探针表 `_cfpay_probe_tmp` 复现，拿到原始报错 `CHECK constraint failed: length(username) BETWEEN 3 AND 64: SQLITE_CONSTRAINT (extended: SQLITE_CONSTRAINT_CHECK) [code: 7500]`，随后立即 `DROP TABLE` 清理，未触碰 `admin_setting` 生产数据。
- 顺带排查：全仓扫描 `migrations/*.sql` 中的 `length(` 约束，只有 `admin_setting` 这两条与用户输入格式相关；`payment_qr_code` 的 `payload BETWEEN 3 AND 2048` 与 `payload_sha256 = 64` 属于数据完整性约束，与应用层校验一致，保留。
- 修复一：新增 `migrations/0010_relax_admin_setting_constraints.sql`，重建 `admin_setting`，账号与入口只保留 `length(...) > 0` 的非空下限；`id = 1`、`session_version > 0`、`password_iterations BETWEEN 100000 AND 1000000` 与「密码哈希/盐/迭代次数三者同时为空或同时非空」的约束原样保留；用显式列名的 `INSERT ... SELECT` 迁移原有行。
- 修复二：`publicErrorMessage` 不再无条件兜底。命中 `CHECK constraint failed` 或 `SQLITE_CONSTRAINT` 时返回「保存的内容不满足数据库约束，请检查长度与格式」；错误码形如自有常量（`^[A-Z][A-Z0-9_]{2,63}$`）时返回「请求处理失败（错误码）」，便于以后漏登记映射时直接看出原因；其它文本（可能含 SQL 或运行时细节）仍返回通用文案，避免泄露内部信息。
- 验证：`tsc --noEmit` 通过；迁移应用后线上 `admin_setting` 仍为 1 行，`username=admin`、`admin_path=admin-change-me`、`session_version` 与 `updated_at` 未变。

### WX-233 收银台过期后仍展示收款码与倒计时

- 现象（甲方复现路径）：打开收银台后不刷新，等订单过期，再从别的标签页切回来，倒计时会重新开始走，而且二维码一直可见；刷新页面才恢复正常。
- 根因一（倒计时漂移）：旧实现用 `let seconds=<服务端秒数>` 配 `setInterval(tick,1000)` 做本地递减。浏览器对后台标签页的定时器有节流，切走期间 `seconds` 几乎不减，切回来就从陈旧值继续倒数，早已过期的订单因此“复活”。
- 根因二（终态不收二维码）：旧实现过期时只改 `status` 文案，既不隐藏 `.qr` 也不停 `setInterval`；`poll()` 收到 `EXPIRED/CLOSED` 同样只改文案。付款人此时扫码仍能付出去，但到账匹配要求 `expires_at >= paidAt`，匹配必然失败，钱只会落进「未匹配收款」等人工处理。
- 根因三（服务端空档）：`expireOrders` 由每分钟 Cron 触发，`expires_at` 已过但 `status` 仍是 `PENDING` 的最长一分钟里，`renderPaymentPage` 照旧渲染收款码与倒计时。线上实测过期订单 `2147a82c72b646e5aec5b64d9298e6c5`：状态接口已返回 `EXPIRED`，页面却仍输出 `<img class="qr" src="/payment-assets/qr/wxpay?v=...">`、尾差提示与 `let seconds=0`。
- 修复一：`renderPaymentPage` 以 `expires_at <= Date.now()` 为准判定过期，不等 Cron 改状态；`EXPIRED`/`CLOSED`/已过期三种情况一律不渲染收款码与倒计时，改渲染「订单已失效 / 请返回商户页面重新下单」提示块，并把脚本初始化为终态 `let finished=true`。
- 修复二：`PAID` 订单同样不再渲染收款码与倒计时，改渲染「订单已支付 / 无需重复付款，正在跳转回商户页面」，避免重复付款；轮询到 `PAID` 仍按原逻辑跳转 `returnUrl`。
- 修复三：倒计时改为绝对截止时间 `const deadline=Date.now()+<服务端秒数>*1000`，每次 `tick` 用 `deadline-Date.now()` 现算剩余秒数，彻底消除后台节流导致的漂移；不再使用递减计数。
- 修复四：新增 `finish()` 终态收敛——停掉 `setInterval`、隐藏 `#timerWrap` 与 `#qr`、写入状态文案；倒计时归零与轮询收到 `EXPIRED/CLOSED` 都走这一条路径。`PAID` 分支也先收起收款码再提示跳转，避免定时器把成功文案覆盖成“已过期”。
- 修复五：轮询改用单一 `pollTimer` 句柄与 `schedulePoll()`，并监听 `visibilitychange`：切回页面时立刻 `tick()` 重算剩余时间并 `schedulePoll(0)` 重新拉状态，不必等被节流的定时器醒来；单一句柄保证不会产生第二条轮询链。
- 边界保留：微信通道不可用的提示只对 `PENDING` 且未过期的订单生效；该状态下倒计时保留，因为订单仍可付款、也可能自然过期。可用性翻转触发的整页刷新加了 `!finished` 守卫，终态页面不会反复刷新。
- 样式：`.unavailable` 泛化为 `.notice` 并提供 `.notice.bad` / `.notice.good` 两种配色，失效与不可用走红色、已支付走绿色。
- 测试：新增 `tests/payment-page.test.ts`（6 项），覆盖 PENDING 正常渲染、`EXPIRED`/`CLOSED` 隐藏收款码与倒计时、`expires_at` 已过但 Cron 未跑的空档、`PAID` 终态、绝对截止时间与终态收起收款码、`visibilitychange` 立即重算与单一轮询句柄；同时修正 `tests/wechat-channel-availability.test.ts` 的夹具——原先用固定时间戳 `2026-09-29T02:00:00+08:00` 造 `expires_at`，随真实时间推移会被新逻辑判为过期，改为区分监控端判定用的固定参考时间与页面渲染用的真实时钟。`npm test` 17 文件 84 项通过。
- 验证中自查出的判定顺序缺陷：首轮实现把 `expired` 放在 `paid` 之前判断，导致**已支付但 `expires_at` 已过去**的订单（付款完成后必然会这样）在线上被渲染成「订单已失效」，实测订单 `33de1077605c40598c50557b704ea535` 页面标题为 `<h2>订单已失效</h2>`。已改为先判 `paid`，`expired` 追加 `!paid` 前置条件，并补测试用例「keeps a paid order as paid even after its expiry time has passed」。
- 断言加固：页面脚本里本身就带有「订单已失效」「请返回商户页面重新下单」等终态文案字面量，`toContain("订单已失效")` 即使提示块没渲染也会通过；相关断言全部改为匹配渲染出的 `<h2>订单已失效</h2>` / `<h2>订单已支付</h2>` 与提示正文。
- 实机核对：用 esbuild 打包 `src/pages.ts` 后在 Node 中渲染五种状态（PENDING 可用、PENDING 但 `expires_at` 已过、`EXPIRED`、`PAID`、PENDING 微信不可用），逐一确认收款码、倒计时与提示块的有无以及 `finished` 初值。

### OPS-021 修改管理员密码失败：Workers 的 PBKDF2 迭代上限

- 现象：后台修改密码仍返回 `{"code":-1,"msg":"请求处理失败"}`。这次不是数据库约束——OPS-020 之后约束类错误已会给出可读文案，说明抛出的是运行时异常且错误文本不是自有错误码格式。
- 排查过程：`wrangler tail` 无法使用，`tail.developers.workers.dev` 的 TLS 握手被网络重置（`api.cloudflare.com` 正常，说明只是该 WebSocket 域名不通）；GraphQL Analytics 里 `workersObservabilityStaticGroups` 与 `workersObservabilityAdaptiveGroups` 均报 unknown field，`workersInvocationsAdaptive` 存在但过滤参数名对不上且账号类型禁止内省，历史日志这条路走不通。
- 取证方式：临时部署一个随机路径的一次性诊断端点 `GET /__cfpay-diag/<48 位十六进制令牌>`，只对固定字符串跑 `crypto.subtle` 的 PBKDF2，不读写任何业务数据、不涉及密钥；用 10000 / 100000 / 200000 / 310000 / 600000 五档迭代逐一实测。
- 根因（实测原文）：`200000`、`310000`、`600000` 三档全部抛 `NotSupportedError`，消息为 `Pbkdf2 failed: iteration counts above 100000 are not supported (requested 310000).`；`10000` 与 `100000` 正常返回 32 字节。即 **Workers 的 `crypto.subtle` 对 PBKDF2 单次调用有 100000 次迭代硬上限**，而 `src/auth.ts` 的 `PASSWORD_ITERATIONS` 是 `310_000`，因此后台保存密码这条路径从上线起就没成功过——与生产 `admin_setting.password_hash` 一直为空、密码实际回退到 Worker Secret `ADMIN_PASSWORD` 的现象一致。
- 为什么前端只看到兜底文案：该异常 `instanceof Error` 为真，`message` 是一句英文描述，既不命中错误码映射，也不匹配自有错误码格式 `^[A-Z][A-Z0-9_]{2,63}$`，于是落到最后一行通用文案。
- 修复一（KDF）：`PASSWORD_ITERATIONS` 提到 `600_000`（OWASP 对 PBKDF2-HMAC-SHA256 的建议值），并改为**链式派生**——以 `WORKER_PBKDF2_MAX_ITERATIONS = 100_000` 为一轮，上一轮 256 位输出作为下一轮口令、盐保持不变，`rounds = iterations / 100000`。总工作量与一次性 600000 次相同，但每次调用都在运行时上限内。`hashAdminPassword` 增加前置校验：迭代次数必须是 100000 的整数倍、不小于 100000、不超过 1000000（与 `admin_setting.password_iterations` 的 CHECK 区间一致），否则抛新错误码 `INVALID_ADMIN_PASSWORD_ITERATIONS`（已登记中文映射）。`verifyAdminPassword` 无需改动，迭代次数照旧随哈希一起入库并参与派生。
- 修复二（可诊断性）：`app.onError` 对 `/admin-api/*` 请求（**登录接口除外**，避免未认证调用者拿到运行时细节）把原始错误文本截断 200 字符后附在消息里，即 `请求处理失败：<原因>`；公开接口仍只返回通用文案，不外泄 SQL 与运行时细节。
- 实测验证：诊断端点改用生产的 `hashAdminPassword` / `verifyAdminPassword` 跑 100000 / 600000 / 1000000 三档，全部 `ok=true`、`roundTrip=true`、`rejectsWrong=true`，`storedIterations` 与 `hashLength=43`、`saltLength=22` 均正确，说明链式派生没有触发 CPU 上限（`Date.now()` 在 Workers 同步计算期间不前进，因此耗时无法用计时判断，只能以请求是否被 1102 终止为准，三档均正常返回 JSON）。
- 清理：验证完成后移除诊断端点与临时导入的 `hashAdminPassword` / `verifyAdminPassword`，重新部署，线上 `/__cfpay-diag/<令牌>` 返回 404。
- 测试：`tests/auth.test.ts` 新增 2 项——链式派生达到目标工作量（同盐下 600000 与 100000 的哈希不同、正确口令通过、错误口令拒绝、用别的迭代次数校验同一份哈希必须失败）与非法迭代次数全部抛 `INVALID_ADMIN_PASSWORD_ITERATIONS`（含旧默认值 `310_000`）。`npm test` 17 文件 87 项通过，`tsc --noEmit` 通过。
- 文档：README 第 1 章与第 12 章、`docs/architecture.md` 安全边界均更新为链式派生说明并记录 Workers 的 100000 次上限原文，避免以后再把迭代数调回超限值。

### OPS-022 放开限制与密码修复的生产验证

- 入口与账号已实际改成功（`admin_setting.updated_at` 为 `2026-09-29 03:02:58`、`session_version` 由 2 升到 4）：账号改成含 `@` 与 `.` 的邮箱形式、入口改成 5 字符短路径。这两者在放开前都会被拒——旧应用层正则不允许 `@`，旧的 8–96 长度下限不允许 5 字符，说明 OPS-019 与 OPS-020 两处放开都已生效。本条刻意不记录入口与账号原文，避免把在用入口写进仓库。
- 入口路由实测：新入口与其带尾斜杠形式均返回 200 并渲染后台页面；旧入口、`/admin`、`/admin.html` 均 404；`/admin.js`、`/api/health` 正常；未登录访问 `/admin-api/session` 返回 401「未登录或会话已过期」。
- 密码仍未落库（`password_hash` 为空、`password_iterations` 为 NULL），即登录仍回退到 Worker Secret `ADMIN_PASSWORD`；OPS-021 修复的是保存路径，需要管理员在后台重新提交一次新密码才会写入哈希。
- 部署变量保持原样：`wrangler.jsonc` 的 `ADMIN_PATH` / `ADMIN_USERNAME` 只是数据库无记录时的引导回退值，**刻意不同步为在用入口**，否则等于把后台入口明文提交进仓库。
- 收银台终态在本次部署后复验：已支付订单渲染「订单已支付」、已过期订单渲染「订单已失效」，两者均无收款码与倒计时；一次性诊断端点已移除，令牌路径与任意猜测路径均 404。

### OPS-023 管理后台改用 cffk 的 Vue 3 + shadcn-vue 前端重构

- 需求：甲方要求禁止纯 HTML+CSS+JS，管理页样式与布局改用 cffk 的 UI 框架重做。
- 技术栈选型：cffk 是 Vike + Vue 3 + Tailwind CSS v4 + `reka-ui`（shadcn-vue new-york / neutral）+ `@lucide/vue` + `vue-sonner` 的 Cloudflare Workers 项目。本次只重构管理页，因此沿用它的 UI 框架与后台布局，但**不引入 Vike SSR、telefunc 与 better-auth**——cfpay 已有自己的 Hono 接口与 HMAC 会话，改成 SSR 等于重写后端鉴权，超出“重构管理页样式和布局”的范围。管理后台做成 Vue 3 SPA，由 Worker 以静态资源方式下发。
- 工程隔离：新增 `admin-ui/` 独立 npm 包（自带 `vite.config.ts`、`tsconfig.json`、TypeScript 5 与 `vue-tsc`）。仓库根仍是 Worker 的 TypeScript 7 + Vitest，两套工具链互不干扰；根 `package.json` 增加 `admin:install` / `admin:build` / `admin:dev` / `admin:typecheck` / `admin:verify`，并把 `deploy` 改成 `admin:build → db:migrate:remote → wrangler deploy`，避免忘记构建就部署。
- 组件复用：从 cffk 的 `components/ui/` 复制 31 个组件族共 179 个文件到 `admin-ui/src/components/ui/`，跳过依赖 `@internationalized/date`、`@unovis/vue`、`reka-ui/date` 的 calendar / chart / combobox / date-picker / date-range-picker / range-calendar；复制前先审计依赖闭包，确认这些组件只依赖 `@/lib/utils` 与少量 ui 内部互引，不含 vike / telefunc / server / auth-client。`AdminDataTable.vue` 直接沿用 cffk 原件；`AdminSidebar`、`AdminLayout`（对应 cffk 的 `@adminPath/+Layout.vue`）、`AdminPageHeader`、`SecretDialog`、`SettingSection`、`StatusBadge`、`CopyButton`、`DetailDialog` 按 cffk 的写法重写为 cfpay 版本，导航从 `<a :href>` 换成 `RouterLink`。
- 主题：`admin-ui/src/styles/tailwind.css` 取自 cffk 的 `pages/tailwind.css`，只删掉店面专用的 `login-payment` / `login-delivery` 令牌与 `.product-rich-content` 富文本样式，其余设计令牌与 `.dark`、`@layer base` 完全一致。
- 入口与路由：入口路径可由管理员随时修改，不能写进构建产物。`admin-ui/index.html` 保留 `window.__CFPAY_ADMIN_BASE__ = "/"` 占位，Worker 下发首页时用新增的 `injectAdminBase` 替换为真实入口（占位符被改写时兜底在 `</head>` 前追加更晚执行的赋值），`vue-router` 用它作 history base。`matchesAdminEntry` 扩展为同时命中入口本身与其所有子路径，Worker 一律返回同一份 SPA 首页，子路由刷新不再 404。
- 资源层坑：Cloudflare 资源层默认 `html_handling` 会把 `/admin-app/index.html` 以 **307** 跳到目录形式，首版实现直接透传该响应，导致 `/cfpay` 返回 307 而不是页面。改为请求目录形式 `ADMIN_SPA_ENTRY = "/admin-app/"`，并保留一次跟随 `location` 的兜底。
- 安全：`BLOCKED_ADMIN_PATHNAMES` 扩到 `/admin`、`/admin.html`、`/admin.js`、`/admin-app`、`/admin-app/index.html`，`RESERVED_ADMIN_PATH_SEGMENTS` 增加 `admin-app`、去掉已不存在的 `vendor`；SPA 产物只能经管理员配置的入口获取。`public/admin-app/` 与 `admin-ui/node_modules/` 加入 `.gitignore`，仓库只存源码。
- 删除的旧前端：`public/admin.html`（164 行）、`public/admin.js`（约 590 行）、`public/vendor/jsQR.js` 及其 LICENSE 全部移除；`public/styles.css` 从 36 行压缩到 13 行，只保留落地页需要的 `.landing` / `.landing-card` / `.eyebrow` / `.health`。二维码解析改用 npm 包 `jsqr`（`lib/qr.ts`，仍优先原生 `BarcodeDetector`），不再依赖 vendored 浏览器构建。
- 功能对齐：登录、支付网关（含四个接口路径与复制、商户 ID/密钥随机生成）、收款码上传与删除、支付宝证书上传/测试查询/删除、监控端密钥与在线设备卡片、微信登录二维码按需弹窗（倒计时、登录成功自动关闭、新二维码自动换图）、未匹配收款详情、订单列表与详情弹窗（订单 + 收款事件 + 结构化字段 + 上报 JSON + 回调任务与尝试记录）、人工补单、管理员安全，全部按原语义移植；错误提示统一走 `vue-sonner`，密钥弹窗提示指向真实的保存按钮名。
- 健壮性：`lib/detail.ts` 的详情构造器对缺失字段一律按空对象渲染成 “-”，后端返回异常结构时不会整页崩掉；文件选择改用 change 事件保存 `File`，不再依赖组件 ref（组件 ref 拿到的是实例而非 DOM 元素）。
- 静态测试：`tests/admin-console-wiring.test.mjs` 按 SPA 结构重写，覆盖旧静态文件已删除、产物目录已忽略、`deploy` 串联前端构建、`@/` 别名导入全部可解析、路由组件文件存在、侧边栏导航与路由一致、前端调用的 `/admin-api/*` 与 Worker 注册路由完全对齐、无 OSS 残留、Worker 走 `matchesAdminEntry` + `ADMIN_SPA_ENTRY` 下发且屏蔽产物直连。`npm test` 17 文件 94 项通过，`tsc --noEmit` 与 `vue-tsc --noEmit` 均通过。
- 浏览器实测：本机原本无浏览器，Playwright 1.63 不支持 Ubuntu 20.04，改装 `playwright@1.44.1` + `npx playwright install chromium` 成功。新增 `tools/verify-admin-ui.mjs`（`npm run admin:verify`）用真实 Chromium 打开线上后台，用 `page.route` 把 `/admin-api/*` 换成固定夹具，无需管理员口令即可巡检；两次运行均 **18 项全通过、退出码 0、零 console 错误、零未捕获异常**，覆盖登录页（无侧边栏）、六个功能页、侧边栏与面包屑、订单详情弹窗、未匹配收款详情弹窗、微信登录二维码弹窗（含 `剩余有效时间 02:5x，登录成功后自动关闭`）、密钥生成弹窗、客户端路由跳转（`/cfpay/qr-codes`）、子路由刷新不 404，截图 11 张落在 `SHOT_DIR`。
- 排查记录：首轮自动化有两处失败，均为脚本夹具问题而非应用缺陷——一是事件 ID 里的 `:` 被前端 `encodeURIComponent` 编码成 `%3A`，夹具键未命中导致返回 `{}`；二是“生成密钥”点到了第一个「生成」按钮（商户 ID，只弹 toast 不弹框）。修正后复验通过，并顺带补上了前述详情构造器的容错。另有一次 Playwright 报“元素在视口外”无法点击，单独复现两次均正常（按钮 x=1356/right=1399 在 1440 视口内、`scrollWidth === clientWidth` 无溢出、Escape 后无残留 dialog 节点），判定为自动化交互层偶发，脚本改用 `safeClick` 兜底。
- 部署：Worker 版本 `dca47e2c-98e9-49d5-9044-e0dfe825180a`；线上 `/cfpay`、`/cfpay/orders`、`/cfpay/monitors` 均 200 且注入 `window.__CFPAY_ADMIN_BASE__="/cfpay"`，`/admin-app`、`/admin-app/index.html`、`/admin`、`/admin.js` 均 404，落地页 `/`、`/styles.css`、`/api/health`、收银台 `/pay/:id` 全部 200，三个构建产物资源 200（入口 JS 5.2 KB、api 分片 158.5 KB、CSS 83.7 KB）。

### OPS-024 心跳提速到 5 秒、后台菜单重分类、文案去重与移动端溢出修复

- 监控端版本 `0.9.0 → 0.9.1`（`kMonitorVersion`、`kMonitorUserAgent`、`wechat_config.cpp` 的心跳 body 与 WinHTTP UA、`tools/lib/wechat-monitor-constants.mjs`）。

**心跳 30 秒 → 5 秒**

- 代码默认值：`wechat_monitor.cpp` 的 `Config::heartbeatIntervalMs` 由 `30'000` 改为 `5'000`，`loadConfig` 的 `positiveSeconds("heartbeatIntervalSeconds", 30)` 改为 `5`，配置界面补默认值的 `wechat_config.cpp:700` 同步改为 `5`，`config/wechat-monitor.example.json` 改为 `5`。
- 现场关键点：只改代码默认值对运行中的监控端**无效**——`%LOCALAPPDATA%\CFPay\WeChatMonitor\config.json` 里显式写着 `"heartbeatIntervalSeconds": 30`，配置文件优先于默认值。已把该文件改为 `5`（改前备份并校验：JSON 仍可解析、17 个键完整、`monitorSecret` 未丢），配置只在监控端启动时读取，因此需要重启监控端才生效。
- 刻意不改的两处容错窗口：`MONITOR_ONLINE_WINDOW_MS` 与 `WECHAT_CHANNEL_FRESH_WINDOW_MS` 仍是 90 秒。心跳 5 秒意味着 18 次容错余量；若把窗口同步收紧到 30 秒，一次网络抖动就会让收银台判定微信不可用并隐藏收款码，误伤真实收款。组件状态重复上报间隔 `kStatusRepeatIntervalMs` 也保持 30 秒，避免 D1 写入量无谓放大 6 倍。
- 写入量核算：5 秒心跳约 17,280 次 D1 写/天/设备，仍在 D1 免费额度（10 万行写/天）内。
- 文档：`docs/monitor-protocol.md` 的“建议每 30 秒发送一次”改为默认 5 秒并说明 90 秒在线窗口与 18 次容错；`docs/wechat-native-observer.md` 两处 30 秒心跳描述与版本号同步；README 的配置表拆出 `heartbeatIntervalSeconds` 单独一行并标注默认 5 秒。

**后台菜单重分类**

- 原分类不合理：`支付网关` 单独置顶，`收款码`/`支付宝证书` 归到「收款配置」，而 `监控端` 与 `订单列表` 一起塞进「运行监控」——订单是业务数据不是监控指标，支付网关本身也是收款配置。
- 新分类：`监控端`、`订单列表` 提为顶层单项（日常巡检优先），`支付网关`、`收款码`、`支付宝证书` 合并进「收款配置」模块，`管理员安全` 顶层单列。侧边栏图标映射按新 path 重排。

**文案重复（甲方反馈“监控页有重复文字”，实际是全站性问题）**

- 取证方式：用 Playwright 抓每页说明性文案的叶子节点，先做完全重复检测，再用最长公共子串找语义重复。首版检测只做“去标点后完全相等/包含”，漏掉了监控页那处（两段文字中间插了不同短语），改为“公共子串 ≥ 8 字且含 ≥ 6 个汉字”后才全部命中；同时用汉字条件排除 `POST / GET`、`appCertPublicKey_` 这类结构性文本的误报。
- 确认并修复 5 处：监控页「设备不持久保存 / 心跳超过 90 秒自动移除」同时出现在页面描述与区块副标题（甲方反馈的那处）；支付网关页「密钥留空表示保持原值」同时在页面描述与区块描述；收款码页「原图只在本机解析，上传的是二维码内容而不是图片」在微信与支付宝两个面板各写一遍；管理员安全页「保存后…用新入口重新登录」同时在页面描述与表单提示；支付宝证书页「加密后存入 D1」在页面描述与区块描述各出现一次。
- 处理原则：页面级描述只讲“这页管什么”，区块级描述只讲该区块特有的语义，同一句话不在同屏出现两次；重复的提示保留更靠近操作位置的那一处。
- 已固化为 `tools/verify-admin-ui.mjs` 的「文案无重复」巡检项，六个页面逐页断言。

**移动端溢出（390×844）**

- 取证结果：`/gateway` 32 个元素超界（最右 469，视口 390）、`/monitors` 74 个（最右 1015）、`/orders` 31 个（表格 725）。且 `document.scrollWidth` 始终等于视口宽度——说明内容不是可横向滚动，而是被 shadcn `Card` 的 `overflow-hidden` **直接裁掉**，`/gateway` 的「复制」按钮在手机上根本点不到。
- 根因：flex 与 grid 子项默认 `min-width:auto`，不会收缩到内容宽度以下；`Input`（`w-full`）与按钮同排时把父容器撑宽，宽表格把设备卡片撑到 1000px 以上，再被祖先裁切。
- 修复：在整条布局链上补 `min-w-0`——`SidebarInset`、`main`、各页根 `section`、`AdminDataTable` 的外层与表格容器、`SettingSection` 的 header/content/footer、设备卡片及其 header、`dl` 与各 `dd`、所有 `flex gap-2` 输入行；输入框加 `min-w-0 flex-1`、并排按钮加 `shrink-0`；详情弹窗宽度改为 `w-[calc(100vw-2rem)] max-w-[calc(100vw-2rem)] sm:max-w-3xl`，内部宽表格由 `overflow-hidden` 改为 `min-w-0 overflow-x-auto`（在边框内滚动而不是被裁切）；设备 ID 的 `code` 加 `min-w-0 max-w-full` 让 `truncate` 生效，长值 `dd` 加 `break-all`；二维码按钮文案较长，加 `h-auto max-w-full whitespace-normal` 允许换行。
- 判定口径：位于 `overflow-x:auto/scroll` 容器内的元素算“容器内滚动”，不判溢出；被 `hidden/clip` 祖先裁掉的一律判失败。已固化为工具的「移动端不溢出」巡检项（7 个页面 + 详情弹窗）。

**会话残留清理**

- 现状盘点：`/tmp` 累计 155 个 21 位随机名目录（Vite dev 的 SSR 依赖预构建缓存，79 MB）、50 个 Ghidra 反编译转储 `.txt`、以及 cffk 联调留下的探针 body/headers、构建日志、HTML 抓取与 JSON 转储共 274 项 92 MB；另有本次会话自己的脚本、截图目录，以及一个因变量未求值而误建的 `/tmp/${SHOT_DIR}` 目录；Windows 侧还有路径变量未展开遗留的 `%LOCALAPPDATA%\CFPay\WeChatMonitor\%LOCALAPPDATA%\CFPay\wechat-monitor.log`。
- 清理结果：分两轮删除 246 项与 273 项，`/tmp` 从 100+ MB 降到 11 MB（仅剩 playwright 依赖与系统目录）；Windows 侧游离目录一并删除。删除前先 dry-run 打印清单与体积，并跳过 `codex-daemon-1000`、`systemd-private-*`、`.X11-unix`、`bun-node-*` 等运行时/系统目录；确认无进程以 `/tmp` 为 cwd。
- 防复发：`tools/verify-admin-ui.mjs` 的截图默认写入 `mkdtempSync` 一次性目录并在结束时 `rmSync`，只有显式设置 `SHOT_DIR` 才保留；`tools/build-single-exe.sh` 增加历史版本化工件清理，升版本不再在 `dist/single-exe/` 堆积 5 MB 的旧 EXE（本次即清掉 0.9.0 那份）。

**发版**

- 单 EXE 重新构建：`cfpay-wechat-monitor-0.9.1-weixin-4.1.15.13-win64.exe`，5,282,878 字节，SHA-256 `ba85eccf43189a9572f225b69f8ffaa555e9a5080165e0295d41eee97d817d66`，内嵌观察 DLL 1,062,126 字节，内嵌校验 `yes`，导入表仍只有 Windows 系统 DLL。
- 验证：`tsc --noEmit` 与 `vue-tsc --noEmit` 通过，`npm test` 17 文件 94 项通过。

### OPS-025 后台登录接入 Cloudflare Turnstile（参考 cffk 配置）

- 参考实现：cffk 用 `lib/turnstile-config.ts` 读取 `TURNSTILE_SITE_KEY` / `TURNSTILE_SECRET_KEY`，两个 Key 同时存在才 `enabled`；公开端点 `GET /api/security/turnstile` 下发 `{enabled, siteKey}`；登录页按需加载 `https://challenges.cloudflare.com/turnstile/v0/api.js?render=explicit` 并显式渲染，令牌通过 `x-captcha-response` 头提交；服务端由 better-auth 的 captcha 插件校验。
- cfpay 落地：新增 `src/turnstile.ts`，`getTurnstileConfig` 与 cffk 完全同构（两个 Key 同时配置才启用，只配一个视为未启用，避免前端弹了验证框而后端不校验）；`verifyTurnstileToken` 自行调用 `siteverify`（cfpay 没有 better-auth），带 8 秒超时，返回 `{success, errorCodes}`，并对传输失败分类为 `verify-timeout` / `verify-network-error` / `http-<status>` / `invalid-verify-response`，便于后台区分“该重试”和“令牌无效”。
- 校验顺序：`POST /admin-api/login` **先过人机验证再比对口令**——顺序反了就挡不住暴力破解。失败按原因抛 `TURNSTILE_REQUIRED` / `TURNSTILE_REJECTED` / `TURNSTILE_VERIFY_UNAVAILABLE`，其中验证服务不可用返回 503（fail-closed，不放行）。
- 端点：公开 `GET /api/security/turnstile` 只回 `{enabled, siteKey}`，Site Key 是 Turnstile 浏览器组件必须使用的公开标识；管理端 `GET /admin-api/security/turnstile` 只回 `enabled` 与两个 Key 的“是否已配置”布尔值，任何 Key 原文都不在管理页展示，**Secret Key 永不出 Worker**。
- 前端：新增 `admin-ui/src/composables/useTurnstile.ts`，未启用时完全不引入任何第三方脚本；启用时加载官方脚本并显式渲染，令牌过期/出错自动清空，登录失败后调用 `reset()`（令牌一次性，不重置会一直带着已消费的令牌重试）。登录按钮在拿到令牌前置灰并显示“请先完成人机验证”。`SecurityPage` 增加只读状态卡片与 `wrangler secret put` 配置指引。
- 配置：`Env` 增加两个可选变量，`.dev.vars.example` 与 `package.json` 的 `cloudflare.bindings` 补充说明；Secret Key 必须用 `wrangler secret put` 配置，不能写进 `vars`。
- 测试：新增 `tests/turnstile.test.ts`（8 项），覆盖“两个 Key 同时配置才启用”、空白与非字符串值、未启用时不调用 Cloudflare、缺令牌直接拒绝、siteverify 的请求体包含 secret/response/remoteip、Cloudflare error-codes 透传、以及四类传输失败的分类与非法 error-codes 过滤。

### OPS-026 设备心跳与组件状态合并为一次上报

- 甲方指出两个接口重复：`/api/monitor/heartbeat` 与 `/api/monitor/status` 都是 5 秒一次、都要 HMAC 签名、回答的都是“监控端活着且 Hook 正常”。确认属实后合并。
- 服务端：`recordComponentStatus` 现在在同一个 `DB.batch` 里同时 upsert `monitor_device` 与 `monitor_component_status`；设备名/平台/版本缺省时用 `COALESCE` 保留原值，因此 0.9.1 之前的监控端仍能正常上报。`/api/monitor/status` 只带设备字段（监控端刚启动、还没有任何 Hook 状态）时退化为只刷新在线状态。
- 兼容路由：`/api/monitor/heartbeat` 保留为过渡别名，仅供 0.9.0 及更早版本使用，全部升级后可删除。刻意保留而不是直接删：升级 EXE 需要完全退出微信，过渡期内旧监控端必须仍显示在线。
- 监控端：`networkLoop` 不再发独立心跳，改为每 `heartbeatIntervalMs` 调用 `reassertStatus` 重申最近一次已知的组件状态。必须由监控端主动重申——观察 DLL 只在状态变化或每 30 秒输出一次记录（`hardware_observer.cpp` 的 `nextHealthReport`），单靠它无法保证 5 秒节奏。
- 消除重复实现：甲方指出 `reportStatus` 与 `reassertStatus` 是同一路由同一份载荷的两套实现。已抽出 `statusPayload`（唯一载荷构造）与 `postStatus`（唯一 POST 与记账出口），两个函数都只做各自的前置工作再委派。
- 顺带修掉一个真 bug：合并前 `reassertStatus` 用的是快照里的**原始英文** message，而 `reportStatus` 上报的是翻译后的中文，等于每 5 秒把后台说明刷回英文。现在翻译发生在写入缓存之前（`ReportFingerprint.extra` 存的就是中文），日志与上报文本也统一成同一份。
- 另一个边界修复：只带设备字段的重申不再用空快照覆盖已记录的组件状态，否则下一次状态变化会失去去重依据。
- 窗口解耦：`MONITOR_ONLINE_WINDOW_MS` 从 90 秒收紧到 **10 秒**（心跳 5 秒 × 连续 2 次未收到即离线）；`MONITOR_COMPONENT_FRESH_WINDOW_MS` 保持 **90 秒**，因为 0.9.1 之前的观察 DLL 每 30 秒才重发一次状态，压到 10 秒会让旧客户端的组件状态永远判定过期、收银台长期隐藏微信收款码。`/admin-api/monitors` 现在同时下发 `onlineWindowMs` 与 `componentWindowMs`，前端的“上报超时”阈值改为跟随服务端下发值，不再自己写常量。

### OPS-027 Hook 上报说明全部中文化

- 现状：后台「说明」列直接显示 `detail_message`，而观察 DLL 通过管道传回的是英文（`Weixin.dll is not loaded in the target process`、`hardware breakpoint installation failed`、`waiting for current Weixin account observation; loginState=unknown; ...` 等），监控端自己的字面量也是英文。
- 方案：**在监控端这一层统一翻译，不改动已注入微信进程、对风控敏感的观察器**。新增 `observerMessageDescription(detailCode, message)`：已是中文（含非 ASCII 字节）直接透传；否则查英文到中文的映射表（21 条，覆盖 DLL 的全部散文描述与四个读取失败码）；带 `;` 的指标串复用仓库既有的 `statusMetrics` 中文标签映射；映射不到的未知描述回退成 `statusDescription(detailCode)`，保证后台永远不会出现英文。
- 覆盖面：`reportStatus` 的上报载荷、`updateDisplay` 的界面说明、以及监控端自己的 16 条英文字面量全部中文化。本地日志此前已经是中文（`statusDescription` + `statusMetrics`），这次让**上报值**与日志一致。
- 自检：`--self-test` 新增 20~24 五条断言，覆盖已知英文散文翻译、指标串前缀+指标同时翻译、含分号但无指标的整串映射、已中文透传、未知描述回退诊断码说明。
- 指标键名（`loginState=` 等）保留英文：它是 DLL 与 EXE 之间的内部线格式，`statusMetrics` 负责在展示层翻译，改键名会同时破坏诊断观察器的输出。

### OPS-028 微信目标标识集中到 wechat_target.h

- 问题：甲方指出 `payload["targetVersion"] = "Weixin 4.1.15.13"` 是写死的。核查发现 `4.1.15.13` 散落在 5 处代码、`0x106fb10` 散落在 5 处、三个账号/昵称 RVA 在 `wechat_host.cpp` 与 `hardware_observer.cpp` 各写一份。升级微信版本时漏改任意一处，就会出现“宿主校验的是 A 版本、观察器断点打在 B 版本偏移”这种直接崩微信的错误。
- 结论：**不该做成配置项**。这些值全部来自对某个具体 `Weixin.dll` 的逆向，换版本必须重新逆向 RVA 与签名字节；可配置只会让人误以为改个字符串就能换版本。正确做法是集中到一处、由代码审查把关。
- 实现：新增 `native/wechat-observer/wechat_target.h`（`namespace cfpay::target`），集中版本号、文件大小、SHA-256、WMPF 指纹与四个 RVA。`wechat_host.cpp` 的九个常量改为指向头文件的别名（名字不变，使用点零改动），`hardware_observer.cpp` 与两个诊断观察器的 RVA 同样改为引用，`wechat_monitor.cpp` 的上报文本改为派生：`weixinTargetLabel()` 拼 `"Weixin " + kWeixinVersion`、`parserHookOffsetText()` 用 `%x` 格式化 `kNewSyncParseRva`。
- 防回归：`--self-test` 新增 25/26 断言，要求派生结果与历史上报值逐字相同（`Weixin 4.1.15.13` / `0x106fb10`），避免格式化写法变化悄悄改变上报内容；`tests/wechat-native-log-language.test.mjs` 新增用例断言三个文件都不再出现字面量、只引用头文件。
- 构建脚本：`tools/build-single-exe.sh` 的微信版本改为从 `wechat_target.h` 的 `kWeixinVersion` 解析（原来解析 `wechat_host.cpp` 的 `kExpectedVersion`），`kObserverApiVersion` 仍从 `wechat_host.cpp` 取。

### OPS-029 后台菜单重分类、文案去重与移动端溢出修复

- 菜单重分类：原分类把「订单列表」塞进「运行监控」（订单是业务数据不是监控指标），又把「支付网关」排除在「收款配置」之外（它同样是收款配置）。改为：`监控端`、`订单列表` 提为顶层单项，`支付网关`/`收款码`/`支付宝证书` 合并进「收款配置」模块，`管理员安全` 顶层单列。
- 文案去重（甲方反馈监控页有重复文字，实测是全站性问题）：用 Playwright 抓每页说明性文案的叶子节点做检测。首版只做“去标点后完全相等/包含”，漏掉了监控页那处（两段中间插了不同短语）；改为“最长公共子串 ≥ 8 字且含 ≥ 6 个汉字”后全部命中，汉字条件同时排除 `POST / GET`、`appCertPublicKey_` 这类结构性文本的误报。共修 5 处：监控页“设备不持久保存 / 心跳超过 90 秒自动移除”同时出现在页面描述与区块副标题（甲方反馈的那处）、网关页“密钥留空表示保持原值”、收款码页“原图只在本机解析”在两个面板各写一遍、安全页“保存后用新入口重新登录”、支付宝页“加密后存入 D1”。原则：页面描述只讲这页管什么，区块描述只讲该区块特有语义，同一句话不在同屏出现两次。
- 移动端溢出（390×844 实测）：`/gateway` 32 个元素超界（最右 469）、`/monitors` 74 个（最右 1015）、`/orders` 31 个（表格 725）。且 `document.scrollWidth` 始终等于视口宽度——内容不是可横向滚动，而是被 shadcn `Card` 的 `overflow-hidden` **直接裁掉**，`/gateway` 的「复制」按钮在手机上根本点不到。根因是 flex/grid 子项默认 `min-width:auto` 不收缩。修复：整条布局链补 `min-w-0`（`SidebarInset`、`main`、各页根 `section`、`AdminDataTable` 外层与表格容器、`SettingSection` 三段、设备卡片及其 header、`dl` 与各 `dd`、所有 `flex gap-2` 输入行），输入框加 `min-w-0 flex-1`、并排按钮加 `shrink-0`，详情弹窗宽度改为 `w-[calc(100vw-2rem)] max-w-[calc(100vw-2rem)] sm:max-w-3xl`，内部宽表格由 `overflow-hidden` 改为 `min-w-0 overflow-x-auto`（在边框内滚动而不是被裁切），设备 ID 的 `code` 加 `min-w-0 max-w-full` 让 `truncate` 生效，长值 `dd` 加 `break-all`，二维码按钮文案加 `h-auto max-w-full whitespace-normal` 允许换行。
- 判定口径：位于 `overflow-x:auto/scroll` 容器内的元素算“容器内滚动”不判溢出；被 `hidden/clip` 祖先裁掉的一律判失败。
- 固化：`tools/verify-admin-ui.mjs` 新增“移动端不溢出”（7 页 + 详情弹窗）与“文案无重复”（6 页）两组巡检，截图改为写入 `mkdtempSync` 一次性目录并在结束时删除，只有显式设置 `SHOT_DIR` 才保留。

### OPS-030 支付宝到账事件的存储与展示

- 甲方问“支付宝查单存的是原始数据吗，怎么只有 3 个 key”。核查 `normalizeLedgerIncome`：存的确实不是账务接口原始响应，而是 4 个键的精简摘要（`accountLogId` / `alipayOrderNo` / `merchantOrderNo` / `memo`），且后三个常为 null；账务明细本身有 7 个字段。
- 修复一（存储）：改为保存完整账务明细——补上 `transAmount`、`transDt`、`direction`，并加 `source: "alipay-accountlog"` 与微信侧的 `source: "paymsg"` 对齐，后台据此判断该用哪套字段名。旧事件保持原结构不受影响。
- 修复二（展示）：`admin-ui/src/lib/detail.ts` 的 `PAYMENT_FIELD_LABELS` 原本只有微信 paymsg 的字段名，支付宝事件的键一个都不匹配，导致「结构化字段」整块消失。现补齐支付宝七个字段的中文标签，并增加**未登记键兜底**：任何不在标签表里的键都按原始键名展示，避免以后后端加字段而后台看不见。

### OPS-031 全量函数注释

- 甲方要求每个函数都加注释。先量化缺口：原生 5 个主文件 225 个、Worker 侧 123 个、前端自有 23 个。
- 完成情况：原生 14 个文件 **323** 个函数、Worker `src/` **142** 个函数、`admin-ui/src` 自有代码 **29** 个函数，合计 **494** 个，全部补齐，脚本复查缺注释为 0。
- 刻意不加注释的范围：`admin-ui/src/components/ui/**` 与 `admin-ui/src/lib/utils.ts` 是从 cffk 原样复制的 shadcn-vue 组件与 `cn()` 工具，改动会让后续从上游同步变困难，因此保持与上游一致。
- 注释内容要求写清“为什么”而不只是复述签名：例如 `parseMoneyToFen` 说明为何全程整数不走浮点、`validateWeixinMemory` 说明磁盘校验通过不代表内存就是这个版本、`updateThreadBreakpoint` 说明只改调试寄存器因此不触发微信的代码完整性检查、`hashAdminPassword` 说明 Workers 的 100000 次迭代上限与链式派生原因、`MONITOR_COMPONENT_FRESH_WINDOW_MS` 说明为何不能与在线窗口取同一个值。
- 构建脚本 `--self-test` 的路径处理修复：原实现用 `wslpath -w` 生成反斜杠路径再交给 bash 执行，bash 无法执行导致自检恒定报“失败”；改为优先用 WSL 形式路径执行 Windows PE，失败再回退。

### OPS-032 会话残留清理与防复发

- 盘点：`/tmp` 累计 155 个 21 位随机名目录（Vite dev 的 SSR 依赖预构建缓存，79 MB）、50 个 Ghidra 反编译转储、以及 cffk 联调留下的探针 body/headers、构建日志、HTML 抓取与 JSON 转储共 274 项 92 MB；另有本次会话自己的脚本与截图目录、一个因变量未求值误建的 `/tmp/${SHOT_DIR}` 目录；Windows 侧还有路径变量未展开遗留的 `%LOCALAPPDATA%\CFPay\WeChatMonitor\%LOCALAPPDATA%\CFPay\wechat-monitor.log`。
- 清理：分两轮删除 246 项与 273 项，`/tmp` 从 100+ MB 降到 11 MB（仅剩 playwright 依赖与系统目录），Windows 侧游离目录一并删除。删除前先 dry-run 打印清单与体积，并跳过 `codex-daemon-1000`、`systemd-private-*`、`.X11-unix`、`bun-node-*` 等运行时/系统目录；确认无进程以 `/tmp` 为 cwd。
- 防复发：验证工具截图改为一次性临时目录并自动删除；`tools/build-single-exe.sh` 增加历史版本化工件清理，升版本不再在 `dist/single-exe/` 堆积 5 MB 的旧 EXE。

### OPS-033 体检中另外发现并修复的问题

- 商户密钥时序侧信道：`handleEpayQuery` 的 `pid+key` 直传鉴权用 `params.key === setting.merchant_key` 比较，`===` 会在首个不同字节处短路；同一函数的签名路径用的是 `constantTimeEqual`。已统一改为定长比较。
- 详情构造器健壮性：`orderDetailSections` / `eventDetailSections` 假定响应结构完整，后端返回异常结构时会抛 TypeError 并让弹窗打不开（自动化验证时正是这样暴露的）。改为对缺失字段一律按空对象渲染成 “-”。
- 文件选择的 ref 取值：`QrCodesPage` 原本用组件 ref 拿 `<input type="file">`，但组件 ref 拿到的是组件实例而不是 DOM 元素。改为在 `change` 事件里记住选中的 `File`。

### OPS-034 部署阻塞与必须遵守的升级顺序（重要）

- 阻塞：wrangler 的 OAuth token 于 `2026-09-28T20:59:38Z` 过期且非交互环境无法刷新（`Not logged in. Your auth token has expired and could not be refreshed`），因此**本轮所有 Worker 侧改动都还没部署**，也无法查询远端 D1。需要甲方在交互终端执行 `wrangler login`，或提供 `CLOUDFLARE_API_TOKEN`。git 推送走 SSH 部署密钥，不受影响；打标签触发的 Release 由 GitHub Actions 自行构建，也不依赖 Cloudflare 凭据。
- 现场实况（读取 `%LOCALAPPDATA%\CFPay\wechat-monitor.log`）：正在运行的监控端是 **0.8.1**，`02:33:12` 启动时记录 `心跳间隔秒=30`，日志里两次心跳相隔 30 秒。我改过的 `config.json`（`heartbeatIntervalSeconds: 5`）只在启动时读取，**尚未生效**。
- 因此存在一个会直接阻断微信收款的顺序陷阱：新 Worker 把在线窗口收紧到 10 秒，而现场监控端还在 30 秒一次心跳——先部署就会立刻判定设备离线，收银台随之隐藏微信收款码。
- 正确顺序（务必按此执行）：
  1. `wrangler login` 恢复 Cloudflare 凭据。
  2. **先重启监控端**（退出监控端再启动即可；观察 DLL 内容未变时不需要退出微信），让它读到 `heartbeatIntervalSeconds: 5`。此时线上仍是旧 Worker（90 秒窗口），5 秒心跳完全安全。
  3. 再 `npm run deploy`（会先构建管理后台前端，再应用迁移，最后部署 Worker）。此时监控端已是 5 秒节奏，10 秒窗口不会误判，无停机。
  4. 有空时再升级到 0.9.1 的 EXE：必须**先完全退出微信**（内嵌观察 DLL 内容变化，被运行中的微信占用时会报 `OBSERVER_DLL_LOCKED_BY_RUNNING_WEIXIN`），替换 EXE 后启动监控端，再启动微信。
- 顺序 2 与 3 不能颠倒；顺序 4 可以延后，0.8.1 的监控端在新 Worker 上仍能正常工作（设备字段缺省时用 `COALESCE` 保留原值，`/api/monitor/heartbeat` 兼容路由仍在）。

### OPS-035 清理 Windows 与项目内的历史残留

- 甲方指出 `%LOCALAPPDATA%\CFPay` 与项目目录里仍有大量只创建不删除的残留。删除前先 dry-run 打印每一项与体积，并区分“活的”与“死的”。
- Windows 侧删除 19 项、64.9 MiB：八个逆向研究目录（`WeChatSessionDiagnostic-0.8.1` 11.4M、`WeChatNetworkDiagnostic` 6.4M、`WeChatSlotDump` 6.0M、`WeChatSyncDump` 5.0M、`WeChatFieldTest-0.8.1` 5.0M、`WeChatWmpfDiagnostic` 2.5M、`WeChatServiceDiagnostic` 2.5M、`NetworkUnwindHarness` 1.5M）、十个 `wechat-smoke-2026092*` 烟测目录（各 2.0M），以及 `WeChatMonitor/backup-0.8.1-parser`（4.9M，是被推翻的旧解析器偏移的备份，不是有效回滚目标）。
- Windows 侧刻意保留：`WeChatMonitor/`（运行用的 EXE、观察 DLL、config.json）、`wechat-monitor-state.json`（**投递队列与已确认收款账号候选**，`pending=0`、`delivered=11`，删掉会丢失已确认的收款账号、被迫重新确认）、`wechat-monitor.log`。
- 项目侧删除 46 项、32.2 MiB：`dist/wechat-hook-harness`（21.4M，诊断 harness 构建产物）、`dist/windows-wechat-monitor`（5.0M，已被单 EXE 路径取代）、`dist/worker-dry-run`（1.1M）、21 个一次性调试用 `.ps1`、18 个接口响应转储（`.json` / `.jsonl` / `.status`，其中 `production-login.headers` 与 `alipay-admin-login.headers` 可能含会话 Cookie，属于不该留在磁盘上的东西）、以及 2026-09-27 那次 `wrangler deploy --dry-run` 留下的 `dist/README.md`、`dist/index.js`、`dist/index.js.map`；另清理 `.wrangler/state/v3/observability`（4.2M，本地可观测性追踪库，可再生）。
- 项目侧保留：`dist/single-exe`（当前 0.9.1 发布工件）、`dist/single-exe-build`（增量编译缓存，删掉每次都要全量重编）、`work/`（已入库的逆向取证记录）、两处 `node_modules`、`public/admin-app`（部署所需产物）。
- 合计删除 65 项、释放 97.1 MiB，0 失败。清理后复查：`git status` 干净、`npm test` 18 文件 105 项通过、`tools/build-single-exe.sh --self-test` 通过、`dist` 从 49M 降到 21M、`.wrangler` 从 4.8M 降到 616K。
- 顺带修一个被清理暴露的脆弱点：`tools/build-windows-wechat-monitor.mjs` 只做打包、不编译，直接假定 `dist/wechat-hook-harness/cfpay-wechat-monitor.exe` 已存在，目录被清掉后会抛一个看不懂的 ENOENT。改为先 `access` 检查并给出明确指引（先跑 `tools/build-wechat-hook-harness.sh`，或改用推荐的 `npm run wechat:monitor:single`），实测退出码 1 且提示可读。
- 约定写进 README 目录结构：`dist/` 只放构建产物，一次性调试脚本与接口响应转储不得写入。
- **清理过程中发现的重要现场状态**：读取 `wechat-monitor.log` 发现监控端已于 `2026-09-29 12:01:53` 正常退出（`Hook宿主已退出 exitCode=0 lastStatus=HEALTHY` 紧接 `原生监控端已停止`），比本次清理早约 53 分钟，与删文件无关；退出前一直是 `HEALTHY / HOOK_ACTIVE`、微信 `processId=3968` 已登录。也就是说当前微信侧没有监控端在线，微信收款不会被自动识别，需要重新启动监控端。该进程此前运行的是 `version=0.8.1`、`心跳间隔秒=30`，即改过的 `config.json`（5 秒）尚未生效。
- 日志另显示 `11:59:56`~`12:00:39` 有一段上报失败（`winError=12002` 超时、`winError=12029` 无法连接），随后自行恢复，属于本机网络/代理抖动，不是监控端缺陷。

### OPS-036 上报 401/400 定位、HTTP 全量日志与状态上报合并（监控端 0.9.2）

**甲方提出的三个问题**

1. `hook 上报日志报 401，为什么界面上是 400？`
2. `401 可能是管理端没更新？`
3. `状态上报一处就行了；不要分 hook 上报和微信登录状态上报，两个合到一起。`

**401 与 400 是两个不同原因，都不是「管理端没更新」**

- 401 的来源被服务端错误码映射锁死：`errorStatus` 只在错误码包含 `SIGNATURE` 或 `AUTH_REQUIRED` 时返回 401，时间戳过期（`MONITOR_TIMESTAMP_EXPIRED`）落到 400，重放落到 409。因此日志里的 401 只可能是 `INVALID_MONITOR_SIGNATURE`，即签名密钥不一致。
- 现场取证：用 `config.json` 里当前的 `monitorSecret` 对线上 `https://pay.example.com/api/monitor/heartbeat` 签一次名并发出，返回 **HTTP 200 `{"ok":true}`**。说明 D1 里的密钥与配置文件一致，Worker 侧没有问题，也不是「管理端没更新」。
- 结论：401 是**正在运行的监控端进程内存里握着一份旧密钥**。`config.json` 的修改时间是 `13:38:43`，而配置只在进程启动时读取一次；日志显示该进程按 5 秒节奏持续 401（`13:56:15`~`13:57:58`）。修复动作就是重启监控端，不需要改代码。
- 界面上的 400 是另一条路径：配置页「测试连接」在 0.9.1 改为 POST `/api/monitor/status` 且只带设备字段，而**线上部署的还是旧 Worker**，旧 Worker 要求组件字段，于是返回 `INVALID_MONITOR_COMPONENT` → 400。部署新 Worker 后即为 200。
- 两个问题共同暴露一个真正的缺陷：**监控端从不读取响应体**。`MonitorApiClient::post` 只在调用方显式传 `responseJson` 时才读响应，状态上报都没传，于是服务端已经给出的 `{"code":-1,"msg":"监控端签名错误"}` 被整个丢弃，日志只剩一个 `httpStatus=401`。甲方随后明确要求：`你给 hook 的日志写详细点不就知道了，包括请求体响应体；请求 code，响应 code`。

**HTTP 全量日志（含脱敏）**

- `post()` 改为无条件读取响应体（上限 8 KiB），并统一调用 `logHttpExchange` 记录：请求路径、HTTP 状态码、WinHTTP 错误码、服务端业务码、服务端中文说明、请求体与响应体（各按 1 KiB 截断并标注原始长度）。
- 落盘策略：调试开关打开时成功与失败都记完整报文；**失败即使用户没开调试日志也无条件落盘**（新增 `appendLogAlways`，与原 `appendLog` 共用 `writeLogLine`）。上报失败正是「现场只剩一个状态码就无从排查」的场景，不该被开关挡住。
- 脱敏：`login.qrPayload`（谁拿到谁就能登录该微信号）与收款原文 `raw`（消息正文）只记录字节数与 SHA-256 前 12 位；请求体不是合法 JSON 时只记录字节数、不写原文。`eventId` 本身是 `sha256(transactionId)`，可以原样保留。密钥与签名只存在于请求头，日志刻意不记录任何请求头。
- 配置页「测试连接」同步改进：读出响应体里的 `msg` 并显示成 `Worker 连接测试失败，HTTP 400：监控组件名称无效`，不再只有一个裸状态码。
- 自检新增 34~38 号断言，验证脱敏确实挡住了二维码与收款原文、同时保留其它字段。

**状态上报合并为单一出口**

- 之前同一个 5 秒周期里会朝 `/api/monitor/status` 发两次内容重叠的请求（`reportStatus` 状态变化时发一次、`reassertStatus` 定期重申发一次），登录状态又单独走 `/api/monitor/wechat-login`，日志里因此出现 `Hook状态已上报`、`状态上报结果`、`微信登录状态已上报` 三条并行时间线。
- 现在改为「快照 + 单点上报」：`setComponentState` / `setLoginState` 只更新内存快照（`ReportSnapshot`）并按内容指纹决定是否置脏，真正发请求的只有网络线程的 `postStatus`，一个路由、一份载荷构造、一套记账。`reportStatus`、`reassertStatus`、`reportWechatLoginState` 三个函数删除，22 处调用点机械改写（同时去掉不再需要的 `client` 与 `observedAt` 参数，`handleHostRecord`、`runHost` 的 `client` 形参一并移除）。
- 载荷里微信登录状态是 `login` 子对象，允许的键与它单独成一条路由时完全一致；二维码只在 `QR_READY` 时上行（`setLoginState` 清空 + `statusPayload` 二次拦截）。
- 服务端 `/api/monitor/status` 在同一个 D1 batch 里写完 `monitor_device`、`monitor_component_status`、`wechat_login_session` 三张表；`login` 载荷非法时整批拒绝，避免出现「设备在线但登录状态是旧的」。`/api/monitor/wechat-login` 路由删除，`/api/monitor/heartbeat` 仍作为旧监控端过渡兼容保留。
- 节奏与退避：状态变化立即上报，无变化按 5 秒重申，失败按 1 秒退避（`kReportRetryMs`），避免脏标记让网络线程每 250 毫秒空转。现场测试需要知道二维码是否真的送达 Worker，因此 `publish` 改为 `waitForLoginUpload` 等待登录指纹被成功上报（上限 6 秒，必须大于一个上报周期），而不是自己再发一次请求。
- 界面状态拆分：`updateDisplay` 不再接收 `networkOk`（改状态的线程并不发请求，让它顺手写「上报成功/失败」只会把还没发生的结果画到界面上），网络线程通过新的 `setNetworkOk` 单独维护。

**顺带修掉的问题**

- 测试断言自相矛盾：`expect(native).not.toMatch(/30'000/)` 与同一文件里 `expect(source).toContain("... >= 30'000")` 直接冲突，导致 105 项测试里有 1 项恒定失败。收窄为只禁止 30 秒作为**上报节奏**存在（`kReportIntervalMs`、`nextReport*`），保留现场测试等待上限与观察器启动宽限这两处合法用途。
- 版本号写死在 5 处（`wechat_monitor.cpp` 的 `kMonitorVersion` 与 `kMonitorUserAgent`、`wechat_config.cpp` 的探测 body 与 UA、`tools/lib/wechat-monitor-constants.mjs`）。新增 `native/wechat-observer/wechat_monitor_version.h` 作为唯一来源，UA 由版本号拼出，配置页探测请求改用 json 构造；构建脚本与 Node 常量都改为从头文件解析（`wechat-monitor-constants.mjs` 同时解析 `wechat_target.h` 的微信版本与 Hook 偏移）。这与甲方此前指出的 `targetVersion` 写死是同一类问题。
- 批量加注释时产生的 `// //` 双斜杠笔误共 105 处、涉及 19 个文件，全部收回成单层注释。
- 过时注释：`reassertStatus` 上方仍写着「观察 DLL 每 30 秒输出一次」，而观察 DLL 早已改成 5 秒；随该函数一起删除。
- 监控端版本 `0.9.1 → 0.9.2`。`bash tools/build-single-exe.sh --self-test` 构建通过、内嵌校验 yes、Windows 自检通过（EXE SHA-256 `0c5bb4b4aa02376852847d831d077720116a5d2fddee09f7227106d0ae647a7b`）；`npm test` 18 文件 108 项通过，`tsc --noEmit` 与 `vue-tsc` 均无错误。

**升级顺序与过渡期影响（已按此执行）**

1. 先 `npm run deploy` 部署新 Worker。`/api/monitor/status` 开始接受 `login` 子对象；不带 `login` 的旧监控端照常工作，组件状态与收款事件都不受影响。
2. **过渡期唯一的损失**：`/api/monitor/wechat-login` 已删除，因此仍在跑 0.9.1 的监控端上报登录状态会拿到 404，后台「显示登录二维码」在这期间不可用（监控端每 5 秒重试一次并在日志里记一条错误，不影响收款）。要恢复扫码登录就必须完成第 3 步。
3. 替换监控端 EXE 到 0.9.2：内嵌观察 DLL 内容有变（现场 DLL `b085e2a8…` → 新构建 `38c2474c…`），必须**先完全退出微信**，否则会报 `OBSERVER_DLL_LOCKED_BY_RUNNING_WEIXIN`；退出微信后需要重新扫码登录。
4. 只重启监控端（不动微信）也能立刻解决现场那个 401——密钥是启动时读取的；但登录二维码要等 EXE 升级到 0.9.2 才恢复。

**部署与线上验证（已执行）**

- `npm run deploy` 两次成功，最终 Worker 版本 `18b8945c-1be4-46c3-897c-f150255a8eb7`；标签 `v0.9.2` 已推送，Release 由 Actions 构建。
- 线上协议验证（临时设备号 `wm-probe-verify`，不触碰真实设备与微信通道状态，10 分钟后由定时任务清理）：
  1. 只带设备字段 → **200**（0.9.1 配置页「测试连接」曾在这里拿到 400）；
  2. 设备 + `login` 子对象 → **200** 且回显 `"login":"QR_READY"`；
  3. `login` 带多余键 `reason` → **400 `{"code":-1,"msg":"微信登录状态参数无效"}`**；
  4. `login` 传成字符串 → **400**（不再静默忽略）；
  5. 故意用错误密钥签名 → **401 `{"code":-1,"msg":"监控端签名错误"}`**，即监控端日志今后会记录的完整原因；
  6. 旧的 `/api/monitor/wechat-login` → **404**（已删除）。
- 后台入口实测：线上 `admin_setting.admin_path` 现值为管理员在后台设置的私有入口（不是 env 里的默认值），请求该入口返回 200 并注入 `window.__CFPAY_ADMIN_BASE__="<入口>"`；入口下的 `/orders`、`/channels/wechat` 返回 200；旧书签 `/<入口>/monitors`、`/<入口>/qr-codes` 由前端兜底路由重定向到微信渠道页而不是白屏；`/admin-app`、`/admin-app/index.html`、`/admin` 均 404。
- `npm run admin:verify`（真实 Chromium + 路由夹具）**32 项全通过、退出码 0**：六个功能页、侧边栏与面包屑（`交易 / 订单列表`）、订单详情弹窗、微信登录二维码弹窗（`剩余有效时间 02:45，登录成功后自动关闭`）、未匹配收款详情弹窗、生成密钥弹窗、客户端路由跳转 `/cfpay/channels/alipay`、子路由刷新、移动端 390×844 不溢出、六页文案无重复、零 console 错误。
- 巡检脚本本身修了两处：`playwright` 改为动态导入，缺失时直接提示 `npm i -D playwright && npx playwright install chromium` 并以退出码 2 结束，不再抛 `ERR_MODULE_NOT_FOUND`；溢出探测遇到 `hidden/clip` 祖先时不再提前停止向上查找——`truncate` 单元格会把内容裁住，元素画不到屏幕外，提前 break 会把所有数据表格误判成溢出（本次 3 项失败全是这个假阳性）。
- 顺带修一个真实布局问题：组件表「说明」列的 `max-w-56 truncate` 挂在 `td` 上，自动表格布局下 `max-width` 不生效，内容会把整张表撑宽；改为在单元格内用 `<span class="block max-w-56 truncate">`，与未匹配收款页既有写法一致。
- 清掉最后一个 30 秒上报间隔的来源：遗留 Node 工具（`tools/lib/wechat-monitor-setup.mjs`、`wechat-monitor-service.mjs`、`tools/wechat-monitor-web/`、`run-windows-wechat-monitor-e2e.ps1`）仍会把 `heartbeatIntervalSeconds: 30` 写进 `config.json`——现场那份配置里的 30 就是这么来的。现在该键不再写入也不再读取，Node 侧服务固定 5 秒，与 C++ 监控端的 `kReportIntervalMs` 和服务端的 10 秒窗口统一口径；`config.json` 里已有的残留键会被忽略。

## Git 追溯

### OPS-037A 监控状态内存服务

- 2026-10-05 新增单例 `MonitorState` Durable Object，设备、组件、微信登录、二维码和 nonce 仅保存在实例内存，不调用 Durable Object Storage。
- 本阶段只建立服务、类型和 Cloudflare 绑定，尚未切换原有 D1 状态链路，便于后续提交独立验证实时推送与移除写入。

### OPS-037B 监控状态实时推送

- 状态上报在保留原 D1 写入的同时双写 `MonitorState`，管理端新增认证 WebSocket，心跳到达后立即广播设备、Hook 和微信登录快照。
- WebSocket 断线后每 2 秒轮询，3 秒后重连；使用 Worker `serverTime` 校准本地时钟，避免两个 5 秒周期错相显示虚假的“7 秒前上报”。

### OPS-037C 移除监控状态 D1 写入

- `/api/monitor/status`、兼容心跳、组件状态、微信登录、二维码和 nonce 全部切换到 `MonitorState`，不再写入 D1；真实支付事件仍按业务要求持久化。
- 管理端监控读取、收银台微信可用性和登录二维码统一读取内存快照；Cron 同步移除 nonce、离线设备和登录会话清理，只保留持久业务任务。
- 在线窗口固定为 10 秒：监控端每 5 秒上报，连续两次未收到即判定失联，不再引入额外 15 秒或 30 秒配置。

### PAY-014 管理端真实支付分步测试

- 2026-10-05 明确测试边界：不允许模拟到账；必须创建正式订单并由用户真实付款，才能验证收款采集链路。
- 管理端新增“交易管理 / 支付测试”，默认建议 0.01 元，创建订单、尾差、收银台、到账匹配和回调任务全部复用正式业务链路。
- 微信测试只认可 Windows Hook 上报的 `wechat:` 真实事件，管理员补单不能通过来源校验；支付宝只认可官方账务查询产生的 `alipay:` 流水。
- 页面每 2 秒展示六个阶段：订单创建、真实到账、采集来源、自动匹配、回调执行、回调验签；不提供模拟事件或模拟成功接口。
- 测试订单使用独立设备标识，在订单列表显示“测试订单”，禁止补单，并从运营面板订单量、收款额和趋势中排除；真实测试数据仍保留在 D1，便于完整链路追溯。

### OPS-038 Cloudflare D1 用量面板

- 管理端面板新增 Cloudflare GraphQL Analytics 用量卡片，展示 UTC 当日读取行数、写入行数、查询次数、数据库存储和最近七日趋势。
- 免费计划按官方额度显示 5,000,000 行读取/日、100,000 行写入/日、5 GB 存储，并在 70%、90%、100% 三档显示预警；UTC 00:00（北京时间 08:00）重置。
- GraphQL 查询结果只在 Worker isolate 内存缓存 5 分钟，不写 D1；Analytics Token 只通过 Worker Secret 配置，接口和页面均不回显。
- 已使用当前 Cloudflare 账户的真实 GraphQL 响应验证 `d1AnalyticsAdaptiveGroups` 和 `d1StorageAdaptiveGroups` 字段结构。

### API-021 接口错误原因透明化

- 所有异常响应统一返回 `code`、中文 `msg` / `message` 和稳定 `error` 代码；只有无法识别的异常额外生成 `errorId`，Worker 日志用同一编号记录完整堆栈、方法和路径。
- 明确识别 D1 日写入/读取额度耗尽、SQL 参数数量错误、迁移缺失、约束失败、D1 暂时不可用、Cloudflare Analytics 配置/权限问题、支付测试不存在、支付宝上游错误和请求超时。
- `/submit.php` 保持易支付浏览器入口需要的纯文本错误，其余管理端、监控端和 `/mapi.php` 返回结构化 JSON；前端不再以“请求失败”掩盖缺失的后端原因。

### UI-004 面板趋势图与状态文案去重

- D1 最近七日用量由表格改为读取/写入额度使用率面积图，与订单趋势卡片在 `xl` 及以上宽度并排显示，较窄页面自动恢复上下两行。
- 根据上线后的视觉反馈再次拆分层级：D1 当前读写、查询和存储改为与首页现有指标一致的独立卡片网格，两张趋势卡内部只保留标题、图表与图例。
- D1 与订单趋势图统一标题区结构、间距、说明句式、图表高度、卡片拉伸规则和周期切换交互；两张图各自保留独立周期状态。
- 生产实测确认 Cloudflare 会拒绝跨度超过 `4w4d` 的 D1 Analytics 查询，官方数据也只保留最近约 31 天；D1 图表因此提供真实可用的 7/30 天切换，并保留禁用的“最近 3 个月”按钮说明平台限制，不伪造 90 天数据。
- D1 Analytics 查询按 `date` 日维度一次读取最近 30 个 UTC 自然日，前端再按所选周期裁剪，结果继续只在 Worker 内存缓存五分钟。
- 微信渠道设备卡只在“上报于”一处显示相对时间，在线徽标仅显示“在线”，不再重复出现两次“x 秒前”。

### OPS-039A 新增统一 D1 内存读穿服务

- 2026-10-06 新增单例 `RuntimeDataState` Durable Object，配置和业务实体优先读取实例内存，缓存未命中时才访问 D1 并自动回填。
- 首批迁移系统设置、管理员配置、支付宝凭据、收款码和订单实体；管理员会话校验与 Hook 验签不再为每个请求重复读取配置行。
- 缓存设置 48 MiB 总预算和 256 KiB 单条非固定数据上限，支持最近访问淘汰、负缓存、前缀失效、统计查询和管理员手动清空；清空只影响内存，不删除 D1 数据。
- `/api/health` 改为纯运行状态检查，不再使用读取 `system_setting` 作为健康判断，避免外部探测持续消耗 D1 行读取。

### OPS-039B 迁移订单与支付查询缓存

- 2026-10-06 订单按系统订单号和商户订单号建立双向内存索引，创建订单改为 `INSERT ... RETURNING`，不再写入后额外读取一遍。
- 收银台状态、易支付查单、后台订单列表、订单详情、支付事件、未匹配收款、面板趋势和真实支付测试优先读取 `RuntimeDataState`；历史实体按需读穿，列表与统计视图写后失效并保留 60 秒安全有效期。
- 微信 Hook、支付宝账务、管理员补单和商户回调完成 D1 写入后同步更新订单与支付事件缓存，同时失效订单列表、面板、未匹配收款、回调明细和支付测试视图。

### OPS-039C 精简定时任务和支付宝轮询

- 2026-10-06 删除每分钟全局订单过期更新和重复的支付宝轮询激活；新订单创建前只清理对应渠道的过期尾差槽位，读取订单时根据 `expires_at` 计算有效状态。
- 支付宝轮询通过内存中的活跃订单索引判断是否继续运行，冷启动只查询一条未过期支付宝订单，不再每轮执行两次 `COUNT(*)`。
- 回调 Cron 将租约超时恢复和到期任务扫描合并为一次候选查询，空闲时不再执行无条件 `UPDATE`；只有实际发现超时租约才恢复任务。

### OPS-039D 清理重复字段和遗留数据表

- 2026-10-06 新增 `0013_runtime_cache_cleanup.sql`，删除已由 `MonitorState` 取代的四张监控状态表、旧 `callback_sink` 表，以及 `system_setting` 中重复保存的两条收款码 URL。
- 微信与支付宝收款码继续以 `payment_qr_code.provider` 主键保证每个渠道只有一条记录；保存和删除不再双写系统设置，收银台地址根据渠道和摘要动态生成。
- 删除旧监控心跳路由、callback sink 路由、旧监控 D1 写入函数、微信登录二维码 D1 加解密链路和相关过渡测试；新增未匹配收款、订单过期和回调租约索引。

### OPS-039E 增加运行缓存与测试数据管理

- 2026-10-06 管理端面板新增运行缓存卡片，展示缓存条目、估算内存、命中、未命中、D1 回源和淘汰次数，并支持只清空内存、不删除 D1 数据。
- 真实支付测试页新增“清理历史测试”操作，只删除已完成、关闭或过期的测试订单；等待真实付款的测试订单不会删除。
- 测试记录清理按支付事件、回调级联和订单顺序执行，完成后同步移除订单实体缓存并失效订单列表、面板和支付测试视图。

### OPS-039F 生产迁移、部署与文档校准

- 2026-10-06 使用项目专用 SSH 私钥将 OPS-039A 至 OPS-039E 的七个中文提交推送到 `main`，远程 D1 成功应用 `0013_runtime_cache_cleanup.sql`，生产 Worker 版本为 `34c721c7-a227-431a-9607-dee179c5d217`。
- 验证通过：Worker 与管理端 TypeScript 类型检查、22 个测试文件 117 项测试、Vue 生产构建、0001 至 0013 全迁移顺序执行、远程外键检查、健康接口、D1 动态管理入口 `/cfpay`、Turnstile 必填校验以及微信/支付宝二维码 SVG 渲染。
- 远程结构确认只保留正式持久数据表；`monitor_device`、`monitor_component_status`、`monitor_nonce`、`wechat_login_session` 和 `callback_sink` 均已删除，原有系统设置、管理员设置与两类收款码数据保留。
- 管理端浏览器巡检脚本逐页包含固定等待，150 秒外部验证窗口在移动端最后一轮等待时结束；终止前没有页面断言报错，类型检查、构建和线上接口验证独立通过。
- 同步修正 README、架构和监控协议中的旧描述：监控状态、微信登录二维码和 nonce 只在 `MonitorState` 内存，D1 仍只承担正式订单、支付事件、回调、配置与支付宝凭据持久化。

### UI-005A 统一面板时区、缓存指标与交易术语

- 2026-10-06 将订单统计统一描述为“北京时间自然日”，将 D1 统计统一描述为“Cloudflare 计费日（北京时间每日 08:00 重置）”；D1 卡片不再使用会与业务日混淆的“今日”。
- 运行缓存接口新增 `budgetBytes`，管理端把“估算内存”改为“缓存数据量（估算）”，使用 `B / KiB / MiB / GiB` 二进制单位并显示 48 MiB 预算占比，不再出现“7.44万 B”。
- 订单、详情和真实支付测试统一使用“订单金额 / 实付金额 / 回调状态 / 收款事件”；回调状态统一为“待回调、回调重试中、回调成功、回调失败”。
- 完整上报数据统一命名为“解析字段”和“完整上报 JSON”，避免与微信消息原文或未解析网络数据混淆。

### UI-005B 修正配置状态、操作反馈与空状态

- 2026-10-06 Turnstile 状态拆分为读取中、未启用、配置不完整和已启用四种，完全未配置时不再错误显示“未完整配置”；密钥名称统一补充 Site Key / Secret Key 的中文用途。
- 支付宝页面改为“证书已配置 / 证书未配置”，保存和删除使用一次成功 Toast，手动账务查询使用包含条数与追踪编号的页面结果，不再同时显示两条成功反馈。
- 支付配置、站点设置、安全配置、账户设置和支付测试根据当前动作显示明确失败标题；已有页面错误区域时不再重复弹出错误 Toast。
- 收款码空状态改为引导用户选择图片并保存，密钥弹窗明确关闭后仍需提交页面表单；表格默认空状态移除英文句号。
- 微信监控字段明确为客户端地址、微信进程 ID、状态说明、最近收款时间和最近上报时间；未收到登录或 Hook 状态时使用可理解的空状态。

### UI-005C 修正收银台、错误提示与无障碍中文

- 2026-10-06 收银台不再向付款人暴露 Hook、监控端或微信登录等内部状态；微信不可用时只提示返回商户页面并改用支付宝重新下单。
- 订单过期统一说明为“系统无法再自动确认这笔订单”，避免“付款无法自动到账”被误解为资金不会到达；收款码缺失时提供返回商户或联系商户的下一步。
- 已支付页面按是否存在返回地址分别显示“即将返回商户页面”或“可以关闭此页面”，轮询成功后的状态文案保持一致。
- D1 配额错误统一说明北京时间每日 08:00 重置；订单创建、二维码不可用和服务器响应格式错误改为符合当前实现且可执行的中文提示。
- Dialog、Sheet 和公共弹窗关闭按钮的可见及屏幕阅读器文案统一为“关闭”，清除组件库残留的英文 `Close`。

### UI-005D 增加前端文案回归检查

- 2026-10-06 新增字节格式单元测试，覆盖 `B / KiB / MiB / GiB` 的边界值和面板实际使用的缓存数据量，防止重新出现“7.44万 B”一类难以理解的显示。
- 收银台测试补齐订单过期、已支付且有返回地址、已支付且无返回地址、收款码缺失和微信通道不可用等状态，确保提示下一步且不向付款人暴露 Hook、监控端等内部实现。
- 管理端浏览器巡检增加支付测试页，并逐页扫描旧时区术语、旧交易术语、英文关闭文案和错误标点；订单与未匹配收款详情同时校验“解析字段”和“完整上报 JSON”。
- 完整验证通过：24 个测试文件共 128 项测试（126 项通过、2 项按环境跳过），Worker 与管理端类型检查、生产构建、桌面/宽屏/移动端布局、重复文案和术语巡检均通过。

### OPS-040A 完善未匹配收款处理闭环

- 2026-10-06 新增 `payment_event_action` 审计记录和 `IGNORED` 事件状态；真实收款事件不再删除，自动匹配、人工匹配、忽略和恢复操作均保留操作者与时间。
- 管理端未匹配收款拆为“待处理 / 处理记录”，支持分页、超过 24 小时筛选、同渠道同实付金额订单匹配、必填原因忽略和误忽略恢复。
- 人工匹配复用正式到账确认与商户回调链路，排除真实支付测试订单；并发处理、金额渠道不一致和订单已关联均返回明确原因。
- 未匹配列表从监控状态接口移出，避免设备状态刷新重复携带交易数据，也不再因固定 `LIMIT 50` 隐藏更早的待处理记录。

### OPS-040B 补全订单与回调异常处理

- 2026-10-06 订单列表增加服务端分页、商户/系统订单号搜索，以及渠道、订单状态和回调状态组合筛选；保留旧 `limit` 参数作为第一页兼容入口。
- 订单列表在查询时计算有效过期状态，不再依赖同渠道创建下一笔订单才显示为“已过期”，同时避免为展示状态增加周期性 D1 写入。
- 回调失败订单增加“重试回调”，每次人工操作只追加一次立即尝试；原尝试次数、响应和错误记录全部保留，接口先返回再通过 Worker 生命周期执行请求，避免 15 秒回调超时拖死管理端请求。
- 订单和回调筛选条件同步到 URL，为面板异常卡片跳转到可直接处理的列表状态提供稳定入口。

### UI-006 调整面板默认周期与异常入口

- 2026-10-06 D1 用量趋势和订单趋势默认均改为最近 7 天，30 天与订单趋势 90 天仍可手动切换。
- 首页回调口径拆为“待完成回调”和“回调失败”；无需回调的 `NONE` 不再计入待处理数量，避免面板虚高。
- 首页新增“超时未处理”指标，固定按超过 24 小时仍为未匹配状态统计；未匹配、超时、待完成回调和回调失败卡片均可直接跳到对应筛选页面。
- 今日订单、今日收款、累计收款和 D1 用量继续作为纯指标展示，不制造所有卡片都可点击的误导。

### OPS-040C 增加交易处理回归验证

- 2026-10-06 新增顺序应用 `0001` 至 `0014` 迁移的内存数据库测试，验证既有已匹配收款保留、自动匹配审计回填以及 `IGNORED` 状态可正常写入。
- `0014` 移除 D1 迁移事务内无效的 `PRAGMA foreign_keys` 切换；当前重建表没有被其它表外键引用，可在保持外键校验开启的情况下完成迁移。
- 覆盖未匹配收款忽略原因校验、忽略与恢复状态转换，订单有效过期状态、分页与组合筛选，以及失败回调每次人工操作只追加一次尝试。
- 管理端浏览器巡检补齐两个趋势默认最近 7 天、回调失败卡片跳转、人工匹配与忽略弹窗、收款详情处理记录，并继续覆盖桌面、宽屏、移动端、重复文案和旧术语。
- 完整验证通过：25 个测试文件共 133 项测试（131 项通过、2 项按环境跳过），Worker 与管理端类型检查、生产构建和本地浏览器巡检全部通过。

### OPS-041A 完善失败回调处理闭环

- 2026-10-08 回调失败订单增加“标记已处理”，处理说明必填 1–200 字，并记录管理员和处理时间；失败响应与全部尝试记录保持不变。
- 已处理回调不允许再次重试，也不提供恢复入口；订单列表增加“回调已处理”筛选，详情展示处理说明，首页失败回调只统计尚未处理的记录。
- 数据库仅在 `callback_job` 增加处理元数据，不改写订单的真实 `FAILED` 回调结果，避免把人工接受误报成回调成功。

### PAY-015 将支付测试调整为快捷支付

- 2026-10-08 管理端“支付测试”改为“快捷支付”，商品名称、金额和渠道提供默认值但不单独持久化；创建后通过 URL 中的订单号继续查看诊断。
- 快捷支付创建的是普通订单，不再设置测试设备和附加标记，也不再从面板统计、候选匹配、补单或订单处理功能中排除；历史测试订单迁移后同样回归普通订单。
- 保留内置易支付回调作为普通 `notify_url`，并继续兼容历史回调路径；六阶段诊断分别展示商品订单、通道收银台、收款采集、匹配确认、回调任务和回调响应的实际数据。
- 收银台在所有订单状态下展示经过转义的商品名称，页面标题同步包含商品名称；桌面、宽屏、移动端和文案巡检全部通过。

### OPS-041B 增加订单与收款导出删除

- 2026-10-08 订单列表增加单条和勾选批量永久删除，覆盖待支付、已支付、已过期和已关闭订单；删除时同步清理关联收款、处理记录、回调任务和全部尝试，并失效运行缓存。
- 删除确认弹窗明确提示待支付订单可能产生后续未匹配到账、已支付订单会丢失审计数据，并要求输入商户订单号或批量确认短语；不提供按筛选条件直接删除。
- 删除前可选导出完整 JSON 或 CSV，但不强制；JSON 保留层级和完整原始数据，CSV 使用 UTF-8 BOM、文本保护和公式转义，避免长订单号、流水号和时间戳变成科学计数法。
- 未匹配收款仅允许永久删除已经填写原因的“已忽略”记录，待处理和已匹配记录不能单独删除；不新增任何自动清理任务。
- 内存 SQLite 回归验证了完整导出、长数字 CSV、订单关联数据级联删除以及已忽略收款状态限制；管理端全尺寸浏览器巡检通过。

### OPS-041C 补充交易生命周期回归验证

- 2026-10-08 新增失败回调处理测试：空处理说明必须拒绝，状态更新必须限定真实 `FAILED` 且尚未处理的任务，处理后记录管理员、原因和时间，并禁止再次重试。
- 快捷支付浏览器回归覆盖默认商品创建、普通订单跳转和商品与订单、通道与收银台、真实收款采集、匹配与确认、回调任务、回调响应六个阶段；历史 `/payment-test` 地址必须重定向到 `/quick-pay`。
- 收银台继续验证商品名称展示和 HTML 转义，避免商品内容进入标题或页面时形成脚本注入。
- 数据生命周期测试覆盖完整 JSON 导出、UTF-8 BOM CSV、长数字文本保护、公式注入转义、订单关联五张表级联删除，以及只有已忽略收款才允许独立导出和删除。
- 管理端浏览器巡检新增已支付订单删除风险、删除前 JSON/CSV 导出、失败回调处理和已忽略收款删除弹窗，并修正夹具路由保留查询参数，确保处理记录页使用真实筛选条件。
- 最终验证通过：25 个测试文件共 138 项测试（136 项通过、2 项按环境跳过），Worker 与管理端类型检查、生产构建和本地 Chromium 全页面巡检全部通过。

### UI-007 统一收银台页脚链接规则

- 2026-10-08 页脚每行不含 `|` 时继续按普通文字安全转义；使用“显示文字|目标”时生成 `<a>` 链接，并统一通过新窗口打开。
- 链接目标支持邮箱、HTTP/HTTPS 完整地址和裸域名，裸域名自动补全为 HTTPS；显式危险协议继续按普通文字显示，避免形成脚本链接。
- 站点设置页同步说明上述输入规则，收银台和公开首页复用同一套安全页脚渲染逻辑。

### PAY-016 支付宝内置浏览器直接付款

- 2026-10-08 收银台根据请求 UA 中的 `AlipayClient` 或 `AliApp(AP/` 标记识别支付宝内置浏览器；普通浏览器行为保持不变。
- 支付宝内打开待支付订单时，不再展示无法由同一设备扫描的支付宝二维码，改为“打开支付宝付款”按钮，点击后跳转到已保存的二维码原文。
- 直接跳转只允许 `https://qr.alipay.com/` 和 `alipays:`，不符合安全协议的内容继续回退显示二维码；订单支付或失效后按钮与二维码一起隐藏。
- 二维码原文只注入当前支付宝收银台页面，不通过二维码状态管理接口回显；专项测试覆盖两种支付宝 UA、普通浏览器回退、危险协议回退和按钮跳转。

### UI-003 账户设置宽屏布局与全局视觉回归

- 账户设置此前连续在“窄卡片”和“全宽卡片 + 窄表单”之间调整，根因是只检查单个宽度，没有把卡片、内容区和表单控件作为同一布局契约。
- 账户页现与其它系统配置页统一使用全宽 `SettingSection`，内部表单和输入框同样占满卡片内容区；按钮保持左对齐，不再单独使用 `max-w-2xl` 制造宽屏断层。
- 通用 Alert 不再依赖 `:has(>svg)` 扩展图标列，改为固定的 `min-content + minmax(0,1fr)` 网格，避免支付宝配置状态和 Turnstile 状态的图标压住标题。
- 公共详情弹窗补充屏幕阅读器说明，消除订单详情和未匹配收款详情打开时的无障碍警告。
- 管理端浏览器巡检新增 2048×1200 宽屏逐页边界检查、Alert 图标与标题矩形相交检查，以及账户页“输入框宽度必须占满卡片内容区”的硬性断言。

### GIT-001 敏感文件排除检查

- 已确认忽略：`.dev.vars`、`config/*.local.json`、`config/alipay/`、`dist/`、`node_modules/`。
- 证书、应用私钥和生成的 Worker Secrets 文件均未进入 Git 暂存区。
- 工作流：每一步完成后先更新本文，再验证，再创建独立提交。

### UI-008 统一动态终态与操作反馈

- 2026-10-08 收银台首次渲染、轮询发现支付成功、轮询发现订单失效和前端倒计时归零统一复用同一组终态提示卡；动态更新不再只修改底部状态文字，手动刷新与自动刷新后的结构、颜色和文案完全一致。
- 删除收银台独立状态栏和“支付成功，可以关闭此页面”第二条提示，已支付页面只保留一组“订单已支付”说明；进入支付或失效终态后同时停止轮询、移除倒计时并隐藏收款区域。
- 管理端复制按钮、快捷支付完成状态和支付宝账务查询移除与页面持久状态重复的成功 Toast；遗留监控配置页同样只保留状态区反馈，不再重复弹出成功提示。
- 回归测试锁定旧重复文案不得重新出现；管理端 Chromium 巡检覆盖全部十个业务页面的桌面、宽屏、移动端边界、文案重复和术语一致性。

### PAY-017 规范人工补单审计展示

- 2026-10-08 人工补单不再向 `payment_event.raw_excerpt` 写入英文占位文本 `manual settlement`，该字段只用于保存监控端或支付宝账务接口的真实结构化上报。
- 新补单在同一批次写入 `payment_event_action`，使用现有 `MANUAL_MATCH` 动作并记录“管理员人工补单”、当前管理员和处理时间；订单详情接口同步返回关联处理记录。
- 管理端订单详情新增独立“人工补单”区块，展示处理方式、操作管理员、处理时间和说明；人工补单不会再出现“解析字段”或“完整上报 JSON”，真实微信与支付宝事件仍保持原展示。
- 新增 `0017_normalize_manual_settlement.sql`：为历史人工补单补齐中文审计记录并清空英文占位原文；SQLite 迁移测试和 Chromium 人工补单详情测试均通过。

### AUTH-001 修复登录成功后偶发停留登录页

- 2026-10-08 确认服务端最近登录请求已经成功，问题位于前端：`POST /admin-api/login` 成功签发 Cookie 后，旧实现立即再次请求 `/admin-api/session`；二次探测偶发失败时仍显示“登录成功”，却把本地认证状态覆盖为未登录，路由守卫因此返回登录页。
- 登录接口成功响应增加管理员账号；前端直接采用该成功响应建立本地会话并跳转面板，不再追加一次无必要的会话探测，同时显式使用同源 Cookie。
- 会话探测增加递增版本保护，较早发起但较晚返回的探测不能覆盖更新的登录、退出或管理员配置变更状态。
- Chromium 新增完整登录提交回归：初始会话探测一次、登录请求一次，成功后稳定进入 `/dash`，不得再发第二次会话探测；本轮完整验证为 Worker 与管理端类型检查通过、25 个测试文件共 144 项测试（142 项通过、2 项按环境跳过）、生产构建通过、97 项浏览器巡检全部通过。

### PAY-018 规范人工补单支付确认来源

- 2026-10-08 人工补单调整为订单的支付确认来源，不再伪造 `manual:*` 收款事件，也不再把并不存在的真实到账标记为 `MATCHED`。
- `payment_order` 增加 `paid_source` 和 `manual_paid_by`：真实到账记录为 `EVENT`，人工补单记录为 `MANUAL`；补单订单继续使用正常支付成功、商户回调、回调重试、导出和删除流程。
- 历史人工补单迁移为订单级确认记录，管理员从既有中文操作记录回填，无法恢复时使用“历史管理员”；关联伪事件及其错误操作记录随迁移删除。
- 管理端订单列表增加“人工补单”标记，详情使用独立“支付确认”区块展示确认方式、管理员和时间，不再展示虚假收款事件或监控端原始数据。

### PAY-019 统一快捷支付单一入口

- 2026-10-08 快捷支付成为唯一的真实付款验证功能，页面组件、Worker 模块、默认商品名称和巡检夹具全部统一使用快捷支付命名。
- 删除管理端 `/payment-test` 重定向、Worker `/api/payment-test/callback/:token` 路由和旧 `cfpay:payment-test:*` 令牌校验；旧地址不再兼容，也不再维护第二套路由行为。
- 正式环境仅有一笔旧订单已回调成功、一笔旧订单已过期且没有回调任务，没有活动任务依赖旧地址，因此删除旧入口不会中断待执行回调。
- 新建快捷支付订单只生成 `/api/quick-pay/callback/:token`，回调只接受 `cfpay:quick-pay:*` 生成的令牌。

### CODE-001 清理重复实现与未调用代码

- 2026-10-08 删除已被 C++ 单 EXE 完全替代的 Node 微信监控服务、配置网页、状态文件实现、解析实现、旧 E2E 转发链路及其四组单元测试；生产和测试不再维护第二套监控运行时。
- 删除未被管理端入口引用的 UI 组件、已完成评审使命的 `site-settings-review` 原型、未调用的运行缓存读取函数，以及根包无引用的 `fast-xml-parser`、`jsqr`、`esbuild`。
- 监控在线窗口只保留在 `monitor-state.ts`，监控端每 5 秒统一上报、连续两次未收到即失联；相关测试改为校验真正执行在线判断的唯一常量。
- Worker 内部使用的状态类型取消无意义导出，README 与原生监控文档明确生产链路只有 C++ 单 EXE；协议测试继续使用独立临时接收端，不再依赖旧 Node 监控服务。
- 清理共删除 53 个仓库文件和约 3600 行重复或不可达实现；Worker、管理端类型检查通过，21 个测试文件共 115 项测试（113 项通过、2 项按环境跳过）、管理端生产构建和 C++ 单 EXE Windows 自检全部通过；生产依赖审计为 0 漏洞。
- 正式部署后巡检发现浏览器夹具仍用旧 `manual:*` 事件模拟补单，生产数据和页面实现均已正确；夹具同步改为 `paid_source='MANUAL'`、无收款事件，并锁定详情只显示“支付确认”。
- 修正后正式环境 97 项 Chromium 巡检全部通过，覆盖登录、全部业务页、补单详情、快捷支付、桌面/宽屏/移动端边界、重复文案和术语一致性。

### PERF-001 收敛管理端重复请求

- 2026-10-08 删除订单管理页每 15 秒自动轮询；订单列表只在首次进入、分页、筛选、手动刷新和业务操作完成后读取，后台标签页不再持续放大 D1 查询。
- 订单、收款事件和候选订单分页改为显式事件入口，删除 `watch` 与显式 `load()` 同时触发的双请求路径；筛选回到第一页时同样只读取一次。
- 三组列表读取增加请求序号保护，旧筛选请求较晚返回时不会覆盖最新页面状态。
- 快捷支付保留前台每 2 秒真实链路诊断；页面隐藏时暂停，重新显示后立即补查，切换订单或离开页面后旧请求不能覆盖当前状态。
- 管理端类型检查通过；后续 Worker 阶段将实现 `refresh=1` 的真实回源语义。

### PERF-002 修正刷新与轮询缓存边界

- 2026-10-08 订单、收款事件和面板接口增加管理员主动刷新语义：只有请求携带 `refresh=1` 时清理对应视图缓存并回源，普通页面进入继续使用运行缓存。
- Cloudflare D1 用量的手动刷新同步跳过 Worker isolate 内五分钟缓存；普通进入仍复用缓存，避免重复调用 GraphQL Analytics API。
- MonitorState 快照直接携带统一的微信通道可用状态；快捷支付每轮诊断只读取一次监控快照，不再连续请求同一个 Durable Object 的两个接口。
- 支付宝活动订单缓存发现最早订单自然过期后立即失效并回源一次；仍有后续有效订单时继续安排账务 Alarm，不再错误停止轮询。
- Worker 和管理端类型检查通过。
- 专项测试覆盖 D1 用量普通读取复用缓存、主动刷新强制请求 Cloudflare，以及支付宝最早订单过期后继续发现后续有效订单。
- 管理端源码回归锁定订单页不得恢复自动轮询、列表分页不得恢复双触发、快捷支付必须按页面可见性暂停，并确保诊断只读取一次微信监控快照。
- 完整验证通过：21 个测试文件共 120 项测试（118 项通过、2 项按环境跳过），Worker 与管理端类型检查和管理端生产构建全部通过。
- 2026-10-08 已推送 `9bb6310`、`7ea1193`、`536a832` 并部署正式版本 `df4cd9d7-187d-4fb5-a009-add1c027e559`；远程 D1 无待执行迁移。
- 正式后台入口以 D1 管理员配置 `/cfpay` 为准，部署变量中的旧入口返回 404 属于预期；使用正式入口完成 Chromium 全页面巡检，全部用例通过，未发现非预期控制台错误、布局越界或重复文案。

### BUILD-003 将微信监控端打入管理端构建

- 2026-10-08 管理端生产构建调整为先构建 Windows 微信监控端单 EXE，再将带监控端版本和支持微信版本的发行文件复制进 Vite 静态资源，避免前端先构建后找不到二进制。
- 微信渠道页新增同源下载入口，展示监控端版本、支持微信版本、文件大小和 SHA-256；下载文件不写入 D1，也不进入 Git，只随 Worker 静态资源发布。
- 监控实时 WebSocket 首次连接仍发送完整快照，后续每 5 秒上报只推送当前设备、组件和登录状态的增量数据，避免重复传输整份设备列表。
- 构建完成后校验静态资源中的 EXE 大小与 SHA-256，并自动清理前端暂存目录；本次产物为 `cfpay-wechat-monitor-0.9.2-weixin-4.1.15.13-win64.exe`。
- Worker 与管理端类型检查、31 项专项测试及完整管理端生产构建通过；生成的单 EXE 为 5,342,395 字节，SHA-256 为 `f78e19d0e1e92546865f8dfdb16db2ec8507aae6070bbff001db8ce3001cad4a`。

### UI-008 统一即时通知与支付宝追踪编号

- 2026-10-08 支付宝手工账务查询不再把“查询完成、条数和追踪编号”写成页面持久 Alert，改为右上角即时通知；通知展示结果条数，并提供追踪编号复制操作。
- 查询响应增加 `traceSource`：优先使用支付宝响应头 `alipay-trace-id` 并标记为“支付宝追踪编号”，响应头缺失时回退到 CFPay 发送的 `alipay-request-id` 并标记为“CFPay 请求编号”。
- 管理端手工查询完成后写入不含账务明细的结构化 Worker 日志，记录追踪编号、编号来源、结果条数和查询时间；未知服务端异常仍使用独立的 `ERR-*` 错误编号，两类编号不混用。
- 清理面板、订单、快捷支付诊断、未匹配收款和微信渠道五处“已刷新”成功 Toast；这些页面的数据变化已经构成成功反馈，读取失败仍保留页面错误和重试入口。
- 保留保存、删除、导出、生成随机值和人工处理等没有充分页面反馈的成功通知；保留首次加载失败、配置状态、轮询异常和支付流程结果等持久页面状态。
- Worker 与管理端类型检查通过，支付宝轮询和管理端通知专项测试共 20 项通过。

### MON-006 将微信失联窗口调整为三次上报

- 2026-10-08 微信监控端继续每 5 秒统一上报一次，Worker 唯一在线窗口由 10 秒调整为 15 秒，即连续 3 次未收到上报才判定设备、Hook 组件和微信通道失联。
- Worker 与管理端统一临界规则：距离最后一次有效上报达到 15 秒即失联；设备列表过滤、微信通道可用性和组件“上报超时”不再在临界点出现一秒内的不一致。
- 前端仅把 15 秒作为接口异常时的回退值，正常情况继续采用 Worker 快照返回的 `onlineWindowMs`；未增加可配置阈值，也没有恢复独立心跳接口。
- README、架构说明、监控协议、原生观察器说明和 C++ 源码注释同步更新为“5 秒一次、连续 3 次、15 秒失联”。
- Worker 与管理端类型检查通过，微信状态和管理端专项测试共 38 项通过；完整管理端构建成功，并重新生成、校验和内置 Windows 单 EXE。
- 2026-10-08 全量验证通过：21 个测试文件共 120 项通过、2 项按环境跳过；Chromium 管理端全页面巡检通过，桌面、宽屏、移动端边界、重复文案和术语检查均无异常。
- 已推送 `a1bcbbc`、`63ae6d6`、`d667972` 并部署正式版本 `c31a47fb-8ea1-49db-9c3c-bfd57abea60d`；远程 D1 无待执行迁移。
- 正式环境只读校验确认 `/api/health`、后台 `/cfpay`、前端脚本与监控端下载均返回 200；线上 EXE 大小为 5,342,395 字节且 SHA-256 与下载清单一致，旧部署入口继续返回 404。

### CI-004 修复部署工作流缺少交叉编译器

- 2026-10-08 GitHub Actions 运行 `37756204345` 在“构建后台”阶段失败；本地已把 `admin:build` 调整为先构建 Windows 单 EXE，但部署工作流仍只安装 Node 依赖，没有安装构建脚本必需的 MinGW-w64。
- `.github/workflows/deploy.yml` 在后台构建前增加与 Release 工作流一致的 `g++-mingw-w64-x86-64`、`cmake`、`ninja-build` 安装和版本确认，保持“先二进制、后前端、再部署”的构建顺序。
- 管理端接线测试新增工作流顺序约束，防止以后删除交叉编译工具链或把安装步骤移到 `admin:build` 之后。
- 修复提交 `0474a1d` 推送后，Cloudflare 于 2026-10-08 09:29:41 UTC 生成新部署 `ca1709cc-cb24-472f-a4d1-ce3da829edfc`；正式健康接口、后台 `/cfpay` 和 Windows 监控端下载均返回 200。

### UI-009 压缩微信接入与收款码布局

- 2026-10-08 微信渠道删除独立“Windows 监控端”大卡片，将版本摘要、下载入口和监控端密钥合并到“监控端接入”；桌面端密钥输入、生成和保存使用紧凑同行布局。
- 完整文件名和 SHA-256 不再长期占据页面，移入“版本信息”弹窗并保留复制能力；下载文件、支持微信版本和完整性校验功能不变。
- 微信与支付宝共用的收款码组件改为桌面双栏：固定尺寸二维码预览在左、文件和操作在右；移动端保持纵向排列，未配置状态不再显示大面积空白。
- 在线监控端说明缩短为“15 秒未上报即离线”，同步状态缩短为“实时同步 / 轮询同步”，删除存储和传输实现说明。
- 管理端类型检查通过，管理端接线专项测试 18 项通过。

### UI-010 重排高占用组件并精简管理端文案

- 2026-10-08 面板业务指标由七个独立卡片改为单个七列分隔网格，D1 用量由四个独立卡片改为单个四列网格；异常指标仍可点击进入对应处理页面。
- 运行缓存去掉六个嵌套边框卡片，改为一个分隔统计行；“缓存数据量（估算）”改为“缓存占用”，清理结果不再解释 D1 回源实现。
- Turnstile 已启用时只显示紧凑状态；只有未启用或配置不完整时才显示可展开的配置命令。后台入口单字段表单在桌面端改为同行保存。
- 全局 `SettingSection` 的垂直间距和内边距统一收紧；账户、支付配置、站点设置、渠道设置继续保留独立保存语义，不为压缩而混合表单。
- 管理端可见文案删除构建顺序、存储位置、AES-GCM、HMAC、轮询周期、缓存回源和英文 `success` 等实现说明；保留 D1 用量、Turnstile、接口路径、JSON、微信版本和 SHA-256 等管理员实际需要的信息。
- 浏览器巡检脚本支持任意后台入口路径，并增加监控端版本弹窗、紧凑指标跳转和新 Turnstile 文案验证；本地构建版本完成桌面、宽屏、390px 移动端、父边框、重复文案和术语巡检。
- Worker 类型检查、21 个测试文件共 122 项测试通过、2 项按环境跳过；管理端生产构建和本地 Chromium 全页面巡检通过。
- 已推送 `c5a7712`、`ddb5aa0`，Cloudflare 于 2026-10-08 10:15:51 UTC 生成部署 `739bec85-6574-45d2-a475-6ae18d8311d6`；正式健康接口、后台入口和 Windows 监控端下载均返回 200，正式环境 Chromium 全页面巡检通过。

### EPAY-001 修复外部 JSON 请求兼容

- 2026-10-12 线上复现：同一组有效 MD5 签名参数通过 GET 或 `application/x-www-form-urlencoded` 调用 `/api.php` 能进入查询并返回“订单不存在”；通过 `application/json` 调用时原实现没有读取请求体，参数表为空，因此返回 401“签名校验失败”。
- 易支付 `/api.php`、`/query.php`、`/mapi.php` 和 `/submit.php` 统一解析查询串、表单和 JSON 标量参数；JSON 数字、布尔值转成协议字符串，数组和嵌套对象拒绝，避免产生不确定签名。
- 鉴权失败日志只记录接口、方法、Content-Type、字段名、商户号、签名类型和长度等脱敏摘要，不记录商户密钥、签名原文或请求体；接口 JSON 错误增加稳定代码 `EPAY_SIGNATURE_INVALID`。
- 通过表单、GET、JSON、非法 JSON 结构专项测试；线上尾日志确认 JSON 请求此前返回 401，表单和 GET 返回业务层 404，证明差异来自请求体解析而非商户配置或签名算法。
- 线上回归发现非法 JSON 的运行时异常文案是 `Unexpected end of JSON input`，原错误归一化未覆盖该文案，会错误返回 500；现归类为 `INVALID_JSON` 并返回 400，调用方可直接修正请求体。

### EPAY-002 修复已支付收银台首次回跳

- 2026-10-20 现场复现订单 `8342b9ecee284690a17a5e34b06d74af`：D1 状态为 `PAID`，状态接口返回了带签名的完整 `returnUrl`，但直接打开已支付收银台时页面脚本使用的是数据库中的原始 `return_url`。
- 该地址虽然能正常跳转，但目标站点拿不到 `trade_no`、`trade_status`、`sign` 等回调参数，因此显示匿名订单不存在，表现为支付成功后没有正确进入支付结果页。
- `/pay/:id` 首次渲染现在与 `/api/orders/:id/status` 统一使用 `buildEpayCallbackParams` 生成完整回跳地址；动态轮询和直接刷新已支付页面使用同一套参数。
- 使用 Playwright 实测原问题并补充已支付直开回跳专项测试；未修改订单数据。

### WECHAT-004 适配微信 4.2.0.11 NewSync 结构

- 2026-11-01 基于本机 `Weixin.dll 4.2.0.11` 静态分析更新目标版本、文件指纹、NewSync 解析入口、响应/命令列表/CmdItem 虚表及账号读取目标；命令列表的子项数组和数量位于 `+0x08/+0x10`，CmdItem 的字符串对象指针和 CmdID 位于 `+0x08/+0x10`。
- 生产观察器在 NewSync 解析返回后验证三层虚表，再由 CmdItem `+0x08` 读取字符串对象并解析既有 protobuf `paymsg` 字段；未切换至 UI 文案、OCR、数据库或原始网络缓冲区。
- 复核发现首轮适配曾把 CmdItem 的字符串指针当作独立载荷对象传递，已改为传递 CmdItem 本体，并增加源码回归测试。
- 构建验证发现 CMake 原先只保证观察 DLL 先构建，却未令 `.incbin` 所在的监控端对象依赖 DLL；增加对象级依赖后，强制重建 DLL 可正确触发 EXE 重编，嵌入校验通过。
- 单 EXE Windows 自检通过，完整测试为 23 个文件通过、1 个跳过，共 131 项通过、2 项跳过；Worker 与管理端类型检查通过。样本 SHA-256 与解析器、账号、昵称及账号调用点定点字节核对通过。
- 首次运行时 Hook 暴露出遗漏：宿主静态校验使用 4.2 账号调用点 `e8 2d 23 4f fe`，观察 DLL 仍保留 4.1 的 `e8 5c bb 4a fe`，因此正确拒绝安装。观察器改为与宿主一致的 4.2 签名，并把内存校验错误细分到具体入口。
- Windows 现场复验通过：目标文件 `Weixin.dll 4.2.0.11`、大小及 SHA-256 校验通过；NewSync、账号、调用点、昵称四处进程内字节均匹配。新单 EXE SHA-256 为 `7ac7264c8c5c5958c52aa13e3a725d71522aeee71c1b65df5415304ac8e96d8e`，内嵌观察 DLL SHA-256 为 `1a3ec617f90e5251d547746d5a9f0e668604558a7718ba8d0cad3710dcba6c84`，Windows 安装副本与构建产物一致。
- 在用户交互会话启动后，账号登录和昵称入口命中，后台连续收到 `HEALTHY / HOOK_ACTIVE`；一次指标快照记录 NewSync 解析入口 13 次、paymsg 扫描 5 次、收款消息 0 条、队列丢弃 0 条。该快照证明解析路径已运行，不代表已捕获真实收款；付款闭环仍等待管理员发起小额真实支付。
- 诊断发现从 WSL 会话创建的 GUI 进程落在 Session 0，导致登录界面无法显示、二维码检测不到；现监控端和微信均运行在用户 Session 1。未触发付款，未推送或发布二进制。
- 修复后的专项测试 18 项通过；完整 Vitest 测试 23 个文件通过、1 个跳过，共 133 项通过、2 项跳过；`git diff --check` 通过。

### WECHAT-005 拆分 4.2 Hook 运行指标

- 2026-11-01 复查 Windows 本机日志时发现旧指标 `receiptEntries` 混计 NewSync 入口与 AddMsg 命令，且 `receiptReturns` 被用作 NewSync 返回计数，名称容易让人误判支付消息已命中。
- 原生监控日志现分别记录 `newSyncEntries`、`newSyncReturns`、`addMsgCommands`、`payMessageScans` 和 `payMessages`；管理端本地日志将这些计数映射为中文说明。测试覆盖字段名称和旧字段移除。
- 更新前，旧 DLL 清理因部分线程硬件断点无法清除而失败。随后先停止监控端、Hook 宿主及目标微信进程树，确认 5 个微信进程退出，再安装新版；没有覆盖仍被映射的 DLL。
- 新 EXE 已安装，SHA-256 为 `7df5775342c5d66b931c2c06b8ddfa25080e8d342a4d2d7bbe0f94aed677e5f5`；微信当前加载 DLL SHA-256 为 `9aa0167ff9be1cac4a33bd9ce277cca2e8de181ac904f34750347efdda0069f2`。微信 4.2.0.11 在用户 Session 1 登录成功，昵称 Hook 命中，Worker 接收 `/api/monitor/status` HTTP 200 且状态为 `HEALTHY / HOOK_ACTIVE`。
- 新指标快照：`newSyncEntries=4`、`newSyncReturns=4`、`addMsgCommands=4`、`payMessageScans=2`、`payMessages=0`、`readFailures=3`、`queueDrops=0`。NewSync Hook 路径已运行；本次没有真实支付，3 次读取失败来源尚不能归因到支付消息解析，需在下一次真实支付观察窗口对比。
- 持续运行至 04:10:28 再观察：状态保持 `HEALTHY / HOOK_ACTIVE`，NewSync 入口/返回增至 `10/10`、AddMsg 命令 `8`、paymsg 扫描 `5`、支付消息 `0`、读取失败 `6`、队列丢弃 `0`。没有付款样本，因此不能把聚合读取失败归因到收款解析。
- 真实收款捕获和 Worker 订单匹配仍需要管理员进行一笔真实小额支付验证；未执行付款，不能以 Hook 健康或计数增长替代支付闭环证据。
