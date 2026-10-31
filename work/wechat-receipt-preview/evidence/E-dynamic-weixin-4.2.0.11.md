# E-DYNAMIC-WEIXIN-4.2.0.11

## Runtime

- 日期：2026-11-01（本机时区 UTC+08:00）
- 目标：Windows `Weixin.exe` / `Weixin.dll` 4.2.0.11，样本 SHA-256 `745cdf2955fa5128cdb00c056c97571156a5f107406a9bb15ac0754f3246b201`
- 监控端 EXE SHA-256：`7ac7264c8c5c5958c52aa13e3a725d71522aeee71c1b65df5415304ac8e96d8e`
- 内嵌观察 DLL SHA-256：`1a3ec617f90e5251d547746d5a9f0e668604558a7718ba8d0cad3710dcba6c84`

## Findings

- 首次运行失败原因：观察 DLL 的账号调用点签名仍为微信 4.1 的 `e8 5c bb 4a fe`；微信 4.2 目标实际为 `e8 2d 23 4f fe`。NewSync、账号 getter、昵称 getter 和账号调用点的进程内存字节均使用定点读取复核。
- 更新调用点签名并重建后，观察器成功安装；账号读取与昵称读取入口均命中，Worker 收到 `HEALTHY / HOOK_ACTIVE`。
- 运行指标快照：NewSync 解析入口 13 次，paymsg 扫描 5 次，收款消息 0 条，队列丢弃 0 条。没有真实支付事件，因此尚不能证明收款字段解析及订单匹配闭环。
- 后续复查发现该次日志的旧 `receiptEntries` 混计了解析入口与 AddMsg 命令，不能作为单一入口计数；新构建将分别记录 NewSync 入口/返回、AddMsg 命令、paymsg 扫描和成功读取载荷后入队的消息。该拆分不改变 Hook 逻辑或支付判定。
- 微信自动登录按钮成功触发，Hook 检测到已登录账号；该步骤不涉及扫码或支付。
- GUI 进程最初被创建在 Session 0，界面不可见。改在登录用户 Session 1 启动后，登录和 Hook 流程正常。当前监控端与微信均留在用户会话中运行。

## Rebuild and reinstall validation

- 时间：2026-11-01 04:08（UTC+08:00）。旧监控、Hook 宿主和微信 5 个进程全部退出后，安装新单 EXE 并在 Session 1 启动微信。
- 新 EXE SHA-256：`7df5775342c5d66b931c2c06b8ddfa25080e8d342a4d2d7bbe0f94aed677e5f5`；当前加载 DLL SHA-256：`9aa0167ff9be1cac4a33bd9ce277cca2e8de181ac904f34750347efdda0069f2`。
- 登录和昵称 Hook 命中，Worker 接收 HTTP 200 状态上报并显示 `HEALTHY / HOOK_ACTIVE`。新指标快照：NewSync 入口/返回 `4/4`，AddMsg 命令 `4`，paymsg 扫描 `2`，支付消息 `0`，读取失败 `3`，队列丢弃 `0`。
- 本轮没有真实支付；因此无法判断 3 次读取失败是否与支付消息有关，也未验证金额、收款账号、订单匹配或商户回调。需要管理员下一步执行小额真实支付。
- 持续观察至 04:10:28，状态仍为 `HEALTHY / HOOK_ACTIVE`；计数增至 NewSync 入口/返回 `10/10`、AddMsg 命令 `8`、paymsg 扫描 `5`、支付消息 `0`、读取失败 `6`、队列丢弃 `0`。这确认同步响应持续经过 4.2 Hook，但没有真实支付样本，不能据此判定收款事件解析正确或失败。
- 04:33 修复嵌套载荷指针层级后重新构建并部署。EXE SHA-256：`f1715d57a219c7a0cee200480c632b9dff5cb3e12729b88554c7bbf9fff6f3ac`；已释放 DLL SHA-256：`b9872a8fedc1af99b6d3fc3c3e59b03c4d450670b024c8c89de7ee9a1399d452`，均与构建产物一致。Windows 自检通过，单 EXE 内嵌校验通过。
- 为替换 DLL 关闭了旧监控和微信进程，随后启动最终构建的监控端及微信；微信当前显示需要登录，后台状态为 `DEGRADED / LOGIN_REQUIRED`。本次重启后尚无登录态 NewSync/paymsg 样本，亦未观察到二维码成功上报；没有发起支付。
- 下一验证点：管理员完成登录后，检查新的 `payMessageMetadataFailures` 是否不再增长；随后由管理员发起真实小额支付，再确认结构化事件到 Worker 订单匹配。未完成前不宣称支付闭环通过。

## Artifacts

- 源码修复：`native/wechat-observer/hardware_observer.cpp`
- 回归断言：`tests/wechat-native-log-language.test.mjs`
- 静态分析记录：`E-static-weixin-4.2.0.11.md`
- 支付闭环状态：等待管理员执行一笔小额真实支付；未执行任何付款。
