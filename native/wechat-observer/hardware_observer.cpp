#include <windows.h>
#include <tlhelp32.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "wechat_target.h"

namespace {

constexpr std::size_t kMaxThreadStates = 512;
constexpr std::size_t kQueueSize = 64;
constexpr std::size_t kUsernameSize = 192;
constexpr std::size_t kNicknameSize = 384;
constexpr std::size_t kFeeDescriptionSize = 128;
constexpr std::size_t kTransactionIdSize = 256;
constexpr std::size_t kMemoSize = 512;
constexpr std::size_t kDiagnosticEventNameSize = 128;
constexpr std::uint64_t kMaxStringBytes = 64 * 1024;
constexpr std::uint64_t kLoginDetectionGraceMs = 15'000;
constexpr LONG kLoggedOutConfirmationCount = 3;
constexpr DWORD64 kResumeFlag = 0x10000;
constexpr DWORD64 kSlot0EnableMask = 0x3;
constexpr DWORD64 kSlot1EnableMask = 0xc;
constexpr DWORD64 kSlot2EnableMask = 0x30;
constexpr DWORD64 kSlot3EnableMask = 0xc0;
constexpr DWORD64 kSlot0ControlMask = static_cast<DWORD64>(0xf) << 16;
constexpr DWORD64 kSlot1ControlMask = static_cast<DWORD64>(0xf) << 20;
constexpr DWORD64 kSlot2ControlMask = static_cast<DWORD64>(0xf) << 24;
constexpr DWORD64 kSlot3ControlMask = static_cast<DWORD64>(0xf) << 28;
constexpr std::uint32_t kWeixinNewSyncParseRva = cfpay::target::kNewSyncParseRva;
constexpr std::uint32_t kWeixinNewSyncResponseVtableRva = cfpay::target::kNewSyncResponseVtableRva;
constexpr std::uint32_t kWeixinCmdListVtableRva = cfpay::target::kCmdListVtableRva;
constexpr std::uint32_t kWeixinCmdItemVtableRva = cfpay::target::kCmdItemVtableRva;
constexpr std::int32_t kWeixinAddMsgCommandId = 5;
constexpr std::int32_t kWeixinSysMsgType = 10002;
constexpr std::size_t kPayContentSize = 2048;
constexpr std::uint32_t kWeixinAccountRva = cfpay::target::kAccountRva;
constexpr std::uint32_t kWeixinAccountCallsiteRva = cfpay::target::kAccountCallsiteRva;
constexpr std::uint32_t kWeixinNicknameRva = cfpay::target::kNicknameRva;
#ifdef CFPAY_DIAGNOSTIC_BUILD
#ifdef CFPAY_WMPF_DIAGNOSTIC_BUILD
constexpr std::array<std::uint32_t, 4> kDiagnosticRvas = {0x2d179f0, 0x2d22100, 0x2d4bac0, 0x2d5a400};
constexpr std::array<std::array<std::uint8_t, 32>, 4> kDiagnosticSignatures = {{
    {0x41, 0x56, 0x56, 0x57, 0x53, 0x48, 0x81, 0xec, 0x78, 0x01, 0x00, 0x00, 0x4c, 0x89, 0xce, 0x4c,
     0x89, 0xc3, 0x48, 0x89, 0xd7, 0x4c, 0x8b, 0xb4, 0x24, 0xc0, 0x01, 0x00, 0x00, 0xe8, 0x4e, 0xf6},
    {0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x56, 0x57, 0x55, 0x53, 0x48, 0x81, 0xec, 0xa8,
     0x01, 0x00, 0x00, 0x4d, 0x89, 0xc6, 0x48, 0x89, 0xd7, 0x4c, 0x8b, 0xa9, 0x98, 0x00, 0x00, 0x00},
    {0x41, 0x57, 0x41, 0x56, 0x56, 0x57, 0x53, 0x48, 0x81, 0xec, 0x90, 0x01, 0x00, 0x00, 0x4c, 0x89,
     0xc6, 0x48, 0x89, 0xd7, 0x48, 0x89, 0xcb, 0x83, 0x3d, 0xb2, 0x6f, 0xf3, 0x0a, 0x02, 0x0f, 0x8f},
    {0x41, 0x57, 0x41, 0x56, 0x56, 0x57, 0x53, 0x48, 0x81, 0xec, 0x70, 0x01, 0x00, 0x00, 0x4c, 0x89,
     0xce, 0x4c, 0x89, 0xc7, 0x48, 0x8b, 0x41, 0x18, 0x48, 0x8b, 0x80, 0x40, 0x06, 0x00, 0x00, 0x48},
}};
#else
constexpr std::array<std::uint32_t, 4> kDiagnosticRvas = {0x1c43ef0, 0x1c3dd7f, 0x48f9667, 0x4900fc4};
constexpr std::array<std::array<std::uint8_t, 32>, 4> kDiagnosticSignatures = {{
    {0x55, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x56, 0x57, 0x53, 0x48, 0x81, 0xec, 0x48,
     0x01, 0x00, 0x00, 0x48, 0x8d, 0xac, 0x24, 0x80, 0x00, 0x00, 0x00, 0x48, 0xc7, 0x85, 0xc0, 0x00},
    {0xe8, 0x6c, 0x61, 0x00, 0x00, 0x90, 0xe8, 0x86, 0x7f, 0x40, 0xfe, 0x90, 0x4c, 0x8b, 0x00, 0x48,
     0x8d, 0x95, 0xb8, 0x03, 0x00, 0x00, 0x48, 0x89, 0xc1, 0x41, 0xff, 0x50, 0x60, 0x90, 0x48, 0x8b},
    {0xe8, 0x84, 0xa8, 0x34, 0xfd, 0x90, 0x48, 0x8b, 0x85, 0x70, 0x02, 0x00, 0x00, 0x48, 0x8b, 0x88,
     0xf8, 0x00, 0x00, 0x00, 0x48, 0xc7, 0x85, 0x98, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x48},
    {0xe8, 0x27, 0x2f, 0x34, 0xfd, 0x90, 0x49, 0x8b, 0x3e, 0xe8, 0x4e, 0xe6, 0x95, 0xfb, 0x90, 0x48,
     0x89, 0xf9, 0x48, 0x89, 0xc2, 0xe8, 0xf2, 0x6d, 0x34, 0xfd, 0x90, 0xe8, 0xdc, 0x3f, 0x0b, 0xfc},
}};
#endif
#endif
constexpr std::array<std::uint8_t, 32> kWeixinNewSyncParseSignature = {
    0x55, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x56, 0x57, 0x53, 0x48, 0x83, 0xec, 0x48,
    0x48, 0x8d, 0x6c, 0x24, 0x40, 0x48, 0xc7, 0x45, 0x00, 0xfe, 0xff, 0xff, 0xff, 0x48, 0x89, 0xd6,
};
constexpr std::array<std::uint8_t, 32> kWeixinAccountSignature = {
    0x55, 0x56, 0x57, 0x53, 0x48, 0x81, 0xec, 0xd8, 0x00, 0x00, 0x00, 0x48, 0x8d, 0xac, 0x24, 0x80,
    0x00, 0x00, 0x00, 0x48, 0xc7, 0x45, 0x50, 0xfe, 0xff, 0xff, 0xff, 0x48, 0x89, 0xd6, 0x48, 0x89,
};
constexpr std::array<std::uint8_t, 5> kWeixinAccountCallsiteSignature = {0xe8, 0x2d, 0x23, 0x4f, 0xfe};
constexpr std::array<std::uint8_t, 32> kWeixinNicknameSignature = {
    0x55, 0x41, 0x57, 0x41, 0x56, 0x41, 0x54, 0x56, 0x57, 0x53, 0x48, 0x83, 0xec, 0x50, 0x48, 0x8d,
    0x6c, 0x24, 0x50, 0x48, 0xc7, 0x45, 0xf8, 0xfe, 0xff, 0xff, 0xff, 0x48, 0x83, 0x7a, 0x38, 0x00,
};

struct ThreadState {
  volatile LONG threadId;
  std::uintptr_t outputAddress;
  std::uintptr_t returnAddress;
  volatile LONG returnArmed;
  std::uintptr_t accountOutputAddress;
  std::uintptr_t accountReturnAddress;
  volatile LONG accountReturnArmed;
  std::uintptr_t nicknameOutputAddress;
  std::uintptr_t nicknameReturnAddress;
  volatile LONG nicknameReturnArmed;
};

enum class EventKind : std::uint8_t {
  Payment,
  Account,
  Nickname,
  DiagnosticEventName,
  SlotDump,
  PayMessage,
};

struct EventRecord {
  EventKind kind;
  bool valid;
  std::int32_t paySubtype;
  std::int32_t beginTransferTime;
  std::uint64_t capturedAt;
  std::uint64_t accountUin;
  bool accountLoggedIn;
  bool loginStateChanged;
  char accountUsername[kUsernameSize];
  char accountNickname[kNicknameSize];
  char feeDescription[kFeeDescriptionSize];
  char transactionId[kTransactionIdSize];
  char memo[kMemoSize];
  char receiverUsername[kUsernameSize];
  char diagnosticEventName[kDiagnosticEventNameSize];
  char eventPath[16];
  char summaryShape[128];
  std::int32_t payMsgType;
  std::int32_t payCreateTime;
  std::uint64_t payServerMsgId;
  char payContent[kPayContentSize];
  char payFromUsername[96];
  char payToUsername[96];
  char error[128];
};

struct QueueSlot {
  volatile LONG state;
  EventRecord event;
};

PVOID g_veh = nullptr;
HANDLE g_outputFile = INVALID_HANDLE_VALUE;
HANDLE g_queueEvent = nullptr;
HANDLE g_workerThread = nullptr;
void* g_hookAddress = nullptr;
std::uintptr_t g_weixinModuleBase = 0;
void* g_accountHookAddress = nullptr;
void* g_nicknameHookAddress = nullptr;
#ifdef CFPAY_DIAGNOSTIC_BUILD
void* g_diagnosticFourthHookAddress = nullptr;
#endif
volatile LONG g_running = 0;
constexpr LONG kLifecycleStopped = 0;
constexpr LONG kLifecycleStarting = 1;
constexpr LONG kLifecycleRunning = 2;
constexpr LONG kLifecycleStopping = 3;
volatile LONG g_lifecycle = kLifecycleStopped;
volatile LONG g_stopResult = FALSE;
volatile LONG g_outputBroken = 0;
volatile LONG64 g_lastAccountFingerprint = 0;
volatile LONG64 g_lastLoggedInObservationAt = 0;
volatile LONG64 g_observerStartedAt = 0;
volatile LONG g_loginState = -1;
volatile LONG g_consecutiveLoggedOutObservations = 0;
volatile LONG64 g_newSyncEntryCount = 0;
volatile LONG64 g_newSyncReturnCount = 0;
volatile LONG64 g_addMsgCommandCount = 0;
volatile LONG64 g_accountEntryCount = 0;
volatile LONG64 g_accountReturnCount = 0;
volatile LONG64 g_nicknameEntryCount = 0;
volatile LONG64 g_nicknameReturnCount = 0;
volatile LONG64 g_payMessageScanCount = 0;
volatile LONG64 g_payMessageCount = 0;
volatile LONG64 g_readFailureCount = 0;
volatile LONG64 g_queueDropCount = 0;
#if defined(CFPAY_DIAGNOSTIC_BUILD) && !defined(CFPAY_WMPF_DIAGNOSTIC_BUILD)
constexpr std::array<std::uintptr_t, 15> kDiagnosticTextOffsets = {
    0x000, 0x048, 0x068, 0x0a8, 0x0c8, 0x100, 0x120, 0x140,
    0x160, 0x198, 0x1b8, 0x208, 0x228, 0x248, 0x270,
};
constexpr std::array<const char*, kDiagnosticTextOffsets.size()> kDiagnosticTextLabels = {
    "f000", "f048", "f068", "f0a8", "f0c8", "f100", "f120", "f140",
    "f160", "f198", "f1b8", "f208", "f228", "f248", "f270",
};
constexpr std::size_t kDiagnosticTextSlotCount = kDiagnosticTextOffsets.size();
constexpr std::size_t kDiagnosticRichRunSize = 0x28;
constexpr std::size_t kMaxDiagnosticRichRuns = 64;
constexpr std::array<std::uint32_t, 6> kDiagnosticCallerReturnRvas = {
    0x1c3dd84, 0x1c43924, 0x48f966c, 0x48fa6f6, 0x48fb566, 0x4900fc9,
};
constexpr std::array<const char*, kDiagnosticCallerReturnRvas.size() + 1> kDiagnosticCallerLabels = {
    "c1c3dd84", "c1c43924", "c48f966c", "c48fa6f6", "c48fb566", "c4900fc9", "cOther",
};
constexpr std::size_t kDiagnosticCallerBucketCount = kDiagnosticCallerLabels.size();
constexpr std::size_t kMaxDiagnosticTrackedViewModels = 8;
constexpr LONG64 kMaxSlotDumpRecords = 20000;
constexpr std::uintptr_t kDiagnosticEmbeddedModelOffset = 0x160;
constexpr std::uintptr_t kDiagnosticSummaryOffset = 0x48;

struct DiagnosticTrackedViewModel {
  volatile LONG state;
  std::uintptr_t address;
  volatile LONG64 lastHash;
  volatile LONG consecutiveReadFailures;
};

volatile LONG64 g_diagnosticTextReadable[kDiagnosticTextSlotCount] = {};
volatile LONG64 g_diagnosticTextBytes[kDiagnosticTextSlotCount] = {};
volatile LONG64 g_diagnosticReceiptKeywords[kDiagnosticTextSlotCount] = {};
volatile LONG64 g_diagnosticPaymentKeywords[kDiagnosticTextSlotCount] = {};
volatile LONG64 g_diagnosticAmountMarkers[kDiagnosticTextSlotCount] = {};
volatile LONG64 g_diagnosticDecimalAmounts[kDiagnosticTextSlotCount] = {};
volatile LONG64 g_diagnosticRichRunsReadable = 0;
volatile LONG64 g_diagnosticRichRunCount = 0;
volatile LONG64 g_diagnosticRichRunBytes = 0;
volatile LONG64 g_diagnosticRichRunReceiptKeywords = 0;
volatile LONG64 g_diagnosticRichRunPaymentKeywords = 0;
volatile LONG64 g_diagnosticRichRunAmountMarkers = 0;
volatile LONG64 g_diagnosticRichRunDecimalAmounts = 0;
volatile LONG64 g_diagnosticServiceSummaryReadable = 0;
volatile LONG64 g_diagnosticServiceSummaryBytes = 0;
volatile LONG64 g_diagnosticServiceSummaryReceiptKeywords = 0;
volatile LONG64 g_diagnosticServiceSummaryPaymentKeywords = 0;
volatile LONG64 g_diagnosticServiceSummaryAmountMarkers = 0;
volatile LONG64 g_diagnosticServiceSummaryDecimalAmounts = 0;
volatile LONG64 g_diagnosticCallerHits[kDiagnosticCallerBucketCount] = {};
volatile LONG64 g_diagnosticCallerBytes[kDiagnosticCallerBucketCount] = {};
volatile LONG64 g_diagnosticCallerReceiptKeywords[kDiagnosticCallerBucketCount] = {};
volatile LONG64 g_diagnosticCallerPaymentKeywords[kDiagnosticCallerBucketCount] = {};
volatile LONG64 g_diagnosticCallerAmountMarkers[kDiagnosticCallerBucketCount] = {};
volatile LONG64 g_diagnosticCallerDecimalAmounts[kDiagnosticCallerBucketCount] = {};
volatile LONG64 g_diagnosticTrackedRegistrations = 0;
volatile LONG64 g_diagnosticTrackedObservations = 0;
volatile LONG64 g_diagnosticTrackedChanges = 0;
volatile LONG64 g_diagnosticTrackedQualifiedChanges = 0;
volatile LONG64 g_diagnosticTrackedReadFailures = 0;
volatile LONG64 g_diagnosticTrackedBytes = 0;
volatile LONG64 g_diagnosticHiddenPreviewCount = 0;
volatile LONG64 g_diagnosticSlotDumpCount = 0;
volatile LONG64 g_diagnosticSlotDumpDrops = 0;
DiagnosticTrackedViewModel g_diagnosticTrackedViewModels[kMaxDiagnosticTrackedViewModels] = {};
#endif
bool g_testMode = false;
bool g_pipeOutput = false;
ThreadState g_threads[kMaxThreadStates] = {};
QueueSlot g_queue[kQueueSize] = {};

// 把记录放进固定大小的环形队列；队列满时丢弃最旧的一条，绝不阻塞微信线程。
bool enqueueEvent(const EventRecord& event);
// 把一条记录标记为无效并写入错误码；无效记录不会进入上报队列。
void setError(EventRecord* event, const char* error);

#ifndef CFPAY_DIAGNOSTIC_BUILD
constexpr std::size_t kMaxTrackedSessionViewModels = 16;
constexpr std::uintptr_t kSessionSummarySourceOffset = 0x48;
constexpr std::uintptr_t kSessionSummaryViewModelOffset = 0x1a8;
constexpr LONG kTrackedSessionReadFailureLimit = 3;
constexpr std::uint64_t kSessionSummaryStartupGraceMs = 30'000;
constexpr std::uint64_t kSessionSummaryDuplicateWindowMs = 8'000;
constexpr std::uint64_t kSessionSummaryStableTextDuplicateWindowMs = 120'000;

struct TrackedSessionViewModel {
  volatile LONG state;
  std::uintptr_t address;
  volatile LONG64 lastHash;
  volatile LONG consecutiveReadFailures;
};

TrackedSessionViewModel g_trackedSessionViewModels[kMaxTrackedSessionViewModels] = {};
#endif

// 取 Unix 纪元毫秒；DLL 运行在微信进程内，所有时间戳都由它自己取，不依赖宿主传参。
std::uint64_t unixTimeMilliseconds() {
  FILETIME fileTime{};
  GetSystemTimeAsFileTime(&fileTime);
  ULARGE_INTEGER value{};
  value.LowPart = fileTime.dwLowDateTime;
  value.HighPart = fileTime.dwHighDateTime;
  return (value.QuadPart - 116444736000000000ULL) / 10000ULL;
}

// 向输出句柄写满指定字节数，短写会继续补写；写不完视为管道已断开。
bool writeBytes(const char* data, DWORD size) {
  DWORD total = 0;
  while (total < size) {
    DWORD written = 0;
    if (!WriteFile(g_outputFile, data + total, size - total, &written, nullptr) || written == 0) return false;
    total += written;
  }
  return true;
}

// 写一行并立即刷新；逐行落盘保证宿主崩溃时已产出的记录不丢。
void writeLine(const std::string& line) {
  if (g_outputFile == INVALID_HANDLE_VALUE) return;
  static constexpr char newline[] = "\r\n";
  const bool success = writeBytes(line.data(), static_cast<DWORD>(line.size())) && writeBytes(newline, 2);
  if (!g_pipeOutput && success) FlushFileBuffers(g_outputFile);
  if (!success && g_pipeOutput &&
      InterlockedCompareExchange(&g_lifecycle, kLifecycleRunning, kLifecycleRunning) == kLifecycleRunning) {
    InterlockedExchange(&g_outputBroken, 1);
  }
}

// 转义 JSON 字符串；这里手写而不用 JSON 库，因为 DLL 要尽量小且不引入额外依赖。
std::string jsonEscape(const char* value) {
  std::string output;
  for (const unsigned char character : std::string(value)) {
    switch (character) {
      case '\\':
        output += "\\\\";
        break;
      case '"':
        output += "\\\"";
        break;
      case '\b':
        output += "\\b";
        break;
      case '\f':
        output += "\\f";
        break;
      case '\n':
        output += "\\n";
        break;
      case '\r':
        output += "\\r";
        break;
      case '\t':
        output += "\\t";
        break;
      default:
        if (character < 0x20) {
          char escaped[7] = {};
          std::snprintf(escaped, sizeof(escaped), "\\u%04x", character);
          output += escaped;
        } else {
          output.push_back(static_cast<char>(character));
        }
    }
  }
  return output;
}

// 输出一行 status 记录（状态、诊断码、说明、观察时间），宿主转发给监控端后成为后台的组件状态。
void writeStatus(const char* status, const char* detailCode, const char* message) {
  writeLine(
      "{\"type\":\"status\",\"status\":\"" + std::string(status) + "\",\"detailCode\":\"" +
      std::string(detailCode) + "\",\"message\":\"" + jsonEscape(message) + "\",\"observedAt\":" +
      std::to_string(unixTimeMilliseconds()) + "}");
}

// 把地址/数值格式化成十六进制字符串，仅用于诊断输出。
std::string hexValue(std::uint64_t value) {
  char output[17] = {};
  std::snprintf(output, sizeof(output), "%016llx", static_cast<unsigned long long>(value));
  return output;
}

// 在自身进程内读取内存并要求读满；断点命中时上下文里的对象可能已被释放，读不满就放弃该条记录。
bool readMemory(const void* address, void* output, std::size_t size) {
  SIZE_T bytesRead = 0;
  return address != nullptr &&
         ReadProcessMemory(GetCurrentProcess(), address, output, size, &bytesRead) != FALSE && bytesRead == size;
}

// 读取一个 32 位整数，读失败返回 false。
bool readInt32(std::uintptr_t address, std::int32_t* output) {
  return readMemory(reinterpret_cast<const void*>(address), output, sizeof(*output));
}

// 连接宿主创建的命名管道，带重试；连不上说明宿主已提前退出，此时不能安装断点。
bool connectNamedPipe(const wchar_t* pipeName) {
  for (int attempt = 0; attempt < 100; attempt += 1) {
    g_outputFile = CreateFileW(pipeName, GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (g_outputFile != INVALID_HANDLE_VALUE) {
      g_pipeOutput = true;
      return true;
    }
    if (GetLastError() != ERROR_PIPE_BUSY || !WaitNamedPipeW(pipeName, 100)) Sleep(50);
  }
  return false;
}

#ifndef CFPAY_DIAGNOSTIC_BUILD
// 注入后在进程内存里二次校验四个 Hook 目标处的签名字节。
// 磁盘文件校验通过不代表内存里就是这个版本（可能被热更新或加壳），
// 校验不过一律不安装断点，避免把断点打在错误地址上导致微信崩溃。
bool validateWeixinMemory(HMODULE module, std::string* failureMessage) {
  const auto base = reinterpret_cast<std::uintptr_t>(module);
  std::array<std::uint8_t, 32> newSyncParseBytes{};
  std::array<std::uint8_t, 32> accountBytes{};
  std::array<std::uint8_t, 5> accountCallsiteBytes{};
  std::array<std::uint8_t, 32> nicknameBytes{};
  if (!readMemory(reinterpret_cast<const void*>(base + kWeixinNewSyncParseRva), newSyncParseBytes.data(), newSyncParseBytes.size())) {
    *failureMessage = "无法读取 NewSync 解析入口内存";
    return false;
  }
  if (newSyncParseBytes != kWeixinNewSyncParseSignature) {
    *failureMessage = "NewSync 解析入口签名不匹配";
    return false;
  }
  if (!readMemory(reinterpret_cast<const void*>(base + kWeixinAccountRva), accountBytes.data(), accountBytes.size())) {
    *failureMessage = "无法读取微信账号函数内存";
    return false;
  }
  if (accountBytes != kWeixinAccountSignature) {
    *failureMessage = "微信账号函数签名不匹配";
    return false;
  }
  if (!readMemory(reinterpret_cast<const void*>(base + kWeixinAccountCallsiteRva), accountCallsiteBytes.data(),
                  accountCallsiteBytes.size())) {
    *failureMessage = "无法读取微信账号调用点内存";
    return false;
  }
  if (accountCallsiteBytes != kWeixinAccountCallsiteSignature) {
    *failureMessage = "微信账号调用点签名不匹配";
    return false;
  }
  if (!readMemory(reinterpret_cast<const void*>(base + kWeixinNicknameRva), nicknameBytes.data(), nicknameBytes.size())) {
    *failureMessage = "无法读取微信昵称函数内存";
    return false;
  }
  if (nicknameBytes != kWeixinNicknameSignature) {
    *failureMessage = "微信昵称函数签名不匹配";
    return false;
  }

  std::int32_t accountRelativeTarget = 0;
  std::memcpy(&accountRelativeTarget, accountCallsiteBytes.data() + 1, sizeof(accountRelativeTarget));
  if (static_cast<std::uint32_t>(kWeixinAccountCallsiteRva + accountCallsiteBytes.size() + accountRelativeTarget) !=
      kWeixinAccountRva) {
    *failureMessage = "微信账号调用点目标地址不匹配";
    return false;
  }
  return true;
}
#endif

#ifdef CFPAY_DIAGNOSTIC_BUILD
// 诊断构建的内存签名校验，只用于研究链路。
bool validateDiagnosticMemory(HMODULE module) {
  const auto base = reinterpret_cast<std::uintptr_t>(module);
  for (std::size_t index = 0; index < kDiagnosticRvas.size(); index += 1) {
    std::array<std::uint8_t, 32> bytes{};
    if (!readMemory(reinterpret_cast<const void*>(base + kDiagnosticRvas[index]), bytes.data(), bytes.size()) ||
        bytes != kDiagnosticSignatures[index]) {
      return false;
    }
  }
  return true;
}
#endif

// 确定输出方式与 Hook 目标：优先用宿主给的命名管道，否则回退到输出文件；
// 同时解析 Weixin.dll 基址并校验内存签名，再算出四个断点的绝对地址。
bool configureOutputAndTarget() {
  wchar_t value[32768] = {};
  if (GetEnvironmentVariableW(L"CFPAY_HOOK_TEST_OUTPUT", value, 32768) > 0) {
    g_testMode = true;
    g_outputFile = CreateFileW(value, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    HMODULE mainModule = GetModuleHandleW(nullptr);
    g_hookAddress = reinterpret_cast<void*>(GetProcAddress(mainModule, "CfpayTestParser"));
    g_accountHookAddress = reinterpret_cast<void*>(GetProcAddress(mainModule, "CfpayTestAccount"));
    g_nicknameHookAddress = reinterpret_cast<void*>(GetProcAddress(mainModule, "CfpayTestNickname"));
    return g_outputFile != INVALID_HANDLE_VALUE && g_hookAddress != nullptr && g_accountHookAddress != nullptr &&
           g_nicknameHookAddress != nullptr;
  }

  if (GetEnvironmentVariableW(L"CFPAY_HOOK_TEST_PIPE", value, 32768) > 0) {
    g_testMode = true;
    if (!connectNamedPipe(value)) return false;
    HMODULE mainModule = GetModuleHandleW(nullptr);
    g_hookAddress = reinterpret_cast<void*>(GetProcAddress(mainModule, "CfpayTestParser"));
    g_accountHookAddress = reinterpret_cast<void*>(GetProcAddress(mainModule, "CfpayTestAccount"));
    g_nicknameHookAddress = reinterpret_cast<void*>(GetProcAddress(mainModule, "CfpayTestNickname"));
    return g_hookAddress != nullptr && g_accountHookAddress != nullptr && g_nicknameHookAddress != nullptr;
  }

  ::swprintf(value, L"\\\\.\\pipe\\cfpay-wechat-observer-%lu", GetCurrentProcessId());
  if (!connectNamedPipe(value)) return false;

#ifdef CFPAY_DIAGNOSTIC_BUILD
#ifdef CFPAY_WMPF_DIAGNOSTIC_BUILD
  HMODULE targetModule = GetModuleHandleW(L"flue.dll");
  const char* missingModuleMessage = "flue.dll is not loaded in the target process";
#else
  HMODULE targetModule = GetModuleHandleW(L"Weixin.dll");
  const char* missingModuleMessage = "Weixin.dll is not loaded in the target process";
#endif
  if (targetModule == nullptr) {
    writeStatus("STOPPED", "PROCESS_NOT_FOUND", missingModuleMessage);
    return false;
  }
  if (!validateDiagnosticMemory(targetModule)) {
    writeStatus("UNSUPPORTED", "SIGNATURE_MISMATCH", "diagnostic candidate memory signature does not match");
    return false;
  }
  const auto base = reinterpret_cast<std::uintptr_t>(targetModule);
  g_hookAddress = reinterpret_cast<void*>(base + kDiagnosticRvas[0]);
  g_accountHookAddress = reinterpret_cast<void*>(base + kDiagnosticRvas[1]);
  g_nicknameHookAddress = reinterpret_cast<void*>(base + kDiagnosticRvas[2]);
  g_diagnosticFourthHookAddress = reinterpret_cast<void*>(base + kDiagnosticRvas[3]);
  return true;
#else
  HMODULE weixin = GetModuleHandleW(L"Weixin.dll");
  if (weixin == nullptr) {
    writeStatus("STOPPED", "PROCESS_NOT_FOUND", "Weixin.dll is not loaded in the target process");
    return false;
  }
  std::string validationMessage;
  if (!validateWeixinMemory(weixin, &validationMessage)) {
    writeStatus("UNSUPPORTED", "SIGNATURE_MISMATCH", validationMessage.c_str());
    return false;
  }
  g_hookAddress = reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(weixin) + kWeixinNewSyncParseRva);
  g_weixinModuleBase = reinterpret_cast<std::uintptr_t>(weixin);
  g_accountHookAddress = reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(weixin) + kWeixinAccountRva);
  g_nicknameHookAddress = reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(weixin) + kWeixinNicknameRva);
  return true;
#endif
}

