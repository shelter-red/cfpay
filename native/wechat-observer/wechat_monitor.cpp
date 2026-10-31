#include <windows.h>
#include <bcrypt.h>
#include <commctrl.h>
#include <objbase.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <winhttp.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "nlohmann/json.hpp"
#include "embedded_observer.h"
#include "wechat_embedded.h"
#include "wechat_monitor_version.h"
#include "wechat_login_assistant.h"
#include "wechat_target.h"

namespace {

using json = nlohmann::json;

constexpr wchar_t kWindowClass[] = L"CFPayWeChatMonitorWindow";
constexpr wchar_t kWindowTitle[] = L"CFPay \u5fae\u4fe1\u76d1\u63a7\u7aef";
constexpr wchar_t kMutexName[] = L"Local\\CFPayWeChatMonitor-0.6";
// 版本号只在 wechat_monitor_version.h 里定义一份，这里取别名供本文件使用。
constexpr const char* kMonitorVersion = cfpay::monitor::kVersion;
constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT kRefreshMessage = WM_APP + 2;
constexpr UINT kRestoreMessage = WM_APP + 3;
constexpr UINT kTrayId = 1;
constexpr UINT kMenuStatus = 2001;
constexpr UINT kMenuRestore = 2002;
constexpr UINT kMenuExit = 2004;
constexpr UINT kTestButtonLogin = 2103;
constexpr UINT kTestQrLogin = 2104;
constexpr UINT kTestPayment = 2105;
constexpr UINT kConfigAutoValidateMessage = WM_APP + 20;
constexpr std::uint64_t kDeliveredRetentionMs = 30ULL * 24 * 60 * 60 * 1000;
constexpr std::size_t kMaxDeliveredEvents = 10'000;
constexpr std::uint64_t kMaxRetryDelayMs = 5 * 60 * 1000;
constexpr std::uint64_t kMaxFutureSkewMs = 5 * 60 * 1000;
constexpr std::uint64_t kMinPaymentTimeMs = 1577836800000ULL;
// 存活与组件状态的统一上报节奏，写死不可配置。
// 服务端据此判定失联：连续 3 次未收到（15 秒）即视为不在线，因此这里改动必须同步改
// src/monitor-state.ts 的 MONITOR_ONLINE_WINDOW_MS。做成可配置只会让两边的口径悄悄错开。
constexpr std::uint64_t kReportIntervalMs = 5'000;
// 上报失败后的重试间隔：状态变化会立刻触发上报，失败时按这个间隔退避，
// 避免网络线程每 250 毫秒空转一次，把接口和日志一起打爆。
constexpr std::uint64_t kReportRetryMs = 1'000;
// 等待「登录状态已送达 Worker」的上限，必须大于一个上报周期，否则定期重申还没轮到就会误判失败。
constexpr std::uint64_t kUploadAckWaitMs = 6'000;
// 单次上报最多读取的响应体字节数。Worker 的响应都是几百字节的 JSON，
// 上限只用于防止异常响应把内存与日志撑爆。
constexpr std::size_t kMaxResponseBytes = 8 * 1024;
// 写进日志的请求体与响应体长度上限，避免二维码内容或长报文刷满日志。
constexpr std::size_t kMaxLoggedBodyBytes = 1024;
constexpr int kMaxAutoRestartPerRun = 1;
constexpr std::int64_t kWeixinSysMsgType = 10002;
constexpr std::int64_t kWeixinPayStatusSuccess = 1;
constexpr std::int64_t kWeixinFeeTypeCny = 1;
struct Config {
  std::wstring baseUrl;
  std::string monitorId;
  std::string monitorSecret;
  std::string expectedReceiverUsername;
  std::string payTypeFilter;
  std::wstring weixinExecutablePath;
  std::wstring observerDllPath;
  std::wstring statePath;
  std::wstring logPath;
  bool debugLogging = false;
  DWORD processPollMs = 5'000;
  DWORD queuePollMs = 1'000;
  std::uint64_t maxEventAgeMs = 600'000;
};

struct Payment {
  std::string eventId;
  std::string amount;
  std::int64_t amountFen = 0;
  std::uint64_t paidAt = 0;
  std::string raw;
  std::string receiverUsername;
};

struct Evaluation {
  bool accepted = false;
  std::string reason;
  Payment payment;
};

struct AccountObservation {
  bool loggedIn = false;
  std::string username;
  std::string nickname;
  std::uint64_t observedAt = 0;
};

enum class FieldTestKind : int {
  None = 0,
  ButtonLogin = 1,
  QrLogin = 2,
  Payment = 3,
};

struct AppState {
  HINSTANCE instance = nullptr;
  HWND window = nullptr;
  HWND statusLabel = nullptr;
  HWND detailLabel = nullptr;
  HWND fieldTestLabel = nullptr;
  HFONT font = nullptr;
  HFONT titleFont = nullptr;
  HBRUSH background = nullptr;
  NOTIFYICONDATAW tray{};
  HANDLE stopEvent = nullptr;
  HANDLE singleton = nullptr;
  std::thread monitorThread;
  std::thread networkThread;
  std::thread loginThread;
  std::mutex displayMutex;
  std::wstring displayStatus = L"\u6b63\u5728\u542f\u52a8";
  std::wstring displayDetail = L"\u6b63\u5728\u8bfb\u53d6\u914d\u7f6e";
  std::wstring fieldTestStatus = L"\u73b0\u573a\u6d4b\u8bd5\uff1a\u672a\u5f00\u59cb";
  bool networkOk = true;
  // 最近一次上报失败的原因（服务端返回的中文说明），显示在状态行与托盘提示里。
  // 失败原因放界面而不是强行写日志：日志开关关掉后就不该再有任何文件写入。
  std::wstring networkDetail;
  std::mutex hostMutex;
  HANDLE hostProcess = nullptr;
  DWORD hostProcessId = 0;
  DWORD weixinProcessId = 0;
  std::atomic<bool> stopping{false};
  std::atomic<DWORD> loginEvidenceProcessId{0};
  std::atomic<std::uint64_t> loginEvidenceObservedAt{0};
  std::atomic<bool> loginEvidenceLoggedIn{false};
  std::atomic<int> fieldTestRequest{static_cast<int>(FieldTestKind::None)};
  std::atomic<int> activeFieldTest{static_cast<int>(FieldTestKind::None)};
  std::atomic<std::uint64_t> fieldTestStartedAt{0};
  bool fieldTestSequence = false;
  std::wstring configPath;
  std::wstring packageDirectory;
  Config config;
};

AppState gState;

// 上报快照：组件（Hook）状态与微信登录状态合并成一份，由网络线程一次性发到 /api/monitor/status。
// 任何线程只更新快照，不自己发请求。过去 Hook 状态与登录状态各有一条上报路径、各有一套去重记账，
// 同一个周期会朝同一个路由发两三次内容重叠的请求，后台也会看到两条互相矛盾的时间线。
struct ReportSnapshot {
  std::string status;                 // 组件状态，空表示还没拿到任何 Hook 状态
  std::string detailCode;             // 诊断码
  std::string message;                // 已翻译成中文的说明
  DWORD processId = 0;                // 目标微信进程号
  std::uint64_t lastEventAt = 0;      // 最后一次收款事件时间
  std::string loginStatus;            // 微信登录状态，空表示还没有
  std::string qrPayload;              // 登录二维码内容，只在 QR_READY 时非空
  DWORD loginProcessId = 0;           // 登录状态所属的微信进程号
  std::string postedFingerprint;      // 最近一次成功上报的组件指纹
  std::string postedLoginFingerprint; // 最近一次成功上报的登录指纹
  std::uint64_t postedAt = 0;         // 最近一次成功上报的时间
  bool dirty = true;                  // 是否存在尚未成功上报的变化
};

std::mutex gReportMutex;
ReportSnapshot gReport;

// 取本地时间的毫秒时间戳（Unix 纪元）。所有上报时间与去重窗口都基于它。
std::uint64_t nowMilliseconds() {
  FILETIME fileTime{};
  GetSystemTimeAsFileTime(&fileTime);
  ULARGE_INTEGER value{};
  value.LowPart = fileTime.dwLowDateTime;
  value.HighPart = fileTime.dwHighDateTime;
  return (value.QuadPart - 116444736000000000ULL) / 10000ULL;
}

// 宽字符转 UTF-8；上报给 Worker 的 JSON 一律是 UTF-8。
std::string wideToUtf8(const std::wstring& value) {
  if (value.empty()) return {};
  const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
  if (size <= 0) return {};
  std::string output(static_cast<std::size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), output.data(), size, nullptr, nullptr);
  return output;
}

// UTF-8 转宽字符；非法字节序列直接返回空串，避免把坏数据带进界面或路径。
std::wstring utf8ToWide(const std::string& value) {
  if (value.empty()) return {};
  const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
  if (size <= 0) return {};
  std::wstring output(static_cast<std::size_t>(size), L'\0');
  MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), output.data(), size);
  return output;
}

// WinHTTP 的 User-Agent：由版本号拼出，避免版本字符串在宽窄两个字面量里各写一份。
std::wstring monitorUserAgent() {
  return utf8ToWide(std::string(cfpay::monitor::kUserAgentPrefix) + cfpay::monitor::kVersion);
}


// 取路径的父目录，兼容正反斜杠。
std::wstring parentDirectory(const std::wstring& path) {
  const std::size_t index = path.find_last_of(L"\\/");
  return index == std::wstring::npos ? L"" : path.substr(0, index);
}

// 拼接路径，自动避免重复或缺失分隔符。
std::wstring joinPath(const std::wstring& left, const std::wstring& right) {
  if (left.empty()) return right;
  if (left.back() == L'\\' || left.back() == L'/') return left + right;
  return left + L"\\" + right;
}

// 取当前 EXE 所在目录，便携模式下的配置与观察 DLL 都以它为基准。
std::wstring moduleDirectory() {
  std::vector<wchar_t> buffer(32768);
  const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
  return length == 0 || length >= buffer.size() ? L"" : parentDirectory(std::wstring(buffer.data(), length));
}

// 决定配置文件位置：优先 EXE 同目录已存在的 config.json（便携模式与现场测试目录），
// 否则一律用数据目录 %LOCALAPPDATA%\CFPay\WeChatMonitor\config.json。
// 首次运行时文件还不存在，也按数据目录创建——程序产生的文件集中在一个目录里才好管理。
std::wstring defaultConfigPath() {
  const std::wstring portable = joinPath(moduleDirectory(), L"config.json");
  if (GetFileAttributesW(portable.c_str()) != INVALID_FILE_ATTRIBUTES) return portable;
  return joinPath(CfpayDataDirectory(), L"config.json");
}

// 展开 %LOCALAPPDATA% 一类环境变量；展开失败时原样返回，便于报错时看到用户实际写了什么。
std::wstring expandEnvironment(const std::wstring& value) {
  const DWORD size = ExpandEnvironmentStringsW(value.c_str(), nullptr, 0);
  if (size == 0) return value;
  std::wstring output(static_cast<std::size_t>(size), L'\0');
  const DWORD written = ExpandEnvironmentStringsW(value.c_str(), output.data(), size);
  if (written == 0 || written > size) return value;
  output.resize(written - 1);
  return output;
}

// 把配置里的相对路径按 EXE 目录解析成绝对路径，环境变量已展开。
std::wstring resolvePath(const std::wstring& baseDirectory, const std::wstring& value) {
  const std::filesystem::path expanded(expandEnvironment(value));
  return std::filesystem::absolute(expanded.is_absolute() ? expanded : std::filesystem::path(baseDirectory) / expanded).wstring();
}

// 确保目录存在（递归创建），失败返回 false 而不是抛异常。
bool ensureDirectory(const std::wstring& directory) {
  std::error_code error;
  std::filesystem::create_directories(std::filesystem::path(directory), error);
  return !error;
}

// 以共享读方式读整个文件；微信正在运行时也能读它的 DLL 与配置。
bool readFile(const std::wstring& path, std::string* output) {
  HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  LARGE_INTEGER size{};
  if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 || size.QuadPart > 8 * 1024 * 1024) {
    CloseHandle(file);
    return false;
  }
  output->assign(static_cast<std::size_t>(size.QuadPart), '\0');
  DWORD bytesRead = 0;
  const bool success = output->empty() ||
                       (ReadFile(file, output->data(), static_cast<DWORD>(output->size()), &bytesRead, nullptr) != FALSE &&
                        bytesRead == output->size());
  CloseHandle(file);
  return success;
}

// 先写 .tmp 再原子替换，避免掉电或并发读写留下半个文件；队列状态与日志都走这条路径。
bool writeFileAtomically(const std::wstring& path, const std::string& value) {
  if (!ensureDirectory(parentDirectory(path))) return false;
  const std::wstring temporaryPath = path + L".tmp";
  HANDLE file = CreateFileW(temporaryPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  DWORD bytesWritten = 0;
  const bool written = WriteFile(file, value.data(), static_cast<DWORD>(value.size()), &bytesWritten, nullptr) != FALSE &&
                       bytesWritten == value.size() && FlushFileBuffers(file) != FALSE;
  CloseHandle(file);
  if (!written) {
    DeleteFileW(temporaryPath.c_str());
    return false;
  }
  if (!MoveFileExW(temporaryPath.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    DeleteFileW(temporaryPath.c_str());
    return false;
  }
  return true;
}

// 按 Windows 命令行转义规则给参数加引号，处理反斜杠与内嵌引号，避免路径里带空格时参数被拆开。
std::wstring quoteArgument(const std::wstring& value) {
  std::wstring output = L"\"";
  std::size_t backslashes = 0;
  for (const wchar_t character : value) {
    if (character == L'\\') {
      backslashes += 1;
    } else if (character == L'\"') {
      output.append(backslashes * 2 + 1, L'\\');
      output.push_back(L'\"');
      backslashes = 0;
    } else {
      output.append(backslashes, L'\\');
      backslashes = 0;
      output.push_back(character);
    }
  }
  output.append(backslashes * 2, L'\\');
  output.push_back(L'\"');
  return output;
}

// 去掉首尾空白，空串返回空。
std::string trim(const std::string& value) {
  const std::size_t first = value.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) return {};
  const std::size_t last = value.find_last_not_of(" \t\r\n");
  return value.substr(first, last - first + 1);
}

// 宽字符版本的 trim。
std::wstring trimWide(const std::wstring& value) {
  const std::size_t first = value.find_first_not_of(L" \t\r\n");
  if (first == std::wstring::npos) return {};
  const std::size_t last = value.find_last_not_of(L" \t\r\n");
  return value.substr(first, last - first + 1);
}

// 校验监控端 ID 格式：长度受限且只允许安全字符集，它会进入 HTTP 头与 D1 主键。
bool validMonitorId(const std::string& value) {
  if (value.empty() || value.size() > 80) return false;
  return std::all_of(value.begin(), value.end(), [](unsigned char character) {
    return std::isalnum(character) != 0 || character == '.' || character == '_' || character == '-';
  });
}

// 校验收款账号格式：必须以 wxid_ 开头且只允许安全字符，防止把任意字符串当成收款账号参与匹配。
bool validWxid(const std::string& value) {
  if (value.rfind("wxid_", 0) != 0 || value.size() > 256) return false;
  return std::all_of(value.begin() + 5, value.end(), [](unsigned char character) {
    return std::isalnum(character) != 0 || character == '_' || character == '-';
  });
}

// 用 BCrypt 计算 SHA-256；用于签名原文摘要与观察 DLL 内容指纹。
std::vector<std::uint8_t> sha256(const std::string& value) {
  BCRYPT_ALG_HANDLE algorithm = nullptr;
  BCRYPT_HASH_HANDLE hash = nullptr;
  DWORD objectLength = 0;
  DWORD hashLength = 0;
  DWORD resultLength = 0;
  std::vector<std::uint8_t> object;
  std::vector<std::uint8_t> digest;
  if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0 ||
      BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectLength), sizeof(objectLength),
                        &resultLength, 0) < 0 ||
      BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&hashLength), sizeof(hashLength), &resultLength,
                        0) < 0) {
    if (algorithm != nullptr) BCryptCloseAlgorithmProvider(algorithm, 0);
    return {};
  }
  object.resize(objectLength);
  digest.resize(hashLength);
  if (BCryptCreateHash(algorithm, &hash, object.data(), objectLength, nullptr, 0, 0) < 0 ||
      BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<char*>(value.data())), static_cast<ULONG>(value.size()), 0) < 0 ||
      BCryptFinishHash(hash, digest.data(), hashLength, 0) < 0) {
    digest.clear();
  }
  if (hash != nullptr) BCryptDestroyHash(hash);
  BCryptCloseAlgorithmProvider(algorithm, 0);
  return digest;
}

// 字节转小写十六进制字符串，协议要求这种形式。
std::string hexValue(const std::vector<std::uint8_t>& bytes) {
  static constexpr char hex[] = "0123456789abcdef";
  std::string output;
  output.reserve(bytes.size() * 2);
  for (const std::uint8_t byte : bytes) {
    output.push_back(hex[byte >> 4]);
    output.push_back(hex[byte & 0x0f]);
  }
  return output;
}

// 用 BCrypt 计算 HMAC-SHA256 并输出小写十六进制，即请求头 X-Signature 的值。
std::string hmacSha256(const std::string& secret, const std::string& value) {
  BCRYPT_ALG_HANDLE algorithm = nullptr;
  BCRYPT_HASH_HANDLE hash = nullptr;
  DWORD objectLength = 0;
  DWORD hashLength = 0;
  DWORD resultLength = 0;
  std::vector<std::uint8_t> object;
  std::vector<std::uint8_t> digest;
  if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG) < 0 ||
      BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectLength), sizeof(objectLength),
                        &resultLength, 0) < 0 ||
      BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&hashLength), sizeof(hashLength), &resultLength,
                        0) < 0) {
    if (algorithm != nullptr) BCryptCloseAlgorithmProvider(algorithm, 0);
    return {};
  }
  object.resize(objectLength);
  digest.resize(hashLength);
  if (BCryptCreateHash(algorithm, &hash, object.data(), objectLength,
                       reinterpret_cast<PUCHAR>(const_cast<char*>(secret.data())), static_cast<ULONG>(secret.size()), 0) < 0 ||
      BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<char*>(value.data())), static_cast<ULONG>(value.size()), 0) < 0 ||
      BCryptFinishHash(hash, digest.data(), hashLength, 0) < 0) {
    digest.clear();
  }
  if (hash != nullptr) BCryptDestroyHash(hash);
  BCryptCloseAlgorithmProvider(algorithm, 0);
  return hexValue(digest);
}

// 字节转 base64url（无填充），用于生成一次性 nonce。
std::string base64Url(const std::vector<std::uint8_t>& bytes) {
  static constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
  std::string output;
  for (std::size_t index = 0; index < bytes.size(); index += 3) {
    const std::uint32_t block = static_cast<std::uint32_t>(bytes[index]) << 16 |
                                (index + 1 < bytes.size() ? static_cast<std::uint32_t>(bytes[index + 1]) << 8 : 0) |
                                (index + 2 < bytes.size() ? bytes[index + 2] : 0);
    output.push_back(alphabet[(block >> 18) & 63]);
    output.push_back(alphabet[(block >> 12) & 63]);
    if (index + 1 < bytes.size()) output.push_back(alphabet[(block >> 6) & 63]);
    if (index + 2 < bytes.size()) output.push_back(alphabet[block & 63]);
  }
  return output;
}

