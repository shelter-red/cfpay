#include <windows.h>
#include <tlhelp32.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "wechat_target.h"

namespace {

constexpr std::size_t kMaxThreadStates = 512;
constexpr std::size_t kQueueSize = 128;
constexpr std::size_t kMaxRecordedFrames = 16;
constexpr std::size_t kMaxWalkFrames = 32;
constexpr std::size_t kMaxCommandIds = 16;
constexpr std::uint64_t kInitialCoverageIntervalMs = 25;
constexpr std::uint64_t kSteadyCoverageIntervalMs = 250;
constexpr DWORD64 kResumeFlag = 0x10000;
constexpr DWORD64 kSlot0EnableMask = 0x3;
constexpr DWORD64 kSlot0ControlMask = static_cast<DWORD64>(0xf) << 16;
constexpr DWORD64 kSlot1EnableMask = 0xc;
constexpr DWORD64 kSlot1ControlMask = static_cast<DWORD64>(0xf) << 20;
constexpr DWORD64 kSlot2EnableMask = 0x30;
constexpr DWORD64 kSlot2ControlMask = static_cast<DWORD64>(0xf) << 24;
constexpr DWORD64 kSlot3EnableMask = 0xc0;
constexpr DWORD64 kSlot3ControlMask = static_cast<DWORD64>(0xf) << 28;
constexpr std::uint32_t kNewSyncResponseParseRva = cfpay::target::kNewSyncParseRva;
constexpr std::uint32_t kNewSyncResponseParseReturnRva = 0x680fcc;
constexpr std::uint32_t kNewSyncResponseVtableRva = 0x8fc0d48;
constexpr std::uint32_t kAddMsgParseRva = 0x1072ab0;
constexpr std::uint32_t kAddMsgVtableRva = 0x8fc0f38;
constexpr std::array<std::uint8_t, 32> kNewSyncResponseParseSignature = {
    0x55, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x56, 0x57, 0x53, 0x48, 0x83, 0xec, 0x48,
    0x48, 0x8d, 0x6c, 0x24, 0x40, 0x48, 0xc7, 0x45, 0x00, 0xfe, 0xff, 0xff, 0xff, 0x48, 0x89, 0xd6,
};
constexpr std::array<std::uint8_t, 32> kAddMsgParseSignature = {
    0x55, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x56, 0x57, 0x53, 0x48, 0x83, 0xec, 0x38,
    0x48, 0x8d, 0x6c, 0x24, 0x30, 0x48, 0xc7, 0x45, 0x00, 0xfe, 0xff, 0xff, 0xff, 0x48, 0x89, 0xd6,
};

struct ThreadState {
  volatile LONG threadId;
  volatile LONG64 pendingResponse;
  volatile LONG64 pendingAddMsg;
  volatile LONG64 addMsgReturn;
};

struct EventRecord {
  const char* type;
  std::uint64_t sequence;
  std::uint64_t parseCount;
  DWORD threadId;
  std::uint64_t callerRva;
  bool insideTargetModule;
  std::array<std::uint64_t, kMaxRecordedFrames> callChainRvas;
  std::uint32_t callChainDepth;
  std::uint32_t walkedFrames;
  bool unwindComplete;
  bool parseSucceeded;
  bool layoutValid;
  std::uint8_t presentMask;
  std::int32_t retCode;
  std::uint32_t continueFlag;
  std::uint32_t status;
  std::uint32_t onlineVersion;
  std::uint32_t serverTime;
  std::int32_t commandCount;
  std::int32_t declaredCommandCount;
  std::array<std::int32_t, kMaxCommandIds> commandIds;
  std::uint32_t commandIdCount;
  bool commandIdsTruncated;
  bool addMsgLayoutValid;
  std::uint8_t addMsgPresentMask;
  std::int32_t addMsgType;
};

struct UnwindResult {
  std::array<std::uint64_t, kMaxRecordedFrames> callChainRvas{};
  std::uint32_t callChainDepth = 0;
  std::uint32_t walkedFrames = 0;
  bool complete = false;
};

struct QueueSlot {
  volatile LONG state;
  EventRecord event;
};

struct NewSyncResponseLayout {
  std::uintptr_t vtable;
  std::uintptr_t commandList;
  std::int32_t retCode;
  std::uint32_t continueFlag;
  std::uintptr_t keyBuffer;
  std::uint32_t status;
  std::uint32_t onlineVersion;
  std::uint32_t serverTime;
  std::uint32_t cachedSize;
  std::uint8_t presentMask;
  std::uint8_t reserved[7];
};

struct NewSyncCommandListLayout {
  std::uintptr_t vtable;
  std::uintptr_t items;
  std::int32_t itemCount;
  std::int32_t allocatedItems;
  std::int32_t itemCapacity;
  std::uint32_t reserved;
  std::int32_t declaredCount;
  std::uint32_t cachedSize;
  std::uint8_t presentMask;
  std::uint8_t tail[7];
};

struct NewSyncCommandLayout {
  std::uintptr_t vtable;
  std::uintptr_t payload;
  std::int32_t commandId;
  std::uint32_t cachedSize;
  std::uint8_t presentMask;
  std::uint8_t tail[7];
};

struct AddMsgLayout {
  std::uintptr_t vtable;
  std::uintptr_t field1;
  std::uintptr_t field3;
  std::int32_t messageType;
  std::uint32_t reserved;
  std::uint8_t presentMask;
  std::uint8_t tail[3];
};

static_assert(sizeof(NewSyncResponseLayout) == 0x38);
static_assert(offsetof(NewSyncResponseLayout, commandList) == 0x08);
static_assert(offsetof(NewSyncResponseLayout, presentMask) == 0x30);
static_assert(sizeof(NewSyncCommandListLayout) == 0x30);
static_assert(offsetof(NewSyncCommandListLayout, itemCount) == 0x10);
static_assert(offsetof(NewSyncCommandListLayout, declaredCount) == 0x20);
static_assert(sizeof(NewSyncCommandLayout) == 0x20);
static_assert(offsetof(NewSyncCommandLayout, commandId) == 0x10);
static_assert(sizeof(AddMsgLayout) == 0x28);
static_assert(offsetof(AddMsgLayout, messageType) == 0x18);

constexpr LONG kLifecycleStopped = 0;
constexpr LONG kLifecycleStarting = 1;
constexpr LONG kLifecycleRunning = 2;
constexpr LONG kLifecycleStopping = 3;

PVOID g_veh = nullptr;
HANDLE g_outputFile = INVALID_HANDLE_VALUE;
HANDLE g_queueEvent = nullptr;
HANDLE g_workerThread = nullptr;
void* g_newSyncResponseParseAddress = nullptr;
void* g_newSyncResponseParseReturnAddress = nullptr;
void* g_addMsgParseAddress = nullptr;
std::uintptr_t g_targetModuleBase = 0;
std::uintptr_t g_targetModuleEnd = 0;
volatile LONG64 g_sequence = 0;
volatile LONG64 g_parseCount = 0;
volatile LONG64 g_addMsgCount = 0;
volatile LONG64 g_readFailureCount = 0;
volatile LONG64 g_unwindFailureCount = 0;
volatile LONG64 g_queueDropCount = 0;
volatile LONG g_running = 0;
volatile LONG g_lifecycle = kLifecycleStopped;
volatile LONG g_stopResult = FALSE;
volatile LONG g_outputBroken = 0;
bool g_pipeOutput = false;
bool g_testMode = false;
ThreadState g_threads[kMaxThreadStates] = {};
QueueSlot g_queue[kQueueSize] = {};

// 取 Unix 纪元毫秒。
std::uint64_t unixTimeMilliseconds() {
  FILETIME fileTime{};
  GetSystemTimeAsFileTime(&fileTime);
  ULARGE_INTEGER value{};
  value.LowPart = fileTime.dwLowDateTime;
  value.HighPart = fileTime.dwHighDateTime;
  return (value.QuadPart - 116444736000000000ULL) / 10000ULL;
}

// 向输出句柄写满指定字节数。
bool writeBytes(const char* data, DWORD size) {
  DWORD total = 0;
  while (total < size) {
    DWORD written = 0;
    if (!WriteFile(g_outputFile, data + total, size - total, &written, nullptr) || written == 0) return false;
    total += written;
  }
  return true;
}

// 写一行并立即刷新。
void writeLine(const std::string& line) {
  if (g_outputFile == INVALID_HANDLE_VALUE) return;
  static constexpr char newline[] = "\r\n";
  const bool success = writeBytes(line.data(), static_cast<DWORD>(line.size())) && writeBytes(newline, 2);
  if (!g_pipeOutput && success) FlushFileBuffers(g_outputFile);
  if (!success && g_pipeOutput) InterlockedExchange(&g_outputBroken, 1);
}

// 输出一行状态记录。
void writeStatus(const char* status, const char* detailCode, const std::string& message) {
  writeLine(
      "{\"type\":\"status\",\"status\":\"" + std::string(status) + "\",\"detailCode\":\"" +
      std::string(detailCode) + "\",\"message\":\"" + message + "\",\"observedAt\":" +
      std::to_string(unixTimeMilliseconds()) + "}");
}

// 在自身进程内读满指定字节数，读不满即失败。
bool readMemory(const void* address, void* output, std::size_t size) {
  SIZE_T bytesRead = 0;
  return address != nullptr && ReadProcessMemory(GetCurrentProcess(), address, output, size, &bytesRead) != FALSE &&
         bytesRead == size;
}

// 连接宿主创建的命名管道，带重试。
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

// 取目标模块的基址与结束地址，用于筛选落在模块内的返回地址。
bool moduleBounds(HMODULE module, std::uintptr_t* base, std::uintptr_t* end) {
  const auto moduleBase = reinterpret_cast<std::uintptr_t>(module);
  IMAGE_DOS_HEADER dos{};
  if (!readMemory(reinterpret_cast<const void*>(moduleBase), &dos, sizeof(dos)) || dos.e_magic != IMAGE_DOS_SIGNATURE) {
    return false;
  }
  IMAGE_NT_HEADERS64 headers{};
  if (!readMemory(reinterpret_cast<const void*>(moduleBase + static_cast<std::uintptr_t>(dos.e_lfanew)), &headers,
                  sizeof(headers)) ||
      headers.Signature != IMAGE_NT_SIGNATURE || headers.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
      headers.OptionalHeader.SizeOfImage == 0) {
    return false;
  }
  *base = moduleBase;
  *end = moduleBase + headers.OptionalHeader.SizeOfImage;
  return *end > *base;
}

// 连接输出管道、解析目标模块基址并校验签名，再算出断点地址。
bool configureOutputAndTarget() {
  wchar_t value[32768] = {};
  if (GetEnvironmentVariableW(L"CFPAY_HOOK_TEST_OUTPUT", value, 32768) > 0) {
    g_testMode = true;
    g_outputFile = CreateFileW(value, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    HMODULE mainModule = GetModuleHandleW(nullptr);
    g_newSyncResponseParseAddress = reinterpret_cast<void*>(GetProcAddress(mainModule, "CfpayTestNewSyncResponseParse"));
    g_newSyncResponseParseReturnAddress =
        reinterpret_cast<void*>(GetProcAddress(mainModule, "CfpayTestNewSyncResponseParseReturn"));
    g_addMsgParseAddress = reinterpret_cast<void*>(GetProcAddress(mainModule, "CfpayTestAddMsgParse"));
    return g_outputFile != INVALID_HANDLE_VALUE && g_newSyncResponseParseAddress != nullptr &&
           g_newSyncResponseParseReturnAddress != nullptr &&
           g_addMsgParseAddress != nullptr &&
           moduleBounds(mainModule, &g_targetModuleBase, &g_targetModuleEnd);
  }

  g_testMode = false;
  wchar_t pipeName[128] = {};
  ::swprintf(pipeName, L"\\\\.\\pipe\\cfpay-wechat-observer-%lu", GetCurrentProcessId());
  if (!connectNamedPipe(pipeName)) return false;

  HMODULE weixin = GetModuleHandleW(L"Weixin.dll");
  if (weixin == nullptr || !moduleBounds(weixin, &g_targetModuleBase, &g_targetModuleEnd)) {
    writeStatus("STOPPED", "PROCESS_NOT_FOUND", "目标进程未加载固定版本 Weixin.dll");
    return false;
  }
  std::array<std::uint8_t, kNewSyncResponseParseSignature.size()> signature{};
  if (!readMemory(reinterpret_cast<const void*>(g_targetModuleBase + kNewSyncResponseParseRva), signature.data(),
                  signature.size()) ||
      signature != kNewSyncResponseParseSignature) {
    writeStatus("UNSUPPORTED", "SIGNATURE_MISMATCH", "NewSyncResponse解析入口签名不匹配");
    return false;
  }
  g_newSyncResponseParseAddress = reinterpret_cast<void*>(g_targetModuleBase + kNewSyncResponseParseRva);
  g_newSyncResponseParseReturnAddress = reinterpret_cast<void*>(g_targetModuleBase + kNewSyncResponseParseReturnRva);
  signature.fill(0);
  if (!readMemory(reinterpret_cast<const void*>(g_targetModuleBase + kAddMsgParseRva), signature.data(),
                  kAddMsgParseSignature.size()) || signature != kAddMsgParseSignature) {
    writeStatus("UNSUPPORTED", "SIGNATURE_MISMATCH", "AddMsg解析入口签名不匹配");
    return false;
  }
  g_addMsgParseAddress = reinterpret_cast<void*>(g_targetModuleBase + kAddMsgParseRva);
  return true;
}

// 按线程 ID 查已登记的线程状态。
ThreadState* findThreadState(DWORD threadId) {
  for (auto& state : g_threads) {
    if (InterlockedCompareExchange(&state.threadId, 0, 0) == static_cast<LONG>(threadId)) return &state;
  }
  return nullptr;
}

// 登记新线程并返回其状态槽。
ThreadState* registerThreadState(DWORD threadId) {
  if (ThreadState* existing = findThreadState(threadId)) return existing;
  for (auto& state : g_threads) {
    if (InterlockedCompareExchange(&state.threadId, static_cast<LONG>(threadId), 0) == 0) return &state;
  }
  return nullptr;
}

// 把一条记录放进固定大小的环形队列，绝不阻塞微信线程。
bool enqueueEvent(const EventRecord& event) {
  for (auto& slot : g_queue) {
    if (InterlockedCompareExchange(&slot.state, 1, 0) != 0) continue;
    slot.event = event;
    MemoryBarrier();
    InterlockedExchange(&slot.state, 2);
    SetEvent(g_queueEvent);
    return true;
  }
  InterlockedIncrement64(&g_queueDropCount);
  return false;
}

// 判断队列里是否还有未输出的记录。
bool hasQueuedEvents() {
  for (auto& slot : g_queue) {
    if (InterlockedCompareExchange(&slot.state, 2, 2) == 2) return true;
  }
  return false;
}

// 记录 NewSyncResponse 对象的结构元数据（虚表、命令列表偏移、存在位图等）。
// 只读结构元数据，不读原始网络缓冲区、正文、金额或账号。
void captureResponseLayout(std::uintptr_t responseAddress, EventRecord* event) {
  if (!event->parseSucceeded || responseAddress == 0) return;

  NewSyncResponseLayout response{};
  if (!readMemory(reinterpret_cast<const void*>(responseAddress), &response, sizeof(response))) {
    InterlockedIncrement64(&g_readFailureCount);
    return;
  }
  if (!g_testMode && response.vtable != g_targetModuleBase + kNewSyncResponseVtableRva) return;

  event->presentMask = response.presentMask;
  event->retCode = response.retCode;
  event->continueFlag = response.continueFlag;
  event->status = response.status;
  event->onlineVersion = response.onlineVersion;
  event->serverTime = response.serverTime;
  event->layoutValid = true;
  if (response.commandList == 0) return;

  NewSyncCommandListLayout commands{};
  if (!readMemory(reinterpret_cast<const void*>(response.commandList), &commands, sizeof(commands)) ||
      commands.itemCount < 0 || commands.itemCount > 4096) {
    event->layoutValid = false;
    InterlockedIncrement64(&g_readFailureCount);
    return;
  }
  event->commandCount = commands.itemCount;
  event->declaredCommandCount = commands.declaredCount;
  if (commands.itemCount == 0) return;
  if (commands.items == 0) {
    event->layoutValid = false;
    InterlockedIncrement64(&g_readFailureCount);
    return;
  }

  const std::uint32_t captureCount =
      static_cast<std::uint32_t>(commands.itemCount < static_cast<std::int32_t>(kMaxCommandIds)
                                     ? commands.itemCount
                                     : static_cast<std::int32_t>(kMaxCommandIds));
  for (std::uint32_t index = 0; index < captureCount; index += 1) {
    std::uintptr_t commandAddress = 0;
    NewSyncCommandLayout command{};
    if (!readMemory(reinterpret_cast<const void*>(commands.items + index * sizeof(std::uintptr_t)),
                    &commandAddress,
                    sizeof(commandAddress)) ||
        commandAddress == 0 ||
        !readMemory(reinterpret_cast<const void*>(commandAddress), &command, sizeof(command))) {
      InterlockedIncrement64(&g_readFailureCount);
      break;
    }
    event->commandIds[event->commandIdCount] = command.commandId;
    event->commandIdCount += 1;
  }
  event->commandIdsTruncated = event->commandIdCount < static_cast<std::uint32_t>(commands.itemCount);
}

// 记录 CmdID=5 的 AddMsg 载荷对象的结构元数据，同样不读消息内容。
void captureAddMsgLayout(std::uintptr_t messageAddress, EventRecord* event) {
  if (messageAddress == 0) return;
  AddMsgLayout message{};
  if (!readMemory(reinterpret_cast<const void*>(messageAddress), &message, sizeof(message))) {
    InterlockedIncrement64(&g_readFailureCount);
    return;
  }
  if (!g_testMode && message.vtable != g_targetModuleBase + kAddMsgVtableRva) return;
  event->addMsgLayoutValid = true;
  event->addMsgPresentMask = message.presentMask;
  event->addMsgType = message.messageType;
}

// 把一条记录写成 JSONL 输出；字段缺失时只输出计数，不输出内容。
void writeEvent(const EventRecord& event) {
  char caller[32] = {};
  std::snprintf(caller, sizeof(caller), "%llx", static_cast<unsigned long long>(event.callerRva));
  std::string callChain = "[";
  for (std::uint32_t index = 0; index < event.callChainDepth; index += 1) {
    char frame[32] = {};
    std::snprintf(frame, sizeof(frame), "%llx", static_cast<unsigned long long>(event.callChainRvas[index]));
    if (index != 0) callChain += ',';
    callChain += "\"0x" + std::string(frame) + "\"";
  }
  callChain += ']';
  std::string commandIds = "[";
  for (std::uint32_t index = 0; index < event.commandIdCount; index += 1) {
    if (index != 0) commandIds += ',';
    commandIds += std::to_string(event.commandIds[index]);
  }
  commandIds += ']';
  writeLine(
      "{\"type\":\"" + std::string(event.type) + "\",\"sequence\":" + std::to_string(event.sequence) +
      ",\"parseCount\":" + std::to_string(event.parseCount) + ",\"threadId\":" +
      std::to_string(event.threadId) + ",\"insideWeixin\":" +
      std::string(event.insideTargetModule ? "true" : "false") + ",\"callerRva\":\"0x" + caller +
      "\",\"callChainRvas\":" + callChain + ",\"callChainDepth\":" + std::to_string(event.callChainDepth) +
      ",\"walkedFrames\":" + std::to_string(event.walkedFrames) + ",\"unwindComplete\":" +
      std::string(event.unwindComplete ? "true" : "false") + ",\"parseSucceeded\":" +
      std::string(event.parseSucceeded ? "true" : "false") + ",\"layoutValid\":" +
      std::string(event.layoutValid ? "true" : "false") + ",\"presentMask\":" +
      std::to_string(event.presentMask) + ",\"retCode\":" + std::to_string(event.retCode) +
      ",\"continueFlag\":" + std::to_string(event.continueFlag) + ",\"status\":" +
      std::to_string(event.status) + ",\"onlineVersion\":" + std::to_string(event.onlineVersion) +
      ",\"serverTime\":" + std::to_string(event.serverTime) + ",\"commandCount\":" +
      std::to_string(event.commandCount) + ",\"declaredCommandCount\":" +
      std::to_string(event.declaredCommandCount) + ",\"commandIds\":" + commandIds +
      ",\"commandIdsTruncated\":" + std::string(event.commandIdsTruncated ? "true" : "false") +
      ",\"addMsgLayoutValid\":" + std::string(event.addMsgLayoutValid ? "true" : "false") +
      ",\"addMsgPresentMask\":" + std::to_string(event.addMsgPresentMask) +
      ",\"addMsgType\":" + std::to_string(event.addMsgType) + "}");
}

// 从异常上下文用 RtlLookupFunctionEntry / RtlVirtualUnwind 展开调用栈，
// 最多 32 层，并只保留落在目标模块内的相对返回地址（最多 16 个），不记录模块外地址与栈内存。
UnwindResult unwindCallChain(const CONTEXT& source) {
  UnwindResult result{};
  CONTEXT context = source;
  const auto* threadEnvironment = reinterpret_cast<const NT_TIB*>(NtCurrentTeb());
  const auto stackLow = reinterpret_cast<std::uintptr_t>(threadEnvironment->StackLimit);
  const auto stackHigh = reinterpret_cast<std::uintptr_t>(threadEnvironment->StackBase);

  for (std::size_t index = 0; index < kMaxWalkFrames; index += 1) {
    if (context.Rip == 0) {
      result.complete = true;
      break;
    }
    if (context.Rsp < stackLow || context.Rsp > stackHigh - sizeof(std::uintptr_t)) break;

    const DWORD64 previousStack = context.Rsp;
    DWORD64 imageBase = 0;
    PRUNTIME_FUNCTION function = RtlLookupFunctionEntry(context.Rip, &imageBase, nullptr);
    if (function != nullptr) {
      PVOID handlerData = nullptr;
      DWORD64 establisherFrame = 0;
      RtlVirtualUnwind(UNW_FLAG_NHANDLER,
                       imageBase,
                       context.Rip,
                       function,
                       &context,
                       &handlerData,
                       &establisherFrame,
                       nullptr);
    } else {
      std::uintptr_t returnAddress = 0;
      if (!readMemory(reinterpret_cast<const void*>(context.Rsp), &returnAddress, sizeof(returnAddress))) break;
      context.Rip = returnAddress;
      context.Rsp += sizeof(returnAddress);
    }
    result.walkedFrames += 1;
    if (context.Rip == 0) {
      result.complete = true;
      break;
    }
    if (context.Rsp <= previousStack) break;
    if (context.Rip >= g_targetModuleBase && context.Rip < g_targetModuleEnd &&
        result.callChainDepth < result.callChainRvas.size()) {
      result.callChainRvas[result.callChainDepth] = context.Rip - g_targetModuleBase;
      result.callChainDepth += 1;
    }
  }
  return result;
}

// 为单个线程设置或清除硬件断点，只改调试寄存器、不写代码页。
bool updateThreadBreakpoint(DWORD threadId, bool enable) {
  if (threadId == GetCurrentThreadId()) return true;
  ThreadState* state = enable ? registerThreadState(threadId) : findThreadState(threadId);
  if (enable && state == nullptr) return false;

  HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_QUERY_INFORMATION,
                             FALSE,
                             threadId);
  if (thread == nullptr) {
    if (GetLastError() == ERROR_INVALID_PARAMETER && state != nullptr) InterlockedExchange(&state->threadId, 0);
    return GetLastError() == ERROR_INVALID_PARAMETER;
  }

  bool success = false;
  const DWORD suspendResult = SuspendThread(thread);
  if (suspendResult != static_cast<DWORD>(-1)) {
    CONTEXT context{};
    context.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    if (GetThreadContext(thread, &context)) {
      if (enable) {
        context.Dr0 = reinterpret_cast<DWORD64>(g_newSyncResponseParseAddress);
        context.Dr7 &= ~(kSlot0EnableMask | kSlot0ControlMask);
        context.Dr7 |= 0x1;
        context.Dr2 = reinterpret_cast<DWORD64>(g_addMsgParseAddress);
        context.Dr7 &= ~(kSlot2EnableMask | kSlot2ControlMask);
        context.Dr7 |= 0x10;
      } else {
        if (context.Dr0 == reinterpret_cast<DWORD64>(g_newSyncResponseParseAddress)) context.Dr0 = 0;
        if (context.Dr1 == reinterpret_cast<DWORD64>(g_newSyncResponseParseReturnAddress)) context.Dr1 = 0;
        if (context.Dr2 == reinterpret_cast<DWORD64>(g_addMsgParseAddress)) context.Dr2 = 0;
        if (context.Dr3 != 0) context.Dr3 = 0;
        context.Dr7 &= ~(kSlot0EnableMask | kSlot0ControlMask);
        context.Dr7 &= ~(kSlot1EnableMask | kSlot1ControlMask);
        context.Dr7 &= ~(kSlot2EnableMask | kSlot2ControlMask);
        context.Dr7 &= ~(kSlot3EnableMask | kSlot3ControlMask);
        if (state != nullptr) InterlockedExchange64(&state->pendingResponse, 0);
        if (state != nullptr) {
          InterlockedExchange64(&state->pendingAddMsg, 0);
          InterlockedExchange64(&state->addMsgReturn, 0);
        }
      }
      context.Dr6 = 0;
      success = SetThreadContext(thread, &context) != FALSE;
    }
    ResumeThread(thread);
  }
  CloseHandle(thread);
  if (!success && state != nullptr && enable) InterlockedExchange(&state->threadId, 0);
  return success;
}

// 遍历目标进程所有线程批量设置/清除断点。
bool updateAllThreadBreakpoints(bool enable) {
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
  if (snapshot == INVALID_HANDLE_VALUE) return false;
  THREADENTRY32 entry{};
  entry.dwSize = sizeof(entry);
  bool sawThread = false;
  bool success = true;
  if (Thread32First(snapshot, &entry)) {
    do {
      if (entry.th32OwnerProcessID != GetCurrentProcessId() || entry.th32ThreadID == GetCurrentThreadId()) continue;
      sawThread = true;
      if (enable && findThreadState(entry.th32ThreadID) != nullptr) continue;
      if (!enable && findThreadState(entry.th32ThreadID) == nullptr) continue;
      if (!updateThreadBreakpoint(entry.th32ThreadID, enable)) success = false;
    } while (Thread32Next(snapshot, &entry));
  } else {
    success = false;
  }
  CloseHandle(snapshot);
  return sawThread && success;
}

// 向量化异常处理器：只处理单步异常，记录解析入口的命中与调用链。
LONG CALLBACK vectoredHandler(PEXCEPTION_POINTERS exception) {
  if (exception->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP) return EXCEPTION_CONTINUE_SEARCH;
  CONTEXT* context = exception->ContextRecord;
  const bool addMsgEntry = (context->Dr6 & 0x4) != 0 &&
                           exception->ExceptionRecord->ExceptionAddress == g_addMsgParseAddress;
  const bool addMsgReturn = (context->Dr6 & 0x8) != 0;
  const bool parseEntry = (context->Dr6 & 0x1) != 0 &&
                          exception->ExceptionRecord->ExceptionAddress == g_newSyncResponseParseAddress;
  const bool parseReturn = (context->Dr6 & 0x2) != 0 &&
                           exception->ExceptionRecord->ExceptionAddress == g_newSyncResponseParseReturnAddress;
  if (!parseEntry && !parseReturn && !addMsgEntry && !addMsgReturn) return EXCEPTION_CONTINUE_SEARCH;

  ThreadState* state = registerThreadState(GetCurrentThreadId());
  if (state == nullptr) return EXCEPTION_CONTINUE_SEARCH;
  if (addMsgReturn && static_cast<std::uintptr_t>(context->Rip) !=
                          static_cast<std::uintptr_t>(InterlockedCompareExchange64(&state->addMsgReturn, 0, 0))) {
    return EXCEPTION_CONTINUE_SEARCH;
  }
  if (addMsgEntry) {
    std::uintptr_t returnAddress = 0;
    if (!readMemory(reinterpret_cast<const void*>(context->Rsp), &returnAddress, sizeof(returnAddress)) ||
        returnAddress == 0) {
      InterlockedIncrement64(&g_readFailureCount);
    } else {
      InterlockedExchange64(&state->pendingAddMsg, static_cast<LONG64>(context->Rcx));
      InterlockedExchange64(&state->addMsgReturn, static_cast<LONG64>(returnAddress));
      context->Dr3 = returnAddress;
      context->Dr7 &= ~(kSlot3EnableMask | kSlot3ControlMask);
      context->Dr7 |= 0x40;
    }
    context->Dr6 = 0;
    context->EFlags |= kResumeFlag;
    return EXCEPTION_CONTINUE_EXECUTION;
  }
  if (addMsgReturn) {
    const std::uintptr_t messageAddress =
        static_cast<std::uintptr_t>(InterlockedExchange64(&state->pendingAddMsg, 0));
    InterlockedExchange64(&state->addMsgReturn, 0);
    context->Dr3 = 0;
    context->Dr7 &= ~(kSlot3EnableMask | kSlot3ControlMask);
    UnwindResult unwind = unwindCallChain(*context);
    EventRecord event{};
    event.type = "addmsg_parse";
    event.sequence = static_cast<std::uint64_t>(InterlockedIncrement64(&g_sequence));
    event.parseCount = static_cast<std::uint64_t>(InterlockedIncrement64(&g_addMsgCount));
    event.threadId = GetCurrentThreadId();
    event.insideTargetModule = unwind.callChainDepth != 0;
    event.callerRva = event.insideTargetModule ? unwind.callChainRvas[0] : 0;
    event.callChainRvas = unwind.callChainRvas;
    event.callChainDepth = unwind.callChainDepth;
    event.walkedFrames = unwind.walkedFrames;
    event.unwindComplete = unwind.complete;
    event.parseSucceeded = g_testMode || (context->Rax & 0xff) != 0;
    event.addMsgType = -1;
    captureAddMsgLayout(messageAddress, &event);
    enqueueEvent(event);
    context->Dr6 = 0;
    context->EFlags |= kResumeFlag;
    return EXCEPTION_CONTINUE_EXECUTION;
  }
  if (parseEntry) {
    InterlockedExchange64(&state->pendingResponse, static_cast<LONG64>(context->Rcx));
    context->Dr1 = reinterpret_cast<DWORD64>(g_newSyncResponseParseReturnAddress);
    context->Dr7 &= ~(kSlot1EnableMask | kSlot1ControlMask);
    context->Dr7 |= 0x4;
    context->Dr6 = 0;
    context->EFlags |= kResumeFlag;
    return EXCEPTION_CONTINUE_EXECUTION;
  }

  const std::uintptr_t responseAddress =
      static_cast<std::uintptr_t>(InterlockedExchange64(&state->pendingResponse, 0));
  context->Dr1 = 0;
  context->Dr7 &= ~(kSlot1EnableMask | kSlot1ControlMask);
  UnwindResult unwind = unwindCallChain(*context);
  if (unwind.callChainDepth < unwind.callChainRvas.size()) {
    for (std::uint32_t index = unwind.callChainDepth; index > 0; index -= 1) {
      unwind.callChainRvas[index] = unwind.callChainRvas[index - 1];
    }
    unwind.callChainRvas[0] = static_cast<std::uint64_t>(
        reinterpret_cast<std::uintptr_t>(g_newSyncResponseParseReturnAddress) - g_targetModuleBase);
    unwind.callChainDepth += 1;
  }
  if (unwind.callChainDepth == 0) {
    InterlockedIncrement64(&g_readFailureCount);
    InterlockedIncrement64(&g_unwindFailureCount);
  } else if (!unwind.complete) {
    InterlockedIncrement64(&g_unwindFailureCount);
  }
  const bool insideTarget = unwind.callChainDepth != 0;
  const std::uint64_t callerRva = insideTarget ? unwind.callChainRvas[0] : 0;
  const std::uint64_t parseCount = static_cast<std::uint64_t>(InterlockedIncrement64(&g_parseCount));
  EventRecord event{};
  event.type = "newsync_response_parse";
  event.sequence = static_cast<std::uint64_t>(InterlockedIncrement64(&g_sequence));
  event.parseCount = parseCount;
  event.threadId = GetCurrentThreadId();
  event.callerRva = callerRva;
  event.insideTargetModule = insideTarget;
  event.callChainRvas = unwind.callChainRvas;
  event.callChainDepth = unwind.callChainDepth;
  event.walkedFrames = unwind.walkedFrames;
  event.unwindComplete = unwind.complete;
  event.parseSucceeded = g_testMode || (context->Rax & 0xff) != 0;
  event.commandCount = -1;
  event.declaredCommandCount = -1;
  captureResponseLayout(responseAddress, &event);
  enqueueEvent(event);

  context->Dr6 = 0;
  context->EFlags |= kResumeFlag;
  return EXCEPTION_CONTINUE_EXECUTION;
}

// 停止流程：清除所有断点并摘除异常处理器；DLL 保持惰性加载。
DWORD WINAPI stopMain(void*);

// 请求异步停止：置生命周期标志并唤醒工作线程，由工作线程自己清理断点。
void requestAsyncStop() {
  if (InterlockedCompareExchange(&g_lifecycle, kLifecycleStopping, kLifecycleRunning) != kLifecycleRunning) return;
  HANDLE cleanup = CreateThread(nullptr, 0, stopMain, nullptr, 0, nullptr);
  if (cleanup == nullptr) {
    InterlockedExchange(&g_lifecycle, kLifecycleRunning);
    return;
  }
  CloseHandle(cleanup);
}

// 工作线程：输出队列里的记录并周期性重申健康状态。
// 首次命中前用更短间隔扫描新建线程，命中后放慢，兼顾覆盖率与开销。
DWORD WINAPI workerMain(void*) {
  std::uint64_t nextCoverageCheck = 0;
  std::uint64_t nextHealthReport = unixTimeMilliseconds();
  while (InterlockedCompareExchange(&g_running, 1, 1) != 0 || hasQueuedEvents()) {
    WaitForSingleObject(g_queueEvent, 250);
    for (auto& slot : g_queue) {
      if (InterlockedCompareExchange(&slot.state, 3, 2) != 2) continue;
      writeEvent(slot.event);
      InterlockedExchange(&slot.state, 0);
    }
    const std::uint64_t now = unixTimeMilliseconds();
    if (InterlockedCompareExchange(&g_running, 1, 1) != 0 && now >= nextCoverageCheck) {
      updateAllThreadBreakpoints(true);
      nextCoverageCheck =
          now + (InterlockedCompareExchange64(&g_parseCount, 0, 0) == 0 ? kInitialCoverageIntervalMs
                                                                        : kSteadyCoverageIntervalMs);
    }
    if (g_pipeOutput && InterlockedCompareExchange(&g_running, 1, 1) != 0 && now >= nextHealthReport) {
      writeStatus(
          "HEALTHY",
          "NEWSYNC_PARSER_ACTIVE",
          "NewSyncResponse解析入口运行中; parseCount=" +
              std::to_string(InterlockedCompareExchange64(&g_parseCount, 0, 0)) + "; callerReadFailures=" +
              std::to_string(InterlockedCompareExchange64(&g_readFailureCount, 0, 0)) + "; unwindFailures=" +
              std::to_string(InterlockedCompareExchange64(&g_unwindFailureCount, 0, 0)) + "; queueDrops=" +
              std::to_string(InterlockedCompareExchange64(&g_queueDropCount, 0, 0)));
      nextHealthReport = now + 5'000;
    }
    if (InterlockedCompareExchange(&g_outputBroken, 0, 0) != 0) requestAsyncStop();
  }
  return 0;
}

// 打开宿主创建的就绪事件，告知宿主断点已安装完成。
void signalReady() {
  wchar_t eventName[96] = {};
  ::swprintf(eventName, L"Local\\CfpayHookTestReady-%lu", GetCurrentProcessId());
  HANDLE event = OpenEventW(EVENT_MODIFY_STATE, FALSE, eventName);
  if (event != nullptr) {
    SetEvent(event);
    CloseHandle(event);
  }
}

// 安装流程：连接输出、校验、注册异常处理器、装断点、启动工作线程。
DWORD WINAPI bootstrapMain(void*) {
  if (InterlockedCompareExchange(&g_lifecycle, kLifecycleStarting, kLifecycleStopped) != kLifecycleStopped) return 4;
  InterlockedExchange(&g_running, 0);
  InterlockedExchange(&g_stopResult, FALSE);
  InterlockedExchange(&g_outputBroken, 0);
  InterlockedExchange64(&g_sequence, 0);
  InterlockedExchange64(&g_parseCount, 0);
  InterlockedExchange64(&g_addMsgCount, 0);
  InterlockedExchange64(&g_readFailureCount, 0);
  InterlockedExchange64(&g_unwindFailureCount, 0);
  InterlockedExchange64(&g_queueDropCount, 0);
  g_pipeOutput = false;
  g_testMode = false;
  g_newSyncResponseParseAddress = nullptr;
  g_newSyncResponseParseReturnAddress = nullptr;
  g_addMsgParseAddress = nullptr;
  g_targetModuleBase = 0;
  g_targetModuleEnd = 0;
  std::memset(g_threads, 0, sizeof(g_threads));
  std::memset(g_queue, 0, sizeof(g_queue));

  if (!configureOutputAndTarget()) {
    signalReady();
    if (g_outputFile != INVALID_HANDLE_VALUE) CloseHandle(g_outputFile);
    g_outputFile = INVALID_HANDLE_VALUE;
    InterlockedExchange(&g_lifecycle, kLifecycleStopped);
    return 1;
  }

  g_queueEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  g_veh = AddVectoredExceptionHandler(1, vectoredHandler);
  InterlockedExchange(&g_running, 1);
  if (g_queueEvent == nullptr || g_veh == nullptr || !updateAllThreadBreakpoints(true)) {
    updateAllThreadBreakpoints(false);
    writeStatus("ERROR", "HOOK_INSTALL_FAILED", "NewSyncResponse解析入口断点安装失败");
    signalReady();
    if (g_veh != nullptr) RemoveVectoredExceptionHandler(g_veh);
    if (g_queueEvent != nullptr) CloseHandle(g_queueEvent);
    if (g_outputFile != INVALID_HANDLE_VALUE) CloseHandle(g_outputFile);
    g_veh = nullptr;
    g_queueEvent = nullptr;
    g_outputFile = INVALID_HANDLE_VALUE;
    InterlockedExchange(&g_running, 0);
    InterlockedExchange(&g_lifecycle, kLifecycleStopped);
    return 2;
  }

  g_workerThread = CreateThread(nullptr, 0, workerMain, nullptr, 0, nullptr);
  if (g_workerThread == nullptr) {
    updateAllThreadBreakpoints(false);
    RemoveVectoredExceptionHandler(g_veh);
    g_veh = nullptr;
    writeStatus("ERROR", "HOOK_INSTALL_FAILED", "NewSyncResponse解析诊断工作线程创建失败");
    signalReady();
    CloseHandle(g_queueEvent);
    CloseHandle(g_outputFile);
    g_queueEvent = nullptr;
    g_outputFile = INVALID_HANDLE_VALUE;
    InterlockedExchange(&g_running, 0);
    InterlockedExchange(&g_lifecycle, kLifecycleStopped);
    return 3;
  }

  InterlockedExchange(&g_lifecycle, kLifecycleRunning);
  writeStatus("HEALTHY", "NEWSYNC_PARSER_ACTIVE", "NewSyncResponse解析入口诊断已安装");
  signalReady();
  return 0;
}

// 停止流程：清除所有断点并摘除异常处理器；DLL 保持惰性加载。
DWORD WINAPI stopMain(void*) {
  const bool cleared = updateAllThreadBreakpoints(false);
  if (g_veh != nullptr) {
    RemoveVectoredExceptionHandler(g_veh);
    g_veh = nullptr;
  }
  InterlockedExchange(&g_running, 0);
  if (g_queueEvent != nullptr) SetEvent(g_queueEvent);
  if (g_workerThread != nullptr) WaitForSingleObject(g_workerThread, 5000);
  writeStatus(cleared ? "STOPPED" : "ERROR", cleared ? "HOOK_STOPPED" : "HOOK_STOP_FAILED",
              cleared ? "NewSyncResponse解析入口断点已清理" : "NewSyncResponse解析入口断点未完全清理");
  if (g_outputFile != INVALID_HANDLE_VALUE) CloseHandle(g_outputFile);
  if (g_workerThread != nullptr) CloseHandle(g_workerThread);
  if (g_queueEvent != nullptr) CloseHandle(g_queueEvent);
  g_outputFile = INVALID_HANDLE_VALUE;
  g_workerThread = nullptr;
  g_queueEvent = nullptr;
  InterlockedExchange(&g_stopResult, cleared ? TRUE : FALSE);
  InterlockedExchange(&g_lifecycle, kLifecycleStopped);
  return 0;
}

}  // namespace

extern "C" __declspec(dllexport) BOOL WINAPI CfpayObserverStop() {
  for (int attempt = 0; attempt < 300; attempt += 1) {
    const LONG lifecycle = InterlockedCompareExchange(&g_lifecycle, kLifecycleStopped, kLifecycleStopped);
    if (lifecycle == kLifecycleStopped) return TRUE;
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
  return 0x00080000;
}

// DLL 入口：只在进程附加时创建工作线程。
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    DisableThreadLibraryCalls(instance);
    HANDLE bootstrap = CreateThread(nullptr, 0, bootstrapMain, nullptr, 0, nullptr);
    if (bootstrap != nullptr) CloseHandle(bootstrap);
  }
  return TRUE;
}