// 读取 MSVC std::string：按“数据指针 / 长度 / 容量”三元组解析，长度与容量不自洽时拒绝读取。
// 小于 16 字节走 SSO 内联缓冲，需要区分处理，否则会把栈上垃圾当成内容。
bool readMsvcString(std::uintptr_t address, char* output, std::size_t outputSize) {
  std::uint64_t length = 0;
  std::uint64_t capacity = 0;
  if (!readMemory(reinterpret_cast<const void*>(address + 0x10), &length, sizeof(length)) ||
      !readMemory(reinterpret_cast<const void*>(address + 0x18), &capacity, sizeof(capacity)) ||
      length > kMaxStringBytes || capacity < length || length >= outputSize) {
    return false;
  }

  std::memset(output, 0, outputSize);
  if (length == 0) return true;

  std::uintptr_t dataAddress = address;
  if (capacity >= 0x10 && !readMemory(reinterpret_cast<const void*>(address), &dataAddress, sizeof(dataAddress))) {
    return false;
  }
  if (dataAddress == 0 || !readMemory(reinterpret_cast<const void*>(dataAddress), output, static_cast<std::size_t>(length))) {
    return false;
  }
  output[length] = '\0';
  return true;
}

#ifndef CFPAY_DIAGNOSTIC_BUILD
// NewSync 结构化支付消息捕获。
// 链路：NewSyncResponse 解析返回 -> 命令列表 -> CmdItem(cmdid=5) -> 载荷 std::string -> 消息 protobuf。
// 观察器只按已还原的固定布局读取结构化字段并转发，不做任何展示文案匹配或金额文本拆解。
constexpr std::size_t kMaxPayloadBytes = 65536;
constexpr std::size_t kMaxCommandsPerResponse = 32;
constexpr std::uintptr_t kResponseCommandListOffset = 0x08;
constexpr std::uintptr_t kCommandListItemsOffset = 0x08;
constexpr std::uintptr_t kCommandListItemCountOffset = 0x10;
constexpr std::uintptr_t kCmdItemStringObjectOffset = 0x08;
constexpr std::uintptr_t kCmdItemIdOffset = 0x10;
constexpr std::uintptr_t kStringSizeOffset = 0x10;
constexpr std::uintptr_t kStringCapacityOffset = 0x18;
constexpr std::uint32_t kPayMsgFieldFrom = 2;
constexpr std::uint32_t kPayMsgFieldTo = 3;
constexpr std::uint32_t kPayMsgFieldMsgType = 4;
constexpr std::uint32_t kPayMsgFieldContent = 5;
constexpr std::uint32_t kPayMsgFieldCreateTime = 9;
constexpr std::uint32_t kPayMsgFieldServerMsgId = 12;
constexpr std::uint32_t kPayMsgContentInnerField = 1;