// 日志落盘的唯一实现：补上时间戳、进程号与线程号后追加写入，超过 2 MiB 自动滚动到 .1。
// 日志互斥加锁，多线程（网络、登录、监控）都能安全写入。
// 是否写盘完全由配置页的「写入本地调试日志」开关决定，没有任何例外：
// 开关关掉后还偷偷写文件，用户会以为设置没生效。上报失败的原因改到界面状态行显示。
void writeLogLine(const std::string& level, const std::string& message) {
  if (!gState.config.debugLogging || gState.config.logPath.empty()) return;
  static std::mutex logMutex;
  std::lock_guard<std::mutex> lock(logMutex);
  ensureDirectory(parentDirectory(gState.config.logPath));
  WIN32_FILE_ATTRIBUTE_DATA attributes{};
  if (GetFileAttributesExW(gState.config.logPath.c_str(), GetFileExInfoStandard, &attributes)) {
    ULARGE_INTEGER size{};
    size.LowPart = attributes.nFileSizeLow;
    size.HighPart = attributes.nFileSizeHigh;
    if (size.QuadPart >= 2 * 1024 * 1024) {
      DeleteFileW((gState.config.logPath + L".1").c_str());
      MoveFileExW(gState.config.logPath.c_str(), (gState.config.logPath + L".1").c_str(), MOVEFILE_REPLACE_EXISTING);
    }
  }
  HANDLE file = CreateFileW(gState.config.logPath.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return;
  SYSTEMTIME localTime{};
  GetLocalTime(&localTime);
  char timestamp[32]{};
  std::snprintf(timestamp, sizeof(timestamp), "%04u-%02u-%02u %02u:%02u:%02u.%03u",
                localTime.wYear, localTime.wMonth, localTime.wDay, localTime.wHour, localTime.wMinute,
                localTime.wSecond, localTime.wMilliseconds);
  const std::string line = std::string(timestamp) + " pid=" + std::to_string(GetCurrentProcessId()) +
                           " tid=" + std::to_string(GetCurrentThreadId()) + " " + level + " " + message + "\r\n";
  DWORD written = 0;
  WriteFile(file, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
  CloseHandle(file);
}

// 追加一行中文调试日志，只在 debugLogging 打开时落盘。
// 内容只含金额分值、字节数、状态码与时间，不写交易号原文、付款方昵称或消息正文。
void appendLog(const std::string& level, const std::string& message) { writeLogLine(level, message); }

// 截断过长的报文体，并标注原始长度，保证单行日志不会被二维码或长报文撑爆。
std::string truncateForLog(const std::string& value, std::size_t limit) {
  if (value.size() <= limit) return value;
  return value.substr(0, limit) + u8"\u2026\uff08\u5df2\u622a\u65ad\uff0c\u539f\u59cb " + std::to_string(value.size()) + u8" \u5b57\u8282\uff09";
}

// 写日志前对请求体脱敏。登录二维码是「谁拿到谁就能登录这个微信号」的凭据，
// 收款原文属于消息正文，两者都只记录长度与 SHA-256 前缀；其余字段原样保留，
// 这样服务端因为哪个字段拒绝请求（400 INVALID_*）仍然一眼可辨。
// 解析失败时只记录字节数：没能确认内容的原文一律不写进日志。
std::string redactForLog(const std::string& requestBody) {
  json payload = json::parse(requestBody, nullptr, false);
  if (payload.is_discarded() || !payload.is_object()) {
    return u8"<\u975e JSON \u8bf7\u6c42\u4f53\uff0c" + std::to_string(requestBody.size()) + u8" \u5b57\u8282>";
  }
  const auto mask = [](const std::string& value) {
    return u8"<\u5df2\u9690\u85cf\uff0c" + std::to_string(value.size()) + u8" \u5b57\u8282\uff0csha256=" + hexValue(sha256(value)).substr(0, 12) +
           u8">";
  };
  if (payload.contains("raw") && payload["raw"].is_string()) {
    payload["raw"] = mask(payload["raw"].get<std::string>());
  }
  if (payload.contains("login") && payload["login"].is_object() && payload["login"].contains("qrPayload") &&
      payload["login"]["qrPayload"].is_string()) {
    payload["login"]["qrPayload"] = mask(payload["login"]["qrPayload"].get<std::string>());
  }
  return truncateForLog(payload.dump(), kMaxLoggedBodyBytes);
}

// 取出服务端响应里的业务码与中文说明（形如 {"code":-1,"msg":"监控端签名错误"}）。
// 这是区分 401 究竟是密钥不对、时间戳过期还是载荷非法的唯一依据，界面状态行与日志共用。
std::pair<std::string, std::string> serverReply(const std::string& responseBody) {
  if (responseBody.empty()) return {u8"\u65e0", {}};
  const json parsed = json::parse(responseBody, nullptr, false);
  if (parsed.is_discarded() || !parsed.is_object()) return {u8"\u65e0", {}};
  std::string code = u8"\u65e0";
  if (parsed.contains("code")) {
    code = parsed["code"].is_string() ? parsed["code"].get<std::string>() : parsed["code"].dump();
  }
  std::string message;
  if (parsed.contains("msg") && parsed["msg"].is_string()) message = parsed["msg"].get<std::string>();
  return {code, message};
}

// 记录一次接口调用的完整现场：请求路径、HTTP 状态码、WinHTTP 错误码、
// 服务端返回的业务码与中文说明、请求体与响应体。
// 过去失败只记一个状态码：服务端明明回了 {"code":-1,"msg":"监控端签名错误"}，
// 却因为响应体只在调用方显式要 JSON 时才读取而被丢弃，401 的真正原因只能靠猜。
// 密钥与签名只出现在请求头里，这里刻意不记录任何请求头。
// 是否落盘由配置页的调试日志开关决定；开关关掉时一行都不写，失败原因改在界面状态行显示。
void logHttpExchange(const wchar_t* requestPath,
                     const std::string& requestBody,
                     bool success,
                     DWORD statusCode,
                     DWORD errorCode,
                     const std::string& responseBody) {
  const auto [serverCode, serverMessage] = serverReply(responseBody);
  const std::string detail = std::string(u8" \u8bf7\u6c42\u8def\u5f84=") + wideToUtf8(requestPath) +
                             u8" httpStatus=" + std::to_string(statusCode) +
                             u8" winError=" + std::to_string(errorCode) + u8" \u670d\u52a1\u7aef\u7801=" + serverCode +
                             u8" \u670d\u52a1\u7aef\u8bf4\u660e=" + (serverMessage.empty() ? std::string(u8"\u65e0") : serverMessage) +
                             u8" \u54cd\u5e94\u4f53=" + truncateForLog(responseBody, kMaxLoggedBodyBytes);
  const std::string redactedRequest = redactForLog(requestBody);
  if (success) {
    appendLog("INFO", std::string(u8"\u63a5\u53e3\u8c03\u7528\u6210\u529f") + detail + u8" \u8bf7\u6c42\u4f53=" + redactedRequest);
    return;
  }
  appendLog("ERROR", std::string(u8"\u63a5\u53e3\u8c03\u7528\u5931\u8d25") + detail + u8" \u8bf7\u6c42\u4f53=" +
                    redactedRequest);
}

// 单 EXE 分发：把内嵌的观察 DLL 释放到程序目录；已存在且内容一致时不重写，避免影响已加载实例。
bool materializeObserverDll(const std::wstring& targetPath, std::string* detail) {
  const std::size_t imageSize = embeddedObserverDllSize();
  const unsigned char* image = embeddedObserverDllImage();
  if (image == nullptr || imageSize == 0) {
    *detail = "OBSERVER_DLL_NOT_EMBEDDED";
    return false;
  }

  std::string existing;
  if (readFile(targetPath, &existing) && existing.size() == imageSize &&
      std::memcmp(existing.data(), image, imageSize) == 0) {
    *detail = u8"\u5185\u5d4c\u89c2\u5bdf DLL \u5df2\u5b58\u5728\u4e14\u5185\u5bb9\u4e00\u81f4";
    return true;
  }

  const std::wstring temporaryPath = targetPath + L".tmp";
  HANDLE file = CreateFileW(temporaryPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                            nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    *detail = "OBSERVER_DLL_WRITE_FAILED winError=" + std::to_string(GetLastError());
    return false;
  }
  DWORD written = 0;
  const BOOL ok = WriteFile(file, image, static_cast<DWORD>(imageSize), &written, nullptr) != FALSE &&
                  written == static_cast<DWORD>(imageSize);
  CloseHandle(file);
  if (!ok) {
    DeleteFileW(temporaryPath.c_str());
    *detail = "OBSERVER_DLL_WRITE_FAILED winError=" + std::to_string(GetLastError());
    return false;
  }
  if (!MoveFileExW(temporaryPath.c_str(), targetPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) {
    const DWORD moveError = GetLastError();
    DeleteFileW(temporaryPath.c_str());
    *detail = moveError == ERROR_SHARING_VIOLATION || moveError == ERROR_USER_MAPPED_FILE
                  ? "OBSERVER_DLL_LOCKED_BY_RUNNING_WEIXIN"
                  : "OBSERVER_DLL_REPLACE_FAILED winError=" + std::to_string(moveError);
    return false;
  }
  *detail = u8"\u5185\u5d4c\u89c2\u5bdf DLL \u5df2\u91ca\u653e";
  return true;
}

// 读取并校验 config.json。
// 逐项检查：Worker 地址必须是 HTTPS 公网地址、监控端 ID 与密钥格式合法、微信主程序必须存在。
// 任何一项不满足都返回明确的错误码，界面与日志显示同一份原因，不做静默默认值。
bool loadConfig(const std::wstring& path, Config* output, std::string* error) {
  std::string text;
  if (!readFile(path, &text)) {
    *error = "CONFIG_FILE_NOT_FOUND";
    return false;
  }
  const json data = json::parse(text, nullptr, false);
  if (data.is_discarded() || !data.is_object()) {
    *error = "CONFIG_FILE_INVALID";
    return false;
  }
  const std::string baseUrl = data.value("baseUrl", "");
  const std::string monitorId = data.value("monitorId", "");
  const std::string monitorSecret = data.value("monitorSecret", "");
  const std::string executablePath = data.value("weixinExecutablePath", "");
  const std::string observerPath = data.value("observerDllPath", "");
  if (baseUrl.rfind("https://", 0) != 0 || !validMonitorId(monitorId) || monitorSecret.size() < 32 ||
      executablePath.empty() || observerPath.empty()) {
    *error = "CONFIG_REQUIRED_VALUE_INVALID";
    return false;
  }
  const std::string receiver = data.value("expectedReceiverUsername", "");
  if (!receiver.empty() && !validWxid(receiver)) {
    *error = "CONFIG_RECEIVER_INVALID";
    return false;
  }
  const std::wstring baseDirectory = parentDirectory(path);
  output->baseUrl = utf8ToWide(baseUrl);
  while (!output->baseUrl.empty() && output->baseUrl.back() == L'/') output->baseUrl.pop_back();
  output->monitorId = monitorId;
  output->monitorSecret = monitorSecret;
  output->expectedReceiverUsername = receiver;
  output->payTypeFilter = data.value("wechatPayTypeFilter", std::string("wx_f2f"));
  output->weixinExecutablePath = resolvePath(baseDirectory, utf8ToWide(executablePath));
  output->observerDllPath = resolvePath(baseDirectory, utf8ToWide(observerPath));
  const wchar_t* localAppData = _wgetenv(L"LOCALAPPDATA");
  const std::wstring defaultRoot = localAppData == nullptr ? baseDirectory : joinPath(localAppData, L"CFPay");
  output->statePath = resolvePath(baseDirectory, utf8ToWide(data.value("statePath", wideToUtf8(joinPath(defaultRoot, L"wechat-monitor-state.json")))));
  output->logPath = resolvePath(baseDirectory, utf8ToWide(data.value("logPath", wideToUtf8(joinPath(defaultRoot, L"wechat-monitor.log")))));
  output->debugLogging = data.value("debugLogging", false);
  const auto positiveSeconds = [&](const char* key, int fallback) {
    const int value = data.value(key, fallback);
    return static_cast<DWORD>((value > 0 ? value : fallback) * 1000);
  };
  output->processPollMs = positiveSeconds("processPollSeconds", 5);
  output->queuePollMs = positiveSeconds("queuePollSeconds", 1);
  output->maxEventAgeMs = static_cast<std::uint64_t>(positiveSeconds("maxEventAgeSeconds", 600));
  if (GetFileAttributesW(output->weixinExecutablePath.c_str()) == INVALID_FILE_ATTRIBUTES) {
    *error = "CONFIG_RUNTIME_FILE_MISSING";
    return false;
  }
  return true;
}

class EventStore {
 public:
  bool open(const std::wstring& path) {
    path_ = path;
    ensureDirectory(parentDirectory(path_));
    std::string text;
    if (!readFile(path_, &text)) {
      state_ = emptyState();
      return persistLocked();
    }
    const json stored = json::parse(text, nullptr, false);
    if (stored.is_discarded() || !stored.is_object() || !stored.contains("pending") || !stored["pending"].is_array() ||
        !stored.contains("delivered") || !stored["delivered"].is_object()) {
      return false;
    }
    state_ = stored;
    state_["version"] = 2;
    if (!state_.contains("receiverCandidates") || !state_["receiverCandidates"].is_array()) {
      state_["receiverCandidates"] = json::array();
    }
    return persistLocked();
  }

  bool enqueue(const Payment& payment) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& item : state_["pending"]) {
      if (item.contains("payment") && item["payment"].value("eventId", "") == payment.eventId) return false;
    }
    if (state_["delivered"].contains(payment.eventId)) return false;
    const std::uint64_t now = nowMilliseconds();
    state_["pending"].push_back({
        {"payment", {{"eventId", payment.eventId}, {"provider", "wxpay"}, {"amount", payment.amount},
                     {"amountFen", payment.amountFen}, {"paidAt", payment.paidAt}, {"raw", payment.raw}}},
        {"attempts", 0}, {"nextAttemptAt", now}, {"createdAt", now}, {"lastError", nullptr},
    });
    return persistLocked();
  }

  std::vector<json> due() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<json> output;
    const std::uint64_t now = nowMilliseconds();
    for (const auto& item : state_["pending"]) {
      if (item.value("nextAttemptAt", UINT64_MAX) <= now) output.push_back(item);
    }
    std::sort(output.begin(), output.end(), [](const json& left, const json& right) {
      return left.value("nextAttemptAt", UINT64_MAX) < right.value("nextAttemptAt", UINT64_MAX);
    });
    return output;
  }

  void markRetry(const std::string& eventId, const std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& item : state_["pending"]) {
      if (!item.contains("payment") || item["payment"].value("eventId", "") != eventId) continue;
      const int attempts = item.value("attempts", 0) + 1;
      const std::uint64_t delay = std::min<std::uint64_t>(kMaxRetryDelayMs, 1000ULL << std::min(attempts - 1, 8));
      item["attempts"] = attempts;
      item["nextAttemptAt"] = nowMilliseconds() + delay;
      item["lastError"] = error.substr(0, 300);
      persistLocked();
      return;
    }
  }

  void markDelivered(const std::string& eventId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& pending = state_["pending"];
    pending.erase(std::remove_if(pending.begin(), pending.end(), [&](const json& item) {
                    return item.contains("payment") && item["payment"].value("eventId", "") == eventId;
                  }),
                  pending.end());
    state_["delivered"][eventId] = nowMilliseconds();
    std::vector<std::pair<std::string, std::uint64_t>> delivered;
    for (auto iterator = state_["delivered"].begin(); iterator != state_["delivered"].end(); ++iterator) {
      if (iterator.value().is_number_unsigned()) delivered.emplace_back(iterator.key(), iterator.value().get<std::uint64_t>());
    }
    const std::uint64_t minimum = nowMilliseconds() - kDeliveredRetentionMs;
    delivered.erase(std::remove_if(delivered.begin(), delivered.end(), [&](const auto& item) { return item.second < minimum; }),
                    delivered.end());
    std::sort(delivered.begin(), delivered.end(), [](const auto& left, const auto& right) { return left.second > right.second; });
    if (delivered.size() > kMaxDeliveredEvents) delivered.resize(kMaxDeliveredEvents);
    state_["delivered"] = json::object();
    for (const auto& item : delivered) state_["delivered"][item.first] = item.second;
    persistLocked();
  }

  void recordCandidate(const std::string& username, const std::string& nickname = {}, std::uint64_t observedAt = 0) {
    if (!validWxid(username)) return;
    std::lock_guard<std::mutex> lock(mutex_);
    const std::uint64_t now = observedAt == 0 ? nowMilliseconds() : observedAt;
    bool found = false;
    for (auto& candidate : state_["receiverCandidates"]) {
      if (candidate.value("username", "") != username) continue;
      candidate["lastSeenAt"] = now;
      candidate["count"] = candidate.value("count", 0) + 1;
      if (!nickname.empty() && nickname.size() <= 128) candidate["nickname"] = nickname;
      found = true;
      break;
    }
    if (!found) {
      state_["receiverCandidates"].push_back(
          {{"username", username}, {"nickname", nickname.size() <= 128 ? nickname : std::string()},
           {"firstSeenAt", now}, {"lastSeenAt", now}, {"count", 1}});
    }
    auto candidates = state_["receiverCandidates"].get<std::vector<json>>();
    std::sort(candidates.begin(), candidates.end(), [](const json& left, const json& right) {
      return left.value("lastSeenAt", 0ULL) > right.value("lastSeenAt", 0ULL);
    });
    if (candidates.size() > 5) candidates.resize(5);
    state_["receiverCandidates"] = candidates;
    persistLocked();
  }

 private:
  static json emptyState() {
    return {{"version", 2}, {"pending", json::array()}, {"delivered", json::object()}, {"receiverCandidates", json::array()}};
  }

  bool persistLocked() {
    return writeFileAtomically(path_, state_.dump() + "\n");
  }

  std::wstring path_;
  json state_ = emptyState();
  std::mutex mutex_;
};

EventStore gStore;

class MonitorApiClient {
 public:
  explicit MonitorApiClient(const Config& config) : config_(config) {}

