#pragma once

#include <cstdint>

// 受支持微信目标的唯一事实来源。
//
// 这里的每一个值都来自对某个具体 Weixin.dll 版本的逆向：文件大小与 SHA-256 用来锁定磁盘文件，
// RVA 用来定位 NewSyncResponse 解析入口、当前登录账号读取入口与其调用点、昵称读取入口。
// 宿主（wechat_host.cpp）用它们做安装前校验，观察器（hardware_observer.cpp）用它们下硬件断点，
// 监控端（wechat_monitor.cpp）用它们生成上报给后台的 targetVersion 与 hookOffset。
//
// 刻意不做成配置项：换微信版本必须重新逆向并同步更新 RVA 与函数签名字节，
// 只改一个版本字符串而偏移不变，会直接让断点打在错误地址上、导致微信崩溃。
// 因此“可配置”只会制造错觉，集中在一处、由代码审查把关才是安全的。
namespace cfpay::target {

// 版本号文本，与 Weixin.dll 的文件版本资源一致。
inline constexpr wchar_t kWeixinVersion[] = L"4.1.15.13";

// 磁盘文件指纹：大小 + SHA-256，安装前逐项校验，任一不符即拒绝注入并上报 UNSUPPORTED。
inline constexpr std::uint64_t kWeixinFileSize = 201552944;
inline constexpr char kWeixinSha256[] = "10f8e995453e2da46d4f2b5080cd6da1f13cc5147746adc119ceae38cb039de5";

// 小程序渲染进程 flue.dll 的指纹，仅诊断构建使用。
inline constexpr std::uint64_t kWmpfFileSize = 237162032;
inline constexpr char kWmpfSha256[] = "5baf4a84a41036ce5b2ef897d85029bde9ee9965b4a69d4f5b594672821cad56";

// micromsg.NewSyncResponse 的 Protobuf 解析入口：收款通知的结构化数据来源。
inline constexpr std::uint32_t kNewSyncParseRva = 0x106fb10;

// 当前登录账号（wxid、登录状态）读取入口，以及校验它归属的调用点。
inline constexpr std::uint32_t kAccountRva = 0x0dc08d0;
inline constexpr std::uint32_t kAccountCallsiteRva = 0x2914d6f;

// 当前账号昵称读取入口。
inline constexpr std::uint32_t kNicknameRva = 0x004b690;

}  // namespace cfpay::target