// 截断可能落在 UTF-8 多字节序列中间，导致宿主 JSON 解析失败；此处按码点边界裁剪并替换非法字节。
std::size_t sanitizeUtf8(char* value, std::size_t length) {
  std::size_t write = 0;
  std::size_t index = 0;
  while (index < length) {
    const auto lead = static_cast<unsigned char>(value[index]);
    std::size_t sequenceLength = 1;
    if (lead < 0x80) {
      sequenceLength = 1;
    } else if ((lead & 0xe0) == 0xc0) {
      sequenceLength = 2;
    } else if ((lead & 0xf0) == 0xe0) {
      sequenceLength = 3;
    } else if ((lead & 0xf8) == 0xf0) {
      sequenceLength = 4;
    } else {
      value[write++] = '?';
      index += 1;
      continue;
    }
    if (index + sequenceLength > length) break;
    bool valid = true;
    for (std::size_t offset = 1; offset < sequenceLength; offset += 1) {
      if ((static_cast<unsigned char>(value[index + offset]) & 0xc0) != 0x80) {
        valid = false;
        break;
      }
    }
    if (!valid) {
      value[write++] = '?';
      index += 1;
      continue;
    }
    for (std::size_t offset = 0; offset < sequenceLength; offset += 1) value[write++] = value[index + offset];
    index += sequenceLength;
  }
  value[write] = '\0';
  return write;
}

// 从内存里解一个 protobuf varint，越界或超过 10 字节即失败。
bool readVarintAt(std::uintptr_t address, std::size_t size, std::size_t* position, std::uint64_t* output) {
  std::uint64_t value = 0;
  int shift = 0;
  while (*position < size) {
    std::uint8_t byte = 0;
    if (!readMemory(reinterpret_cast<const void*>(address + *position), &byte, sizeof(byte))) return false;
    *position += 1;
    value |= static_cast<std::uint64_t>(byte & 0x7f) << shift;
    if ((byte & 0x80) == 0) {
      *output = value;
      return true;
    }
    shift += 7;
    if (shift > 63) return false;
  }
  return false;
}

struct ProtobufField {
  std::uint32_t field;
  std::uint8_t wireType;
  std::size_t dataOffset;
  std::size_t length;
  std::uint64_t varint;
};

// 顺序解析 protobuf 字段：读出 tag、线类型与长度，遇到不认识的线类型立即停止，不做猜测性跳过。
bool nextProtobufField(std::uintptr_t address, std::size_t size, std::size_t* position, ProtobufField* output) {
  std::uint64_t tag = 0;
  if (!readVarintAt(address, size, position, &tag)) return false;
  output->field = static_cast<std::uint32_t>(tag >> 3);
  output->wireType = static_cast<std::uint8_t>(tag & 7);
  output->dataOffset = 0;
  output->length = 0;
  output->varint = 0;
  switch (output->wireType) {
    case 0:
      return readVarintAt(address, size, position, &output->varint);
    case 1:
      if (*position + 8 > size) return false;
      *position += 8;
      return true;
    case 2: {
      std::uint64_t length = 0;
      if (!readVarintAt(address, size, position, &length)) return false;
      if (length > size || *position + length > size) return false;
      output->dataOffset = *position;
      output->length = static_cast<std::size_t>(length);
      *position += output->length;
      return true;
    }
    case 5:
      if (*position + 4 > size) return false;
      *position += 4;
      return true;
    default:
      return false;
  }
}

// 读取 CmdItem 内 std::string 对象的数据指针与长度（容量 >= 16 时数据在堆上）。
bool resolvePayloadString(std::uintptr_t commandItemAddress, std::uintptr_t* dataAddress, std::size_t* length) {
  std::uintptr_t vtable = 0;
  std::uintptr_t stringObject = 0;
  if (!readMemory(reinterpret_cast<const void*>(commandItemAddress), &vtable, sizeof(vtable)) ||
      vtable != g_weixinModuleBase + kWeixinCmdItemVtableRva) {
    return false;
  }
  if (!readMemory(reinterpret_cast<const void*>(commandItemAddress + kCmdItemStringObjectOffset), &stringObject,
                  sizeof(stringObject)) ||
      stringObject == 0) {
    return false;
  }
  std::uint64_t size = 0;
  std::uint64_t capacity = 0;
  if (!readMemory(reinterpret_cast<const void*>(stringObject + kStringSizeOffset), &size, sizeof(size)) ||
      !readMemory(reinterpret_cast<const void*>(stringObject + kStringCapacityOffset), &capacity, sizeof(capacity)) ||
      size == 0 || size > kMaxPayloadBytes || capacity < size) {
    return false;
  }
  std::uintptr_t data = stringObject;
  if (capacity >= 0x10 &&
      !readMemory(reinterpret_cast<const void*>(stringObject), &data, sizeof(data)) ) {
    return false;
  }
  if (data < 0x10000) return false;
  *dataAddress = data;
  *length = static_cast<std::size_t>(size);
  return true;
}

// 解析 CmdID=5 的消息载荷，按标签名提取 paymsg 的结构化字段（transid、fee、feetype、status、type、timestamp 等）。
// 只取协议字段，绝不解析“微信支付收款 x 元”这类展示文案；字段缺失或类型不符就整条丢弃。
void capturePayMessage(std::uintptr_t commandItemAddress, std::uint64_t capturedAt) {
  std::uintptr_t dataAddress = 0;
  std::size_t payloadLength = 0;
  if (!resolvePayloadString(commandItemAddress, &dataAddress, &payloadLength)) {
    InterlockedIncrement64(&g_readFailureCount);
    return;
  }

  std::int32_t msgType = 0;
  std::int32_t createTime = 0;
  std::uint64_t serverMsgId = 0;
  std::size_t contentOffset = 0;
  std::size_t contentLength = 0;
  bool hasContent = false;
  std::size_t fromOffset = 0;
  std::size_t fromLength = 0;
  std::uintptr_t fromBase = 0;
  std::size_t toOffset = 0;
  std::size_t toLength = 0;
  std::uintptr_t toBase = 0;

  std::size_t position = 0;
  ProtobufField field{};
  while (position < payloadLength && nextProtobufField(dataAddress, payloadLength, &position, &field)) {
    if (field.field == kPayMsgFieldMsgType && field.wireType == 0) {
      msgType = static_cast<std::int32_t>(field.varint);
    } else if (field.field == kPayMsgFieldCreateTime && field.wireType == 0) {
      createTime = static_cast<std::int32_t>(field.varint);
    } else if (field.field == kPayMsgFieldServerMsgId && field.wireType == 0) {
      serverMsgId = field.varint;
    } else if ((field.field == kPayMsgFieldFrom || field.field == kPayMsgFieldTo) && field.wireType == 2) {
      const std::uintptr_t innerAddress = dataAddress + field.dataOffset;
      const std::size_t innerLength = field.length;
      std::size_t innerPosition = 0;
      ProtobufField inner{};
      while (innerPosition < innerLength && nextProtobufField(innerAddress, innerLength, &innerPosition, &inner)) {
        if (inner.field == kPayMsgContentInnerField && inner.wireType == 2) {
          if (field.field == kPayMsgFieldFrom) {
            fromBase = innerAddress;
            fromOffset = inner.dataOffset;
            fromLength = inner.length;
          } else {
            toBase = innerAddress;
            toOffset = inner.dataOffset;
            toLength = inner.length;
          }
          break;
        }
      }
    } else if (field.field == kPayMsgFieldContent && field.wireType == 2) {
      // 内容字段是嵌套消息，正文位于其内部字段 1。
      const std::uintptr_t innerAddress = dataAddress + field.dataOffset;
      const std::size_t innerLength = field.length;
      std::size_t innerPosition = 0;
      ProtobufField inner{};
      while (innerPosition < innerLength && nextProtobufField(innerAddress, innerLength, &innerPosition, &inner)) {
        if (inner.field == kPayMsgContentInnerField && inner.wireType == 2) {
          // 内层偏移必须叠加外层字段偏移，才是相对载荷缓冲区起点的绝对偏移。
          contentOffset = field.dataOffset + inner.dataOffset;
          contentLength = inner.length;
          hasContent = true;
          break;
        }
      }
    }
  }

  InterlockedIncrement64(&g_payMessageScanCount);
  if (msgType != kWeixinSysMsgType || !hasContent || contentLength == 0) return;

  EventRecord event{};
  event.kind = EventKind::PayMessage;
  event.valid = true;
  event.capturedAt = capturedAt;
  event.payMsgType = msgType;
  event.payCreateTime = createTime;
  event.payServerMsgId = serverMsgId;
  if (fromLength > 0 && fromLength < sizeof(event.payFromUsername)) {
    if (readMemory(reinterpret_cast<const void*>(fromBase + fromOffset), event.payFromUsername, fromLength)) {
      event.payFromUsername[fromLength] = '\0';
    }
  }
  if (toLength > 0 && toLength < sizeof(event.payToUsername)) {
    if (readMemory(reinterpret_cast<const void*>(toBase + toOffset), event.payToUsername, toLength)) {
      event.payToUsername[toLength] = '\0';
    }
  }
  const std::size_t copyLength = contentLength < kPayContentSize - 1 ? contentLength : kPayContentSize - 1;
  if (!readMemory(reinterpret_cast<const void*>(dataAddress + contentOffset), event.payContent, copyLength)) {
    event.valid = false;
    setError(&event, "PAY_MESSAGE_CONTENT_READ_FAILED");
    InterlockedIncrement64(&g_readFailureCount);
  } else {
    event.payContent[copyLength] = '\0';
    sanitizeUtf8(event.payContent, copyLength);
    InterlockedIncrement64(&g_payMessageCount);
  }
  enqueueEvent(event);
}

// 断点命中后解析已解包的 NewSyncResponse：遍历命令列表，只处理 CmdID=5 的 AddMsg 载荷。
// 全程只读，不修改微信的任何内存与代码页。
void captureNewSyncResponse(std::uintptr_t responseAddress, std::uint64_t capturedAt) {
  std::uintptr_t vtable = 0;
  std::uintptr_t commandList = 0;
  if (!readMemory(reinterpret_cast<const void*>(responseAddress), &vtable, sizeof(vtable)) ||
      vtable != g_weixinModuleBase + kWeixinNewSyncResponseVtableRva) {
    return;
  }
  if (!readMemory(reinterpret_cast<const void*>(responseAddress + kResponseCommandListOffset), &commandList,
                  sizeof(commandList)) ||
      commandList == 0) {
    return;
  }
  std::uintptr_t commandListVtable = 0;
  if (!readMemory(reinterpret_cast<const void*>(commandList), &commandListVtable, sizeof(commandListVtable)) ||
      commandListVtable != g_weixinModuleBase + kWeixinCmdListVtableRva) {
    InterlockedIncrement64(&g_readFailureCount);
    return;
  }
  std::int32_t itemCount = 0;
  std::uintptr_t items = 0;
  if (!readMemory(reinterpret_cast<const void*>(commandList + kCommandListItemCountOffset), &itemCount,
                  sizeof(itemCount)) ||
      !readMemory(reinterpret_cast<const void*>(commandList + kCommandListItemsOffset), &items, sizeof(items)) ||
      itemCount <= 0 || itemCount > 4096 || items == 0) {
    InterlockedIncrement64(&g_readFailureCount);
    return;
  }
  const std::int32_t scanCount =
      itemCount < static_cast<std::int32_t>(kMaxCommandsPerResponse) ? itemCount
                                                                    : static_cast<std::int32_t>(kMaxCommandsPerResponse);
  for (std::int32_t index = 0; index < scanCount; index += 1) {
    std::uintptr_t commandAddress = 0;
    if (!readMemory(reinterpret_cast<const void*>(items + static_cast<std::size_t>(index) * sizeof(std::uintptr_t)),
                    &commandAddress, sizeof(commandAddress)) ||
        commandAddress == 0) {
      InterlockedIncrement64(&g_readFailureCount);
      break;
    }
    std::uintptr_t commandVtable = 0;
    std::int32_t commandId = 0;
    if (!readMemory(reinterpret_cast<const void*>(commandAddress), &commandVtable, sizeof(commandVtable)) ||
        commandVtable != g_weixinModuleBase + kWeixinCmdItemVtableRva ||
        !readMemory(reinterpret_cast<const void*>(commandAddress + kCmdItemIdOffset), &commandId, sizeof(commandId))) {
      InterlockedIncrement64(&g_readFailureCount);
      continue;
    }
    if (commandId != kWeixinAddMsgCommandId) continue;
    InterlockedIncrement64(&g_addMsgCommandCount);
    capturePayMessage(commandAddress, capturedAt);
  }
}
#endif

