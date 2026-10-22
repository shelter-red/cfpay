#pragma once

// 监控端版本号的唯一事实来源。
//
// 同一份版本号会出现在四个地方：EXE 上报给 Worker 的 version 字段、WinHTTP 的 User-Agent、
// 配置界面「测试连接」发出的探测请求、构建脚本产出的发布工件文件名。
// 过去它曾在多个 C++ 文件和旧 Node 监控工具中各写一份，升版本时漏改任何一处，
// 后台就会显示成「设备在线但版本对不上」，且无从判断哪边是错的。
//
// 刻意不做成配置项或命令行参数：版本标识必须与二进制本身一致，可配置只会制造错觉。
namespace cfpay::monitor {

// 监控端版本，语义化版本号的三段文本。
inline constexpr char kVersion[] = "0.9.2";

// WinHTTP User-Agent 前缀，与版本号拼出完整 UA。
inline constexpr char kUserAgentPrefix[] = "CFPay-WeChat-Monitor/";

}  // namespace cfpay::monitor
