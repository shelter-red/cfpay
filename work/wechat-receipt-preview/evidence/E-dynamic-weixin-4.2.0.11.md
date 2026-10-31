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
- 微信自动登录按钮成功触发，Hook 检测到已登录账号；该步骤不涉及扫码或支付。
- GUI 进程最初被创建在 Session 0，界面不可见。改在登录用户 Session 1 启动后，登录和 Hook 流程正常。当前监控端与微信均留在用户会话中运行。

## Artifacts

- 源码修复：`native/wechat-observer/hardware_observer.cpp`
- 回归断言：`tests/wechat-native-log-language.test.mjs`
- 静态分析记录：`E-static-weixin-4.2.0.11.md`
- 支付闭环状态：等待管理员执行一笔小额真实支付；未执行任何付款。