#if defined(CFPAY_DIAGNOSTIC_BUILD) && !defined(CFPAY_WMPF_DIAGNOSTIC_BUILD)
// 判断文本是否包含子串，仅诊断构建使用。
bool containsText(const char* value, const char* needle) {
  return std::strstr(value, needle) != nullptr;
}

// 判断文本里是否出现小数金额形态，仅诊断构建使用。
bool containsDecimalAmount(const char* value) {
  for (const char* cursor = value; *cursor != '\0'; cursor += 1) {
    if (*cursor < '0' || *cursor > '9') continue;
    while (*cursor >= '0' && *cursor <= '9') cursor += 1;
    if (*cursor == '.' && cursor[1] >= '0' && cursor[1] <= '9' && cursor[2] >= '0' && cursor[2] <= '9' &&
        (cursor[3] < '0' || cursor[3] > '9')) {
      return true;
    }
    if (*cursor == '\0') break;
  }
  return false;
}

struct DiagnosticTextShape {
  std::size_t bytes;
  bool receiptKeyword;
  bool paymentKeyword;
  bool amountMarker;
  bool decimalAmount;
};

// 统计一段文本的形态特征（关键词、金额标记、长度），仅诊断构建使用。
DiagnosticTextShape inspectDiagnosticText(const char* value) {
  return {
      std::strlen(value),
      containsText(value, "\xe6\x94\xb6\xe6\xac\xbe") || containsText(value, "\xe5\x88\xb0\xe8\xb4\xa6"),
      containsText(value, "\xe6\x94\xaf\xe4\xbb\x98"),
      containsText(value, "\xe9\x87\x91\xe9\xa2\x9d") || containsText(value, "\xe5\x85\x83") ||
          containsText(value, "\xc2\xa5") || containsText(value, "\xef\xbf\xa5"),
      containsDecimalAmount(value),
  };
}

// 判断文本形态是否像收款摘要；这是历史研究用的启发式，生产链路不使用。
bool isReceiptSummaryShape(const DiagnosticTextShape& shape) {
  return shape.receiptKeyword && shape.paymentKeyword && shape.amountMarker && shape.decimalAmount;
}

// 对文本做 FNV-1a 哈希，用哈希代替原文输出，避免把聊天内容写进诊断文件。
std::uint64_t diagnosticTextHash(const char* value) {
  std::uint64_t hash = 1469598103934665603ULL;
  for (const unsigned char* cursor = reinterpret_cast<const unsigned char*>(value); *cursor != 0; cursor += 1) {
    hash ^= *cursor;
    hash *= 1099511628211ULL;
  }
  return hash == 0 ? 1 : hash;
}

// 按槽位统计文本形态与哈希，只记计数不记原文。
void classifyDiagnosticText(std::size_t slot, std::uintptr_t stringAddress) {
  char value[1024] = {};
  if (slot >= kDiagnosticTextSlotCount || !readMsvcString(stringAddress, value, sizeof(value))) {
    InterlockedIncrement64(&g_readFailureCount);
    return;
  }
  const DiagnosticTextShape shape = inspectDiagnosticText(value);
  InterlockedIncrement64(&g_diagnosticTextReadable[slot]);
  InterlockedExchange64(&g_diagnosticTextBytes[slot], static_cast<LONG64>(shape.bytes));
  if (shape.receiptKeyword) InterlockedIncrement64(&g_diagnosticReceiptKeywords[slot]);
  if (shape.paymentKeyword) InterlockedIncrement64(&g_diagnosticPaymentKeywords[slot]);
  if (shape.amountMarker) InterlockedIncrement64(&g_diagnosticAmountMarkers[slot]);
  if (shape.decimalAmount) InterlockedIncrement64(&g_diagnosticDecimalAmounts[slot]);
}

// 把某个槽位的统计结果拼成指标串。
std::string diagnosticTextMetrics(const char* label, std::size_t slot) {
  return std::string(label) + "(read=" +
         std::to_string(InterlockedCompareExchange64(&g_diagnosticTextReadable[slot], 0, 0)) + ",bytes=" +
         std::to_string(InterlockedCompareExchange64(&g_diagnosticTextBytes[slot], 0, 0)) + ",receipt=" +
         std::to_string(InterlockedCompareExchange64(&g_diagnosticReceiptKeywords[slot], 0, 0)) + ",payment=" +
         std::to_string(InterlockedCompareExchange64(&g_diagnosticPaymentKeywords[slot], 0, 0)) + ",amount=" +
         std::to_string(InterlockedCompareExchange64(&g_diagnosticAmountMarkers[slot], 0, 0)) + ",decimal=" +
         std::to_string(InterlockedCompareExchange64(&g_diagnosticDecimalAmounts[slot], 0, 0)) + ')';
}

// 统计富文本 run 的数量与形态，仅诊断构建使用。
void classifyDiagnosticRichRuns(std::uintptr_t modelAddress) {
  std::uintptr_t begin = 0;
  std::uintptr_t end = 0;
  if (!readMemory(reinterpret_cast<const void*>(modelAddress + 0x1e8), &begin, sizeof(begin)) ||
      !readMemory(reinterpret_cast<const void*>(modelAddress + 0x1f0), &end, sizeof(end)) || end < begin) {
    InterlockedIncrement64(&g_readFailureCount);
    return;
  }

  const std::uintptr_t byteCount = end - begin;
  if (byteCount % kDiagnosticRichRunSize != 0 || byteCount / kDiagnosticRichRunSize > kMaxDiagnosticRichRuns ||
      (byteCount != 0 && begin == 0)) {
    InterlockedIncrement64(&g_readFailureCount);
    return;
  }

  const std::size_t runCount = static_cast<std::size_t>(byteCount / kDiagnosticRichRunSize);
  std::size_t totalBytes = 0;
  bool receiptKeyword = false;
  bool paymentKeyword = false;
  bool amountMarker = false;
  bool decimalAmount = false;
  for (std::size_t index = 0; index < runCount; index += 1) {
    char value[1024] = {};
    if (!readMsvcString(begin + index * kDiagnosticRichRunSize, value, sizeof(value))) {
      InterlockedIncrement64(&g_readFailureCount);
      return;
    }
    const DiagnosticTextShape shape = inspectDiagnosticText(value);
    totalBytes += shape.bytes;
    receiptKeyword = receiptKeyword || shape.receiptKeyword;
    paymentKeyword = paymentKeyword || shape.paymentKeyword;
    amountMarker = amountMarker || shape.amountMarker;
    decimalAmount = decimalAmount || shape.decimalAmount;
  }

  InterlockedIncrement64(&g_diagnosticRichRunsReadable);
  InterlockedExchange64(&g_diagnosticRichRunCount, static_cast<LONG64>(runCount));
  InterlockedExchange64(&g_diagnosticRichRunBytes, static_cast<LONG64>(totalBytes));
  if (receiptKeyword) InterlockedIncrement64(&g_diagnosticRichRunReceiptKeywords);
  if (paymentKeyword) InterlockedIncrement64(&g_diagnosticRichRunPaymentKeywords);
  if (amountMarker) InterlockedIncrement64(&g_diagnosticRichRunAmountMarkers);
  if (decimalAmount) InterlockedIncrement64(&g_diagnosticRichRunDecimalAmounts);
}

// 拼出富文本 run 的指标串。
std::string diagnosticRichRunMetrics() {
  return "richRuns(read=" +
         std::to_string(InterlockedCompareExchange64(&g_diagnosticRichRunsReadable, 0, 0)) + ",count=" +
         std::to_string(InterlockedCompareExchange64(&g_diagnosticRichRunCount, 0, 0)) + ",bytes=" +
         std::to_string(InterlockedCompareExchange64(&g_diagnosticRichRunBytes, 0, 0)) + ",receipt=" +
         std::to_string(InterlockedCompareExchange64(&g_diagnosticRichRunReceiptKeywords, 0, 0)) + ",payment=" +
         std::to_string(InterlockedCompareExchange64(&g_diagnosticRichRunPaymentKeywords, 0, 0)) + ",amount=" +
         std::to_string(InterlockedCompareExchange64(&g_diagnosticRichRunAmountMarkers, 0, 0)) + ",decimal=" +
         std::to_string(InterlockedCompareExchange64(&g_diagnosticRichRunDecimalAmounts, 0, 0)) + ')';
}

// 统计服务号会话摘要的形态，仅诊断构建使用。
void classifyDiagnosticServiceSummary(std::uintptr_t modelAddress) {
  char value[1024] = {};
  if (!readMsvcString(modelAddress + 0x48, value, sizeof(value))) {
    InterlockedIncrement64(&g_readFailureCount);
    return;
  }
  const DiagnosticTextShape shape = inspectDiagnosticText(value);
  InterlockedIncrement64(&g_diagnosticServiceSummaryReadable);
  InterlockedExchange64(&g_diagnosticServiceSummaryBytes, static_cast<LONG64>(shape.bytes));
  if (shape.receiptKeyword) InterlockedIncrement64(&g_diagnosticServiceSummaryReceiptKeywords);
  if (shape.paymentKeyword) InterlockedIncrement64(&g_diagnosticServiceSummaryPaymentKeywords);
  if (shape.amountMarker) InterlockedIncrement64(&g_diagnosticServiceSummaryAmountMarkers);
  if (shape.decimalAmount) InterlockedIncrement64(&g_diagnosticServiceSummaryDecimalAmounts);
}

// 拼出服务号摘要的指标串。
std::string diagnosticServiceSummaryMetrics() {
  return "serviceSummary(read=" +
         std::to_string(InterlockedCompareExchange64(&g_diagnosticServiceSummaryReadable, 0, 0)) + ",bytes=" +
         std::to_string(InterlockedCompareExchange64(&g_diagnosticServiceSummaryBytes, 0, 0)) + ",receipt=" +
         std::to_string(InterlockedCompareExchange64(&g_diagnosticServiceSummaryReceiptKeywords, 0, 0)) + ",payment=" +
         std::to_string(InterlockedCompareExchange64(&g_diagnosticServiceSummaryPaymentKeywords, 0, 0)) + ",amount=" +
         std::to_string(InterlockedCompareExchange64(&g_diagnosticServiceSummaryAmountMarkers, 0, 0)) + ",decimal=" +
         std::to_string(InterlockedCompareExchange64(&g_diagnosticServiceSummaryDecimalAmounts, 0, 0)) + ')';
}

// 把返回地址按相对模块基址的偏移分桶，用于统计调用来源而不记录绝对地址。
std::size_t diagnosticCallerBucket(std::uintptr_t returnAddress) {
  const std::uintptr_t moduleBase = reinterpret_cast<std::uintptr_t>(g_hookAddress) - kDiagnosticRvas[0];
  for (std::size_t index = 0; index < kDiagnosticCallerReturnRvas.size(); index += 1) {
    if (returnAddress == moduleBase + kDiagnosticCallerReturnRvas[index]) return index;
  }
  return kDiagnosticCallerBucketCount - 1;
}

// 按调用者桶统计摘要命中情况。
void classifyDiagnosticCallerSummary(std::uintptr_t modelAddress, std::uintptr_t returnAddress) {
  char value[1024] = {};
  if (!readMsvcString(modelAddress + 0x48, value, sizeof(value))) {
    InterlockedIncrement64(&g_readFailureCount);
    return;
  }
  const std::size_t bucket = diagnosticCallerBucket(returnAddress);
  const DiagnosticTextShape shape = inspectDiagnosticText(value);
  InterlockedIncrement64(&g_diagnosticCallerHits[bucket]);
  InterlockedExchange64(&g_diagnosticCallerBytes[bucket], static_cast<LONG64>(shape.bytes));
  if (shape.receiptKeyword) InterlockedIncrement64(&g_diagnosticCallerReceiptKeywords[bucket]);
  if (shape.paymentKeyword) InterlockedIncrement64(&g_diagnosticCallerPaymentKeywords[bucket]);
  if (shape.amountMarker) InterlockedIncrement64(&g_diagnosticCallerAmountMarkers[bucket]);
  if (shape.decimalAmount) InterlockedIncrement64(&g_diagnosticCallerDecimalAmounts[bucket]);
}

// 登记待轮询的 ViewModel 地址，供后续观察其字段变化。
void trackDiagnosticViewModel(
    std::uintptr_t viewModelAddress,
    std::uintptr_t modelAddress,
    std::uintptr_t returnAddress) {
  const std::uintptr_t moduleBase = reinterpret_cast<std::uintptr_t>(g_hookAddress) - kDiagnosticRvas[0];
  if (viewModelAddress == 0 || returnAddress != moduleBase + kDiagnosticCallerReturnRvas[1]) return;

  char value[1024] = {};
  if (!readMsvcString(modelAddress + kDiagnosticSummaryOffset, value, sizeof(value))) return;
  const DiagnosticTextShape shape = inspectDiagnosticText(value);
  if (!isReceiptSummaryShape(shape)) return;

  for (auto& tracked : g_diagnosticTrackedViewModels) {
    if (InterlockedCompareExchange(&tracked.state, 2, 2) == 2 && tracked.address == viewModelAddress) return;
  }

  for (auto& tracked : g_diagnosticTrackedViewModels) {
    if (InterlockedCompareExchange(&tracked.state, 1, 0) != 0) continue;
    tracked.address = viewModelAddress;
    InterlockedExchange64(&tracked.lastHash, static_cast<LONG64>(diagnosticTextHash(value)));
    InterlockedExchange(&tracked.consecutiveReadFailures, 0);
    MemoryBarrier();
    InterlockedExchange(&tracked.state, 2);
    InterlockedIncrement64(&g_diagnosticTrackedRegistrations);
    return;
  }
}