  bool post(const wchar_t* requestPath,
            const json& payload,
            DWORD* statusCode = nullptr,
            DWORD* errorCode = nullptr,
            json* responseJson = nullptr,
            std::string* serverMessage = nullptr) const {
    if (statusCode != nullptr) *statusCode = 0;
    if (errorCode != nullptr) *errorCode = 0;
    const std::string body = payload.dump();
    const std::string timestamp = std::to_string(nowMilliseconds() / 1000);
    std::vector<std::uint8_t> nonceBytes(24);
    if (BCryptGenRandom(nullptr, nonceBytes.data(), static_cast<ULONG>(nonceBytes.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) {
      if (errorCode != nullptr) *errorCode = ERROR_GEN_FAILURE;
      return false;
    }
    const std::string nonce = base64Url(nonceBytes);
    const std::string signature = hmacSha256(config_.monitorSecret, timestamp + "\n" + nonce + "\n" + hexValue(sha256(body)));
    if (signature.empty()) {
      if (errorCode != nullptr) *errorCode = ERROR_GEN_FAILURE;
      return false;
    }

    const std::wstring url = config_.baseUrl + requestPath;
    URL_COMPONENTSW components{};
    components.dwStructSize = sizeof(components);
    components.dwSchemeLength = static_cast<DWORD>(-1);
    components.dwHostNameLength = static_cast<DWORD>(-1);
    components.dwUrlPathLength = static_cast<DWORD>(-1);
    components.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &components) || components.nScheme != INTERNET_SCHEME_HTTPS) {
      if (errorCode != nullptr) *errorCode = ERROR_INVALID_DATA;
      return false;
    }
    const std::wstring host(components.lpszHostName, components.dwHostNameLength);
    std::wstring path(components.lpszUrlPath, components.dwUrlPathLength);
    if (components.dwExtraInfoLength != 0) path.append(components.lpszExtraInfo, components.dwExtraInfoLength);

    HINTERNET session = WinHttpOpen(monitorUserAgent().c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (session == nullptr) {
      if (errorCode != nullptr) *errorCode = GetLastError();
      return false;
    }
    WinHttpSetTimeouts(session, 5000, 5000, 5000, 10000);
    HINTERNET connection = WinHttpConnect(session, host.c_str(), components.nPort, 0);
    DWORD requestError = connection == nullptr ? GetLastError() : 0;
    HINTERNET request = connection == nullptr
                            ? nullptr
                            : WinHttpOpenRequest(connection, L"POST", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                                 WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (connection != nullptr && request == nullptr) requestError = GetLastError();
    const std::wstring headers = L"Content-Type: application/json\r\nX-Monitor-Id: " + utf8ToWide(config_.monitorId) +
                                 L"\r\nX-Timestamp: " + utf8ToWide(timestamp) + L"\r\nX-Nonce: " + utf8ToWide(nonce) +
                                 L"\r\nX-Signature: " + utf8ToWide(signature) + L"\r\n";
    bool success = request != nullptr;
    if (success && WinHttpSendRequest(request, headers.c_str(), static_cast<DWORD>(-1), const_cast<char*>(body.data()),
                                     static_cast<DWORD>(body.size()), static_cast<DWORD>(body.size()), 0) == FALSE) {
      requestError = GetLastError();
      success = false;
    }
    if (success && WinHttpReceiveResponse(request, nullptr) == FALSE) {
      requestError = GetLastError();
      success = false;
    }
    DWORD responseCode = 0;
    DWORD responseSize = sizeof(responseCode);
    bool statusKnown = false;
    if (success) {
      if (WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                              WINHTTP_HEADER_NAME_BY_INDEX, &responseCode, &responseSize,
                              WINHTTP_NO_HEADER_INDEX) == FALSE) {
        requestError = GetLastError();
        success = false;
      } else {
        statusKnown = true;
        success = responseCode >= 200 && responseCode < 300;
      }
    }
    // 无论成功还是失败都读响应体：失败时服务端返回的 {"code":-1,"msg":"..."} 是唯一的原因来源，
    // 只在调用方显式要 JSON 时才读，会把 401/400 的真正理由整个丢掉。
    std::string responseBody;
    if (statusKnown) {
      while (responseBody.size() < kMaxResponseBytes) {
        DWORD available = 0;
        if (WinHttpQueryDataAvailable(request, &available) == FALSE) {
          requestError = GetLastError();
          success = false;
          break;
        }
        if (available == 0) break;
        const std::size_t offset = responseBody.size();
        const DWORD wanted = static_cast<DWORD>(std::min<std::size_t>(available, kMaxResponseBytes - offset));
        responseBody.resize(offset + wanted);
        DWORD bytesRead = 0;
        if (WinHttpReadData(request, responseBody.data() + offset, wanted, &bytesRead) == FALSE) {
          requestError = GetLastError();
          responseBody.resize(offset);
          success = false;
          break;
        }
        responseBody.resize(offset + bytesRead);
        if (bytesRead == 0) break;
      }
    }
    if (success && responseJson != nullptr) {
      *responseJson = json::parse(responseBody, nullptr, false);
      if (responseJson->is_discarded() || !responseJson->is_object()) {
        requestError = ERROR_INVALID_DATA;
        success = false;
      }
    }
    if (statusCode != nullptr) *statusCode = responseCode;
    if (errorCode != nullptr) *errorCode = requestError;
    if (serverMessage != nullptr) {
      const auto [code, message] = serverReply(responseBody);
      *serverMessage = success ? std::string()
                               : (!message.empty() ? message
                                  : code != u8"\u65e0" ? u8"\u670d\u52a1\u7aef\u8fd4\u56de\u7801 " + code
                                                     : u8"HTTP " + std::to_string(responseCode));
    }
    logHttpExchange(requestPath, body, success, responseCode, requestError, responseBody);
    if (request != nullptr) WinHttpCloseHandle(request);
    if (connection != nullptr) WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return success;
  }

 private:
  const Config& config_;
};

// 把上报结果整理成中文诊断片段（结果/HTTP 状态/WinHTTP 错误码），日志与界面共用。
std::string uploadDiagnostic(bool success, DWORD statusCode, DWORD errorCode) {
  return std::string(u8"\u7ed3\u679c=") + (success ? u8"\u6210\u529f" : u8"\u5931\u8d25") +
         " httpStatus=" + std::to_string(statusCode) + " winError=" + std::to_string(errorCode);
}

// 配置文件处理结果码到中文说明的映射：这些码由 EnsureWechatConfigFile 给出，
// 直接显示英文码用户看不懂「CONFIG_UPDATED」到底改了什么。
std::string configDetailDescription(const std::string& detail) {
  if (detail == "CONFIG_CREATED") return u8"\u9996\u6b21\u8fd0\u884c\uff0c\u5df2\u6309\u4ee3\u7801\u91cc\u7684\u9ed8\u8ba4\u503c\u521b\u5efa\u914d\u7f6e\u6587\u4ef6";
  if (detail == "CONFIG_UPDATED") return u8"\u914d\u7f6e\u6587\u4ef6\u5df2\u8865\u9f50\u7f3a\u5931\u5b57\u6bb5\uff0c\u539f\u6709\u53d6\u503c\u672a\u6539\u52a8";
  if (detail == "CONFIG_REBUILT_FROM_INVALID_FILE") return u8"\u539f\u914d\u7f6e\u6587\u4ef6\u683c\u5f0f\u65e0\u6548\uff0c\u5df2\u5907\u4efd\u5e76\u6309\u9ed8\u8ba4\u503c\u91cd\u5efa";
  if (detail == "CONFIG_UNCHANGED") return u8"\u914d\u7f6e\u6587\u4ef6\u5b57\u6bb5\u9f50\u5168\uff0c\u65e0\u9700\u6539\u52a8";
  if (detail == "CONFIG_DIRECTORY_UNAVAILABLE") return u8"\u65e0\u6cd5\u521b\u5efa\u914d\u7f6e\u76ee\u5f55";
  if (detail == "CONFIG_WRITE_FAILED") return u8"\u914d\u7f6e\u6587\u4ef6\u5199\u5165\u5931\u8d25";
  return detail;
}

// 诊断码到中文说明的映射，界面、日志与未识别描述的兜底都用它。
std::string statusDescription(const std::string& detailCode) {
  if (detailCode == "HOOK_ACTIVE") return u8"\u5fae\u4fe1\u5df2\u767b\u5f55\uff0cHook \u6b63\u5e38\u8fd0\u884c";
  if (detailCode == "WEIXIN_LOGIN_CHECKING") return u8"\u6b63\u5728\u68c0\u6d4b\u5fae\u4fe1\u767b\u5f55\u72b6\u6001";
  if (detailCode == "WEIXIN_LOGIN_REQUIRED") return u8"\u672a\u68c0\u6d4b\u5230\u5df2\u767b\u5f55\u7684\u5fae\u4fe1\u8d26\u53f7";
  if (detailCode == "TARGET_VALIDATED") return u8"\u5fae\u4fe1\u76ee\u6807\u6587\u4ef6\u6821\u9a8c\u901a\u8fc7";
  if (detailCode == "HOOK_INSTALLING") return u8"\u6b63\u5728\u542f\u52a8 Hook \u5bbf\u4e3b";
  if (detailCode == "HOOK_REACTIVATING") return u8"\u6b63\u5728\u6062\u590d\u5df2\u52a0\u8f7d\u7684\u89c2\u5bdf\u7ec4\u4ef6";
  if (detailCode == "HOOK_STOPPED") return u8"Hook \u5df2\u505c\u6b62\u5e76\u6e05\u7406\u65ad\u70b9";
  if (detailCode == "THREAD_COVERAGE_INCOMPLETE") return u8"\u90e8\u5206\u5fae\u4fe1\u7ebf\u7a0b\u672a\u8986\u76d6";
  if (detailCode == "THREAD_COVERAGE_RESTORED") return u8"\u5fae\u4fe1\u7ebf\u7a0b\u8986\u76d6\u5df2\u6062\u590d";
  if (detailCode == "RECEIVER_CONFIRMATION_REQUIRED") return u8"\u5f53\u524d\u5fae\u4fe1\u8d26\u53f7\u9700\u8981\u672c\u673a\u786e\u8ba4";
  if (detailCode == "ACCOUNT_MISMATCH") return u8"\u5f53\u524d\u5fae\u4fe1\u8d26\u53f7\u4e0e\u5df2\u786e\u8ba4\u8d26\u53f7\u4e0d\u4e00\u81f4";
  if (detailCode == "PROCESS_NOT_FOUND") return u8"\u672a\u627e\u5230\u53ef\u7528\u7684\u5fae\u4fe1\u8fdb\u7a0b";
  if (detailCode == "PROCESS_EXITED") return u8"\u5fae\u4fe1\u8fdb\u7a0b\u6216 Hook \u5bbf\u4e3b\u5df2\u9000\u51fa";
  if (detailCode == "SIGNATURE_MISMATCH") return u8"\u5fae\u4fe1\u7248\u672c\u6216\u51fd\u6570\u7b7e\u540d\u4e0d\u5339\u914d";
  if (detailCode == "VERSION_MISMATCH") return u8"\u5fae\u4fe1\u7248\u672c\u4e0e\u5f53\u524d\u76d1\u63a7\u7ec4\u4ef6\u4e0d\u5339\u914d";
  if (detailCode == "FILE_NOT_FOUND") return u8"\u672a\u627e\u5230\u6216\u65e0\u6cd5\u6253\u5f00\u5fae\u4fe1\u76ee\u6807\u6587\u4ef6";
  if (detailCode == "HOOK_INSTALL_FAILED") return u8"Hook \u5b89\u88c5\u5931\u8d25";
  if (detailCode == "HOOK_NOT_ACTIVE") return u8"Hook \u89c2\u5bdf\u7ec4\u4ef6\u672a\u5728\u76ee\u6807\u8fdb\u7a0b\u4e2d\u8fd0\u884c";
  if (detailCode == "HOOK_STOP_FAILED") return u8"Hook \u505c\u6b62\u5931\u8d25";
  if (detailCode == "ACCOUNT_READ_FAILED") return u8"\u5f53\u524d\u5fae\u4fe1\u8d26\u53f7\u4fe1\u606f\u8bfb\u53d6\u5931\u8d25";
  if (detailCode == "READ_FAILED") return u8"\u89c2\u5bdf\u6570\u636e\u8bfb\u53d6\u5931\u8d25";
  return u8"\u72b6\u6001\u5df2\u66f4\u65b0";
}

// 收款记录被拒绝的原因码到中文说明的映射。
std::string rejectionReasonDescription(const std::string& reason) {
  if (reason == "INVALID_RECORD") return u8"\u6536\u6b3e\u8bb0\u5f55\u7ed3\u6784\u4e0d\u5b8c\u6574";
  if (reason == "INVALID_RECEIPT_FIELDS") return u8"\u6536\u6b3e\u52a9\u624b\u7ed3\u6784\u5316\u5b57\u6bb5\u7f3a\u5931\u6216\u7c7b\u578b\u65e0\u6548";
  if (reason == "RECEIVER_MISMATCH") return u8"\u6536\u6b3e\u8d26\u53f7\u4e0e\u5df2\u786e\u8ba4\u8d26\u53f7\u4e0d\u4e00\u81f4";
  if (reason == "TRANSACTION_ID_MISSING") return u8"\u6536\u6b3e\u8bb0\u5f55\u7f3a\u5c11\u4ea4\u6613\u6807\u8bc6";
  if (reason == "INVALID_AMOUNT") return u8"\u6536\u6b3e\u91d1\u989d\u7f3a\u5931\u6216\u683c\u5f0f\u65e0\u6548";
  if (reason == "INVALID_PAYMENT_TIME") return u8"\u6536\u6b3e\u65f6\u95f4\u7f3a\u5931\u6216\u683c\u5f0f\u65e0\u6548";
  if (reason == "PAYMENT_TIME_OUT_OF_RANGE") return u8"\u6536\u6b3e\u65f6\u95f4\u8d85\u51fa\u5141\u8bb8\u7a97\u53e3";
  if (reason == "INVALID_LOGIN_STATE") return u8"\u8d26\u53f7\u8bb0\u5f55\u7f3a\u5c11\u6709\u6548\u767b\u5f55\u72b6\u6001";
  if (reason == "INVALID_ACCOUNT_RECORD") return u8"\u5f53\u524d\u8d26\u53f7\u8bb0\u5f55\u4e0d\u7b26\u5408 wxid \u683c\u5f0f";
  if (reason == "RECEIVER_CONFIRMATION_REQUIRED") return u8"\u5f53\u524d\u6536\u6b3e\u8d26\u53f7\u5c1a\u672a\u5728\u672c\u673a\u786e\u8ba4";
  return u8"\u6536\u6b3e\u8bb0\u5f55\u672a\u901a\u8fc7\u5b89\u5168\u6821\u9a8c";
}

// 微信登录状态码到中文说明的映射。
std::string loginStatusDescription(const std::string& status) {
  if (status == "LOGIN_REQUIRED") return u8"\u5fae\u4fe1\u9700\u8981\u5b8c\u6210\u767b\u5f55";
  if (status == "LOGIN_CLICKED") return u8"\u5df2\u8c03\u7528\u5fae\u4fe1\u767b\u5f55\u6309\u94ae\uff0c\u6b63\u5728\u7b49\u5f85\u8fdb\u5165\u4e3b\u754c\u9762";
  if (status == "QR_READY") return u8"\u5df2\u5728\u672c\u673a\u89e3\u6790\u5fae\u4fe1\u767b\u5f55\u4e8c\u7ef4\u7801";
  if (status == "LOGGED_IN") return u8"\u5fae\u4fe1\u5df2\u767b\u5f55";
  if (status == "RESTARTING") return u8"\u767b\u5f55\u672a\u8fdb\u5165\u4e3b\u754c\u9762\uff0c\u6b63\u5728\u53d7\u9650\u91cd\u542f\u5fae\u4fe1";
  return u8"\u5fae\u4fe1\u767b\u5f55\u72b6\u6001\u5df2\u66f4\u65b0";
}

// 登录辅助的诊断码到中文说明的映射。
std::string loginDiagnosticDescription(const std::string& diagnostic) {
  if (diagnostic == "QR_DECODED") return u8"\u5df2\u89e3\u6790\u767b\u5f55\u4e8c\u7ef4\u7801";
  if (diagnostic == "LOGIN_BUTTON_INVOKED") return u8"\u5df2\u8c03\u7528\u767b\u5f55\u6309\u94ae";
  if (diagnostic == "LOGIN_BUTTON_FOUND") return u8"\u5df2\u627e\u5230\u767b\u5f55\u6309\u94ae\uff0c\u4f46\u672a\u6210\u529f\u8c03\u7528";
  if (diagnostic == "LOGIN_UI_NOT_RECOGNIZED") return u8"\u672a\u8bc6\u522b\u5fae\u4fe1\u767b\u5f55\u4e8c\u7ef4\u7801\u6216\u767b\u5f55\u6309\u94ae";
  return u8"\u5fae\u4fe1\u767b\u5f55\u754c\u9762\u68c0\u67e5\u7ed3\u679c\u672a\u77e5";
}

// 把观察 DLL 传回的英文指标串翻译成中文标签（登录状态、各断点命中数、读取失败数等）。
// 键名与取值都翻译，未知键原样保留，避免新增指标时后台看不到。
std::string statusMetrics(const std::string& message) {
  const std::size_t separator = message.find(';');
  if (separator == std::string::npos) return {};

  const std::map<std::string, std::string> labels = {
      {"loginState", u8"\u767b\u5f55\u72b6\u6001"},
      {"loginAgeMs", u8"\u767b\u5f55\u8bc1\u636e\u8ddd\u4eca\u6beb\u79d2"},
      {"receiptEntries", u8"\u6536\u6b3e\u52a9\u624b\u5165\u53e3\u547d\u4e2d\u6570"},
      {"receiptReturns", u8"\u6536\u6b3e\u52a9\u624b\u8fd4\u56de\u547d\u4e2d\u6570"},
      {"accountEntries", u8"\u8d26\u53f7\u5165\u53e3\u547d\u4e2d\u6570"},
      {"accountReturns", u8"\u8d26\u53f7\u8fd4\u56de\u547d\u4e2d\u6570"},
      {"nicknameEntries", u8"\u6635\u79f0\u5165\u53e3\u547d\u4e2d\u6570"},
      {"nicknameReturns", u8"\u6635\u79f0\u8fd4\u56de\u547d\u4e2d\u6570"},
      {"readFailures", u8"\u8bfb\u53d6\u5931\u8d25\u6570"},
      {"queueDrops", u8"\u961f\u5217\u4e22\u5f03\u6570"},
      {"threads", u8"\u5df2\u8986\u76d6\u7ebf\u7a0b\u6570"},
  };
  const std::map<std::string, std::string> loginStates = {
      {"logged_in", u8"\u5df2\u767b\u5f55"},
      {"logged_out", u8"\u672a\u767b\u5f55"},
      {"unknown", u8"\u672a\u77e5"},
  };

  std::string output;
  std::size_t cursor = separator + 1;
  while (cursor < message.size()) {
    const std::size_t next = message.find(';', cursor);
    const std::string metric = trim(message.substr(cursor, next == std::string::npos ? next : next - cursor));
    const std::size_t equals = metric.find('=');
    if (equals != std::string::npos) {
      const std::string key = trim(metric.substr(0, equals));
      std::string value = trim(metric.substr(equals + 1));
      const auto label = labels.find(key);
      if (key == "loginState") {
        const auto state = loginStates.find(value);
        if (state != loginStates.end()) value = state->second;
      }
      output += "; " + (label == labels.end() ? key : label->second) + "=" + value;
    }
    if (next == std::string::npos) break;
    cursor = next + 1;
  }
  return output;
}

// 上报给后台的说明一律用中文。观察 DLL 通过管道传回的是内部英文描述，
// 统一在监控端这一层翻译，避免改动已注入微信进程、风控敏感的观察器。
bool hasNonAsciiByte(const std::string& text) {
  for (const unsigned char byte : text) {
    if (byte >= 0x80) return true;
  }
  return false;
}

// 把要上报的说明统一成中文。
// 观察 DLL 通过管道传回的是内部英文描述，集中在监控端这一层翻译，
// 避免改动已注入微信进程、对风控敏感的观察器；映射不到的未知描述回退成诊断码的中文说明，
// 保证后台「说明」列永远不会出现英文。
std::string observerMessageDescription(const std::string& detailCode, const std::string& message) {
  if (hasNonAsciiByte(message)) return message;  // 调用方已经给出中文说明
  static const std::map<std::string, std::string> kObserverMessages = {
      {"Weixin.dll is not loaded in the target process", u8"\u76ee\u6807\u8fdb\u7a0b\u5c1a\u672a\u52a0\u8f7d Weixin.dll"},
      {"flue.dll is not loaded in the target process", u8"\u76ee\u6807\u8fdb\u7a0b\u5c1a\u672a\u52a0\u8f7d flue.dll"},
      {"Weixin NewSync parser or account signature does not match", u8"\u5fae\u4fe1 NewSync \u89e3\u6790\u51fd\u6570\u6216\u8d26\u53f7\u51fd\u6570\u7b7e\u540d\u4e0d\u5339\u914d"},
      {"diagnostic candidate memory signature does not match", u8"\u8bca\u65ad\u5019\u9009\u5730\u5740\u7684\u5185\u5b58\u7b7e\u540d\u4e0d\u5339\u914d"},
      {"hardware breakpoint installation failed", u8"\u786c\u4ef6\u65ad\u70b9\u5b89\u88c5\u5931\u8d25"},
      {"observer worker thread creation failed", u8"\u89c2\u5bdf\u5668\u5de5\u4f5c\u7ebf\u7a0b\u521b\u5efa\u5931\u8d25"},
      {"one or more live threads were not covered", u8"\u90e8\u5206\u5fae\u4fe1\u7ebf\u7a0b\u5c1a\u672a\u8986\u76d6"},
      {"all live threads are covered again", u8"\u5fae\u4fe1\u7ebf\u7a0b\u8986\u76d6\u5df2\u5168\u90e8\u6062\u590d"},
      {"hardware breakpoints cleared; DLL remains loaded", u8"\u786c\u4ef6\u65ad\u70b9\u5df2\u6e05\u9664\uff0cDLL \u4ecd\u4fdd\u6301\u52a0\u8f7d"},
      {"one or more thread breakpoints could not be cleared", u8"\u90e8\u5206\u7ebf\u7a0b\u65ad\u70b9\u672a\u80fd\u6e05\u9664"},
      {"WMPF browser diagnostic breakpoints installed", u8"WMPF \u6d4f\u89c8\u5668\u8bca\u65ad\u65ad\u70b9\u5df2\u5b89\u88c5"},
      {"waiting for current Weixin account observation", u8"\u6b63\u5728\u7b49\u5f85\u5f53\u524d\u5fae\u4fe1\u8d26\u53f7\u7684\u89c2\u5bdf\u7ed3\u679c"},
      {"test receipt and account observer active", u8"\u6d4b\u8bd5\u76ee\u6807\u7684\u6536\u6b3e\u4e0e\u8d26\u53f7\u89c2\u5bdf\u5668\u5df2\u5c31\u7eea"},
      {"Weixin receipt observer active and current account detected", u8"\u5fae\u4fe1\u6536\u6b3e\u89c2\u5bdf\u5668\u5df2\u5c31\u7eea\u5e76\u68c0\u6d4b\u5230\u5f53\u524d\u8d26\u53f7"},
      {"Weixin receipt parser and account observer active", u8"\u5fae\u4fe1\u6536\u6b3e\u89e3\u6790\u4e0e\u8d26\u53f7\u89c2\u5bdf\u5668\u5df2\u5c31\u7eea"},
      {"receipt and account breakpoints installed in test target", u8"\u6d4b\u8bd5\u76ee\u6807\u7684\u6536\u6b3e\u4e0e\u8d26\u53f7\u65ad\u70b9\u5df2\u5b89\u88c5"},
      {"Weixin NewSync payment observer active", u8"\u5fae\u4fe1 NewSync \u6536\u6b3e\u89c2\u5bdf\u5668\u5df2\u5c31\u7eea"},
      {"ACCOUNT_READ_FAILED", u8"\u5f53\u524d\u5fae\u4fe1\u8d26\u53f7\u4fe1\u606f\u8bfb\u53d6\u5931\u8d25"},
      {"NICKNAME_READ_FAILED", u8"\u5f53\u524d\u5fae\u4fe1\u6635\u79f0\u8bfb\u53d6\u5931\u8d25"},
      {"PAY_MESSAGE_CONTENT_READ_FAILED", u8"\u6536\u6b3e\u6d88\u606f\u5185\u5bb9\u8bfb\u53d6\u5931\u8d25"},
      {"READ_FAILED", u8"\u89c2\u5bdf\u6570\u636e\u8bfb\u53d6\u5931\u8d25"}};
  const auto whole = kObserverMessages.find(message);
  if (whole != kObserverMessages.end()) return whole->second + statusMetrics(message);
  const std::size_t separator = message.find(';');
  if (separator != std::string::npos) {
    const auto prefix = kObserverMessages.find(trim(message.substr(0, separator)));
    if (prefix != kObserverMessages.end()) return prefix->second + statusMetrics(message);
  }
  // 映射不到的未知描述回退成诊断码的中文说明，保证后台不会出现英文。
  return statusDescription(detailCode);
}

// 组件状态到中文短名的映射，用于界面标题。
std::wstring statusName(const std::string& status) {
  if (status == "HEALTHY") return L"\u5065\u5eb7";
  if (status == "STARTING") return L"\u6b63\u5728\u542f\u52a8";
  if (status == "DEGRADED") return L"\u5df2\u964d\u7ea7";
  if (status == "ERROR") return L"\u9519\u8bef";
  if (status == "UNSUPPORTED") return L"\u7248\u672c\u4e0d\u652f\u6301";
  return L"\u5df2\u505c\u6b62";
}

// 按当前状态刷新窗口文本与托盘提示；托盘右键菜单也在这里更新状态行。
void refreshWindowAndTray() {
  std::wstring status;
  std::wstring detail;
  std::wstring fieldTestStatus;
  bool networkOk = true;
  std::wstring networkDetail;
  {
    std::lock_guard<std::mutex> lock(gState.displayMutex);
    status = gState.displayStatus;
    detail = gState.displayDetail;
    fieldTestStatus = gState.fieldTestStatus;
    networkOk = gState.networkOk;
    networkDetail = gState.networkDetail;
  }
  if (!networkOk) {
    status += L" / \u4e0a\u62a5\u5f02\u5e38";
    if (!networkDetail.empty()) status += L"\uff1a" + networkDetail;
  }
  if (gState.statusLabel != nullptr) SetWindowTextW(gState.statusLabel, status.c_str());
  if (gState.detailLabel != nullptr) SetWindowTextW(gState.detailLabel, detail.c_str());
  if (gState.fieldTestLabel != nullptr) SetWindowTextW(gState.fieldTestLabel, fieldTestStatus.c_str());
  const std::wstring tooltip = L"CFPay \u5fae\u4fe1\u76d1\u63a7: " + status;
  wcsncpy_s(gState.tray.szTip, tooltip.c_str(), _TRUNCATE);
  Shell_NotifyIconW(NIM_MODIFY, &gState.tray);
}

// 读取当前正在进行的现场测试类型（原子量），无测试时返回 None。
FieldTestKind currentFieldTest() {
  return static_cast<FieldTestKind>(gState.activeFieldTest.load());
}

// 更新现场测试的状态文本并刷新界面。
void updateFieldTestStatus(const std::wstring& status) {
  {
    std::lock_guard<std::mutex> lock(gState.displayMutex);
    gState.fieldTestStatus = status;
  }
  if (gState.window != nullptr) PostMessageW(gState.window, kRefreshMessage, 0, 0);
}

// 申请开始一项现场测试；用 CAS 保证同时只有一项在跑，重复点击会被拒绝而不是并发执行。
bool requestFieldTest(FieldTestKind kind) {
  int expected = static_cast<int>(FieldTestKind::None);
  if (gState.activeFieldTest.load() != expected ||
      !gState.fieldTestRequest.compare_exchange_strong(expected, static_cast<int>(kind))) {
    return false;
  }
  if (kind == FieldTestKind::ButtonLogin) {
    updateFieldTestStatus(L"\u73b0\u573a\u6d4b\u8bd5\uff1a\u6b63\u5728\u51c6\u5907\u6309\u94ae\u767b\u5f55\u6d4b\u8bd5");
  } else if (kind == FieldTestKind::QrLogin) {
    updateFieldTestStatus(L"\u73b0\u573a\u6d4b\u8bd5\uff1a\u6b63\u5728\u51c6\u5907\u626b\u7801\u767b\u5f55\u6d4b\u8bd5");
  } else {
    updateFieldTestStatus(L"\u73b0\u573a\u6d4b\u8bd5\uff1a\u6b63\u5728\u68c0\u67e5\u6536\u6b3e\u6d4b\u8bd5\u6761\u4ef6");
  }
  return true;
}

// 结束现场测试并记录成败与原因；同样用 CAS，避免重复结束或被别的测试覆盖。
void finishFieldTest(FieldTestKind kind, bool success, const std::wstring& detail) {
  int expected = static_cast<int>(kind);
  if (!gState.activeFieldTest.compare_exchange_strong(expected, static_cast<int>(FieldTestKind::None))) return;
  gState.fieldTestStartedAt = 0;
  updateFieldTestStatus(std::wstring(L"\u73b0\u573a\u6d4b\u8bd5\uff1a") +
                        (success ? L"\u901a\u8fc7 - " : L"\u5931\u8d25 - ") + detail);
  appendLog(success ? "INFO" : "ERROR",
            std::string(success ? u8"\u73b0\u573a\u6d4b\u8bd5\u5df2\u901a\u8fc7 \u8bf4\u660e="
                                : u8"\u73b0\u573a\u6d4b\u8bd5\u5df2\u5931\u8d25 \u8bf4\u660e=") +
                wideToUtf8(detail));
  if (success && gState.fieldTestSequence) {
    const FieldTestKind next = kind == FieldTestKind::ButtonLogin
                                   ? FieldTestKind::QrLogin
                                   : (kind == FieldTestKind::QrLogin ? FieldTestKind::Payment : FieldTestKind::None);
    if (next != FieldTestKind::None) {
      gState.fieldTestRequest = static_cast<int>(next);
      appendLog("INFO", kind == FieldTestKind::ButtonLogin
                            ? u8"\u73b0\u573a\u6d4b\u8bd5\u5e8f\u5217\u5c06\u7ee7\u7eed\u626b\u7801\u767b\u5f55\u6d4b\u8bd5"
                            : u8"\u73b0\u573a\u6d4b\u8bd5\u5e8f\u5217\u5c06\u7ee7\u7eed\u6536\u6b3e\u5339\u914d\u6d4b\u8bd5");
    }
  }
}

// 登录类现场测试在拿到登录证据后自动判定成功，不需要人工再点一次。
void completeLoginFieldTest() {
  const FieldTestKind kind = currentFieldTest();
  if (kind == FieldTestKind::ButtonLogin) {
    finishFieldTest(kind, true, L"\u6309\u94ae\u767b\u5f55\u540e Hook \u5df2\u5065\u5eb7");
  } else if (kind == FieldTestKind::QrLogin) {
    finishFieldTest(kind, true, L"\u626b\u7801\u767b\u5f55\u540e Hook \u5df2\u5065\u5eb7");
  }
}

// 更新界面上的状态与说明；说明文本统一走中文翻译层，三个常见状态另有更口语化的固定文案。
// 上报是否正常由网络线程通过 setNetworkOk 单独维护：改状态的线程并不发请求，
// 让它顺手写「上报成功/失败」只会把还没发生的结果先画到界面上。
void updateDisplay(const std::string& status, const std::string& detailCode, const std::string& message) {
  {
    std::lock_guard<std::mutex> lock(gState.displayMutex);
    gState.displayStatus = statusName(status) + L" (" + utf8ToWide(detailCode) + L")";
    if (detailCode == "HOOK_ACTIVE") {
      gState.displayDetail = L"\u5fae\u4fe1\u5df2\u767b\u5f55\uff0cHook \u6b63\u5e38\u8fd0\u884c";
    } else if (detailCode == "WEIXIN_LOGIN_CHECKING") {
      gState.displayDetail = L"\u6b63\u5728\u68c0\u6d4b\u5fae\u4fe1\u767b\u5f55\u72b6\u6001";
    } else if (detailCode == "WEIXIN_LOGIN_REQUIRED") {
      gState.displayDetail = L"\u5fae\u4fe1\u672a\u767b\u5f55\uff0c\u8bf7\u5148\u5b8c\u6210\u767b\u5f55";
    } else {
      gState.displayDetail = utf8ToWide(observerMessageDescription(detailCode, message));
    }
  }
  if (gState.window != nullptr) PostMessageW(gState.window, kRefreshMessage, 0, 0);
}

// 更新界面上的「上报是否正常」标记与失败原因，只有网络线程在真正拿到上报结果后调用。
void setNetworkOk(bool ok, const std::wstring& detail = {}) {
  {
    std::lock_guard<std::mutex> lock(gState.displayMutex);
    if (gState.networkOk == ok && gState.networkDetail == detail) return;
    gState.networkOk = ok;
    gState.networkDetail = detail;
  }
  if (gState.window != nullptr) PostMessageW(gState.window, kRefreshMessage, 0, 0);
}

// 上报给后台的目标版本文本：由 wechat_target.h 的版本号拼出，与宿主实际校验的版本同源。
std::string weixinTargetLabel() { return "Weixin " + wideToUtf8(cfpay::target::kWeixinVersion); }

// 上报给后台的 Hook 偏移文本：由 NewSyncResponse 解析入口的 RVA 格式化而来，
// 与观察器实际下断点的地址同源，避免“上报一个偏移、断点打在另一个偏移”。
std::string parserHookOffsetText() {
  char buffer[16] = {};
  std::snprintf(buffer, sizeof(buffer), "0x%x", cfpay::target::kNewSyncParseRva);
  return std::string(buffer);
}

// 设备信息（计算机名、平台、监控端版本）随每次状态上报一起发送，
// 服务端据此同时刷新 monitor_device 与 monitor_component_status，不再需要单独的心跳请求。
json deviceFields() {
  wchar_t computerName[256] = {};
  DWORD length = static_cast<DWORD>(std::size(computerName));
  GetComputerNameW(computerName, &length);
  return json{{"name", wideToUtf8(computerName)}, {"platform", "windows"}, {"version", kMonitorVersion}};
}

// 上报载荷：设备信息 + 组件（Hook）状态 + 微信登录状态，三块合并成同一个请求。
// snapshot.status 为空表示监控端还没拿到任何 Hook 状态，此时只发设备字段与登录状态。
// observedAt 一律取当前时间：服务端会拒绝超过 10 分钟的 observedAt，
// 沿用最初的观察时间会让长期稳定的状态在 10 分钟后被判过期。
json statusPayload(const ReportSnapshot& snapshot, std::uint64_t now) {
  json payload = deviceFields();
  if (!snapshot.status.empty()) {
    payload["component"] = "wechat_hook";
    payload["status"] = snapshot.status;
    payload["targetVersion"] = weixinTargetLabel();
    payload["hookOffset"] = parserHookOffsetText();
    payload["detailCode"] = snapshot.detailCode;
    payload["message"] = snapshot.message;
    payload["observedAt"] = now;
    if (snapshot.processId != 0) payload["processId"] = snapshot.processId;
    if (snapshot.lastEventAt != 0) payload["lastEventAt"] = snapshot.lastEventAt;
  }
  // 微信登录状态作为子对象随同上报，允许的键与它单独成一条路由时完全一致，
  // 服务端只认 status / observedAt / qrPayload 三个键。
  if (!snapshot.loginStatus.empty()) {
    json login{{"status", snapshot.loginStatus}, {"observedAt", now}};
    // 二维码只在 QR_READY 时上行：其它状态带着二维码，后台会继续显示已经失效的登录码。
    if (!snapshot.qrPayload.empty() && snapshot.loginStatus == "QR_READY") login["qrPayload"] = snapshot.qrPayload;
    payload["login"] = std::move(login);
  }
  return payload;
}

// 组件状态指纹：只含会改变语义的字段，不含时间。
// 与最近一次成功上报的指纹一致，说明这次只是定期重申，不必标记为待上报。
std::string componentFingerprint(const ReportSnapshot& snapshot) {
  return snapshot.status + "|" + snapshot.detailCode + "|" + snapshot.message + "|" +
         std::to_string(snapshot.processId) + "|" + std::to_string(snapshot.lastEventAt);
}

// 登录状态指纹：二维码内容只取哈希，避免明文二维码留在指纹串里。
std::string loginFingerprint(const ReportSnapshot& snapshot) {
  const std::string qrHash = snapshot.qrPayload.empty() ? std::string() : hexValue(sha256(snapshot.qrPayload));
  return snapshot.loginStatus + "|" + std::to_string(snapshot.loginProcessId) + "|" + qrHash;
}

// 记录组件（Hook）状态：只更新快照，不发请求，上报统一由网络线程执行。
// 说明在这里就翻译成中文，后台看到的不会是观察 DLL 通过管道传回的英文原文。
void setComponentState(const std::string& status,
                       const std::string& detailCode,
                       const std::string& message,
                       DWORD processId,
                       std::uint64_t lastEventAt = 0) {
  const std::string translated = observerMessageDescription(detailCode, message);
  {
    std::lock_guard<std::mutex> lock(gReportMutex);
    gReport.status = status;
    gReport.detailCode = detailCode;
    gReport.message = translated;
    gReport.processId = processId;
    gReport.lastEventAt = lastEventAt;
    if (componentFingerprint(gReport) != gReport.postedFingerprint) gReport.dirty = true;
  }
  updateDisplay(status, detailCode, translated);
  appendLog("INFO", std::string(u8"\u7ec4\u4ef6\u72b6\u6001\u5df2\u66f4\u65b0 status=") + status + " detail=" + detailCode +
                        " processId=" + std::to_string(processId) + u8" \u8bf4\u660e=" + translated);
}

// 记录微信登录状态：同样只更新快照。二维码内容只在 QR_READY 时保留，
// 其它状态一律清空，避免过期二维码继续被上报到后台。
void setLoginState(const std::string& status, DWORD processId, const std::string& qrPayload = {}) {
  const std::string qr = status == "QR_READY" ? qrPayload : std::string();
  {
    std::lock_guard<std::mutex> lock(gReportMutex);
    gReport.loginStatus = status;
    gReport.loginProcessId = processId;
    gReport.qrPayload = qr;
    if (loginFingerprint(gReport) != gReport.postedLoginFingerprint) gReport.dirty = true;
  }
  appendLog("INFO", std::string(u8"\u5fae\u4fe1\u767b\u5f55\u72b6\u6001\u5df2\u66f4\u65b0 status=") + status + " processId=" + std::to_string(processId) +
                        u8" \u8bf4\u660e=" + loginStatusDescription(status) +
                        (qr.empty() ? std::string() : u8" \u4e8c\u7ef4\u7801\u5b57\u8282=" + std::to_string(qr.size())));
}

// 唯一的上报出口：设备、组件与微信登录状态合并成一个请求发到 /api/monitor/status。
// 成功后只在内容没有再次变化时清除待上报标记，期间产生的新变化留到下一次上报，不会被吞掉。
bool postStatus(const MonitorApiClient& client, DWORD* outStatusCode = nullptr, DWORD* outErrorCode = nullptr,
                std::string* serverMessage = nullptr) {
  ReportSnapshot snapshot;
  {
    std::lock_guard<std::mutex> lock(gReportMutex);
    snapshot = gReport;
  }
  const std::uint64_t now = nowMilliseconds();
  DWORD statusCode = 0;
  DWORD errorCode = 0;
  const bool posted =
      client.post(L"/api/monitor/status", statusPayload(snapshot, now), &statusCode, &errorCode, nullptr, serverMessage);
  if (outStatusCode != nullptr) *outStatusCode = statusCode;
  if (outErrorCode != nullptr) *outErrorCode = errorCode;
  std::lock_guard<std::mutex> lock(gReportMutex);
  if (!posted) {
    gReport.dirty = true;
    return false;
  }
  gReport.postedAt = now;
  gReport.postedFingerprint = componentFingerprint(snapshot);
  gReport.postedLoginFingerprint = loginFingerprint(snapshot);
  if (componentFingerprint(gReport) == gReport.postedFingerprint &&
      loginFingerprint(gReport) == gReport.postedLoginFingerprint) {
    gReport.dirty = false;
  }
  return true;
}

// 等待网络线程把指定的登录状态送达 Worker。上报只有一处，这里只等结果，不自己再发一次请求。
// 现场测试（扫码登录）需要确认二维码确实上报成功，才能提示「等待扫码」而不是「正在重试上报」。
bool waitForLoginUpload(const std::string& expectedFingerprint, std::uint64_t timeoutMs) {
  const std::uint64_t deadline = nowMilliseconds() + timeoutMs;
  while (!gState.stopping.load()) {
    {
      std::lock_guard<std::mutex> lock(gReportMutex);
      if (gReport.postedLoginFingerprint == expectedFingerprint) return true;
    }
    if (nowMilliseconds() >= deadline) return false;
    if (WaitForSingleObject(gState.stopEvent, 50) == WAIT_OBJECT_0) return false;
  }
  return false;
}

// 把金额文本解析成整数分。
// 全程整数运算，不经浮点；同时返回归一化后的文本用于校验，避免 1.005 这类值被四舍五入成错误的分。
std::optional<std::pair<std::int64_t, std::string>> parseAmount(const std::string& value) {
  std::wstring normalized;
  for (wchar_t character : trimWide(utf8ToWide(value))) {
    if (character >= 0xff10 && character <= 0xff19) character = L'0' + (character - 0xff10);
    if (character == 0xff0e) character = L'.';
    normalized.push_back(character);
  }
  normalized = trimWide(normalized);
  auto startsWithInsensitive = [&](const wchar_t* prefix) {
    const std::size_t length = std::wcslen(prefix);
    return normalized.size() >= length && _wcsnicmp(normalized.c_str(), prefix, length) == 0;
  };
  if (startsWithInsensitive(L"CNY") || startsWithInsensitive(L"RMB")) normalized = trimWide(normalized.substr(3));
  else if (!normalized.empty() && (normalized.front() == 0x00a5 || normalized.front() == 0xffe5)) {
    normalized = trimWide(normalized.substr(1));
  }
  if (!normalized.empty() && normalized.back() == 0x5143) normalized = trimWide(normalized.substr(0, normalized.size() - 1));
  const std::size_t dot = normalized.find(L'.');
  if (dot != std::wstring::npos && normalized.find(L'.', dot + 1) != std::wstring::npos) return std::nullopt;
  const std::wstring yuan = dot == std::wstring::npos ? normalized : normalized.substr(0, dot);
  std::wstring fraction = dot == std::wstring::npos ? L"" : normalized.substr(dot + 1);
  if (yuan.empty() || yuan.size() > 8 || fraction.size() > 2 || (yuan.size() > 1 && yuan.front() == L'0') ||
      !std::all_of(yuan.begin(), yuan.end(), iswdigit) || !std::all_of(fraction.begin(), fraction.end(), iswdigit)) {
    return std::nullopt;
  }
  while (fraction.size() < 2) fraction.push_back(L'0');
  const long long amountFen = std::stoll(yuan) * 100 + (fraction.empty() ? 0 : std::stoll(fraction));
  if (amountFen <= 0 || amountFen > 9999999999LL) return std::nullopt;
  return std::make_pair(static_cast<std::int64_t>(amountFen), wideToUtf8(yuan + L"." + fraction));
}

// 解析支付时间：接受秒或毫秒时间戳，并限制在合理区间内，防止用异常时间绕过事件时效校验。
std::optional<std::uint64_t> parsePaymentTime(const json& value) {
  if (value.is_number_integer()) {
    const std::int64_t number = value.get<std::int64_t>();
    if (number > 0) {
      const std::uint64_t timestamp = static_cast<std::uint64_t>(number) * 1000;
      if (timestamp >= kMinPaymentTimeMs) return timestamp;
    }
    return std::nullopt;
  }
  if (!value.is_string()) return std::nullopt;
  const std::string normalized = trim(value.get<std::string>());
  if ((normalized.size() == 10 || normalized.size() == 13) &&
      std::all_of(normalized.begin(), normalized.end(), [](unsigned char character) { return std::isdigit(character) != 0; })) {
    try {
      const std::uint64_t number = std::stoull(normalized);
      const std::uint64_t timestamp = normalized.size() == 10 ? number * 1000 : number;
      if (timestamp >= kMinPaymentTimeMs) return timestamp;
    } catch (...) {
    }
  }
  return std::nullopt;
}

// 按标签名提取 paymsg 协议字段，自动去除首尾空白与 CDATA 包裹。
std::string extractXmlValue(const std::string& xml, const std::string& tag) {
  const std::string open = "<" + tag + ">";
  const std::string close = "</" + tag + ">";
  const std::size_t start = xml.find(open);
  if (start == std::string::npos) return {};
  const std::size_t valueStart = start + open.size();
  const std::size_t end = xml.find(close, valueStart);
  if (end == std::string::npos) return {};
  std::string value = xml.substr(valueStart, end - valueStart);
  std::size_t begin = 0;
  while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin])) != 0) begin += 1;
  if (begin >= value.size()) return {};
  std::size_t last = value.size() - 1;
  while (last > begin && std::isspace(static_cast<unsigned char>(value[last])) != 0) last -= 1;
  value = value.substr(begin, last - begin + 1);
  static constexpr char cdataOpen[] = "<![CDATA[";
  static constexpr char cdataClose[] = "]]>";
  const std::size_t cdataOpenLength = sizeof(cdataOpen) - 1;
  const std::size_t cdataCloseLength = sizeof(cdataClose) - 1;
  if (value.size() > cdataOpenLength + cdataCloseLength && value.compare(0, cdataOpenLength, cdataOpen) == 0 &&
      value.compare(value.size() - cdataCloseLength, cdataCloseLength, cdataClose) == 0) {
    value = value.substr(cdataOpenLength, value.size() - cdataOpenLength - cdataCloseLength);
  }
  return value;
}

