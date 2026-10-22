// 结构化记录槽位转储诊断：只观察 Weixin.dll 中已确认会在真实到账时被调用的记录追加入口，
// 读取待追加记录内的字符串槽位并输出掩码骨架，用于离线定位结构化字段偏移。
// 不修改微信代码页、参数或返回值，不读取原始网络缓冲区，不输出账号、交易号或金额原文。
#include <windows.h>
#include <tlhelp32.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

constexpr std::size_t kMaxThreadStates = 512;
constexpr std::size_t kQueueSize = 512;
constexpr std::uint64_t kMaxStringBytes = 4096;
constexpr std::uint64_t kInitialCoverageIntervalMs = 250;
constexpr std::uint64_t kSteadyCoverageIntervalMs = 250;
constexpr DWORD64 kResumeFlag = 0x10000;
constexpr DWORD64 kSlot0EnableMask = 0x3;
constexpr DWORD64 kSlot0ControlMask = static_cast<DWORD64>(0xf) << 16;

// 记录追加函数 FUN_18490d5c0：param_1+8 指向服务会话对象，param_2 指向待追加的 0x300 记录。
constexpr std::uint32_t kRecordAppendRva = 0x490d5c0;
constexpr std::uint32_t kRecordSize = 0x300;
constexpr std::uint32_t kNestedObjectOffset = 0x290;
constexpr std::uint32_t kNestedObjectSize = 0x290;
constexpr std::array<std::uint8_t, 32> kRecordAppendSignature = {
    0x55, 0x56, 0x57, 0x53, 0x48, 0x83, 0xec, 0x38, 0x48, 0x8d, 0x6c, 0x24, 0x30, 0x48, 0xc7, 0x45,
    0x00, 0xfe, 0xff, 0xff, 0xff, 0x48, 0x89, 0xd7, 0x48, 0x8b, 0x71, 0x08, 0x48, 0x8b, 0x9e, 0x40,
};

constexpr LONG kLifecycleStopped = 0;
constexpr LONG kLifecycleStarting = 1;
constexpr LONG kLifecycleRunning = 2;
constexpr LONG kLifecycleStopping = 3;
constexpr LONG64 kMaxDumpedHits = 400;
constexpr LONG64 kMaxSlotRecords = 20000;

struct ThreadState {
  volatile LONG threadId;
};

struct SlotRecord {
  std::uint64_t sequence;
  std::uint32_t offset;
  std::uint32_t bytes;
  bool nested;
  char shape[112];
};

struct QueueSlot {
  volatile LONG state;
  SlotRecord record;
};

PVOID g_veh = nullptr;
HANDLE g_outputFile = INVALID_HANDLE_VALUE;
HANDLE g_dumpFile = INVALID_HANDLE_VALUE;
HANDLE g_queueEvent = nullptr;
HANDLE g_workerThread = nullptr;
void* g_hookAddress = nullptr;
std::uintptr_t g_moduleBase = 0;
std::uintptr_t g_moduleEnd = 0;
volatile LONG g_running = 0;
volatile LONG g_lifecycle = kLifecycleStopped;
volatile LONG g_stopResult = FALSE;
volatile LONG g_outputBroken = 0;
volatile LONG64 g_sequence = 0;
volatile LONG64 g_hitCount = 0;
volatile LONG64 g_dumpedHitCount = 0;
volatile LONG64 g_slotCount = 0;
volatile LONG64 g_readFailureCount = 0;
volatile LONG64 g_queueDropCount = 0;
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

// 转义 JSON 字符串；手写实现以免诊断 DLL 引入 JSON 库依赖。
std::string jsonEscape(const char* value);

// 转义 JSON 字符串；手写实现以免诊断 DLL 引入 JSON 库依赖。
std::string jsonEscape(const char* value) {
  std::string output;
  for (const unsigned char* cursor = reinterpret_cast<const unsigned char*>(value); *cursor != 0; cursor += 1) {
    if (*cursor < 0x20) continue;
    if (*cursor == '"' || *cursor == '\\') output.push_back('\\');
    output.push_back(static_cast<char>(*cursor));
  }
  return output;
}

// 向输出句柄写满指定字节数，短写继续补写。
bool writeDumpBytes(const char* data, DWORD size) {
  DWORD total = 0;
  while (total < size) {
    DWORD written = 0;
    if (!WriteFile(g_dumpFile, data + total, size - total, &written, nullptr) || written == 0) return false;
    total += written;
  }
  return true;
}