// 轮询已登记的 ViewModel，输出字段变化计数。
void pollDiagnosticTrackedViewModels() {
  for (auto& tracked : g_diagnosticTrackedViewModels) {
    if (InterlockedCompareExchange(&tracked.state, 2, 2) != 2) continue;

    char value[1024] = {};
    if (!readMsvcString(
            tracked.address + kDiagnosticEmbeddedModelOffset + kDiagnosticSummaryOffset, value, sizeof(value))) {
      InterlockedIncrement64(&g_diagnosticTrackedReadFailures);
      if (InterlockedIncrement(&tracked.consecutiveReadFailures) >= 3) {
        tracked.address = 0;
        InterlockedExchange64(&tracked.lastHash, 0);
        InterlockedExchange(&tracked.state, 0);
      }
      continue;
    }

    InterlockedExchange(&tracked.consecutiveReadFailures, 0);
    InterlockedIncrement64(&g_diagnosticTrackedObservations);
    const DiagnosticTextShape shape = inspectDiagnosticText(value);
    InterlockedExchange64(&g_diagnosticTrackedBytes, static_cast<LONG64>(shape.bytes));
    const LONG64 hash = static_cast<LONG64>(diagnosticTextHash(value));
    const LONG64 previousHash = InterlockedExchange64(&tracked.lastHash, hash);
    if (previousHash != 0 && previousHash != hash) {
      InterlockedIncrement64(&g_diagnosticTrackedChanges);
      if (isReceiptSummaryShape(shape)) InterlockedIncrement64(&g_diagnosticTrackedQualifiedChanges);
    }
  }
}

// 拼出已跟踪 ViewModel 的指标串。
std::string diagnosticTrackedViewModelMetrics() {
  std::size_t active = 0;
  for (auto& tracked : g_diagnosticTrackedViewModels) {
    if (InterlockedCompareExchange(&tracked.state, 2, 2) == 2) active += 1;
  }
  return "trackedViewModels(active=" + std::to_string(active) + ",registered=" +
         std::to_string(InterlockedCompareExchange64(&g_diagnosticTrackedRegistrations, 0, 0)) + ",observed=" +
         std::to_string(InterlockedCompareExchange64(&g_diagnosticTrackedObservations, 0, 0)) + ",changes=" +
         std::to_string(InterlockedCompareExchange64(&g_diagnosticTrackedChanges, 0, 0)) + ",qualified=" +
         std::to_string(InterlockedCompareExchange64(&g_diagnosticTrackedQualifiedChanges, 0, 0)) + ",readFailures=" +
         std::to_string(InterlockedCompareExchange64(&g_diagnosticTrackedReadFailures, 0, 0)) + ",bytes=" +
         std::to_string(InterlockedCompareExchange64(&g_diagnosticTrackedBytes, 0, 0)) + ')';
}

// 拼出某个调用者桶的指标串。
std::string diagnosticCallerMetrics(const char* label, std::size_t bucket) {
  return std::string(label) + "(hits=" +
         std::to_string(InterlockedCompareExchange64(&g_diagnosticCallerHits[bucket], 0, 0)) + ",bytes=" +
         std::to_string(InterlockedCompareExchange64(&g_diagnosticCallerBytes[bucket], 0, 0)) + ",receipt=" +
         std::to_string(InterlockedCompareExchange64(&g_diagnosticCallerReceiptKeywords[bucket], 0, 0)) + ",payment=" +
         std::to_string(InterlockedCompareExchange64(&g_diagnosticCallerPaymentKeywords[bucket], 0, 0)) + ",amount=" +
         std::to_string(InterlockedCompareExchange64(&g_diagnosticCallerAmountMarkers[bucket], 0, 0)) + ",decimal=" +
         std::to_string(InterlockedCompareExchange64(&g_diagnosticCallerDecimalAmounts[bucket], 0, 0)) + ')';
}