// 分转元字符串，保留两位小数。
std::string formatFen(std::int64_t amountFen) {
  const std::int64_t yuan = amountFen / 100;
  const std::int64_t fraction = amountFen % 100;
  char buffer[32] = {};
  std::snprintf(buffer, sizeof(buffer), "%lld.%02lld", static_cast<long long>(yuan), static_cast<long long>(fraction));
  return buffer;
}

// 解析纯十进制整数字段，含非数字字符即返回兜底值，不用会抛异常的转换。
std::int64_t parseDecimalField(const std::string& value, std::int64_t fallback) {
  if (value.empty()) return fallback;
  for (const char character : value) {
    if (std::isdigit(static_cast<unsigned char>(character)) == 0 && character != '-') return fallback;
  }
  try {
    return std::stoll(value);
  } catch (...) {
    return fallback;
  }
}

// 取字符串字段并 trim，非字符串返回空。
std::string cleanIdentifier(const json& value) {
  if (!value.is_string()) return {};
  const std::string result = trim(value.get<std::string>());
  return result.empty() || result.size() > 256 ? "" : result;
}

// 对一条收款记录做全部安全判定，返回是否接受与拒绝原因。
// 判定全部来自协议结构化字段：消息类型、支付状态、金额为正、币种为 CNY、收款类型匹配配置、
// 收款账号等于本机已确认账号、交易号非空、时间在允许窗口内。
// 不解析展示文案、不做 OCR、不读数据库，避免把“微信支付收款 x 元”这类文本当成判定依据。
Evaluation evaluateRecord(const json& record, const Config& config, std::uint64_t now = 0) {
  Evaluation result;
  if (!record.is_object() || record.value("type", "") != "event" || !record.contains("fields") ||
      !record["fields"].is_object()) {
    result.reason = "INVALID_RECORD";
    return result;
  }
  const json& fields = record["fields"];
  if (!fields.contains("paySubtype") || !fields["paySubtype"].is_number_integer() ||
      !fields.contains("beginTransferTime")) {
    result.reason = "INVALID_RECEIPT_FIELDS";
    return result;
  }
  const std::string receiver = cleanIdentifier(fields.value("receiverUsername", json()));
  if (receiver.empty()) {
    result.reason = "INVALID_RECEIPT_FIELDS";
    return result;
  }
  if (!config.expectedReceiverUsername.empty() && receiver != config.expectedReceiverUsername) {
    result.reason = "RECEIVER_MISMATCH";
    return result;
  }
  const std::string transactionId = cleanIdentifier(fields.value("transactionId", json()));
  if (transactionId.empty()) {
    result.reason = "TRANSACTION_ID_MISSING";
    return result;
  }
  const auto amount = parseAmount(fields.value("feeDescription", ""));
  if (!amount.has_value()) {
    result.reason = "INVALID_AMOUNT";
    return result;
  }
  const auto paidAt = parsePaymentTime(fields["beginTransferTime"]);
  if (!paidAt.has_value()) {
    result.reason = "INVALID_PAYMENT_TIME";
    return result;
  }
  const std::uint64_t current = now == 0 ? nowMilliseconds() : now;
  if (*paidAt < current - config.maxEventAgeMs || *paidAt > current + kMaxFutureSkewMs) {
    result.reason = "PAYMENT_TIME_OUT_OF_RANGE";
    return result;
  }
  result.accepted = true;
  result.payment.eventId = "wechat:" + hexValue(sha256(transactionId));
  result.payment.amountFen = amount->first;
  result.payment.amount = amount->second;
  result.payment.paidAt = *paidAt;
  result.payment.raw = u8"\u5fae\u4fe1\u7ed3\u6784\u5316\u6536\u6b3e " + result.payment.amount + u8" \u5143";
  result.payment.receiverUsername = receiver;
  return result;
}