// 转储只写本机文件并逐行落盘；命名管道仅用于宿主注入握手，避免宿主转发链路阻塞工作线程。
void writeLine(const std::string& line) {
  if (g_dumpFile == INVALID_HANDLE_VALUE) return;
  static constexpr char newline[] = "\r\n";
  const bool success = writeDumpBytes(line.data(), static_cast<DWORD>(line.size())) &&
                       writeDumpBytes(newline, 2);
  if (success) FlushFileBuffers(g_dumpFile);
  if (!success) InterlockedExchange(&g_outputBroken, 1);
}

// 输出握手记录，告知宿主诊断 DLL 已就绪及其目标偏移。
void writePipeHandshake(const char* detailCode, const std::string& message) {
  if (g_outputFile == INVALID_HANDLE_VALUE) return;
  const std::string line = "{\"type\":\"status\",\"status\":\"HEALTHY\",\"detailCode\":\"" +
                           std::string(detailCode) + "\",\"message\":\"" + jsonEscape(message.c_str()) +
                           "\",\"observedAt\":" + std::to_string(unixTimeMilliseconds()) + "}\r\n";
  DWORD written = 0;
  WriteFile(g_outputFile, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
}

// 输出一行状态记录，宿主据此在界面上反映诊断链路是否正常。
void writeStatus(const char* status, const char* detailCode, const std::string& message) {
  writeLine("{\"type\":\"status\",\"status\":\"" + std::string(status) + "\",\"detailCode\":\"" +
            std::string(detailCode) + "\",\"message\":\"" + jsonEscape(message.c_str()) + "\",\"observedAt\":" +
            std::to_string(unixTimeMilliseconds()) + "}");
}

// 在自身进程内读满指定字节数，读不满即失败。
bool readMemory(const void* address, void* output, std::size_t size) {
  SIZE_T bytesRead = 0;
  return address != nullptr && ReadProcessMemory(GetCurrentProcess(), address, output, size, &bytesRead) != FALSE &&
         bytesRead == size;
}

// 连接宿主创建的命名管道，带重试；连不上说明宿主已退出。
bool connectNamedPipe(const wchar_t* pipeName) {
  for (int attempt = 0; attempt < 300; attempt += 1) {
    g_outputFile = CreateFileW(pipeName, GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (g_outputFile != INVALID_HANDLE_VALUE) return true;
    if (GetLastError() != ERROR_PIPE_BUSY || !WaitNamedPipeW(pipeName, 100)) Sleep(50);
  }
  return false;
}

// 取目标模块的基址与结束地址，用于判断某个返回地址是否落在模块内。
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

// 掩码骨架：数字转 '#'，ASCII 字母转 'a'，保留中文与标点，只用于判断字段类型与长度。
std::string maskedShape(const char* value) {
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

// 按 MSVC std::string 布局读取字符串，长度与容量不自洽时拒绝读取。
bool readMsvcStringAt(std::uintptr_t address, char* output, std::size_t outputSize) {
  std::uint64_t length = 0;
  std::uint64_t capacity = 0;
  if (!readMemory(reinterpret_cast<const void*>(address + 0x10), &length, sizeof(length)) ||
      !readMemory(reinterpret_cast<const void*>(address + 0x18), &capacity, sizeof(capacity)) ||
      length == 0 || length > kMaxStringBytes || capacity < length || capacity > kMaxStringBytes ||
      length >= outputSize) {
    return false;
  }
  std::uintptr_t dataAddress = address;
  if (capacity >= 0x10 && !readMemory(reinterpret_cast<const void*>(address), &dataAddress, sizeof(dataAddress))) {
    return false;
  }
  if (dataAddress < 0x10000 || !readMemory(reinterpret_cast<const void*>(dataAddress), output,
                                           static_cast<std::size_t>(length))) {
    return false;
  }
  output[length] = '\0';
  return true;
}

// 把一条槽位记录放进固定大小的环形队列，满了丢弃最旧的一条，绝不阻塞微信线程。
bool enqueueSlot(const SlotRecord& record) {
  for (auto& slot : g_queue) {
    if (InterlockedCompareExchange(&slot.state, 1, 0) == 0) {
      slot.record = record;
      MemoryBarrier();
      InterlockedExchange(&slot.state, 2);
      SetEvent(g_queueEvent);
      return true;
    }
  }
  InterlockedIncrement64(&g_queueDropCount);
  return false;
}

// id 形态：长度 8-64，字符集限定为账号标识常用字符；服务号与群号属于公开标识，可直接输出原文。
bool isIdentifierShape(const char* value) {
  const std::size_t length = std::strlen(value);
  if (length < 8 || length > 64) return false;
  for (const unsigned char* cursor = reinterpret_cast<const unsigned char*>(value); *cursor != 0; cursor += 1) {
    const unsigned char character = *cursor;
    const bool allowed = (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
                         (character >= '0' && character <= '9') || character == '_' || character == '-' ||
                         character == '.' || character == '@';
    if (!allowed) return false;
  }
  return true;
}

// 按 0x20 步长遍历对象的各个槽位，记录形态而不是原文。
// 这是研究用转储：只输出偏移、字节形态与长度，不输出消息正文、金额或账号。
void dumpObjectSlots(std::uintptr_t objectAddress, std::uint32_t objectSize, bool nested, std::uint64_t sequence) {
  for (std::uint32_t offset = 0; offset + 0x20 <= objectSize; offset += 8) {
    if (InterlockedCompareExchange64(&g_slotCount, 0, 0) >= kMaxSlotRecords) return;
    char value[1024] = {};
    if (!readMsvcStringAt(objectAddress + offset, value, sizeof(value))) continue;
    SlotRecord record{};
    record.sequence = sequence;
    record.offset = offset;
    record.bytes = static_cast<std::uint32_t>(std::strlen(value));
    record.nested = nested;
    if (isIdentifierShape(value)) {
      std::strncpy(record.shape, value, sizeof(record.shape) - 1);
    } else {
      std::string shape = maskedShape(value);
      // 长字符串额外输出累积 XOR 与 FNV 哈希，用于验证其内容是否与金额相关。
      if (record.bytes >= 32) {
        unsigned int xorAccumulator = 0;
        std::uint64_t fnv = 1469598103934665603ULL;
        for (const unsigned char* cursor = reinterpret_cast<const unsigned char*>(value); *cursor != 0; cursor += 1) {
          xorAccumulator ^= *cursor;
          fnv ^= *cursor;
          fnv *= 1099511628211ULL;
        }
        char digest[48] = {};
        std::snprintf(digest, sizeof(digest), "|xor=%u|fnv=%016llx", xorAccumulator,
                      static_cast<unsigned long long>(fnv));
        if (shape.size() + sizeof(digest) < sizeof(record.shape)) shape += digest;
      }
      std::strncpy(record.shape, shape.c_str(), sizeof(record.shape) - 1);
    }
    InterlockedIncrement64(&g_slotCount);
    enqueueSlot(record);
  }
}

// 数值字段发现：只输出时间戳形态与可能是金额分值的 32 位整数，用于离线定位结构化偏移。
void dumpRecordNumbers(std::uintptr_t recordAddress, std::uint64_t sequence) {
  const std::uint64_t nowMs = unixTimeMilliseconds();
  const std::uint64_t nowSeconds = nowMs / 1000;
  int emitted = 0;
  for (std::uint32_t offset = 0; offset + 8 <= kRecordSize && emitted < 60; offset += 4) {
    std::uint64_t quad = 0;
    if (!readMemory(reinterpret_cast<const void*>(recordAddress + offset), &quad, sizeof(quad))) continue;
    const char* kind = nullptr;
    std::uint64_t value = 0;
    if (offset % 8 == 0 && quad > nowSeconds - 172800 && quad < nowSeconds + 3600) {
      kind = "ts_s";
      value = quad;
    } else if (offset % 8 == 0 && quad > nowMs - 172800000ULL && quad < nowMs + 3600000ULL) {
      kind = "ts_ms";
      value = quad;
    } else {
      std::uint32_t dword = 0;
      if (readMemory(reinterpret_cast<const void*>(recordAddress + offset), &dword, sizeof(dword)) && dword > 0 &&
          dword < 10000000) {
        kind = "i32";
        value = dword;
      }
    }
    if (kind == nullptr) continue;
    if (InterlockedCompareExchange64(&g_slotCount, 0, 0) >= kMaxSlotRecords) return;
    SlotRecord record{};
    record.sequence = sequence;
    record.offset = offset;
    record.bytes = 0;
    record.nested = false;
    std::snprintf(record.shape, sizeof(record.shape), "%s=%llu", kind, static_cast<unsigned long long>(value));
    InterlockedIncrement64(&g_slotCount);
    enqueueSlot(record);
    emitted += 1;
  }
}

// 为一次断点命中分配递增序号并转储对应记录的槽位。
void dumpRecord(std::uintptr_t recordAddress) {
  const std::uint64_t sequence = static_cast<std::uint64_t>(InterlockedIncrement64(&g_sequence));
  dumpObjectSlots(recordAddress, kRecordSize, false, sequence);
  dumpRecordNumbers(recordAddress, sequence);
  std::uintptr_t nested = 0;
  if (readMemory(reinterpret_cast<const void*>(recordAddress + kNestedObjectOffset), &nested, sizeof(nested)) &&
      nested > 0x10000) {
    dumpObjectSlots(nested, kNestedObjectSize, true, sequence);
  }
}

// 判断队列里是否还有未输出的记录。
bool hasQueuedRecords() {
  for (auto& slot : g_queue) {
    if (InterlockedCompareExchange(&slot.state, 2, 2) == 2) return true;
  }
  return false;
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

// 为单个线程设置或清除硬件断点，只改调试寄存器、不写代码页。
bool updateThreadBreakpoint(DWORD threadId, bool enable) {
  if (threadId == GetCurrentThreadId()) return true;
  ThreadState* state = enable ? registerThreadState(threadId) : findThreadState(threadId);
  if (enable && state == nullptr) return false;
  HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_QUERY_INFORMATION,
                             FALSE, threadId);
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
        context.Dr0 = reinterpret_cast<DWORD64>(g_hookAddress);
        context.Dr7 &= ~(kSlot0EnableMask | kSlot0ControlMask);
        context.Dr7 |= 0x1;
      } else if (context.Dr0 == reinterpret_cast<DWORD64>(g_hookAddress)) {
        context.Dr0 = 0;
        context.Dr7 &= ~(kSlot0EnableMask | kSlot0ControlMask);
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

// 遍历目标进程所有线程批量设置/清除断点。
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

// 向量化异常处理器：只处理单步异常并转储命中记录，其余异常交给后续处理器。
LONG CALLBACK vectoredHandler(PEXCEPTION_POINTERS exception) {
  if (exception->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP) return EXCEPTION_CONTINUE_SEARCH;
  CONTEXT* context = exception->ContextRecord;
  if ((context->Dr6 & 0x1) == 0 || exception->ExceptionRecord->ExceptionAddress != g_hookAddress) {
    return EXCEPTION_CONTINUE_SEARCH;
  }
  InterlockedIncrement64(&g_hitCount);
  if (InterlockedCompareExchange64(&g_dumpedHitCount, 0, 0) < kMaxDumpedHits) {
    InterlockedIncrement64(&g_dumpedHitCount);
    dumpRecord(static_cast<std::uintptr_t>(context->Rdx));
  }
  context->Dr6 = 0;
  context->EFlags |= kResumeFlag;
  return EXCEPTION_CONTINUE_EXECUTION;
}

// 工作线程：输出队列里的转储记录，并周期性补齐新建线程的断点覆盖。
DWORD WINAPI workerMain(void*) {
  std::uint64_t nextCoverageCheck = 0;
  std::uint64_t nextHealthReport = unixTimeMilliseconds() + 1000;
  while (InterlockedCompareExchange(&g_running, 1, 1) != 0 || hasQueuedRecords()) {
    WaitForSingleObject(g_queueEvent, 250);
    for (auto& slot : g_queue) {
      if (InterlockedCompareExchange(&slot.state, 3, 2) == 2) {
        char offsetText[16] = {};
        std::snprintf(offsetText, sizeof(offsetText), "0x%03x", slot.record.offset);
        writeLine("{\"type\":\"record_slot\",\"seq\":" + std::to_string(slot.record.sequence) + ",\"offset\":\"" +
                  offsetText + "\",\"bytes\":" + std::to_string(slot.record.bytes) + ",\"nested\":" +
                  (slot.record.nested ? "true" : "false") + ",\"shape\":\"" + jsonEscape(slot.record.shape) + "\"}");
        InterlockedExchange(&slot.state, 0);
      }
    }
    const std::uint64_t now = unixTimeMilliseconds();
    if (InterlockedCompareExchange(&g_running, 1, 1) != 0 && now >= nextCoverageCheck) {
      updateAllThreadBreakpoints(true);
      nextCoverageCheck = now + (InterlockedCompareExchange64(&g_hitCount, 0, 0) == 0 ? kInitialCoverageIntervalMs
                                                                                   : kSteadyCoverageIntervalMs);
    }
    if (InterlockedCompareExchange(&g_running, 1, 1) != 0 && now >= nextHealthReport) {
      int threadCount = 0;
      for (auto& state : g_threads) {
        if (InterlockedCompareExchange(&state.threadId, 0, 0) != 0) threadCount += 1;
      }
      writeStatus("HEALTHY", "RECORD_DUMP_ACTIVE",
                  "结构化记录转储运行中; recordAppends=" +
                      std::to_string(InterlockedCompareExchange64(&g_hitCount, 0, 0)) + "; dumpedHits=" +
                      std::to_string(InterlockedCompareExchange64(&g_dumpedHitCount, 0, 0)) + "; slots=" +
                      std::to_string(InterlockedCompareExchange64(&g_slotCount, 0, 0)) + "; readFailures=" +
                      std::to_string(InterlockedCompareExchange64(&g_readFailureCount, 0, 0)) + "; queueDrops=" +
                      std::to_string(InterlockedCompareExchange64(&g_queueDropCount, 0, 0)) + "; threads=" +
                      std::to_string(threadCount));
      nextHealthReport = now + 5000;
    }
    if (InterlockedCompareExchange(&g_outputBroken, 0, 0) != 0) {
      InterlockedExchange(&g_running, 0);
      break;
    }
  }
  return 0;
}

// 连接输出管道、解析目标模块基址并校验签名，再算出断点地址。
bool configureOutputAndTarget() {
  wchar_t pipeName[128] = {};
  ::swprintf(pipeName, L"\\\\.\\pipe\\cfpay-wechat-observer-%lu", GetCurrentProcessId());
  if (!connectNamedPipe(pipeName)) return false;
  writePipeHandshake("PIPE_CONNECTED", "结构化记录转储已连接命名管道");

  HMODULE weixin = GetModuleHandleW(L"Weixin.dll");
  if (weixin == nullptr || !moduleBounds(weixin, &g_moduleBase, &g_moduleEnd)) {
    writeStatus("STOPPED", "PROCESS_NOT_FOUND", "目标进程未加载固定版本 Weixin.dll");
    writePipeHandshake("PROCESS_NOT_FOUND", "目标进程未加载固定版本 Weixin.dll");
    return false;
  }
  std::array<std::uint8_t, kRecordAppendSignature.size()> signature{};
  if (!readMemory(reinterpret_cast<const void*>(g_moduleBase + kRecordAppendRva), signature.data(), signature.size()) ||
      signature != kRecordAppendSignature) {
    writeStatus("UNSUPPORTED", "SIGNATURE_MISMATCH", "结构化记录追加入口签名不匹配");
    writePipeHandshake("SIGNATURE_MISMATCH", "结构化记录追加入口签名不匹配");
    return false;
  }
  wchar_t modulePath[MAX_PATH] = {};
  HMODULE self = GetModuleHandleW(L"cfpay-hook-record-diagnostic.dll");
  if (self == nullptr || GetModuleFileNameW(self, modulePath, MAX_PATH) == 0) {
    writePipeHandshake("HOOK_INSTALL_FAILED",
                       "无法解析诊断 DLL 自身路径 winError=" + std::to_string(GetLastError()));
    return false;
  }
  wchar_t* separator = std::wcsrchr(modulePath, L'\\');
  if (separator == nullptr) {
    writePipeHandshake("HOOK_INSTALL_FAILED", "诊断 DLL 路径缺少目录分隔符");
    return false;
  }
  *(separator + 1) = L'\0';
  static constexpr wchar_t kDumpFileName[] = L"record-dump.jsonl";
  wchar_t dumpPath[MAX_PATH + 64] = {};
  const std::size_t directoryLength = std::wcslen(modulePath);
  if (directoryLength + (sizeof(kDumpFileName) / sizeof(wchar_t)) >= MAX_PATH + 64) {
    writePipeHandshake("HOOK_INSTALL_FAILED", "诊断 DLL 路径过长，无法构造转储文件路径");
    return false;
  }
  std::memcpy(dumpPath, modulePath, directoryLength * sizeof(wchar_t));
  std::memcpy(dumpPath + directoryLength, kDumpFileName, sizeof(kDumpFileName));
  g_dumpFile = CreateFileW(dumpPath, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                           nullptr);
  if (g_dumpFile == INVALID_HANDLE_VALUE) {
    writePipeHandshake("HOOK_INSTALL_FAILED",
                       "结构化记录转储文件无法创建 winError=" + std::to_string(GetLastError()));
    return false;
  }
  g_hookAddress = reinterpret_cast<void*>(g_moduleBase + kRecordAppendRva);
  char dumpPathUtf8[MAX_PATH * 3] = {};
  WideCharToMultiByte(CP_UTF8, 0, dumpPath, -1, dumpPathUtf8, sizeof(dumpPathUtf8) - 1, nullptr, nullptr);
  writeStatus("HEALTHY", "RECORD_DUMP_READY", std::string("转储文件已创建 path=") + dumpPathUtf8);
  writePipeHandshake("RECORD_DUMP_ACTIVE", std::string("结构化记录转储断点已安装 path=") + dumpPathUtf8 +
                                                 " exists=" +
                                                 std::to_string(GetFileAttributesW(dumpPath) != INVALID_FILE_ATTRIBUTES) +
                                                 " selfModule=" +
                                                 std::to_string(reinterpret_cast<std::uintptr_t>(self)));
  return true;
}

// 安装流程：连接输出、校验、注册异常处理器、装断点、启动工作线程；失败时先清理再上报。
DWORD WINAPI bootstrapMain(void*) {
  if (InterlockedCompareExchange(&g_lifecycle, kLifecycleStarting, kLifecycleStopped) != kLifecycleStopped) return 4;
  InterlockedExchange(&g_running, 0);
  InterlockedExchange(&g_stopResult, FALSE);
  InterlockedExchange(&g_outputBroken, 0);
  InterlockedExchange64(&g_sequence, 0);
  InterlockedExchange64(&g_hitCount, 0);
  InterlockedExchange64(&g_dumpedHitCount, 0);
  InterlockedExchange64(&g_slotCount, 0);
  InterlockedExchange64(&g_readFailureCount, 0);
  InterlockedExchange64(&g_queueDropCount, 0);
  g_hookAddress = nullptr;
  std::memset(g_threads, 0, sizeof(g_threads));
  std::memset(g_queue, 0, sizeof(g_queue));

  if (!configureOutputAndTarget()) {
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
  if (g_queueEvent == nullptr || g_veh == nullptr || !updateAllThreadBreakpoints(true)) {
    updateAllThreadBreakpoints(false);
    writeStatus("ERROR", "HOOK_INSTALL_FAILED", "结构化记录转储断点安装失败");
    writePipeHandshake("HOOK_INSTALL_FAILED", "结构化记录转储断点安装失败");
    if (g_veh != nullptr) {
      RemoveVectoredExceptionHandler(g_veh);
      g_veh = nullptr;
    }
    if (g_queueEvent != nullptr) {
      CloseHandle(g_queueEvent);
      g_queueEvent = nullptr;
    }
    CloseHandle(g_outputFile);
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
    writeStatus("ERROR", "HOOK_INSTALL_FAILED", "结构化记录转储工作线程创建失败");
    CloseHandle(g_outputFile);
    g_outputFile = INVALID_HANDLE_VALUE;
    CloseHandle(g_queueEvent);
    g_queueEvent = nullptr;
    InterlockedExchange(&g_running, 0);
    InterlockedExchange(&g_lifecycle, kLifecycleStopped);
    return 3;
  }

  InterlockedExchange(&g_lifecycle, kLifecycleRunning);
  writeStatus("HEALTHY", "RECORD_DUMP_ACTIVE", "结构化记录转储断点已安装");
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
              cleared ? "结构化记录转储断点已清理" : "部分线程断点未能清理");
  if (g_outputFile != INVALID_HANDLE_VALUE) {
    CloseHandle(g_outputFile);
    g_outputFile = INVALID_HANDLE_VALUE;
  }
  if (g_dumpFile != INVALID_HANDLE_VALUE) {
    FlushFileBuffers(g_dumpFile);
    CloseHandle(g_dumpFile);
    g_dumpFile = INVALID_HANDLE_VALUE;
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
  return 0x00090000;
}

// DLL 入口：只在进程附加时创建工作线程，避免在加载器锁里做重活。
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    DisableThreadLibraryCalls(instance);
    HANDLE bootstrap = CreateThread(nullptr, 0, bootstrapMain, nullptr, 0, nullptr);
    if (bootstrap != nullptr) CloseHandle(bootstrap);
  }
  return TRUE;
}