// 把文本脱敏成形态串（只保留字符类别与长度），诊断输出不含原文。
std::string maskedSlotShape(const char* value) {
  std::string output;
  for (const unsigned char* cursor = reinterpret_cast<const unsigned char*>(value); *cursor != 0; cursor += 1) {
    if (output.size() >= 96) break;
    const unsigned char character = *cursor;
    if (character >= '0' && character <= '9') output.push_back('#');
    else if ((character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z')) output.push_back('a');
    else if (character < 0x20) continue;
    else output.push_back(static_cast<char>(character));
  }
  return output;
}

// 把一次槽位转储入队，超过上限后丢弃，避免诊断输出无限增长。
void enqueueSlotDump(const char* slot, const char* shape, std::int32_t sequence, std::size_t bytes) {
  if (InterlockedCompareExchange64(&g_diagnosticSlotDumpCount, 0, 0) >= kMaxSlotDumpRecords) {
    InterlockedIncrement64(&g_diagnosticSlotDumpDrops);
    return;
  }
  EventRecord event{};
  event.kind = EventKind::SlotDump;
  event.valid = true;
  event.capturedAt = unixTimeMilliseconds();
  event.paySubtype = sequence;
  event.beginTransferTime = static_cast<std::int32_t>(bytes);
  std::strncpy(event.eventPath, slot, sizeof(event.eventPath) - 1);
  std::strncpy(event.summaryShape, shape, sizeof(event.summaryShape) - 1);
  InterlockedIncrement64(&g_diagnosticSlotDumpCount);
  enqueueEvent(event);
}

// 转储会话模型的若干槽位形态，仅诊断构建使用。
void dumpSessionModelSlots(std::uintptr_t modelAddress, std::uintptr_t returnAddress, std::int32_t sequence) {
  const std::uintptr_t moduleBase = reinterpret_cast<std::uintptr_t>(g_hookAddress) - kDiagnosticRvas[0];
  std::string meta = "bucket=" + std::string(kDiagnosticCallerLabels[diagnosticCallerBucket(returnAddress)]) +
                     ";retRva=0x" + hexValue(returnAddress - moduleBase) +
                     ";modelRva=0x" + hexValue(modelAddress - moduleBase);
  enqueueSlotDump("meta", meta.c_str(), sequence, 0);
  for (std::size_t index = 0; index < kDiagnosticTextOffsets.size(); index += 1) {
    char value[1024] = {};
    if (!readMsvcString(modelAddress + kDiagnosticTextOffsets[index], value, sizeof(value))) continue;
    if (value[0] == '\0') continue;
    const std::string shape = maskedSlotShape(value);
    enqueueSlotDump(kDiagnosticTextLabels[index], shape.c_str(), sequence, std::strlen(value));
  }
}

// 采集会话预览文本的形态特征，仅诊断构建使用。
void captureSessionPreviewShape(std::uintptr_t modelAddress) {
  for (std::size_t index = 0; index < kDiagnosticTextOffsets.size(); index += 1) {
    classifyDiagnosticText(index, modelAddress + kDiagnosticTextOffsets[index]);
  }
  classifyDiagnosticRichRuns(modelAddress);
  std::uint8_t hidden = 0;
  if (!readMemory(reinterpret_cast<const void*>(modelAddress + 0x1d9), &hidden, sizeof(hidden))) {
    InterlockedIncrement64(&g_readFailureCount);
    return;
  }
  if (hidden == 1) InterlockedIncrement64(&g_diagnosticHiddenPreviewCount);
}
#endif

#ifdef CFPAY_WMPF_DIAGNOSTIC_BUILD
// 读取小程序渲染进程 flue.dll 的字符串对象，仅 WMPF 诊断构建使用。
bool readFlueString(std::uintptr_t address, char* output, std::size_t outputSize) {
  std::array<std::uint8_t, 24> object{};
  if (!readMemory(reinterpret_cast<const void*>(address), object.data(), object.size())) return false;

  std::uint64_t length = object[0x17];
  std::uintptr_t dataAddress = address;
  const auto marker = static_cast<std::int8_t>(object[0x17]);
  if (marker < 0) {
    std::memcpy(&dataAddress, object.data(), sizeof(dataAddress));
    std::memcpy(&length, object.data() + 0x08, sizeof(length));
  }
  if (length > kMaxStringBytes || length >= outputSize || dataAddress == 0) return false;

  std::memset(output, 0, outputSize);
  if (length == 0) return true;
  if (marker >= 0) {
    std::memcpy(output, object.data(), static_cast<std::size_t>(length));
  } else if (!readMemory(reinterpret_cast<const void*>(dataAddress), output, static_cast<std::size_t>(length))) {
    return false;
  }
  output[length] = '\0';
  return true;
}

// 读取诊断事件名，读不到或为空都视为失败。
bool readDiagnosticEventName(std::uintptr_t address, char* output, std::size_t outputSize) {
  if (!readFlueString(address, output, outputSize) || output[0] == '\0') return false;
  for (const unsigned char character : std::string(output)) {
    const bool allowed = (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
                         (character >= '0' && character <= '9') || character == '_' || character == '-' ||
                         character == '.' || character == ':' || character == '/' || character == '$' ||
                         character == '@';
    if (!allowed) {
      std::memset(output, 0, outputSize);
      return false;
    }
  }
  return true;
}
#endif

// 把一条记录标记为无效并写入错误码；无效记录不会进入上报队列。
void setError(EventRecord* event, const char* error) {
  event->valid = false;
  std::strncpy(event->error, error, sizeof(event->error) - 1);
}

// 从收款入口的输出对象里采集一条收款记录（账号、金额、交易号、时间等）。
EventRecord captureEvent(std::uintptr_t outputAddress) {
  EventRecord event{};
  event.kind = EventKind::Payment;
  event.valid = true;
  event.capturedAt = unixTimeMilliseconds();
  if (!readInt32(outputAddress + 0x1c8, &event.paySubtype) ||
      !readMsvcString(outputAddress + 0x1d0, event.feeDescription, sizeof(event.feeDescription)) ||
      !readMsvcString(outputAddress + 0x1f0, event.transactionId, sizeof(event.transactionId)) ||
      !readInt32(outputAddress + 0x230, &event.beginTransferTime) ||
      !readMsvcString(outputAddress + 0x238, event.memo, sizeof(event.memo)) ||
      !readMsvcString(outputAddress + 0x258, event.receiverUsername, sizeof(event.receiverUsername))) {
    setError(&event, "READ_FAILED");
  }
  return event;
}

// 从账号读取入口的输出对象里采集当前登录账号（uin、wxid、登录状态）。
EventRecord captureAccount(std::uintptr_t outputAddress) {
  EventRecord event{};
  event.kind = EventKind::Account;
  event.valid = true;
  event.capturedAt = unixTimeMilliseconds();
  std::uint8_t loggedIn = 0;
  if (!readMemory(reinterpret_cast<const void*>(outputAddress), &event.accountUin, sizeof(event.accountUin)) ||
      !readMsvcString(outputAddress + 0x08, event.accountUsername, sizeof(event.accountUsername)) ||
      !readMemory(reinterpret_cast<const void*>(outputAddress + 0x28), &loggedIn, sizeof(loggedIn))) {
    setError(&event, "ACCOUNT_READ_FAILED");
  }
  event.accountLoggedIn = loggedIn != 0;
  return event;
}

// 从昵称读取入口的输出对象里采集当前账号昵称。
EventRecord captureNickname(std::uintptr_t outputAddress) {
  EventRecord event{};
  event.kind = EventKind::Nickname;
  event.valid = readMsvcString(outputAddress, event.accountNickname, sizeof(event.accountNickname));
  event.capturedAt = unixTimeMilliseconds();
  if (!event.valid) setError(&event, "NICKNAME_READ_FAILED");
  return event;
}

// 对账号记录做指纹哈希，用于识别“同一账号的重复观察”。
std::uint64_t accountFingerprint(const EventRecord& event) {
  std::uint64_t hash = 1469598103934665603ULL ^ event.accountUin;
  for (const unsigned char* cursor = reinterpret_cast<const unsigned char*>(event.accountUsername); *cursor != 0;
       cursor += 1) {
    const unsigned char character = *cursor;
    hash ^= character;
    hash *= 1099511628211ULL;
  }
  return hash == 0 ? 1 : hash;
}

// 判断账号记录是否构成有效的已登录证据：字段齐全、登录标志为真、uin 与 wxid 都非空。
bool isLoggedInAccount(const EventRecord& event) {
  return event.valid && event.accountLoggedIn && event.accountUin != 0 && event.accountUsername[0] != '\0';
}

// 把记录放进固定大小的环形队列；队列满时丢弃最旧的一条，绝不阻塞微信线程。
bool enqueueEvent(const EventRecord& event) {
  for (auto& slot : g_queue) {
    if (InterlockedCompareExchange(&slot.state, 1, 0) == 0) {
      slot.event = event;
      MemoryBarrier();
      InterlockedExchange(&slot.state, 2);
      SetEvent(g_queueEvent);
      return true;
    }
  }
  InterlockedIncrement64(&g_queueDropCount);
  return false;
}

#ifndef CFPAY_DIAGNOSTIC_BUILD
// 拼出 Hook 运行指标：已覆盖线程数、登录状态与其距今时长、各断点的入口/返回命中数、读取失败数、队列丢弃数。
// 这些计数会由监控端翻译成中文后上报，是判断 Hook 是否真的在工作的主要依据。
std::string hookMetricsMessage(const char* prefix) {
  int threadCount = 0;
  for (auto& state : g_threads) {
    if (InterlockedCompareExchange(&state.threadId, 0, 0) != 0) threadCount += 1;
  }
  const std::uint64_t now = unixTimeMilliseconds();
  const std::uint64_t lastLoggedInAt =
      static_cast<std::uint64_t>(InterlockedCompareExchange64(&g_lastLoggedInObservationAt, 0, 0));
  const LONG loginState = InterlockedCompareExchange(&g_loginState, 0, 0);
  const std::uint64_t loginAgeMs = lastLoggedInAt == 0 || now < lastLoggedInAt ? 0 : now - lastLoggedInAt;
  return std::string(prefix) + "; loginState=" +
         (loginState == 1 ? "logged_in" : (loginState == 0 ? "logged_out" : "unknown")) +
         "; loginAgeMs=" + std::to_string(loginAgeMs) + "; newSyncEntries=" +
         std::to_string(InterlockedCompareExchange64(&g_newSyncEntryCount, 0, 0)) + "; addMsgCommands=" +
         std::to_string(InterlockedCompareExchange64(&g_addMsgCommandCount, 0, 0)) + "; payMessageScans=" +
         std::to_string(InterlockedCompareExchange64(&g_payMessageScanCount, 0, 0)) + "; payMessages=" +
         std::to_string(InterlockedCompareExchange64(&g_payMessageCount, 0, 0)) + "; newSyncReturns=" +
         std::to_string(InterlockedCompareExchange64(&g_newSyncReturnCount, 0, 0)) + "; accountEntries=" +
         std::to_string(InterlockedCompareExchange64(&g_accountEntryCount, 0, 0)) + "; accountReturns=" +
         std::to_string(InterlockedCompareExchange64(&g_accountReturnCount, 0, 0)) + "; nicknameEntries=" +
         std::to_string(InterlockedCompareExchange64(&g_nicknameEntryCount, 0, 0)) + "; nicknameReturns=" +
         std::to_string(InterlockedCompareExchange64(&g_nicknameReturnCount, 0, 0)) + "; readFailures=" +
         std::to_string(InterlockedCompareExchange64(&g_readFailureCount, 0, 0)) + "; queueDrops=" +
         std::to_string(InterlockedCompareExchange64(&g_queueDropCount, 0, 0)) + "; threads=" +
         std::to_string(threadCount);
}

// 判断最近一次“已登录”观察是否仍在有效期内。
bool loginObservationFresh(std::uint64_t now) {
  const std::uint64_t lastLoggedInAt =
      static_cast<std::uint64_t>(InterlockedCompareExchange64(&g_lastLoggedInObservationAt, 0, 0));
  return InterlockedCompareExchange(&g_loginState, 0, 0) == 1 && lastLoggedInAt != 0 && now >= lastLoggedInAt;
}

// 输出健康状态：有新鲜的登录观察就报 HEALTHY，处于检测宽限期内报 STARTING，否则报 DEGRADED 要求登录。
// 进程存在不等于已登录，因此这里以账号观察结果为准。
void writeLoginHealth(std::uint64_t now, const char* healthyPrefix) {
  if (loginObservationFresh(now)) {
    const std::string metrics = hookMetricsMessage(healthyPrefix);
    writeStatus("HEALTHY", "HOOK_ACTIVE", metrics.c_str());
    return;
  }
  const std::uint64_t startedAt =
      static_cast<std::uint64_t>(InterlockedCompareExchange64(&g_observerStartedAt, 0, 0));
  if (InterlockedCompareExchange(&g_loginState, 0, 0) == -1 && startedAt != 0 && now >= startedAt &&
      now - startedAt < kLoginDetectionGraceMs) {
    const std::string metrics = hookMetricsMessage("waiting for current Weixin account observation");
    writeStatus("STARTING", "WEIXIN_LOGIN_CHECKING", metrics.c_str());
    return;
  }
  const std::string metrics = hookMetricsMessage("Weixin is running but no logged-in current account was detected");
  writeStatus("DEGRADED", "WEIXIN_LOGIN_REQUIRED", metrics.c_str());
}
#endif

// 判断队列里是否还有待输出的记录。
bool hasQueuedEvents() {
  for (auto& slot : g_queue) {
    if (InterlockedCompareExchange(&slot.state, 2, 2) == 2) return true;
  }
  return false;
}

// 把一条记录写成 JSONL 输出。
// 无效记录只输出错误码；收款记录必须通过全部字段校验才会输出，避免把半条数据交给上层判定。
void writeEvent(const EventRecord& event) {
  if (!event.valid) {
    writeStatus("ERROR", "READ_FAILED", event.error);
    return;
  }
  if (event.kind == EventKind::Account) {
    writeLine(
        "{\"type\":\"account\",\"capturedAt\":" + std::to_string(event.capturedAt) +
        ",\"fields\":{\"uin\":" + std::to_string(event.accountUin) + ",\"username\":\"" +
        jsonEscape(event.accountUsername) + "\",\"nickname\":\"" + jsonEscape(event.accountNickname) +
        "\",\"loggedIn\":" +
        std::string(event.accountLoggedIn ? "true" : "false") + "}}");
    return;
  }
  if (event.kind == EventKind::Nickname) {
    writeLine(
        "{\"type\":\"nickname\",\"capturedAt\":" + std::to_string(event.capturedAt) +
        ",\"fields\":{\"nickname\":\"" + jsonEscape(event.accountNickname) + "\"}}");
    return;
  }
  if (event.kind == EventKind::DiagnosticEventName) {
    writeLine(
        "{\"type\":\"diagnostic_event_name\",\"source\":\"NotifyEvent\",\"capturedAt\":" +
        std::to_string(event.capturedAt) + ",\"eventName\":\"" + jsonEscape(event.diagnosticEventName) + "\"}");
    return;
  }
  if (event.kind == EventKind::PayMessage) {
    writeLine(
        "{\"type\":\"pay_message\",\"capturedAt\":" + std::to_string(event.capturedAt) +
        ",\"fields\":{\"msgType\":" + std::to_string(event.payMsgType) + ",\"createTime\":" +
        std::to_string(event.payCreateTime) + ",\"serverMsgId\":" + std::to_string(event.payServerMsgId) +
        ",\"contentBytes\":" + std::to_string(std::strlen(event.payContent)) + ",\"fromUsername\":\"" +
        jsonEscape(event.payFromUsername) + "\",\"toUsername\":\"" + jsonEscape(event.payToUsername) +
        "\",\"content\":\"" + jsonEscape(event.payContent) + "\"}}");
    return;
  }
  writeLine(
      "{\"type\":\"event\",\"source\":\"receipt_parser\",\"capturedAt\":" + std::to_string(event.capturedAt) +
      ",\"fields\":{\"paySubtype\":" + std::to_string(event.paySubtype) + ",\"feeDescription\":\"" +
      jsonEscape(event.feeDescription) + "\",\"transactionId\":\"" + jsonEscape(event.transactionId) +
      "\",\"beginTransferTime\":" + std::to_string(event.beginTransferTime) + ",\"memo\":\"" +
      jsonEscape(event.memo) + "\",\"receiverUsername\":\"" + jsonEscape(event.receiverUsername) + "\"}}");
}

// 遍历目标进程的所有线程批量设置/清除断点，返回是否全部成功。
bool updateAllThreadBreakpoints(bool enable);
// 停止流程：清除所有线程的断点并摘除异常处理器；DLL 保持惰性加载，不做动态卸载。
DWORD WINAPI stopMain(void*);

// 请求异步停止：置生命周期标志并唤醒工作线程，由工作线程自己清理断点，不在调用方线程里动线程上下文。
void requestAsyncStop() {
  if (InterlockedCompareExchange(&g_lifecycle, kLifecycleStopping, kLifecycleRunning) != kLifecycleRunning) return;
  HANDLE cleanup = CreateThread(nullptr, 0, stopMain, nullptr, 0, nullptr);
  if (cleanup == nullptr) {
    InterlockedExchange(&g_lifecycle, kLifecycleRunning);
    return;
  }
  CloseHandle(cleanup);
}

// 工作线程：把队列里的记录逐条写出，并按节奏重申健康状态、补齐新建线程的断点覆盖。
// 首次命中前用更短的间隔扫描新线程，命中后放慢，兼顾覆盖率与开销。
DWORD WINAPI workerMain(void*) {
#ifdef CFPAY_DIAGNOSTIC_BUILD
  std::uint64_t nextCoverageCheck = 0;
  std::uint64_t nextHealthReport = unixTimeMilliseconds();
#else
  std::uint64_t nextCoverageCheck = 0;
  std::uint64_t nextHealthReport = unixTimeMilliseconds() + kLoginDetectionGraceMs;
  int consecutiveCoverageFailures = 0;
  bool coverageWarningWritten = false;
#endif
  while (InterlockedCompareExchange(&g_running, 1, 1) != 0 || hasQueuedEvents()) {
    WaitForSingleObject(g_queueEvent, 250);
#ifndef CFPAY_DIAGNOSTIC_BUILD
    bool accountStateChanged = false;
#endif
    for (auto& slot : g_queue) {
      if (InterlockedCompareExchange(&slot.state, 3, 2) == 2) {
#ifndef CFPAY_DIAGNOSTIC_BUILD
        if (slot.event.kind == EventKind::Account && slot.event.valid && slot.event.loginStateChanged) {
          accountStateChanged = true;
        }
#endif
        writeEvent(slot.event);
        InterlockedExchange(&slot.state, 0);
      }
    }
    const std::uint64_t now = unixTimeMilliseconds();
#ifdef CFPAY_DIAGNOSTIC_BUILD
    if (InterlockedCompareExchange(&g_running, 1, 1) != 0 && now >= nextCoverageCheck) {
      updateAllThreadBreakpoints(true);
#ifndef CFPAY_WMPF_DIAGNOSTIC_BUILD
      pollDiagnosticTrackedViewModels();
#endif
      nextCoverageCheck = now + 250;
    }
    if (g_pipeOutput && InterlockedCompareExchange(&g_running, 1, 1) != 0 && now >= nextHealthReport) {
#ifdef CFPAY_WMPF_DIAGNOSTIC_BUILD
      const std::string metrics =
          "WMPF browser diagnostic active; sendAppletEvent=" +
          std::to_string(InterlockedCompareExchange64(&g_newSyncEntryCount, 0, 0)) + "; sessionSendData=" +
          std::to_string(InterlockedCompareExchange64(&g_newSyncReturnCount, 0, 0)) + "; notifyEvent=" +
          std::to_string(InterlockedCompareExchange64(&g_accountEntryCount, 0, 0)) + "; dispatchEvent=" +
          std::to_string(InterlockedCompareExchange64(&g_accountReturnCount, 0, 0)) + "; eventNameReadFailures=" +
          std::to_string(InterlockedCompareExchange64(&g_readFailureCount, 0, 0));
#else
      std::string metrics =
          "会话摘要诊断运行中; viewModelBind=" +
          std::to_string(InterlockedCompareExchange64(&g_newSyncEntryCount, 0, 0)) + "; updateWrapper=" +
          std::to_string(InterlockedCompareExchange64(&g_newSyncReturnCount, 0, 0)) + "; serviceFamilyBind=" +
          std::to_string(InterlockedCompareExchange64(&g_accountEntryCount, 0, 0)) + "; globalFamilyBind=" +
          std::to_string(InterlockedCompareExchange64(&g_accountReturnCount, 0, 0)) + "; hiddenPreview=" +
          std::to_string(InterlockedCompareExchange64(&g_diagnosticHiddenPreviewCount, 0, 0));
      for (std::size_t index = 0; index < kDiagnosticTextSlotCount; index += 1) {
        metrics += "; " + diagnosticTextMetrics(kDiagnosticTextLabels[index], index);
      }
      metrics += "; " + diagnosticRichRunMetrics() + "; " + diagnosticServiceSummaryMetrics() + "; " +
                 diagnosticTrackedViewModelMetrics() +
                 "; textReadFailures=" +
                 std::to_string(InterlockedCompareExchange64(&g_readFailureCount, 0, 0));
      for (std::size_t bucket = 0; bucket < kDiagnosticCallerBucketCount; bucket += 1) {
        metrics += "; " + diagnosticCallerMetrics(kDiagnosticCallerLabels[bucket], bucket);
      }
#endif
      writeStatus("HEALTHY", "DIAGNOSTIC_ACTIVE", metrics.c_str());
      nextHealthReport = now + 1'000;
    }
#else
    if (accountStateChanged && InterlockedCompareExchange(&g_running, 1, 1) != 0) {
      writeLoginHealth(
          now,
          g_testMode ? "test receipt and account observer active" : "Weixin receipt observer active and current account detected");
    }
    if (InterlockedCompareExchange(&g_running, 1, 1) != 0 &&
        InterlockedCompareExchange(&g_lifecycle, kLifecycleRunning, kLifecycleRunning) == kLifecycleRunning &&
        now >= nextCoverageCheck) {
      if (!updateAllThreadBreakpoints(true)) {
        consecutiveCoverageFailures += 1;
        if (consecutiveCoverageFailures >= 3 && !coverageWarningWritten) {
          writeStatus("DEGRADED", "THREAD_COVERAGE_INCOMPLETE", "one or more live threads were not covered");
          coverageWarningWritten = true;
        }
      } else {
        consecutiveCoverageFailures = 0;
        if (coverageWarningWritten) {
          if (loginObservationFresh(now)) {
            writeStatus("HEALTHY", "THREAD_COVERAGE_RESTORED", "all live threads are covered again");
          } else {
            writeLoginHealth(now, "all live threads are covered again");
          }
          coverageWarningWritten = false;
        }
      }
      nextCoverageCheck = now + 250;
    }
    if (g_pipeOutput && InterlockedCompareExchange(&g_running, 1, 1) != 0 && now >= nextHealthReport) {
      writeLoginHealth(
          now,
          g_testMode ? "test receipt and account observer active" : "Weixin receipt parser and account observer active");
      nextHealthReport = now + 5'000;
    }
#endif
    if (InterlockedCompareExchange(&g_outputBroken, 0, 0) != 0) requestAsyncStop();
  }
  return 0;
}

// 按线程 ID 查已登记的线程状态。
ThreadState* findThreadState(DWORD threadId) {
  for (auto& state : g_threads) {
    if (static_cast<DWORD>(InterlockedCompareExchange(&state.threadId, 0, 0)) == threadId) return &state;
  }
  return nullptr;
}

// 登记新线程并返回其状态槽；槽位用满后不再登记，避免无界增长。
ThreadState* registerThreadState(DWORD threadId) {
  if (auto* existing = findThreadState(threadId)) return existing;
  for (auto& state : g_threads) {
    if (InterlockedCompareExchange(&state.threadId, static_cast<LONG>(threadId), 0) == 0) return &state;
  }
  return nullptr;
}

// 为单个线程设置或清除四个硬件断点（DR0-DR3）。
// 只改调试寄存器，不写任何代码页，因此不触发微信的代码完整性检查。
bool updateThreadBreakpoint(DWORD threadId, bool enable) {
  if (threadId == GetCurrentThreadId()) return true;
  ThreadState* state = enable ? registerThreadState(threadId) : findThreadState(threadId);
  if (enable && state == nullptr) return false;

  HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_QUERY_INFORMATION,
                             FALSE,
                             threadId);
  if (!thread) {
    if (GetLastError() == ERROR_INVALID_PARAMETER && state != nullptr) {
      InterlockedExchange(&state->threadId, 0);
      return true;
    }
    return false;
  }

  bool success = false;
  const DWORD suspendResult = SuspendThread(thread);
  if (suspendResult != static_cast<DWORD>(-1)) {
    CONTEXT context{};
    context.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    if (GetThreadContext(thread, &context)) {
      if (enable) {
#ifdef CFPAY_DIAGNOSTIC_BUILD
        context.Dr0 = reinterpret_cast<DWORD64>(g_hookAddress);
        context.Dr1 = reinterpret_cast<DWORD64>(g_accountHookAddress);
        context.Dr2 = reinterpret_cast<DWORD64>(g_nicknameHookAddress);
        context.Dr3 = reinterpret_cast<DWORD64>(g_diagnosticFourthHookAddress);
        context.Dr7 &= ~(kSlot0EnableMask | kSlot0ControlMask | kSlot1EnableMask | kSlot1ControlMask |
                         kSlot2EnableMask | kSlot2ControlMask | kSlot3EnableMask | kSlot3ControlMask);
        context.Dr7 |= 0x55;
#else
        context.Dr0 = reinterpret_cast<DWORD64>(g_hookAddress);
        context.Dr2 = reinterpret_cast<DWORD64>(g_accountHookAddress);
        context.Dr7 &= ~(kSlot0EnableMask | kSlot0ControlMask);
        context.Dr7 &= ~(kSlot2EnableMask | kSlot2ControlMask);
        context.Dr3 = reinterpret_cast<DWORD64>(g_nicknameHookAddress);
        context.Dr7 |= 0x51;
#endif
      } else {
#ifdef CFPAY_DIAGNOSTIC_BUILD
        if (context.Dr0 == reinterpret_cast<DWORD64>(g_hookAddress)) context.Dr0 = 0;
        if (context.Dr1 == reinterpret_cast<DWORD64>(g_accountHookAddress)) context.Dr1 = 0;
        if (context.Dr2 == reinterpret_cast<DWORD64>(g_nicknameHookAddress)) context.Dr2 = 0;
        if (context.Dr3 == reinterpret_cast<DWORD64>(g_diagnosticFourthHookAddress)) context.Dr3 = 0;
        context.Dr7 &= ~(kSlot0EnableMask | kSlot0ControlMask | kSlot1EnableMask | kSlot1ControlMask |
                         kSlot2EnableMask | kSlot2ControlMask | kSlot3EnableMask | kSlot3ControlMask);
#else
        if (context.Dr0 == reinterpret_cast<DWORD64>(g_hookAddress)) {
          context.Dr0 = 0;
          context.Dr7 &= ~(kSlot0EnableMask | kSlot0ControlMask);
        }
        if (state != nullptr && context.Dr1 == state->returnAddress) {
          context.Dr1 = 0;
          context.Dr7 &= ~(kSlot1EnableMask | kSlot1ControlMask);
        }
        if (context.Dr2 == reinterpret_cast<DWORD64>(g_accountHookAddress)) {
          context.Dr2 = 0;
          context.Dr7 &= ~(kSlot2EnableMask | kSlot2ControlMask);
        }
        if (context.Dr3 == reinterpret_cast<DWORD64>(g_nicknameHookAddress)) {
          context.Dr3 = 0;
          context.Dr7 &= ~(kSlot3EnableMask | kSlot3ControlMask);
        }
        if (state != nullptr && context.Dr2 == state->accountReturnAddress) {
          context.Dr2 = 0;
          context.Dr7 &= ~(kSlot2EnableMask | kSlot2ControlMask);
        }
        if (state != nullptr && context.Dr3 == state->nicknameReturnAddress) {
          context.Dr3 = 0;
          context.Dr7 &= ~(kSlot3EnableMask | kSlot3ControlMask);
        }
#endif
      }
      context.Dr6 = 0;
      success = SetThreadContext(thread, &context) != FALSE;
    }
    ResumeThread(thread);
  } else {
    DWORD exitCode = STILL_ACTIVE;
    if (GetExitCodeThread(thread, &exitCode) != FALSE && exitCode != STILL_ACTIVE) success = true;
  }
  CloseHandle(thread);
  if (enable && state != nullptr && (!success || suspendResult == static_cast<DWORD>(-1))) {
    InterlockedExchange(&state->threadId, 0);
  }
  return success;
}

// 遍历目标进程的所有线程批量设置/清除断点，返回是否全部成功。
bool updateAllThreadBreakpoints(bool enable) {
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
  if (snapshot == INVALID_HANDLE_VALUE) return false;

  THREADENTRY32 entry{};
  entry.dwSize = sizeof(entry);
  bool sawTargetThread = false;
  bool success = true;
  if (Thread32First(snapshot, &entry)) {
    do {
      if (entry.th32OwnerProcessID == GetCurrentProcessId() && entry.th32ThreadID != GetCurrentThreadId()) {
        sawTargetThread = true;
        if (enable && findThreadState(entry.th32ThreadID) != nullptr) continue;
        if (!enable && findThreadState(entry.th32ThreadID) == nullptr) continue;
        if (!updateThreadBreakpoint(entry.th32ThreadID, enable)) success = false;
      }
    } while (Thread32Next(snapshot, &entry));
  } else {
    success = false;
  }
  CloseHandle(snapshot);
  return sawTargetThread && success;
}

// 向量化异常处理器：只处理单步异常，按命中的断点地址分派到收款/账号/昵称采集逻辑。
// 处理完清除单步标志并恢复执行，微信自身感知不到断点存在；不是单步异常一律交给后续处理器。
LONG CALLBACK vectoredHandler(PEXCEPTION_POINTERS exception) {
  if (exception->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP) return EXCEPTION_CONTINUE_SEARCH;

  CONTEXT* context = exception->ContextRecord;
  ThreadState* state = findThreadState(GetCurrentThreadId());
  if (state == nullptr) return EXCEPTION_CONTINUE_SEARCH;

  const bool slot0 = (context->Dr6 & 0x1) != 0;
  const bool slot1 = (context->Dr6 & 0x2) != 0;
  const bool slot2 = (context->Dr6 & 0x4) != 0;
  const bool slot3 = (context->Dr6 & 0x8) != 0;
  bool handled = false;

#ifdef CFPAY_DIAGNOSTIC_BUILD
  if (slot0 && exception->ExceptionRecord->ExceptionAddress == g_hookAddress) {
    InterlockedIncrement64(&g_newSyncEntryCount);
#ifndef CFPAY_WMPF_DIAGNOSTIC_BUILD
    captureSessionPreviewShape(static_cast<std::uintptr_t>(context->Rdx));
    std::uintptr_t returnAddress = 0;
    if (readMemory(reinterpret_cast<const void*>(context->Rsp), &returnAddress, sizeof(returnAddress))) {
      dumpSessionModelSlots(static_cast<std::uintptr_t>(context->Rdx),
                            returnAddress,
                            static_cast<std::int32_t>(InterlockedCompareExchange64(&g_newSyncEntryCount, 0, 0)));
      classifyDiagnosticCallerSummary(static_cast<std::uintptr_t>(context->Rdx), returnAddress);
      trackDiagnosticViewModel(
          static_cast<std::uintptr_t>(context->Rcx), static_cast<std::uintptr_t>(context->Rdx), returnAddress);
    } else {
      InterlockedIncrement64(&g_readFailureCount);
    }
#endif
    handled = true;
  }
  if (slot1 && exception->ExceptionRecord->ExceptionAddress == g_accountHookAddress) {
    InterlockedIncrement64(&g_newSyncReturnCount);
    handled = true;
  }
  if (slot2 && exception->ExceptionRecord->ExceptionAddress == g_nicknameHookAddress) {
    InterlockedIncrement64(&g_accountEntryCount);
#ifdef CFPAY_WMPF_DIAGNOSTIC_BUILD
    EventRecord event{};
    event.kind = EventKind::DiagnosticEventName;
    event.valid = readDiagnosticEventName(
        static_cast<std::uintptr_t>(context->Rdx), event.diagnosticEventName, sizeof(event.diagnosticEventName));
    event.capturedAt = unixTimeMilliseconds();
    if (event.valid) {
      enqueueEvent(event);
    } else {
      InterlockedIncrement64(&g_readFailureCount);
    }
#else
    classifyDiagnosticServiceSummary(static_cast<std::uintptr_t>(context->Rdx));
#endif
    handled = true;
  }
  if (slot3 && exception->ExceptionRecord->ExceptionAddress == g_diagnosticFourthHookAddress) {
    InterlockedIncrement64(&g_accountReturnCount);
    handled = true;
  }
#else

  if (slot0 && exception->ExceptionRecord->ExceptionAddress == g_hookAddress) {
    if (g_testMode) {
      std::uintptr_t returnAddress = 0;
      if (readMemory(reinterpret_cast<const void*>(context->Rsp), &returnAddress, sizeof(returnAddress)) &&
          returnAddress != 0) {
        state->outputAddress = static_cast<std::uintptr_t>(context->Rcx);
        state->returnAddress = returnAddress;
        InterlockedExchange(&state->returnArmed, 1);
        context->Dr1 = returnAddress;
        context->Dr7 &= ~(kSlot1EnableMask | kSlot1ControlMask);
        context->Dr7 |= 0x4;
      } else {
        InterlockedIncrement64(&g_readFailureCount);
      }
    } else {
      InterlockedIncrement64(&g_newSyncEntryCount);
      std::uintptr_t returnAddress = 0;
      if (readMemory(reinterpret_cast<const void*>(context->Rsp), &returnAddress, sizeof(returnAddress)) &&
          returnAddress != 0) {
        state->outputAddress = static_cast<std::uintptr_t>(context->Rcx);
        state->returnAddress = returnAddress;
        InterlockedExchange(&state->returnArmed, 1);
        context->Dr1 = returnAddress;
        context->Dr7 &= ~(kSlot1EnableMask | kSlot1ControlMask);
        context->Dr7 |= 0x4;
      } else {
        InterlockedIncrement64(&g_readFailureCount);
      }
    }
    handled = true;
  }
#endif

#ifndef CFPAY_DIAGNOSTIC_BUILD
  if (slot1 && !g_testMode && InterlockedCompareExchange(&state->returnArmed, 1, 1) == 1 &&
      reinterpret_cast<std::uintptr_t>(exception->ExceptionRecord->ExceptionAddress) == state->returnAddress) {
    captureNewSyncResponse(state->outputAddress, unixTimeMilliseconds());
    InterlockedIncrement64(&g_newSyncReturnCount);
    context->Dr1 = 0;
    context->Dr7 &= ~(kSlot1EnableMask | kSlot1ControlMask);
    state->returnAddress = 0;
    state->outputAddress = 0;
    InterlockedExchange(&state->returnArmed, 0);
    handled = true;
  }
#endif

  if (slot1 && g_testMode && InterlockedCompareExchange(&state->returnArmed, 1, 1) == 1 &&
      reinterpret_cast<std::uintptr_t>(exception->ExceptionRecord->ExceptionAddress) == state->returnAddress) {
    const EventRecord payment = captureEvent(state->outputAddress);
    InterlockedIncrement64(&g_newSyncReturnCount);
    if (!payment.valid) InterlockedIncrement64(&g_readFailureCount);
    enqueueEvent(payment);
    context->Dr1 = 0;
    context->Dr7 &= ~(kSlot1EnableMask | kSlot1ControlMask);
    state->returnAddress = 0;
    state->outputAddress = 0;
    InterlockedExchange(&state->returnArmed, 0);
    handled = true;
  }

  if (slot2 && exception->ExceptionRecord->ExceptionAddress == g_accountHookAddress &&
      InterlockedCompareExchange(&state->accountReturnArmed, 1, 1) == 0) {
    InterlockedIncrement64(&g_accountEntryCount);
    std::uintptr_t returnAddress = 0;
    if (readMemory(reinterpret_cast<const void*>(context->Rsp), &returnAddress, sizeof(returnAddress)) &&
        returnAddress != 0) {
      state->accountOutputAddress = static_cast<std::uintptr_t>(context->Rdx);
      state->accountReturnAddress = returnAddress;
      InterlockedExchange(&state->accountReturnArmed, 1);
      context->Dr2 = returnAddress;
      context->Dr7 &= ~(kSlot2EnableMask | kSlot2ControlMask);
      context->Dr7 |= 0x10;
    } else {
      InterlockedIncrement64(&g_readFailureCount);
    }
    handled = true;
  }

  if (slot2 && InterlockedCompareExchange(&state->accountReturnArmed, 1, 1) == 1 &&
      reinterpret_cast<std::uintptr_t>(exception->ExceptionRecord->ExceptionAddress) == state->accountReturnAddress) {
    EventRecord account = captureAccount(state->accountOutputAddress);
    InterlockedIncrement64(&g_accountReturnCount);
    bool shouldEnqueue = !account.valid;
    if (!account.valid) {
      InterlockedIncrement64(&g_readFailureCount);
    } else if (isLoggedInAccount(account)) {
      InterlockedExchange(&g_consecutiveLoggedOutObservations, 0);
      const LONG previousState = InterlockedExchange(&g_loginState, 1);
      const LONG64 previousObservedAt =
          InterlockedExchange64(&g_lastLoggedInObservationAt, static_cast<LONG64>(account.capturedAt));
      const std::uint64_t fingerprint = accountFingerprint(account);
      const LONG64 previousFingerprint =
          InterlockedExchange64(&g_lastAccountFingerprint, static_cast<LONG64>(fingerprint));
      const bool observationChanged = previousObservedAt <= 0 ||
                                      account.capturedAt < static_cast<std::uint64_t>(previousObservedAt);
      account.loginStateChanged = previousState != 1 || observationChanged;
      shouldEnqueue = previousState != 1 || previousFingerprint != static_cast<LONG64>(fingerprint) ||
                      observationChanged;
    } else {
      const LONG previousState = InterlockedCompareExchange(&g_loginState, 0, 0);
      const LONG consecutive = InterlockedIncrement(&g_consecutiveLoggedOutObservations);
      if (previousState == 1 && consecutive < kLoggedOutConfirmationCount) {
        account.loginStateChanged = false;
        shouldEnqueue = false;
      } else {
        InterlockedExchange(&g_loginState, 0);
        InterlockedExchange64(&g_lastLoggedInObservationAt, 0);
        InterlockedExchange64(&g_lastAccountFingerprint, 0);
        account.loginStateChanged = previousState != 0;
        shouldEnqueue = previousState != 0;
      }
    }
    if (shouldEnqueue) {
      enqueueEvent(account);
    }
    context->Dr2 = reinterpret_cast<DWORD64>(g_accountHookAddress);
    context->Dr7 &= ~(kSlot2EnableMask | kSlot2ControlMask);
    context->Dr7 |= 0x10;
    state->accountReturnAddress = 0;
    state->accountOutputAddress = 0;
    InterlockedExchange(&state->accountReturnArmed, 0);
    handled = true;
  }

  if (slot3 && exception->ExceptionRecord->ExceptionAddress == g_nicknameHookAddress &&
      InterlockedCompareExchange(&state->nicknameReturnArmed, 1, 1) == 0) {
    InterlockedIncrement64(&g_nicknameEntryCount);
    std::uintptr_t returnAddress = 0;
    if (readMemory(reinterpret_cast<const void*>(context->Rsp), &returnAddress, sizeof(returnAddress)) &&
        returnAddress != 0) {
      state->nicknameOutputAddress = static_cast<std::uintptr_t>(context->Rdx) + 0x28;
      state->nicknameReturnAddress = returnAddress;
      InterlockedExchange(&state->nicknameReturnArmed, 1);
      context->Dr3 = returnAddress;
      context->Dr7 &= ~(kSlot3EnableMask | kSlot3ControlMask);
      context->Dr7 |= 0x40;
    } else {
      InterlockedIncrement64(&g_readFailureCount);
    }
    handled = true;
  }

  if (slot3 && InterlockedCompareExchange(&state->nicknameReturnArmed, 1, 1) == 1 &&
      reinterpret_cast<std::uintptr_t>(exception->ExceptionRecord->ExceptionAddress) == state->nicknameReturnAddress) {
    const EventRecord nickname = captureNickname(state->nicknameOutputAddress);
    InterlockedIncrement64(&g_nicknameReturnCount);
    if (!nickname.valid) InterlockedIncrement64(&g_readFailureCount);
    if (!nickname.valid || nickname.accountNickname[0] != '\0') enqueueEvent(nickname);
    context->Dr3 = reinterpret_cast<DWORD64>(g_nicknameHookAddress);
    context->Dr7 &= ~(kSlot3EnableMask | kSlot3ControlMask);
    context->Dr7 |= 0x40;
    state->nicknameReturnAddress = 0;
    state->nicknameOutputAddress = 0;
    InterlockedExchange(&state->nicknameReturnArmed, 0);
    handled = true;
  }

  if (!handled) return EXCEPTION_CONTINUE_SEARCH;
  context->Dr6 = 0;
  context->EFlags |= kResumeFlag;
  return EXCEPTION_CONTINUE_EXECUTION;
}

// 打开宿主创建的就绪事件，告知宿主“断点已安装完成”，宿主据此开始读管道。
void signalReady() {
  wchar_t eventName[96] = {};
  ::swprintf(eventName, L"Local\\CfpayHookTestReady-%lu", GetCurrentProcessId());
  HANDLE event = OpenEventW(EVENT_MODIFY_STATE, FALSE, eventName);
  if (event) {
    SetEvent(event);
    CloseHandle(event);
  }
}

// 安装流程：连接输出通道、校验内存签名、注册异常处理器、为所有线程装断点、启动工作线程，最后置就绪事件。
// 任何一步失败都会先清理已装的部分再上报错误，不留半安装状态。
DWORD WINAPI bootstrapMain(void*) {
  if (InterlockedCompareExchange(&g_lifecycle, kLifecycleStarting, kLifecycleStopped) != kLifecycleStopped) return 4;
  InterlockedExchange(&g_running, 0);
  InterlockedExchange(&g_stopResult, FALSE);
  InterlockedExchange(&g_outputBroken, 0);
  InterlockedExchange64(&g_lastAccountFingerprint, 0);
  InterlockedExchange64(&g_lastLoggedInObservationAt, 0);
  InterlockedExchange64(&g_observerStartedAt, static_cast<LONG64>(unixTimeMilliseconds()));
  InterlockedExchange(&g_loginState, -1);
  InterlockedExchange(&g_consecutiveLoggedOutObservations, 0);
  InterlockedExchange64(&g_newSyncEntryCount, 0);
  InterlockedExchange64(&g_newSyncReturnCount, 0);
  InterlockedExchange64(&g_addMsgCommandCount, 0);
  InterlockedExchange64(&g_accountEntryCount, 0);
  InterlockedExchange64(&g_accountReturnCount, 0);
  InterlockedExchange64(&g_nicknameEntryCount, 0);
  InterlockedExchange64(&g_nicknameReturnCount, 0);
  InterlockedExchange64(&g_payMessageScanCount, 0);
  InterlockedExchange64(&g_payMessageCount, 0);
  InterlockedExchange64(&g_readFailureCount, 0);
  InterlockedExchange64(&g_queueDropCount, 0);
#if defined(CFPAY_DIAGNOSTIC_BUILD) && !defined(CFPAY_WMPF_DIAGNOSTIC_BUILD)
  for (std::size_t index = 0; index < kDiagnosticTextSlotCount; index += 1) {
    InterlockedExchange64(&g_diagnosticTextReadable[index], 0);
    InterlockedExchange64(&g_diagnosticTextBytes[index], 0);
    InterlockedExchange64(&g_diagnosticReceiptKeywords[index], 0);
    InterlockedExchange64(&g_diagnosticPaymentKeywords[index], 0);
    InterlockedExchange64(&g_diagnosticAmountMarkers[index], 0);
    InterlockedExchange64(&g_diagnosticDecimalAmounts[index], 0);
  }
  InterlockedExchange64(&g_diagnosticRichRunsReadable, 0);
  InterlockedExchange64(&g_diagnosticRichRunCount, 0);
  InterlockedExchange64(&g_diagnosticRichRunBytes, 0);
  InterlockedExchange64(&g_diagnosticRichRunReceiptKeywords, 0);
  InterlockedExchange64(&g_diagnosticRichRunPaymentKeywords, 0);
  InterlockedExchange64(&g_diagnosticRichRunAmountMarkers, 0);
  InterlockedExchange64(&g_diagnosticRichRunDecimalAmounts, 0);
  InterlockedExchange64(&g_diagnosticServiceSummaryReadable, 0);
  InterlockedExchange64(&g_diagnosticServiceSummaryBytes, 0);
  InterlockedExchange64(&g_diagnosticServiceSummaryReceiptKeywords, 0);
  InterlockedExchange64(&g_diagnosticServiceSummaryPaymentKeywords, 0);
  InterlockedExchange64(&g_diagnosticServiceSummaryAmountMarkers, 0);
  InterlockedExchange64(&g_diagnosticServiceSummaryDecimalAmounts, 0);
  for (std::size_t bucket = 0; bucket < kDiagnosticCallerBucketCount; bucket += 1) {
    InterlockedExchange64(&g_diagnosticCallerHits[bucket], 0);
    InterlockedExchange64(&g_diagnosticCallerBytes[bucket], 0);
    InterlockedExchange64(&g_diagnosticCallerReceiptKeywords[bucket], 0);
    InterlockedExchange64(&g_diagnosticCallerPaymentKeywords[bucket], 0);
    InterlockedExchange64(&g_diagnosticCallerAmountMarkers[bucket], 0);
    InterlockedExchange64(&g_diagnosticCallerDecimalAmounts[bucket], 0);
  }
  InterlockedExchange64(&g_diagnosticHiddenPreviewCount, 0);
#endif
  g_testMode = false;
  g_pipeOutput = false;
  g_hookAddress = nullptr;
  g_weixinModuleBase = 0;
  g_accountHookAddress = nullptr;
  g_nicknameHookAddress = nullptr;
#ifdef CFPAY_DIAGNOSTIC_BUILD
  g_diagnosticFourthHookAddress = nullptr;
#endif
  std::memset(g_threads, 0, sizeof(g_threads));
  std::memset(g_queue, 0, sizeof(g_queue));

  if (!configureOutputAndTarget()) {
    signalReady();
    if (g_outputFile != INVALID_HANDLE_VALUE) {
      CloseHandle(g_outputFile);
      g_outputFile = INVALID_HANDLE_VALUE;
    }
    InterlockedExchange(&g_lifecycle, kLifecycleStopped);
    return 1;
  }

  g_queueEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  g_veh = AddVectoredExceptionHandler(1, vectoredHandler);
  InterlockedExchange(&g_running, 1);
  const bool breakpointsInstalled = g_hookAddress != nullptr && g_accountHookAddress != nullptr &&
                                    g_nicknameHookAddress != nullptr &&
#ifdef CFPAY_DIAGNOSTIC_BUILD
                                    g_diagnosticFourthHookAddress != nullptr &&
#endif
                                    g_queueEvent != nullptr && g_veh != nullptr && updateAllThreadBreakpoints(true);
  if (!breakpointsInstalled) {
    updateAllThreadBreakpoints(false);
    writeStatus("ERROR", "HOOK_INSTALL_FAILED", "hardware breakpoint installation failed");
    signalReady();
    if (g_veh != nullptr) {
      RemoveVectoredExceptionHandler(g_veh);
      g_veh = nullptr;
    }
    if (g_queueEvent != nullptr) {
      CloseHandle(g_queueEvent);
      g_queueEvent = nullptr;
    }
    if (g_outputFile != INVALID_HANDLE_VALUE) {
      CloseHandle(g_outputFile);
      g_outputFile = INVALID_HANDLE_VALUE;
    }
    InterlockedExchange(&g_running, 0);
    InterlockedExchange(&g_lifecycle, kLifecycleStopped);
    return 2;
  }

  g_workerThread = CreateThread(nullptr, 0, workerMain, nullptr, 0, nullptr);
  if (g_workerThread == nullptr) {
    updateAllThreadBreakpoints(false);
    RemoveVectoredExceptionHandler(g_veh);
    g_veh = nullptr;
    writeStatus("ERROR", "HOOK_INSTALL_FAILED", "observer worker thread creation failed");
    signalReady();
    CloseHandle(g_outputFile);
    g_outputFile = INVALID_HANDLE_VALUE;
    CloseHandle(g_queueEvent);
    g_queueEvent = nullptr;
    InterlockedExchange(&g_running, 0);
    InterlockedExchange(&g_lifecycle, kLifecycleStopped);
    return 3;
  }

  InterlockedExchange(&g_lifecycle, kLifecycleRunning);
#ifdef CFPAY_DIAGNOSTIC_BUILD
#ifdef CFPAY_WMPF_DIAGNOSTIC_BUILD
  writeStatus("HEALTHY", "DIAGNOSTIC_ACTIVE", "WMPF browser diagnostic breakpoints installed");
#else
  writeStatus("HEALTHY", "DIAGNOSTIC_ACTIVE", "会话摘要诊断断点已安装");
#endif
#else
  writeLoginHealth(
      unixTimeMilliseconds(),
      g_testMode ? "receipt and account breakpoints installed in test target"
                 : "Weixin NewSync payment observer active");
#endif
  signalReady();
  return 0;
}

// 停止流程：清除所有线程的断点并摘除异常处理器；DLL 保持惰性加载，不做动态卸载。
DWORD WINAPI stopMain(void*) {
  const bool cleared = updateAllThreadBreakpoints(false);
  if (g_veh != nullptr) {
    RemoveVectoredExceptionHandler(g_veh);
    g_veh = nullptr;
  }
  InterlockedExchange(&g_running, 0);
  if (g_queueEvent != nullptr) SetEvent(g_queueEvent);
  if (g_workerThread != nullptr) WaitForSingleObject(g_workerThread, 5000);
  writeStatus(cleared ? "STOPPED" : "ERROR", cleared ? "HOOK_STOPPED" : "HOOK_STOP_FAILED", cleared
                                                                                                    ? "hardware breakpoints cleared; DLL remains loaded"
                                                                                                    : "one or more thread breakpoints could not be cleared");
  if (g_outputFile != INVALID_HANDLE_VALUE) {
    CloseHandle(g_outputFile);
    g_outputFile = INVALID_HANDLE_VALUE;
  }
  if (g_workerThread != nullptr) {
    CloseHandle(g_workerThread);
    g_workerThread = nullptr;
  }
  if (g_queueEvent != nullptr) {
    CloseHandle(g_queueEvent);
    g_queueEvent = nullptr;
  }
  InterlockedExchange(&g_stopResult, cleared ? TRUE : FALSE);
  InterlockedExchange(&g_lifecycle, kLifecycleStopped);
  return 0;
}

}  // namespace

extern "C" __declspec(dllexport) BOOL WINAPI CfpayObserverStop() {
  bool waitedForCleanup = false;
  for (int attempt = 0; attempt < 300; attempt += 1) {
    const LONG lifecycle = InterlockedCompareExchange(&g_lifecycle, kLifecycleStopped, kLifecycleStopped);
    if (lifecycle == kLifecycleStopped) {
      return waitedForCleanup ? InterlockedCompareExchange(&g_stopResult, FALSE, FALSE) != FALSE : TRUE;
    }
    if (lifecycle == kLifecycleRunning &&
        InterlockedCompareExchange(&g_lifecycle, kLifecycleStopping, kLifecycleRunning) == kLifecycleRunning) {
      HANDLE cleanup = CreateThread(nullptr, 0, stopMain, nullptr, 0, nullptr);
      if (cleanup == nullptr) {
        InterlockedExchange(&g_lifecycle, kLifecycleRunning);
        return FALSE;
      }
      const DWORD waitResult = WaitForSingleObject(cleanup, 10'000);
      CloseHandle(cleanup);
      return waitResult == WAIT_OBJECT_0 && InterlockedCompareExchange(&g_stopResult, FALSE, FALSE) != FALSE;
    }
    if (lifecycle == kLifecycleStopping) waitedForCleanup = true;
    Sleep(50);
  }
  return FALSE;
}

extern "C" __declspec(dllexport) BOOL WINAPI CfpayObserverStart() {
  for (int attempt = 0; attempt < 300; attempt += 1) {
    const LONG lifecycle = InterlockedCompareExchange(&g_lifecycle, kLifecycleStopped, kLifecycleStopped);
    if (lifecycle == kLifecycleRunning) return FALSE;
    if (lifecycle == kLifecycleStopped) return bootstrapMain(nullptr) == 0;
    Sleep(50);
  }
  return FALSE;
}

extern "C" __declspec(dllexport) DWORD WINAPI CfpayObserverApiVersion() {
  return 0x000a0000;
}

// DLL 入口：只在进程附加时创建工作线程，其余原因直接返回，避免在加载器锁里做重活。
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    DisableThreadLibraryCalls(instance);
    HANDLE bootstrap = CreateThread(nullptr, 0, bootstrapMain, nullptr, 0, nullptr);
    if (bootstrap != nullptr) CloseHandle(bootstrap);
  }
  return TRUE;
}