// 判断进程是否已加载指定模块；没加载 Weixin.dll 的微信进程还不是可注入目标。
bool processHasModule(DWORD processId, const wchar_t* moduleName) {
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, processId);
  if (snapshot == INVALID_HANDLE_VALUE) return false;
  MODULEENTRY32W entry{};
  entry.dwSize = sizeof(entry);
  bool found = false;
  if (Module32FirstW(snapshot, &entry)) {
    do {
      if (_wcsicmp(entry.szModule, moduleName) == 0) {
        found = true;
        break;
      }
    } while (Module32NextW(snapshot, &entry));
  }
  CloseHandle(snapshot);
  return found;
}

// 按可执行文件绝对路径精确匹配微信进程。
// requireModule 为真时还要求已加载 Weixin.dll，避免注入到刚启动、还没加载目标模块的进程。
std::vector<DWORD> findWeixinProcesses(const std::wstring& expectedPath, bool requireModule) {
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot == INVALID_HANDLE_VALUE) return {};
  PROCESSENTRY32W entry{};
  entry.dwSize = sizeof(entry);
  std::vector<DWORD> result;
  if (Process32FirstW(snapshot, &entry)) {
    do {
      if (_wcsicmp(entry.szExeFile, L"Weixin.exe") != 0) continue;
      HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
      if (process == nullptr) continue;
      std::vector<wchar_t> path(32768);
      DWORD length = static_cast<DWORD>(path.size());
      const bool matches = QueryFullProcessImageNameW(process, 0, path.data(), &length) != FALSE &&
                           _wcsicmp(std::wstring(path.data(), length).c_str(), expectedPath.c_str()) == 0;
      CloseHandle(process);
      if (matches && (!requireModule || processHasModule(entry.th32ProcessID, L"Weixin.dll"))) {
        result.push_back(entry.th32ProcessID);
      }
    } while (Process32NextW(snapshot, &entry));
  }
  CloseHandle(snapshot);
  return result;
}

// 取第一个满足条件的微信进程 ID，没有则返回 0。
DWORD findWeixinProcess(const std::wstring& expectedPath) {
  const std::vector<DWORD> processes = findWeixinProcesses(expectedPath, true);
  return processes.empty() ? 0 : processes.front();
}

// 以自身子进程的方式执行受限的注入/停止命令（--internal-host …）。
// 用独立子进程隔离注入失败或崩溃，主进程不会因为宿主异常而退出。
bool launchLifecycleCommand(const wchar_t* mode, DWORD processId, DWORD timeoutMilliseconds) {
  const std::wstring program = joinPath(gState.packageDirectory, L"cfpay-wechat-monitor.exe");
  std::wstring command = quoteArgument(program) + L" --internal-host " + mode + L" " + std::to_wstring(processId);
  std::vector<wchar_t> mutableCommand(command.begin(), command.end());
  mutableCommand.push_back(L'\0');
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  if (!CreateProcessW(program.c_str(), mutableCommand.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                      gState.packageDirectory.c_str(), &startup, &process)) {
    return false;
  }
  const DWORD wait = WaitForSingleObject(process.hProcess, timeoutMilliseconds);
  DWORD exitCode = 1;
  GetExitCodeProcess(process.hProcess, &exitCode);
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  return wait == WAIT_OBJECT_0 && exitCode == 0;
}

