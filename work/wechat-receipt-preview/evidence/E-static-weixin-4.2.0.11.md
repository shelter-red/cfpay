# E-STATIC-WEIXIN-4.2.0.11

## 样本

- 文件：`/mnt/d/Program Files/Tencent/Weixin/4.2.0.11/Weixin.dll`
- 大小：`205929008`
- SHA-256：`745cdf2955fa5128cdb00c056c97571156a5f107406a9bb15ac0754f3246b201`
- 架构/基址：PE x64 / `0x180000000`
- 分析：Ghidra 12.1.4，使用离线项目和 `.pdata` 定点脚本；未停止或重启微信。

## NewSync 结构

- `micromsg.NewSyncResponse` 解析器 RVA：`0x10b0230`，函数大小 `0x51d`；虚表 RVA `0x929f408` 的 slot 7 指向该函数。
- 响应 `+0x08` 指向命令列表对象；其虚表 RVA 为 `0x96d0558`，slot 7 指向解析器 `0x2fcc400`。
- 命令列表对象布局：`+0x08` 子项指针数组、`+0x10` 子项数量、`+0x14` 容量、`+0x18` 分配边界、`+0x20` 协议声明数量、`+0x28` 存在位图。
- `micromsg.CmdItem` 虚表 RVA 为 `0x96d04d8`，slot 7 指向解析器 `0x2fcbe10`；对象大小 `0x20`，`+0x08` 为嵌套 AddMsg 对象指针、`+0x10` 为 CmdID、`+0x18` 为存在位图。
- 4.2 嵌套载荷链经 Ghidra 定点复核：CmdItem 解析器 `FUN_182fcbe10` 在协议字段 2 出现时创建 0x20 字节 AddMsg 对象并调用 `FUN_1806a2ef0`；该解析器把 AddMsg 字段 2 的存在位置于对象 `+0x18` 的 bit 1，并将内容字符串对象指针放在 `+0x08`。字符串读取辅助函数 `FUN_181641e70` 明确使用 MSVC `std::string` 的 size `+0x10`、capacity `+0x18`，容量至少 16 时从对象首 qword 读取堆数据指针。
- 因此正确内存链为 `CmdItem+0x08 -> AddMsg`、`AddMsg+0x08 -> std::string`；此前把第一层指针直接当作字符串对象，导致元数据读取落在 AddMsg 的非字符串字段上。生产解析现在先检查 AddMsg 存在位，再读取嵌套字符串。
- 生产代码仍只处理 `CmdID=5`，随后按既有 protobuf 协议字段解析 `AddMsg/paymsg`；不读取原始网络缓冲区，不解析 UI 文案。
- 早期把 `0x96d0558` 称为 CmdItem、把 `+0x20` 当作命令 ID 的初始假设已由解析器的数据写入点纠正；命令 ID 仍在 CmdItem `+0x10`。

## 账号与昵称

- 账号 getter 候选 RVA：`0x0e07f70`。反编译与 4.1 getter 在 `param+0x10` 子对象、虚调用槽 `+0x18` 和源行 `0x62` 一致。
- 账号调用点 RVA：`0x2915c3e`，机器码 `e8 2d 23 4f fe`，rel32 目标为 `0x0e07f70`。
- 昵称 getter RVA `0x004b690` 的原 32 字节签名在 4.2 `.text` 中唯一命中。
- 账号 getter 的 32 字节入口签名并不唯一，因此校验还依赖完整文件大小、SHA-256、入口 RVA、调用点字节和 rel32 目标；不能脱离样本指纹单独复用该签名。

## 运行限制

- 静态解析与 PE 字节校验已完成；尚未在运行中的 4.2 微信进程观察 Hook 安装或自然 NewSync 流量。
- 尚未确认真实收款是否经过此解析入口，也未进行真实付款闭环；收款测试需等待用户付款操作。
- 当前网络来源诊断中的返回地址与 AddMsg 解析器偏移没有迁移到 4.2，本次不启用该诊断。

## 可复现脚本

- `tools/ghidra/InspectWechat42NewSyncChain.java`
- `tools/ghidra/InspectWechat42MemoryMapping.java`
- `tools/ghidra/InspectWechat42KnownHooks.java`
- `tools/ghidra/FindWechatNewSyncParserCandidates.java`