// 停止当前正在运行的宿主：先请求观察器清理断点，超时未退出才强制结束宿主进程。
void stopActiveHost() {
  DWORD weixinProcessId = 0;
  HANDLE hostProcess = nullptr;
  {
    std::lock_guard<std::mutex> lock(gState.hostMutex);
    weixinProcessId = gState.weixinProcessId;
    hostProcess = gState.hostProcess;
  }
  if (weixinProcessId != 0) launchLifecycleCommand(L"--stop-weixin", weixinProcessId, 15'000);
  if (hostProcess != nullptr && WaitForSingleObject(hostProcess, 5'000) != WAIT_OBJECT_0) TerminateProcess(hostProcess, 1);
}

// 处理宿主转发来的一条 JSONL 记录，是整个收款链路的判定入口。
// 按 type 分派：status（组件状态与登录证据）、account（当前账号与昵称）、event（收款记录）。
// 账号未经本机确认时只记录候选状态并上报 DEGRADED，绝不进入支付队列。
void handleHostRecord(
                      const json& record,
                      DWORD processId,
                      std::uint64_t* lastEventAt,
                      AccountObservation* accountObservation) {
  const std::string type = record.value("type", "");
  if (type == "status") {
    const std::string nativeStatus = record.value("status", "ERROR");
    const std::string detailCode = record.value("detailCode", "READ_FAILED");
    const std::uint64_t observedAt = record.value("observedAt", nowMilliseconds());
    if (nativeStatus == "HEALTHY") {
      if (!accountObservation->loggedIn || accountObservation->observedAt == 0) {
        gState.loginEvidenceProcessId = processId;
        gState.loginEvidenceLoggedIn = false;
        gState.loginEvidenceObservedAt = observedAt;
        setComponentState("DEGRADED", "WEIXIN_LOGIN_REQUIRED",
                          u8"\u5fae\u4fe1\u5c1a\u672a\u767b\u5f55\uff0c\u8bf7\u5148\u5b8c\u6210\u767b\u5f55", processId,
                          *lastEventAt);
      } else if (gState.config.expectedReceiverUsername.empty()) {
        setComponentState("DEGRADED", "RECEIVER_CONFIRMATION_REQUIRED",
                          u8"\u5f53\u524d\u5fae\u4fe1\u8d26\u53f7\u9700\u8981\u672c\u673a\u786e\u8ba4\u540e\u624d\u4f1a\u4e0a\u62a5\u6536\u6b3e",
                          processId, *lastEventAt);
      } else if (gState.config.expectedReceiverUsername != accountObservation->username) {
        setComponentState("DEGRADED", "ACCOUNT_MISMATCH",
                          u8"\u5f53\u524d\u5fae\u4fe1\u8d26\u53f7\u4e0e\u5df2\u786e\u8ba4\u7684\u6536\u6b3e\u8d26\u53f7\u4e0d\u4e00\u81f4",
                          processId, *lastEventAt);
      } else {
        gState.loginEvidenceProcessId = processId;
        gState.loginEvidenceLoggedIn = true;
        gState.loginEvidenceObservedAt = observedAt;
        setLoginState("LOGGED_IN", processId);
        setComponentState("HEALTHY", detailCode,
                          detailCode == "HOOK_ACTIVE" ? u8"\u5fae\u4fe1\u5df2\u767b\u5f55\uff0cHook \u6b63\u5e38\u8fd0\u884c"
                                                      : record.value("message", u8"\u89c2\u5bdf\u7ec4\u4ef6\u8fd0\u884c\u6b63\u5e38"),
                          processId, *lastEventAt);
        if (detailCode == "HOOK_ACTIVE") {
          appendLog("INFO", u8"Hook\u539f\u751f\u6307\u6807 " + record.value("message", std::string()));
          completeLoginFieldTest();
        }
      }
    } else {
      setComponentState(nativeStatus, detailCode,
                        record.value("message", u8"\u89c2\u5bdf\u7ec4\u4ef6\u4e0a\u62a5\u4e86\u65e0\u6548\u72b6\u6001"),
                        processId, *lastEventAt);
    }
    return;
  }
  if (type == "account") {
    const json fields = record.contains("fields") && record["fields"].is_object() ? record["fields"] : json::object();
    if (!fields.contains("loggedIn") || !fields["loggedIn"].is_boolean()) {
      appendLog("WARN", std::string(u8"\u8d26\u53f7Hook\u8bb0\u5f55\u5df2\u62d2\u7edd reason=INVALID_LOGIN_STATE processId=") +
                            std::to_string(processId) + u8" \u8bf4\u660e=" +
                            rejectionReasonDescription("INVALID_LOGIN_STATE"));
      return;
    }
    const bool loggedIn = fields.value("loggedIn", false);
    const std::string username = fields.value("username", "");
    const std::uint64_t capturedAt = record.value("capturedAt", nowMilliseconds());
    accountObservation->loggedIn = loggedIn;
    accountObservation->username = loggedIn ? username : "";
    accountObservation->nickname = loggedIn && fields.value("nickname", std::string()).size() <= 128
                                       ? fields.value("nickname", std::string())
                                       : "";
    accountObservation->observedAt = capturedAt;
    gState.loginEvidenceProcessId = processId;
    gState.loginEvidenceLoggedIn = loggedIn;
    gState.loginEvidenceObservedAt = capturedAt;
    if (!loggedIn) {
      setComponentState("DEGRADED", "WEIXIN_LOGIN_REQUIRED",
                        u8"\u5fae\u4fe1\u5c1a\u672a\u767b\u5f55\uff0c\u8bf7\u5148\u5b8c\u6210\u767b\u5f55", processId,
                        *lastEventAt);
      appendLog("INFO", std::string(u8"\u8d26\u53f7Hook\u68c0\u6d4b\u5230\u672a\u767b\u5f55 processId=") +
                            std::to_string(processId) + " capturedAt=" + std::to_string(capturedAt));
      return;
    }
    if (!validWxid(username)) {
      gState.loginEvidenceLoggedIn = false;
      appendLog("WARN", std::string(u8"\u8d26\u53f7Hook\u8bb0\u5f55\u5df2\u62d2\u7edd reason=INVALID_ACCOUNT_RECORD processId=") +
                            std::to_string(processId) + u8" \u8bf4\u660e=" +
                            rejectionReasonDescription("INVALID_ACCOUNT_RECORD"));
      setComponentState("DEGRADED", "WEIXIN_LOGIN_REQUIRED",
                        u8"\u5fae\u4fe1\u5c1a\u672a\u767b\u5f55\uff0c\u8bf7\u5148\u5b8c\u6210\u767b\u5f55", processId,
                        *lastEventAt);
      return;
    }
    gStore.recordCandidate(username, accountObservation->nickname, capturedAt);
    if (gState.config.expectedReceiverUsername.empty()) {
      setComponentState("DEGRADED", "RECEIVER_CONFIRMATION_REQUIRED",
                        u8"\u5df2\u53d1\u73b0\u5f53\u524d\u5fae\u4fe1\u8d26\u53f7\uff0c\u9700\u672c\u673a\u786e\u8ba4\u540e\u624d\u4f1a\u4e0a\u62a5\u6536\u6b3e",
                        processId, *lastEventAt);
    } else if (gState.config.expectedReceiverUsername != username) {
      setComponentState("DEGRADED", "ACCOUNT_MISMATCH",
                        u8"\u5f53\u524d\u5fae\u4fe1\u8d26\u53f7\u4e0e\u672c\u673a\u5df2\u786e\u8ba4\u7684\u6536\u6b3e\u8d26\u53f7\u4e0d\u4e00\u81f4",
                        processId, *lastEventAt);
    } else {
      setLoginState("LOGGED_IN", processId);
      setComponentState("HEALTHY", "HOOK_ACTIVE", u8"\u5fae\u4fe1\u5df2\u767b\u5f55\uff0cHook \u6b63\u5e38\u8fd0\u884c",
                        processId, *lastEventAt);
      completeLoginFieldTest();
    }
    appendLog("INFO", std::string(u8"\u8d26\u53f7Hook\u8bb0\u5f55\u5df2\u4fdd\u5b58 processId=") + std::to_string(processId) +
                          " capturedAt=" + std::to_string(capturedAt) + u8" \u5df2\u914d\u7f6e\u6536\u6b3e\u8d26\u53f7=" +
                          (gState.config.expectedReceiverUsername.empty() ? u8"\u5426" : u8"\u662f"));
    return;
  }
  if (type == "nickname") {
    const json fields = record.contains("fields") && record["fields"].is_object() ? record["fields"] : json::object();
    const std::string nickname = fields.value("nickname", "");
    const std::uint64_t capturedAt = record.value("capturedAt", nowMilliseconds());
    if (nickname.empty() || nickname.size() > 128 || !accountObservation->loggedIn || accountObservation->username.empty()) {
      appendLog("WARN", std::string(u8"\u6635\u79f0Hook\u8bb0\u5f55\u5df2\u5ffd\u7565 reason=INVALID_ACCOUNT_NICKNAME"));
      return;
    }
    accountObservation->nickname = nickname;
    gStore.recordCandidate(accountObservation->username, nickname, capturedAt);
    appendLog("INFO", std::string(u8"\u6635\u79f0Hook\u5df2\u66f4\u65b0\u5f53\u524d\u5fae\u4fe1\u6635\u79f0 processId=") +
                              std::to_string(processId));
    return;
  }
  if (type == "pay_message") {
    const json fields = record.contains("fields") && record["fields"].is_object() ? record["fields"] : json::object();
    const std::string content = fields.value("content", std::string());
    const std::string fromUsername = cleanIdentifier(fields.value("fromUsername", json()));
    const std::string toUsername = cleanIdentifier(fields.value("toUsername", json()));
    const std::int64_t msgType = fields.value("msgType", 0);
    const std::int64_t createTime = fields.value("createTime", 0);
    const std::uint64_t serverMsgId = fields.value("serverMsgId", 0ULL);
    const std::uint64_t capturedAt = record.value("capturedAt", nowMilliseconds());

    const std::string transid = extractXmlValue(content, "transid");
    const std::string outTradeNo = extractXmlValue(content, "outtradeno");
    const std::string payType = extractXmlValue(content, "type");
    const std::string payerUsername = extractXmlValue(content, "username");
    const std::string payerDisplayName = extractXmlValue(content, "displayname");
    const std::int64_t payMsgType = parseDecimalField(extractXmlValue(content, "PayMsgType"), -1);
    const std::int64_t fee = parseDecimalField(extractXmlValue(content, "fee"), -1);
    const std::int64_t feeType = parseDecimalField(extractXmlValue(content, "feetype"), -1);
    const std::int64_t payStatus = parseDecimalField(extractXmlValue(content, "status"), -1);
    const std::int64_t scene = parseDecimalField(extractXmlValue(content, "scene"), -1);
    const std::int64_t payTimestamp = parseDecimalField(extractXmlValue(content, "timestamp"), 0);

    appendLog("INFO", std::string(u8"\u652f\u4ed8\u901a\u77e5\u5df2\u6355\u83b7 processId=") + std::to_string(processId) +
                          " msgType=" + std::to_string(msgType) + " payMsgType=" + std::to_string(payMsgType) +
                          " status=" + std::to_string(payStatus) + " fee=" + std::to_string(fee) +
                          " feeType=" + std::to_string(feeType) + " type=" + payType +
                          " contentHead=" + content.substr(0, content.size() < 24 ? content.size() : 24) +
                          " transidBytes=" + std::to_string(transid.size()) +
                          " timestamp=" + std::to_string(payTimestamp) +
                          " toUsernameBytes=" + std::to_string(toUsername.size()));

    if (msgType != kWeixinSysMsgType) return;
    if (transid.empty() || transid.size() > 256) {
      appendLog("WARN", u8"\u652f\u4ed8\u901a\u77e5\u5df2\u62d2\u7edd reason=TRANSID_MISSING");
      return;
    }
    if (payStatus != kWeixinPayStatusSuccess) {
      appendLog("INFO", u8"\u652f\u4ed8\u901a\u77e5\u5df2\u5ffd\u7565 reason=STATUS_NOT_SUCCESS status=" +
                            std::to_string(payStatus));
      return;
    }
    if (fee <= 0 || fee > 100000000 || feeType != kWeixinFeeTypeCny) {
      appendLog("WARN", u8"\u652f\u4ed8\u901a\u77e5\u5df2\u62d2\u7edd reason=INVALID_FEE fee=" +
                            std::to_string(fee) + " feeType=" + std::to_string(feeType));
      return;
    }
    if (!gState.config.payTypeFilter.empty() && payType != gState.config.payTypeFilter) {
      appendLog("INFO", u8"\u652f\u4ed8\u901a\u77e5\u5df2\u5ffd\u7565 reason=PAY_TYPE_MISMATCH type=" + payType);
      return;
    }
    if (gState.config.expectedReceiverUsername.empty()) {
      setComponentState("DEGRADED", "RECEIVER_CONFIRMATION_REQUIRED",
                        "payment notification requires a locally confirmed receiver account", processId, *lastEventAt);
      appendLog("WARN", u8"\u652f\u4ed8\u901a\u77e5\u5df2\u6682\u505c\u4e0a\u62a5 reason=RECEIVER_CONFIRMATION_REQUIRED");
      return;
    }
    if (!toUsername.empty() && toUsername != gState.config.expectedReceiverUsername) {
      setComponentState("DEGRADED", "ACCOUNT_MISMATCH",
                        "payment notification receiver does not match the locally confirmed account", processId,
                        *lastEventAt);
      appendLog("WARN", u8"\u652f\u4ed8\u901a\u77e5\u5df2\u62d2\u7edd reason=RECEIVER_MISMATCH");
      return;
    }
    const std::int64_t paidSeconds = payTimestamp > 0 ? payTimestamp : createTime;
    const std::uint64_t paidAt = paidSeconds > 0 ? static_cast<std::uint64_t>(paidSeconds) * 1000 : capturedAt;
    const std::uint64_t now = nowMilliseconds();
    if (paidAt + kMaxFutureSkewMs < now || paidAt > now + kMaxFutureSkewMs) {
      appendLog("WARN", u8"\u652f\u4ed8\u901a\u77e5\u5df2\u62d2\u7edd reason=PAYMENT_TIME_OUT_OF_RANGE paidAt=" +
                            std::to_string(paidAt));
      return;
    }

    // 上报的是解析后的结构化 JSON，不包含微信消息原文。
    const json detail = {{"source", "wechat_paymsg"},
                         {"transid", transid},
                         {"outtradeno", outTradeNo},
                         {"payMsgType", payMsgType},
                         {"fee", fee},
                         {"feetype", feeType},
                         {"status", payStatus},
                         {"scene", scene},
                         {"type", payType},
                         {"timestamp", payTimestamp},
                         {"msgType", msgType},
                         {"createTime", createTime},
                         {"serverMsgId", std::to_string(serverMsgId)},
                         {"toUsername", toUsername},
                         {"fromUsername", fromUsername},
                         {"payerUsername", payerUsername},
                         {"payerDisplayName", payerDisplayName},
                         {"contentBytes", content.size()}};

    Payment payment;
    payment.eventId = "wechat:" + hexValue(sha256(transid));
    payment.amountFen = fee;
    payment.amount = formatFen(fee);
    payment.paidAt = paidAt;
    payment.raw = detail.dump();
    payment.receiverUsername = gState.config.expectedReceiverUsername;
    const bool enqueued = gStore.enqueue(payment);
    if (enqueued) *lastEventAt = paidAt;
    appendLog("INFO", std::string(enqueued ? u8"\u652f\u4ed8\u901a\u77e5\u5df2\u5165\u961f"
                                          : u8"\u652f\u4ed8\u901a\u77e5\u5df2\u53bb\u91cd") +
                          " amountFen=" + std::to_string(payment.amountFen) +
                          " paidAt=" + std::to_string(paidAt) + " transidBytes=" + std::to_string(transid.size()));
    return;
  }
  if (type != "event") {
    appendLog("WARN", std::string(u8"\u5bbf\u4e3b\u8bb0\u5f55\u5df2\u5ffd\u7565 type=") +
                          (type.empty() ? std::string(u8"<\u7a7a>") : type));
    return;
  }
  const json fields = record.contains("fields") && record["fields"].is_object() ? record["fields"] : json::object();
  appendLog("INFO", std::string(u8"\u6536\u6b3eHook\u4e8b\u4ef6\u5df2\u6355\u83b7 processId=") + std::to_string(processId) +
                        " capturedAt=" + std::to_string(record.value("capturedAt", 0ULL)) +
                        " paySubtype=" + std::to_string(fields.value("paySubtype", -1)) +
                        " feeDescriptionBytes=" +
                        std::to_string(fields.value("feeDescription", std::string()).size()) +
                        " transactionIdBytes=" + std::to_string(fields.value("transactionId", std::string()).size()) +
                        " receiverUsernameBytes=" +
                        std::to_string(fields.value("receiverUsername", std::string()).size()));
  const Evaluation evaluation = evaluateRecord(record, gState.config);
  if (!evaluation.accepted) {
    appendLog("WARN", std::string(u8"\u6536\u6b3eHook\u4e8b\u4ef6\u5df2\u62d2\u7edd reason=") + evaluation.reason +
                          u8" \u8bf4\u660e=" + rejectionReasonDescription(evaluation.reason));
    return;
  }
  if (gState.config.expectedReceiverUsername.empty()) {
    gStore.recordCandidate(evaluation.payment.receiverUsername, {}, record.value("capturedAt", nowMilliseconds()));
    setComponentState("DEGRADED", "RECEIVER_CONFIRMATION_REQUIRED",
                      "receiver account discovered; local confirmation is required before payment upload", processId,
                      *lastEventAt);
    appendLog("WARN", std::string(u8"\u6536\u6b3eHook\u4e8b\u4ef6\u5df2\u6682\u505c\u4e0a\u62a5 reason=RECEIVER_CONFIRMATION_REQUIRED amountFen=") +
                          std::to_string(evaluation.payment.amountFen) + u8" \u8bf4\u660e=" +
                          rejectionReasonDescription("RECEIVER_CONFIRMATION_REQUIRED"));
    return;
  }
  *lastEventAt = evaluation.payment.paidAt;
  const bool enqueued = gStore.enqueue(evaluation.payment);
  appendLog("INFO", std::string(enqueued ? u8"\u6536\u6b3e\u4e8b\u4ef6\u5df2\u5165\u961f" : u8"\u6536\u6b3e\u4e8b\u4ef6\u5df2\u53bb\u91cd") +
                        " amountFen=" + std::to_string(evaluation.payment.amountFen) +
                        " paidAt=" + std::to_string(evaluation.payment.paidAt));
}

// 启动宿主并逐行消费它的 JSONL 输出。
// 宿主退出后上报 PROCESS_EXITED 并重新等待目标进程；管道读到无效 JSON 只记日志，不中断整条链路。
void runHost(DWORD processId, std::uint64_t* lastEventAt) {
  gState.loginEvidenceProcessId = processId;
  gState.loginEvidenceObservedAt = 0;
  gState.loginEvidenceLoggedIn = false;
  appendLog("INFO", std::string(u8"\u6b63\u5728\u542f\u52a8\u539f\u751fHook\u5bbf\u4e3b processId=") + std::to_string(processId));
  setComponentState("STARTING", "HOOK_INSTALLING",
                    u8"\u5df2\u53d1\u73b0\u901a\u8fc7\u6821\u9a8c\u7684\u5fae\u4fe1\u8fdb\u7a0b", processId,
                    *lastEventAt);
  SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
  HANDLE readPipe = nullptr;
  HANDLE writePipe = nullptr;
  if (!CreatePipe(&readPipe, &writePipe, &security, 0)) {
    appendLog("ERROR", std::string(u8"Hook\u5bbf\u4e3b\u7ba1\u9053\u521b\u5efa\u5931\u8d25 winError=") + std::to_string(GetLastError()));
    setComponentState("ERROR", "HOOK_INSTALL_FAILED", u8"\u65e0\u6cd5\u521b\u5efa\u5bbf\u4e3b\u8f93\u51fa\u7ba1\u9053",
                      processId, *lastEventAt);
    return;
  }
  SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);
  HANDLE nullError = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING, 0, nullptr);
  const std::wstring program = joinPath(gState.packageDirectory, L"cfpay-wechat-monitor.exe");
  std::wstring command = quoteArgument(program) + L" --internal-host --run-weixin " + std::to_wstring(processId) + L" " +
                         quoteArgument(gState.config.observerDllPath);
  std::vector<wchar_t> mutableCommand(command.begin(), command.end());
  mutableCommand.push_back(L'\0');
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  startup.dwFlags = STARTF_USESTDHANDLES;
  startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  startup.hStdOutput = writePipe;
  startup.hStdError = nullError;
  PROCESS_INFORMATION process{};
  const BOOL created = CreateProcessW(program.c_str(), mutableCommand.data(), nullptr, nullptr, TRUE,
                                      CREATE_NO_WINDOW, nullptr, gState.packageDirectory.c_str(), &startup, &process);
  CloseHandle(writePipe);
  if (nullError != INVALID_HANDLE_VALUE) CloseHandle(nullError);
  if (!created) {
    appendLog("ERROR", std::string(u8"Hook\u5bbf\u4e3b\u8fdb\u7a0b\u521b\u5efa\u5931\u8d25 processId=") + std::to_string(processId) +
                           " winError=" + std::to_string(GetLastError()));
    CloseHandle(readPipe);
    setComponentState("ERROR", "HOOK_INSTALL_FAILED", u8"\u65e0\u6cd5\u542f\u52a8\u539f\u751f\u5bbf\u4e3b\u8fdb\u7a0b",
                      processId, *lastEventAt);
    return;
  }
  appendLog("INFO", std::string(u8"Hook\u5bbf\u4e3b\u5df2\u542f\u52a8 processId=") + std::to_string(processId) +
                        " hostProcessId=" + std::to_string(process.dwProcessId));
  {
    std::lock_guard<std::mutex> lock(gState.hostMutex);
    gState.hostProcess = process.hProcess;
    gState.hostProcessId = process.dwProcessId;
    gState.weixinProcessId = processId;
  }
  CloseHandle(process.hThread);
  std::string pending;
  std::array<char, 8192> buffer{};
  std::string lastStatus;
  AccountObservation accountObservation;
  while (!gState.stopping.load()) {
    DWORD bytesRead = 0;
    if (!ReadFile(readPipe, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead, nullptr) || bytesRead == 0) break;
    pending.append(buffer.data(), bytesRead);
    std::size_t newline = 0;
    while ((newline = pending.find('\n')) != std::string::npos) {
      std::string line = trim(pending.substr(0, newline));
      pending.erase(0, newline + 1);
      if (line.empty()) continue;
      const json record = json::parse(line, nullptr, false);
      if (record.is_discarded()) {
        appendLog("ERROR", std::string(u8"Hook\u5bbf\u4e3b\u8f93\u51fa\u4e86\u65e0\u6548JSON bytes=") + std::to_string(line.size()));
        continue;
      }
      if (record.value("type", "") == "status") lastStatus = record.value("status", "");
      handleHostRecord(record, processId, lastEventAt, &accountObservation);
    }
  }
  CloseHandle(readPipe);
  WaitForSingleObject(process.hProcess, 5'000);
  DWORD exitCode = 1;
  GetExitCodeProcess(process.hProcess, &exitCode);
  {
    std::lock_guard<std::mutex> lock(gState.hostMutex);
    gState.hostProcess = nullptr;
    gState.hostProcessId = 0;
    gState.weixinProcessId = 0;
  }
  CloseHandle(process.hProcess);
  if (gState.loginEvidenceProcessId.load() == processId) {
    gState.loginEvidenceProcessId = 0;
    gState.loginEvidenceObservedAt = 0;
    gState.loginEvidenceLoggedIn = false;
  }
  appendLog("INFO", std::string(u8"Hook\u5bbf\u4e3b\u5df2\u9000\u51fa processId=") + std::to_string(processId) +
                        " exitCode=" + std::to_string(exitCode) + " lastStatus=" +
                        (lastStatus.empty() ? std::string(u8"<\u65e0>") : lastStatus));
  if (!gState.stopping.load() && lastStatus != "ERROR" && lastStatus != "UNSUPPORTED") {
    setComponentState(exitCode == 0 ? "STOPPED" : "ERROR", exitCode == 0 ? "PROCESS_EXITED" : "HOOK_INSTALL_FAILED",
                      exitCode == 0 ? "native host exited" : "native host failed", processId, *lastEventAt);
  }
}

// 登录辅助线程：在明确的未登录证据下点击登录按钮或解析登录二维码上报。
// 按钮点击后一段时间仍无登录证据时最多自动重启微信一次，达到上限只上报 LOGIN_REQUIRED，
// 绝不循环点击或循环重启，避免触发风控。
void loginLoop() {
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  const MonitorApiClient client(gState.config);
  std::string lastStatus;
  std::string lastQrHash;
  bool lastPublishPosted = false;
  std::uint64_t nextReportAt = 0;
  DWORD clickedProcessId = 0;
  std::uint64_t clickedAt = 0;
  bool restartAttempted = false;
  int autoRestartCount = 0;
  DWORD qrObservedProcessId = 0;
  DWORD qrSwitchInvokedProcessId = 0;

  auto publish = [&](const std::string& status, DWORD processId, const std::string& qrPayload = {}) {
    const std::uint64_t now = nowMilliseconds();
    const std::string qrHash = qrPayload.empty() ? std::string() : hexValue(sha256(qrPayload));
    if (status == lastStatus && qrHash == lastQrHash && now < nextReportAt) return lastPublishPosted;
    setLoginState(status, processId, qrPayload);
    // 上报只有网络线程一个出口，这里只等结果：现场测试需要确认二维码确实送到了 Worker，
    // 才能提示「等待扫码」而不是「正在重试上报」。
    ReportSnapshot expected;
    expected.loginStatus = status;
    expected.loginProcessId = processId;
    expected.qrPayload = status == "QR_READY" ? qrPayload : std::string();
    const bool posted = waitForLoginUpload(loginFingerprint(expected), kUploadAckWaitMs);
    lastStatus = status;
    lastQrHash = qrHash;
    lastPublishPosted = posted;
    nextReportAt = now + kReportIntervalMs;
    return posted;
  };

  while (!gState.stopping.load()) {
    const std::uint64_t now = nowMilliseconds();
    const std::vector<DWORD> processes = findWeixinProcesses(gState.config.weixinExecutablePath, false);
    const DWORD evidenceProcessId = gState.loginEvidenceProcessId.load();
    const bool evidenceForRunningProcess = evidenceProcessId != 0 &&
                                           std::find(processes.begin(), processes.end(), evidenceProcessId) != processes.end();
    const bool loggedIn = evidenceForRunningProcess && gState.loginEvidenceLoggedIn.load();
    const FieldTestKind requestedTest =
        static_cast<FieldTestKind>(gState.fieldTestRequest.exchange(static_cast<int>(FieldTestKind::None)));
    if (requestedTest == FieldTestKind::ButtonLogin || requestedTest == FieldTestKind::QrLogin) {
      updateFieldTestStatus(requestedTest == FieldTestKind::ButtonLogin
                                ? L"\u73b0\u573a\u6d4b\u8bd5\uff1a\u6b63\u5728\u91cd\u542f\u5fae\u4fe1\u8fdb\u5165\u666e\u901a\u767b\u5f55\u9875"
                                : L"\u73b0\u573a\u6d4b\u8bd5\uff1a\u6b63\u5728\u91cd\u542f\u5fae\u4fe1\u8fdb\u5165\u626b\u7801\u767b\u5f55\u9875");
      stopActiveHost();
      gState.loginEvidenceProcessId = 0;
      gState.loginEvidenceObservedAt = 0;
      gState.loginEvidenceLoggedIn = false;
      DWORD restartedProcessId = 0;
      DWORD errorCode = 0;
      gState.activeFieldTest = static_cast<int>(requestedTest);
      gState.fieldTestStartedAt = nowMilliseconds();
      clickedProcessId = 0;
      clickedAt = 0;
      restartAttempted = false;
      qrObservedProcessId = 0;
      qrSwitchInvokedProcessId = 0;
      if (!RestartExactWeixinExecutable(gState.config.weixinExecutablePath, &restartedProcessId, &errorCode)) {
        finishFieldTest(requestedTest, false,
                        L"\u65e0\u6cd5\u4ece Windows \u672c\u673a\u91cd\u542f\u5fae\u4fe1\uff0c\u9519\u8bef " +
                            std::to_wstring(errorCode));
      } else {
        appendLog("INFO", std::string(requestedTest == FieldTestKind::ButtonLogin
                                           ? u8"\u6309\u94ae\u767b\u5f55\u73b0\u573a\u6d4b\u8bd5\u5df2\u542f\u52a8 processId="
                                           : u8"\u626b\u7801\u767b\u5f55\u73b0\u573a\u6d4b\u8bd5\u5df2\u542f\u52a8 processId=") +
                              std::to_string(restartedProcessId));
      }
      if (WaitForSingleObject(gState.stopEvent, 500) == WAIT_OBJECT_0) break;
      continue;
    }
    if (requestedTest == FieldTestKind::Payment) {
      gState.activeFieldTest = static_cast<int>(FieldTestKind::Payment);
      if (!loggedIn) {
        finishFieldTest(FieldTestKind::Payment, false, L"\u5fae\u4fe1\u6216 Hook \u5c1a\u672a\u5065\u5eb7");
      } else {
        gState.fieldTestStartedAt = now;
        updateFieldTestStatus(L"\u73b0\u573a\u6d4b\u8bd5\uff1a\u7b49\u5f85\u65b0\u8ba2\u5355\u4ed8\u6b3e\u5e76\u5339\u914d Worker");
        appendLog("INFO", u8"\u6536\u6b3e\u73b0\u573a\u6d4b\u8bd5\u5df2\u5f00\u59cb\uff0c\u7b49\u5f85\u65b0\u7684\u7ed3\u6784\u5316\u6536\u6b3e\u4e8b\u4ef6");
      }
    }

    if (loggedIn) {
      publish("LOGGED_IN", evidenceProcessId);
      clickedProcessId = 0;
      clickedAt = 0;
      restartAttempted = false;
      autoRestartCount = 0;
      qrObservedProcessId = 0;
      qrSwitchInvokedProcessId = 0;
    } else {
      std::vector<DWORD> candidates;
      candidates = processes;

      WechatLoginUiResult loginUi;
      DWORD loginUiProcessId = 0;
      for (const DWORD processId : candidates) {
        const WechatLoginUiResult inspected = InspectWechatLoginUi(processId, false);
        if (inspected.kind == WechatLoginUiKind::QrReady) {
          loginUi = inspected;
          loginUiProcessId = processId;
          break;
        }
        if (loginUi.kind == WechatLoginUiKind::None && inspected.kind == WechatLoginUiKind::LoginRequired) {
          loginUi = inspected;
          loginUiProcessId = processId;
        }
      }

      const FieldTestKind activeTest = currentFieldTest();
      if (activeTest == FieldTestKind::QrLogin && loginUi.kind == WechatLoginUiKind::QrReady) {
        const bool posted = publish("QR_READY", loginUiProcessId, loginUi.qrPayload);
        qrObservedProcessId = loginUiProcessId;
        clickedProcessId = 0;
        clickedAt = 0;
        updateFieldTestStatus(posted
                                  ? L"\u73b0\u573a\u6d4b\u8bd5\uff1a\u4e8c\u7ef4\u7801\u5df2\u4e0a\u62a5 Worker\uff0c\u7b49\u5f85\u626b\u7801"
                                  : L"\u73b0\u573a\u6d4b\u8bd5\uff1a\u4e8c\u7ef4\u7801\u5df2\u89e3\u6790\uff0c\u6b63\u5728\u91cd\u8bd5\u4e0a\u62a5");
      } else if (activeTest == FieldTestKind::QrLogin && loginUi.kind == WechatLoginUiKind::LoginRequired) {
        if (qrSwitchInvokedProcessId != loginUiProcessId) {
          if (InvokeWechatAccountSwitch(loginUiProcessId)) {
            qrSwitchInvokedProcessId = loginUiProcessId;
            updateFieldTestStatus(L"\u73b0\u573a\u6d4b\u8bd5\uff1a\u5df2\u5207\u6362\u8d26\u53f7\uff0c\u7b49\u5f85\u4e8c\u7ef4\u7801");
            appendLog("INFO", std::string(u8"\u626b\u7801\u767b\u5f55\u6d4b\u8bd5\u5df2\u8c03\u7528\u5207\u6362\u8d26\u53f7 processId=") +
                                  std::to_string(loginUiProcessId));
          } else if (now - gState.fieldTestStartedAt.load() >= 30'000) {
            restartAttempted = true;
            finishFieldTest(activeTest, false, L"\u672a\u80fd\u8c03\u7528\u5207\u6362\u8d26\u53f7\u6309\u94ae");
          }
        } else if (qrObservedProcessId == 0 && now - gState.fieldTestStartedAt.load() >= 30'000) {
          restartAttempted = true;
          finishFieldTest(activeTest, false, L"\u5207\u6362\u8d26\u53f7\u540e\u672a\u89e3\u6790\u5230\u4e8c\u7ef4\u7801");
        }
      } else if (activeTest == FieldTestKind::QrLogin) {
        if (qrObservedProcessId == 0 && now - gState.fieldTestStartedAt.load() >= 30'000) {
          restartAttempted = true;
          finishFieldTest(activeTest, false, L"\u672a\u8bc6\u522b\u5fae\u4fe1\u767b\u5f55\u9875");
        }
      } else if (loginUi.kind == WechatLoginUiKind::QrReady) {
        publish("QR_READY", loginUiProcessId, loginUi.qrPayload);
        qrObservedProcessId = loginUiProcessId;
        clickedProcessId = 0;
        clickedAt = 0;
      } else if (loginUi.kind == WechatLoginUiKind::LoginRequired && clickedProcessId == 0 && !restartAttempted &&
                 qrObservedProcessId != loginUiProcessId) {
        const WechatLoginUiResult action = InspectWechatLoginUi(loginUiProcessId, true);
        if (action.kind == WechatLoginUiKind::QrReady) {
          publish("QR_READY", loginUiProcessId, action.qrPayload);
        } else if (action.kind == WechatLoginUiKind::LoginClicked) {
          clickedProcessId = loginUiProcessId;
          clickedAt = now;
          publish("LOGIN_CLICKED", loginUiProcessId);
          if (activeTest == FieldTestKind::ButtonLogin) {
            updateFieldTestStatus(L"\u73b0\u573a\u6d4b\u8bd5\uff1a\u5df2\u8c03\u7528\u767b\u5f55\u6309\u94ae\uff0c\u7b49\u5f85 Hook \u5065\u5eb7");
          }
          appendLog("INFO", std::string(u8"\u5df2\u8c03\u7528\u5fae\u4fe1\u767b\u5f55\u6309\u94ae processId=") +
                                std::to_string(loginUiProcessId));
        } else {
          publish("LOGIN_REQUIRED", loginUiProcessId);
          appendLog("WARN", std::string(u8"\u5fae\u4fe1\u767b\u5f55\u6309\u94ae\u8c03\u7528\u5931\u8d25 processId=") +
                                std::to_string(loginUiProcessId) + " diagnostic=" + action.diagnostic +
                                u8" \u8bf4\u660e=" + loginDiagnosticDescription(action.diagnostic));
          if (activeTest == FieldTestKind::ButtonLogin) {
            restartAttempted = true;
            finishFieldTest(activeTest, false, L"\u767b\u5f55\u6309\u94ae\u8c03\u7528\u5931\u8d25");
          }
        }
      } else if (clickedProcessId != 0) {
        publish("LOGIN_CLICKED", clickedProcessId);
        if (!restartAttempted && autoRestartCount >= kMaxAutoRestartPerRun) {
          publish("LOGIN_REQUIRED", clickedProcessId);
          appendLog("WARN", std::string(u8"\u5df2\u8fbe\u5230\u672c\u6b21\u8fd0\u884c\u81ea\u52a8\u91cd\u542f\u4e0a\u9650\uff0c\u4e0d\u518d\u91cd\u542f\u5fae\u4fe1 processId=") +
                                std::to_string(clickedProcessId) + u8" \u4e0a\u9650=" +
                                std::to_string(kMaxAutoRestartPerRun));
          restartAttempted = true;
          clickedProcessId = 0;
          clickedAt = 0;
        } else if (!restartAttempted && clickedAt != 0 && now - clickedAt >= 20'000) {
          if (activeTest == FieldTestKind::ButtonLogin) {
            restartAttempted = true;
            finishFieldTest(activeTest, false, L"\u8c03\u7528\u767b\u5f55\u6309\u94ae\u540e 20 \u79d2\u672a\u8fdb\u5165 Hook \u5065\u5eb7\u72b6\u6001");
            clickedProcessId = 0;
            clickedAt = 0;
            continue;
          }
          restartAttempted = true;
          publish("RESTARTING", clickedProcessId);
          appendLog("WARN", std::string(u8"\u767b\u5f55\u6309\u94ae\u8c03\u7528\u540e\u672a\u8fdb\u5165\u4e3b\u754c\u9762\uff0c\u6b63\u5728\u53d7\u9650\u91cd\u542f\u5fae\u4fe1 processId=") +
                                std::to_string(clickedProcessId));
          stopActiveHost();
          DWORD restartedProcessId = 0;
          DWORD errorCode = 0;
          if (RestartExactWeixinExecutable(gState.config.weixinExecutablePath, &restartedProcessId, &errorCode)) {
            autoRestartCount += 1;
            appendLog("INFO", std::string(u8"\u5fae\u4fe1\u53d7\u9650\u91cd\u542f\u5df2\u5b8c\u6210 processId=") +
                                  std::to_string(restartedProcessId) + u8" \u672c\u6b21\u8fd0\u884c\u91cd\u542f\u6b21\u6570=" +
                                  std::to_string(autoRestartCount));
          } else {
            appendLog("ERROR", std::string(u8"\u5fae\u4fe1\u53d7\u9650\u91cd\u542f\u5931\u8d25 winError=") +
                                   std::to_string(errorCode));
          }
          gState.loginEvidenceProcessId = 0;
          gState.loginEvidenceObservedAt = 0;
          gState.loginEvidenceLoggedIn = false;
          clickedProcessId = 0;
          clickedAt = 0;
        }
      } else if (evidenceForRunningProcess && !gState.loginEvidenceLoggedIn.load()) {
        publish("LOGIN_REQUIRED", evidenceProcessId);
      } else if (activeTest == FieldTestKind::ButtonLogin && now - gState.fieldTestStartedAt.load() >= 30'000) {
        restartAttempted = true;
        finishFieldTest(activeTest, false, L"30 \u79d2\u5185\u672a\u8bc6\u522b\u5230\u666e\u901a\u767b\u5f55\u6309\u94ae");
      }
    }

    if (WaitForSingleObject(gState.stopEvent, 1'000) == WAIT_OBJECT_0) break;
  }
  CoUninitialize();
}

// 把持久化队列里到期的收款事件上报给 Worker。
// 先落盘再上报，失败按指数退避重试，进程重启后继续；成功的事件按保留期与条数上限清理。
void flushPayments(const MonitorApiClient& client) {
  for (const json& item : gStore.due()) {
    if (!item.contains("payment") || !item["payment"].is_object()) continue;
    const json& payment = item["payment"];
    const std::string eventId = payment.value("eventId", "");
    if (eventId.empty()) continue;
    json payload = {{"eventId", eventId},
                    {"provider", payment.value("provider", "wxpay")},
                    {"amount", payment.value("amount", "")},
                    {"paidAt", payment.value("paidAt", 0ULL)},
                    {"raw", payment.value("raw", "")}};
    DWORD statusCode = 0;
    DWORD errorCode = 0;
    json response;
    if (client.post(L"/api/monitor/payment", payload, &statusCode, &errorCode, &response)) {
      gStore.markDelivered(eventId);
      appendLog("INFO", std::string(u8"\u6536\u6b3e\u4e8b\u4ef6\u5df2\u4e0a\u62a5 amountFen=") + std::to_string(payment.value("amountFen", 0LL)) +
                            " paidAt=" + std::to_string(payment.value("paidAt", 0ULL)) + u8" \u4e0a\u62a5" +
                            uploadDiagnostic(true, statusCode, errorCode));
      if (currentFieldTest() == FieldTestKind::Payment) {
        const std::uint64_t paidAt = payment.value("paidAt", 0ULL);
        const std::uint64_t startedAt = gState.fieldTestStartedAt.load();
        if (paidAt + 5'000 >= startedAt) {
          if (response.value("matched", false)) {
            finishFieldTest(FieldTestKind::Payment, true, L"\u6536\u6b3e\u4e8b\u4ef6\u5df2\u4e0a\u62a5\u5e76\u5339\u914d\u8ba2\u5355");
          } else {
            finishFieldTest(FieldTestKind::Payment, false, L"\u6536\u6b3e\u4e8b\u4ef6\u5df2\u4e0a\u62a5\uff0c\u4f46 Worker \u672a\u5339\u914d\u8ba2\u5355");
          }
        }
      }
    } else {
      gStore.markRetry(eventId, "HTTP_" + std::to_string(statusCode));
      appendLog("ERROR", std::string(u8"\u6536\u6b3e\u4e8b\u4ef6\u5df2\u5b89\u6392\u91cd\u8bd5 amountFen=") + std::to_string(payment.value("amountFen", 0LL)) +
                             u8" \u4e0a\u62a5" + uploadDiagnostic(false, statusCode, errorCode));
    }
  }
}

// 网络线程：全程序唯一的状态上报出口，同时按 queuePollMs 刷新收款队列。
// 状态一有变化就立刻上报，没变化则按 kReportIntervalMs 定期重申，失败按 kReportRetryMs 退避。
// 两个节奏互相独立，队列发送慢不会拖慢状态上报；上报本身的成败由 postStatus 内部记日志。
void networkLoop() {
  const MonitorApiClient client(gState.config);
  std::uint64_t nextReport = 0;
  std::uint64_t nextRetry = 0;
  std::uint64_t nextQueue = 0;
  while (!gState.stopping.load()) {
    const std::uint64_t now = nowMilliseconds();
    bool pending = false;
    {
      std::lock_guard<std::mutex> lock(gReportMutex);
      pending = gReport.dirty;
    }
    // 变化触发与定期重申共用同一个出口。脏标记只在上报成功后清除，
    // 因此失败时必须靠 nextRetry 退避，否则网络线程会每 250 毫秒重试一次。
    if (now >= nextReport || (pending && now >= nextRetry)) {
      DWORD statusCode = 0;
      DWORD errorCode = 0;
      std::string serverMessage;
      const bool success = postStatus(client, &statusCode, &errorCode, &serverMessage);
      setNetworkOk(success, success ? std::wstring() : utf8ToWide(serverMessage));
      nextReport = now + kReportIntervalMs;
      nextRetry = success ? 0 : now + kReportRetryMs;
    }
    if (now >= nextQueue) {
      flushPayments(client);
      nextQueue = now + gState.config.queuePollMs;
    }
    if (WaitForSingleObject(gState.stopEvent, 250) == WAIT_OBJECT_0) break;
  }
}

// 主循环：加载配置、释放内嵌观察 DLL、打开状态存储，然后启动网络与登录线程并持续跟踪微信进程。
// 进程消失时上报 STOPPED，重新出现时再次注入；配置或 DLL 不可用时直接以明确错误码退出，不带病运行。
void monitorLoop() {
  CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  std::string error;
  // 配置文件的默认值写在代码里（与内嵌观察 DLL 同一套思路）：首次运行自动创建，
  // 已存在时只补缺失字段、绝不覆盖用户填过的值，因此仓库不需要再维护 example JSON。
  std::string configDetail;
  if (!EnsureWechatConfigFile(gState.configPath, &configDetail)) {
    updateDisplay("ERROR", "CONFIG_INVALID", configDetailDescription(configDetail));
    CoUninitialize();
    return;
  }
  if (!loadConfig(gState.configPath, &gState.config, &error)) {
    updateDisplay("ERROR", "CONFIG_INVALID", error);
    CoUninitialize();
    return;
  }
  // 配置就绪后才有了日志路径，因此配置文件的处理结果在这里补记一行。
  appendLog("INFO", std::string(u8"\u914d\u7f6e\u6587\u4ef6\u5df2\u5c31\u7eea detail=") + configDetail + u8" \u8bf4\u660e=" +
                        configDetailDescription(configDetail) + u8" path=" + wideToUtf8(gState.configPath));
  {
    std::string observerDetail;
    if (!materializeObserverDll(gState.config.observerDllPath, &observerDetail)) {
      appendLog("ERROR", std::string(u8"\u89c2\u5bdf\u5668 DLL \u51c6\u5907\u5931\u8d25 reason=") + observerDetail);
      updateDisplay("ERROR", "OBSERVER_DLL_UNAVAILABLE", observerDetail);
      CoUninitialize();
      return;
    }
    const std::string imageDigest = hexValue(sha256(std::string(reinterpret_cast<const char*>(embeddedObserverDllImage()),
                                                                embeddedObserverDllSize())));
    appendLog("INFO", std::string(u8"\u89c2\u5bdf\u5668 DLL \u5df2\u5c31\u7eea state=") + observerDetail +
                          " bytes=" + std::to_string(embeddedObserverDllSize()) + " sha256=" + imageDigest +
                          " path=" + wideToUtf8(gState.config.observerDllPath));
  }
  if (!gStore.open(gState.config.statePath)) {
    updateDisplay("ERROR", "STATE_INVALID", u8"\u76d1\u63a7\u7aef\u72b6\u6001\u6587\u4ef6\u65e0\u6548");
    CoUninitialize();
    return;
  }
  appendLog("INFO", std::string(u8"\u539f\u751f\u76d1\u63a7\u7aef\u5df2\u542f\u52a8 version=") + kMonitorVersion +
                        u8" \u5df2\u914d\u7f6e\u6536\u6b3e\u8d26\u53f7=" +
                        (gState.config.expectedReceiverUsername.empty() ? u8"\u5426" : u8"\u662f") +
                        u8" \u4e0a\u62a5\u95f4\u9694\u79d2=" + std::to_string(kReportIntervalMs / 1000) +
                        u8" \u8fdb\u7a0b\u68c0\u67e5\u95f4\u9694\u79d2=" + std::to_string(gState.config.processPollMs / 1000) +
                        u8" \u961f\u5217\u68c0\u67e5\u95f4\u9694\u79d2=" + std::to_string(gState.config.queuePollMs / 1000));
  gState.networkThread = std::thread(networkLoop);
  gState.loginThread = std::thread(loginLoop);
  const MonitorApiClient client(gState.config);
  std::uint64_t lastEventAt = 0;
  while (!gState.stopping.load()) {
    const DWORD processId = findWeixinProcess(gState.config.weixinExecutablePath);
    if (processId == 0) {
      const std::vector<DWORD> loginProcesses = findWeixinProcesses(gState.config.weixinExecutablePath, false);
      if (loginProcesses.empty()) {
        appendLog("WARN", u8"\u672a\u627e\u5230\u5fae\u4fe1\u76ee\u6807\u8fdb\u7a0b");
        setComponentState("STOPPED", "PROCESS_NOT_FOUND", u8"\u5fae\u4fe1\u8fdb\u7a0b\u672a\u5728\u8fd0\u884c", 0,
                          lastEventAt);
      } else {
        appendLog("INFO", std::string(u8"\u5fae\u4fe1\u5df2\u542f\u52a8\u4f46\u5c1a\u672a\u8fdb\u5165\u4e3b\u7a0b\u5e8f processId=") +
                              std::to_string(loginProcesses.front()));
        setComponentState("DEGRADED", "WEIXIN_LOGIN_REQUIRED",
                          u8"\u5fae\u4fe1\u5c1a\u672a\u767b\u5f55\uff0c\u8bf7\u5148\u5b8c\u6210\u767b\u5f55",
                          loginProcesses.front(), lastEventAt);
      }
      if (WaitForSingleObject(gState.stopEvent, gState.config.processPollMs) == WAIT_OBJECT_0) break;
      continue;
    }
    appendLog("INFO", std::string(u8"\u5df2\u53d1\u73b0\u5fae\u4fe1\u76ee\u6807\u8fdb\u7a0b processId=") + std::to_string(processId));
    runHost(processId, &lastEventAt);
    if (!gState.stopping.load() && WaitForSingleObject(gState.stopEvent, gState.config.processPollMs) == WAIT_OBJECT_0) break;
  }
  if (gState.networkThread.joinable()) gState.networkThread.join();
  if (gState.loginThread.joinable()) gState.loginThread.join();
  appendLog("INFO", u8"\u539f\u751f\u76d1\u63a7\u7aef\u5df2\u505c\u6b62");
  CoUninitialize();
}

// 还原并前置主窗口，托盘菜单的“还原窗口”走这里。
void showWindow() {
  ShowWindow(gState.window, SW_RESTORE);
  SetForegroundWindow(gState.window);
}

// 在光标位置弹出托盘右键菜单；先设为前台窗口，否则菜单点别处不会自动关闭。
void showTrayMenu() {
  POINT point{};
  GetCursorPos(&point);
  HMENU menu = CreatePopupMenu();
  std::wstring status;
  {
    std::lock_guard<std::mutex> lock(gState.displayMutex);
    status = L"\u72b6\u6001: " + gState.displayStatus +
             (gState.networkOk ? L""
                               : L" / \u4e0a\u62a5\u5f02\u5e38" + (gState.networkDetail.empty()
                                                                        ? L""
                                                                        : L"\uff1a" + gState.networkDetail));
  }
  AppendMenuW(menu, MF_STRING | MF_GRAYED, kMenuStatus, status.c_str());
  AppendMenuW(menu, MF_STRING, kMenuRestore, L"\u663e\u793a\u7a97\u53e3");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, kMenuExit, L"\u9000\u51fa");
  SetForegroundWindow(gState.window);
  TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_LEFTALIGN, point.x, point.y, 0, gState.window, nullptr);
  DestroyMenu(menu);
}

struct ChildBounds {
  HWND parent = nullptr;
  int right = 0;
  int bottom = 0;
};

// EnumChildWindows 回调：累积所有子控件的包围盒。
BOOL CALLBACK collectChildBounds(HWND child, LPARAM parameter) {
  auto* bounds = reinterpret_cast<ChildBounds*>(parameter);
  RECT rectangle{};
  if (!GetWindowRect(child, &rectangle)) return TRUE;
  POINT points[2] = {{rectangle.left, rectangle.top}, {rectangle.right, rectangle.bottom}};
  MapWindowPoints(nullptr, bounds->parent, points, 2);
  bounds->right = std::max(bounds->right, static_cast<int>(points[1].x));
  bounds->bottom = std::max(bounds->bottom, static_cast<int>(points[1].y));
  return TRUE;
}

// 按子控件包围盒调整窗口大小，避免控件被挤出可视区域。
void fitWindowToChildren(HWND window) {
  ChildBounds bounds{window};
  EnumChildWindows(window, collectChildBounds, reinterpret_cast<LPARAM>(&bounds));
  const int clientWidth = std::max(758, bounds.right + 28);
  const int clientHeight = std::max(640, bounds.bottom + 24);
  RECT outer{0, 0, clientWidth, clientHeight};
  AdjustWindowRectEx(&outer, static_cast<DWORD>(GetWindowLongPtrW(window, GWL_STYLE)), FALSE,
                     static_cast<DWORD>(GetWindowLongPtrW(window, GWL_EXSTYLE)));
  SetWindowPos(window, nullptr, 0, 0, outer.right - outer.left, outer.bottom - outer.top,
               SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

// 主窗口消息循环：创建控件、刷新状态、处理托盘图标与菜单、处理自定义刷新/还原消息、退出时清理。
// 界面只有一个窗口，所有面板都在里面，不存在多窗口残留。
LRESULT CALLBACK windowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
  switch (message) {
    case WM_CREATE: {
      gState.window = window;
      gState.statusLabel = CreateWindowExW(0, L"STATIC", L"\u6b63\u5728\u542f\u52a8", WS_CHILD | WS_VISIBLE,
                                           28, 20, 702, 36, window, nullptr, gState.instance, nullptr);
      gState.detailLabel = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT,
                                           28, 56, 702, 28, window, nullptr, gState.instance, nullptr);
      gState.fieldTestLabel = CreateWindowExW(0, L"STATIC", L"\u73b0\u573a\u6d4b\u8bd5\uff1a\u672a\u5f00\u59cb",
                                              WS_CHILD | WS_VISIBLE | SS_LEFT,
                                              28, 90, 370, 26, window, nullptr, gState.instance, nullptr);
      HWND buttonLogin = CreateWindowExW(0, L"BUTTON", L"\u767b\u5f55\u6d4b\u8bd5", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                         410, 84, 96, 32, window, reinterpret_cast<HMENU>(kTestButtonLogin),
                                         gState.instance, nullptr);
      HWND qrLogin = CreateWindowExW(0, L"BUTTON", L"\u626b\u7801\u6d4b\u8bd5", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                     512, 84, 96, 32, window, reinterpret_cast<HMENU>(kTestQrLogin),
                                     gState.instance, nullptr);
      HWND payment = CreateWindowExW(0, L"BUTTON", L"\u6536\u6b3e\u6d4b\u8bd5", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                     614, 84, 96, 32, window, reinterpret_cast<HMENU>(kTestPayment),
                                     gState.instance, nullptr);
      CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_ETCHEDHORZ,
                      28, 122, 702, 2, window, nullptr, gState.instance, nullptr);
      SendMessageW(gState.statusLabel, WM_SETFONT, reinterpret_cast<WPARAM>(gState.titleFont), TRUE);
      SendMessageW(gState.detailLabel, WM_SETFONT, reinterpret_cast<WPARAM>(gState.font), TRUE);
      SendMessageW(gState.fieldTestLabel, WM_SETFONT, reinterpret_cast<WPARAM>(gState.font), TRUE);
      SendMessageW(buttonLogin, WM_SETFONT, reinterpret_cast<WPARAM>(gState.font), TRUE);
      SendMessageW(qrLogin, WM_SETFONT, reinterpret_cast<WPARAM>(gState.font), TRUE);
      SendMessageW(payment, WM_SETFONT, reinterpret_cast<WPARAM>(gState.font), TRUE);
      if (!InitializeWechatConfigPanel(window, gState.configPath, 108)) {
        updateDisplay("ERROR", "CONFIG_UI_FAILED", u8"\u65e0\u6cd5\u521d\u59cb\u5316\u914d\u7f6e\u754c\u9762");
      }
      fitWindowToChildren(window);
      PostMessageW(window, kConfigAutoValidateMessage, 0, 0);
      return 0;
    }
    case kConfigAutoValidateMessage:
      RunWechatConfigPanelAutoValidate();
      return 0;
    case WM_COMMAND:
      if (HandleWechatConfigPanelCommand(wParam, lParam)) return 0;
      if (LOWORD(wParam) == kTestButtonLogin || LOWORD(wParam) == kTestQrLogin ||
          LOWORD(wParam) == kTestPayment) {
        const FieldTestKind kind = LOWORD(wParam) == kTestButtonLogin
                                       ? FieldTestKind::ButtonLogin
                                       : (LOWORD(wParam) == kTestQrLogin ? FieldTestKind::QrLogin
                                                                         : FieldTestKind::Payment);
        if (!requestFieldTest(kind)) {
          MessageBoxW(window, L"\u5df2\u6709\u73b0\u573a\u6d4b\u8bd5\u6b63\u5728\u8fdb\u884c\uff0c\u8bf7\u7b49\u5f85\u5b8c\u6210\u3002",
                      L"CFPay", MB_OK | MB_ICONINFORMATION);
        }
      }
      else if (LOWORD(wParam) == kMenuRestore) showWindow();
      else if (LOWORD(wParam) == kMenuExit) DestroyWindow(window);
      return 0;
    case WM_CLOSE:
      ShowWindow(window, SW_HIDE);
      return 0;
    case kTrayMessage:
      if (LOWORD(lParam) == WM_RBUTTONUP || LOWORD(lParam) == WM_CONTEXTMENU) showTrayMenu();
      else if (LOWORD(lParam) == WM_LBUTTONDBLCLK) showWindow();
      return 0;
    case kRefreshMessage:
      refreshWindowAndTray();
      return 0;
    case kRestoreMessage:
      showWindow();
      return 0;
    case WM_CTLCOLORSTATIC:
      SetBkMode(reinterpret_cast<HDC>(wParam), TRANSPARENT);
      return reinterpret_cast<LRESULT>(gState.background);
    case WM_DESTROY:
      gState.stopping.store(true);
      if (gState.stopEvent != nullptr) SetEvent(gState.stopEvent);
      stopActiveHost();
      if (gState.monitorThread.joinable()) gState.monitorThread.join();
      Shell_NotifyIconW(NIM_DELETE, &gState.tray);
      DestroyWechatConfigPanel();
      if (gState.font != nullptr) DeleteObject(gState.font);
      if (gState.titleFont != nullptr) DeleteObject(gState.titleFont);
      if (gState.background != nullptr) DeleteObject(gState.background);
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcW(window, message, wParam, lParam);
}

struct CommandLineOptions {
  std::wstring configPath;
  bool tray = false;
  bool selfTest = false;
  bool check = false;
  bool fieldTestSequence = false;
};

// 解析命令行：配置文件路径与各种运行模式（自检、内嵌宿主、配置窗口、托盘、现场测试序列）。
CommandLineOptions parseCommandLine() {
  CommandLineOptions options;
  options.configPath = defaultConfigPath();
  const std::wstring packageDirectory = moduleDirectory();
  options.fieldTestSequence = packageDirectory.find(L"WeChatFieldTest-") != std::wstring::npos;
  int count = 0;
  LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &count);
  if (arguments != nullptr) {
    for (int index = 1; index < count; index += 1) {
      if (std::wcscmp(arguments[index], L"--config") == 0 && index + 1 < count) {
        options.configPath = std::filesystem::absolute(arguments[++index]).wstring();
      } else if (std::wcscmp(arguments[index], L"--tray") == 0) {
        options.tray = true;
      } else if (std::wcscmp(arguments[index], L"--self-test") == 0) {
        options.selfTest = true;
      } else if (std::wcscmp(arguments[index], L"--check") == 0) {
        options.check = true;
      } else if (std::wcscmp(arguments[index], L"--field-test-sequence") == 0) {
        options.fieldTestSequence = true;
      }
    }
    LocalFree(arguments);
  }
  return options;
}

// 自检入口：不接触微信，只验证金额解析、时间解析、记录判定、中文说明映射与翻译层等纯逻辑。
// 每个断言返回不同的非零码，构建脚本据此定位失败环节。
int runSelfTest() {
  Config config;
  config.expectedReceiverUsername = "wxid_receiver";
  const std::uint64_t now = nowMilliseconds();
  const json record = {{"type", "event"},
                       {"capturedAt", now},
                       {"fields", {{"paySubtype", 1},
                                   {"feeDescription", "CNY 0.01"},
                                   {"transactionId", "native-self-test"},
                                   {"beginTransferTime", now / 1000},
                                   {"memo", "self-test"},
                                   {"receiverUsername", "wxid_receiver"}}}};
  const Evaluation evaluation = evaluateRecord(record, config, now);
  if (!evaluation.accepted || evaluation.payment.amountFen != 1 || evaluation.payment.amount != "0.01" ||
      evaluation.payment.eventId.rfind("wechat:", 0) != 0) {
    return 10;
  }
  json wrong = record;
  wrong["fields"]["receiverUsername"] = "wxid_other";
  if (evaluateRecord(wrong, config, now).reason != "RECEIVER_MISMATCH") return 11;
  if (statusDescription("HOOK_ACTIVE").find(u8"\u5fae\u4fe1\u5df2\u767b\u5f55") == std::string::npos) return 12;
  if (statusMetrics("observer active; loginState=logged_in; receiptEntries=2; threads=3") !=
      u8"; \u767b\u5f55\u72b6\u6001=\u5df2\u767b\u5f55; \u6536\u6b3e\u52a9\u624b\u5165\u53e3\u547d\u4e2d\u6570=2; \u5df2\u8986\u76d6\u7ebf\u7a0b\u6570=3") {
    return 13;
  }
  if (uploadDiagnostic(false, 503, 12029) != u8"\u7ed3\u679c=\u5931\u8d25 httpStatus=503 winError=12029") return 14;
  if (loginStatusDescription("QR_READY") != u8"\u5df2\u5728\u672c\u673a\u89e3\u6790\u5fae\u4fe1\u767b\u5f55\u4e8c\u7ef4\u7801") return 15;
  if (loginDiagnosticDescription("LOGIN_UI_NOT_RECOGNIZED") !=
      u8"\u672a\u8bc6\u522b\u5fae\u4fe1\u767b\u5f55\u4e8c\u7ef4\u7801\u6216\u767b\u5f55\u6309\u94ae") {
    return 16;
  }
  if (rejectionReasonDescription("INVALID_AMOUNT") != u8"\u6536\u6b3e\u91d1\u989d\u7f3a\u5931\u6216\u683c\u5f0f\u65e0\u6548") return 17;
  if (rejectionReasonDescription("UNKNOWN_REASON") != u8"\u6536\u6b3e\u8bb0\u5f55\u672a\u901a\u8fc7\u5b89\u5168\u6821\u9a8c") return 18;
  if (statusDescription("VERSION_MISMATCH") != u8"\u5fae\u4fe1\u7248\u672c\u4e0e\u5f53\u524d\u76d1\u63a7\u7ec4\u4ef6\u4e0d\u5339\u914d") return 19;
  // 上报给后台的说明必须一律是中文：观察 DLL 的英文描述在监控端翻译，
  // 指标串沿用 statusMetrics 的中文标签，未知描述回退到诊断码说明。
  if (observerMessageDescription("PROCESS_NOT_FOUND", "Weixin.dll is not loaded in the target process") !=
      u8"\u76ee\u6807\u8fdb\u7a0b\u5c1a\u672a\u52a0\u8f7d Weixin.dll") {
    return 20;
  }
  if (observerMessageDescription("WEIXIN_LOGIN_CHECKING",
                                 "waiting for current Weixin account observation; loginState=unknown; threads=3") !=
      u8"\u6b63\u5728\u7b49\u5f85\u5f53\u524d\u5fae\u4fe1\u8d26\u53f7\u7684\u89c2\u5bdf\u7ed3\u679c; \u767b\u5f55\u72b6\u6001=\u672a\u77e5; \u5df2\u8986\u76d6\u7ebf\u7a0b\u6570=3") {
    return 21;
  }
  if (observerMessageDescription("HOOK_STOPPED", "hardware breakpoints cleared; DLL remains loaded") !=
      u8"\u786c\u4ef6\u65ad\u70b9\u5df2\u6e05\u9664\uff0cDLL \u4ecd\u4fdd\u6301\u52a0\u8f7d") {
    return 22;
  }
  if (observerMessageDescription("HOOK_ACTIVE", u8"\u5fae\u4fe1\u5df2\u767b\u5f55\uff0cHook \u6b63\u5e38\u8fd0\u884c") != u8"\u5fae\u4fe1\u5df2\u767b\u5f55\uff0cHook \u6b63\u5e38\u8fd0\u884c") {
    return 23;
  }
  if (observerMessageDescription("READ_FAILED", "an unmapped english detail") != statusDescription("READ_FAILED")) {
    return 24;
  }
  // 上报的目标版本与 Hook 偏移必须由 wechat_target.h 派生，格式与历史上报值保持一致，
  // 否则后台按字符串比对的展示与既有记录会对不上。
  if (weixinTargetLabel() != "Weixin 4.2.0.11") return 25;
  if (parserHookOffsetText() != "0x10b0230") return 26;
  // 合并上报的载荷必须同时带上组件状态与微信登录状态：两者共用一个请求，
  // 少一块后台就会出现「设备在线但没有登录状态」这类自相矛盾的记录。
  ReportSnapshot merged;
  merged.status = "HEALTHY";
  merged.detailCode = "HOOK_ACTIVE";
  merged.message = u8"\u5fae\u4fe1\u5df2\u767b\u5f55\uff0cHook \u6b63\u5e38\u8fd0\u884c";
  merged.processId = 4308;
  merged.lastEventAt = now;
  merged.loginStatus = "QR_READY";
  merged.loginProcessId = 4308;
  merged.qrPayload = "https://weixin.qq.com/x/self-test";
  const json mergedPayload = statusPayload(merged, now);
  if (mergedPayload.value("component", "") != "wechat_hook") return 27;
  if (!mergedPayload.contains("login") || mergedPayload["login"].value("status", "") != "QR_READY" ||
      mergedPayload["login"].value("qrPayload", "") != "https://weixin.qq.com/x/self-test") {
    return 28;
  }
  if (mergedPayload["login"].value("observedAt", static_cast<std::uint64_t>(0)) != now) return 29;
  // 非 QR_READY 的登录状态不得携带二维码内容，否则后台会继续显示已经过期的登录二维码。
  ReportSnapshot loggedIn;
  loggedIn.status = "HEALTHY";
  loggedIn.detailCode = "HOOK_ACTIVE";
  loggedIn.loginStatus = "LOGGED_IN";
  loggedIn.qrPayload = "https://weixin.qq.com/x/self-test";
  if (statusPayload(loggedIn, now)["login"].contains("qrPayload")) return 30;
  // 上报记账：内容没变化时不能一直标记为待上报，否则网络线程会认为永远有新状态，
  // 把 5 秒一次的重申变成每 250 毫秒一次的空转。
  {
    std::lock_guard<std::mutex> lock(gReportMutex);
    gReport = ReportSnapshot{};
  }
  setComponentState("HEALTHY", "HOOK_ACTIVE", u8"\u5fae\u4fe1\u5df2\u767b\u5f55\uff0cHook \u6b63\u5e38\u8fd0\u884c", 4308, 0);
  if (!gReport.dirty) return 31;
  {
    std::lock_guard<std::mutex> lock(gReportMutex);
    gReport.postedFingerprint = componentFingerprint(gReport);
    gReport.dirty = false;
  }
  setComponentState("HEALTHY", "HOOK_ACTIVE", u8"\u5fae\u4fe1\u5df2\u767b\u5f55\uff0cHook \u6b63\u5e38\u8fd0\u884c", 4308, 0);
  if (gReport.dirty) return 32;
  setComponentState("DEGRADED", "WEIXIN_LOGIN_REQUIRED", u8"\u5fae\u4fe1\u5c1a\u672a\u767b\u5f55\uff0c\u8bf7\u5148\u5b8c\u6210\u767b\u5f55", 4308, 0);
  if (!gReport.dirty) return 33;
  // 日志脱敏：登录二维码是可直接登录账号的凭据、收款原文属于消息正文，两者都不能明文落盘；
  // 其余字段必须原样保留，否则服务端因为哪个字段拒绝请求就又看不出来了。
  const json sensitive{{"component", "wechat_hook"},
                       {"login", {{"status", "QR_READY"}, {"qrPayload", "https://weixin.qq.com/x/self-test-secret"}}},
                       {"raw", u8"\u5fae\u4fe1\u652f\u4ed8\u6536\u6b3e0.01\u5143"}};
  const json redacted = json::parse(redactForLog(sensitive.dump()), nullptr, false);
  if (redacted.is_discarded() || !redacted.is_object()) return 34;
  if (redacted.value("component", "") != "wechat_hook") return 35;
  if (redacted["login"].value("qrPayload", "").find("self-test-secret") != std::string::npos) return 36;
  if (redacted.value("raw", "").find("0.01") != std::string::npos) return 37;
  if (redacted.value("raw", "").find("sha256=") == std::string::npos) return 38;
  return 0;
}

}  // namespace

// GUI 入口：解析命令行后按模式分派到自检、内嵌宿主、配置窗口或常驻监控。
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
  int argumentCount = 0;
  LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
  if (arguments != nullptr && argumentCount >= 2 && std::wcscmp(arguments[1], L"--internal-host") == 0) {
    const int result = RunWechatHostCommand(argumentCount - 1, arguments + 1);
    LocalFree(arguments);
    return result;
  }
  if (arguments != nullptr && argumentCount == 4 && std::wcscmp(arguments[1], L"--self-test-login-ui") == 0) {
    const DWORD processId = static_cast<DWORD>(std::wcstoul(arguments[3], nullptr, 10));
    const int result = RunWechatLoginAssistantSelfTest(arguments[2], processId);
    LocalFree(arguments);
    return result;
  }
  if (arguments != nullptr && argumentCount == 3 && std::wcscmp(arguments[1], L"--self-test-login-restart") == 0) {
    DWORD restartedProcessId = 0;
    DWORD errorCode = 0;
    const bool restarted = RestartExactWeixinExecutable(arguments[2], &restartedProcessId, &errorCode);
    int result = restarted ? 0 : 24;
    if (restarted) {
      Sleep(500);
      const WechatLoginUiResult loginUi = InspectWechatLoginUi(restartedProcessId, false);
      if (loginUi.kind != WechatLoginUiKind::LoginRequired) result = 25;
    }
    LocalFree(arguments);
    return result;
  }
  if (arguments != nullptr) LocalFree(arguments);
  const CommandLineOptions options = parseCommandLine();
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  if (options.selfTest) {
    const int result = runSelfTest();
    CoUninitialize();
    return result;
  }
  if (options.check) {
    Config config;
    std::string error;
    const int result = loadConfig(options.configPath, &config, &error) ? 0 : 12;
    CoUninitialize();
    return result;
  }

  gState.singleton = CreateMutexW(nullptr, FALSE, kMutexName);
  if (gState.singleton == nullptr) {
    CoUninitialize();
    return 1;
  }
  if (GetLastError() == ERROR_ALREADY_EXISTS) {
    HWND existing = FindWindowW(kWindowClass, nullptr);
    if (existing != nullptr) PostMessageW(existing, kRestoreMessage, 0, 0);
    CloseHandle(gState.singleton);
    CoUninitialize();
    return 0;
  }

  SetProcessDPIAware();
  INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES};
  InitCommonControlsEx(&controls);
  gState.instance = instance;
  gState.packageDirectory = moduleDirectory();
  // 单 EXE 分发：任何运行模式（含自检与内部宿主）启动时都先尽力释放内嵌观察 DLL，
  // 正式的错误报告仍在配置加载之后进行。
  {
    std::string earlyDetail;
    materializeObserverDll(joinPath(gState.packageDirectory, L"cfpay-hook-observer.dll"), &earlyDetail);
  }
  gState.configPath = options.configPath;
  gState.fieldTestSequence = options.fieldTestSequence;
  gState.stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  gState.background = CreateSolidBrush(RGB(246, 247, 249));
  gState.font = CreateFontW(-18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                           CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
  gState.titleFont = CreateFontW(-26, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");

  WNDCLASSEXW windowClass{};
  windowClass.cbSize = sizeof(windowClass);
  windowClass.lpfnWndProc = windowProcedure;
  windowClass.hInstance = instance;
  windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  windowClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
  windowClass.hbrBackground = gState.background;
  windowClass.lpszClassName = kWindowClass;
  if (!RegisterClassExW(&windowClass)) return 2;

  HWND window = CreateWindowExW(0, kWindowClass, kWindowTitle,
                                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                                CW_USEDEFAULT, CW_USEDEFAULT, 780, 680, nullptr, nullptr, instance, nullptr);
  if (window == nullptr) return 3;

  gState.tray.cbSize = sizeof(gState.tray);
  gState.tray.hWnd = window;
  gState.tray.uID = kTrayId;
  gState.tray.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
  gState.tray.uCallbackMessage = kTrayMessage;
  gState.tray.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
  wcsncpy_s(gState.tray.szTip, L"CFPay \u5fae\u4fe1\u76d1\u63a7: \u6b63\u5728\u542f\u52a8", _TRUNCATE);
  Shell_NotifyIconW(NIM_ADD, &gState.tray);
  gState.tray.uVersion = NOTIFYICON_VERSION_4;
  Shell_NotifyIconW(NIM_SETVERSION, &gState.tray);

  if (!options.tray) {
    ShowWindow(window, showCommand == 0 ? SW_SHOWNORMAL : showCommand);
    UpdateWindow(window);
  }
  if (gState.fieldTestSequence) {
    updateFieldTestStatus(L"\u73b0\u573a\u6d4b\u8bd5\uff1a\u81ea\u52a8\u5e8f\u5217\u6b63\u5728\u51c6\u5907\u6309\u94ae\u767b\u5f55\u6d4b\u8bd5");
    gState.fieldTestRequest = static_cast<int>(FieldTestKind::ButtonLogin);
  }
  gState.monitorThread = std::thread(monitorLoop);

  MSG message{};
  while (GetMessageW(&message, nullptr, 0, 0) > 0) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
  if (gState.stopEvent != nullptr) CloseHandle(gState.stopEvent);
  if (gState.singleton != nullptr) CloseHandle(gState.singleton);
  CoUninitialize();
  return static_cast<int>(message.wParam);
}
