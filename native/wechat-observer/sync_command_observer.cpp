// NewSync 命令对象结构化转储诊断：在响应解析返回处枚举命令列表，输出每个命令的 CmdID、
// 虚表 RVA 与载荷对象类型，并对载荷对象做结构化字段扫描（字符串掩码骨架、时间戳与整数候选）。
// 只读取已解包的对象，不接触原始网络缓冲区，不输出账号、交易号或金额原文，结果只写本机文件。
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
constexpr std::size_t kQueueSize = 1024;
constexpr std::uint64_t kMaxStringBytes = 4096;
constexpr std::uint64_t kCoverageIntervalMs = 250;
constexpr DWORD64 kResumeFlag = 0x10000;
constexpr DWORD64 kSlot0EnableMask = 0x3;
constexpr DWORD64 kSlot0ControlMask = static_cast<DWORD64>(0xf) << 16;
constexpr DWORD64 kSlot1EnableMask = 0xc;
constexpr DWORD64 kSlot1ControlMask = static_cast<DWORD64>(0xf) << 20;

constexpr std::uint32_t kNewSyncParseRva = cfpay::target::kNewSyncParseRva;
constexpr std::uint32_t kNewSyncResponseVtableRva = 0x8fc0d48;
constexpr std::uint32_t kMaxCommandsPerResponse = 32;
constexpr std::uint32_t kEmptyStringSentinelRva = 0xb916de0;
constexpr std::uint32_t kPayloadScanSize = 0x140;
constexpr std::array<std::uint8_t, 32> kNewSyncParseSignature = {
    0x55, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x56, 0x57, 0x53, 0x48, 0x83, 0xec, 0x48,
    0x48, 0x8d, 0x6c, 0x24, 0x40, 0x48, 0xc7, 0x45, 0x00, 0xfe, 0xff, 0xff, 0xff, 0x48, 0x89, 0xd6,
};

constexpr LONG kLifecycleStopped = 0;
constexpr LONG kLifecycleStarting = 1;
constexpr LONG kLifecycleRunning = 2;
constexpr LONG kLifecycleStopping = 3;
constexpr LONG64 kMaxDumpLines = 60000;

struct ResponseLayout {
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

struct CommandListLayout {
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

struct CommandLayout {
  std::uintptr_t vtable;
  std::uintptr_t payload;
  std::int32_t commandId;
  std::uint32_t cachedSize;
  std::uint8_t presentMask;
  std::uint8_t tail[7];
};

static_assert(sizeof(ResponseLayout) == 0x38);
static_assert(offsetof(ResponseLayout, commandList) == 0x08);
static_assert(sizeof(CommandListLayout) == 0x30);
static_assert(offsetof(CommandListLayout, itemCount) == 0x10);
static_assert(sizeof(CommandLayout) == 0x20);
static_assert(offsetof(CommandLayout, commandId) == 0x10);

struct ThreadState {
  volatile LONG threadId;
  std::uintptr_t responseAddress;
  std::uintptr_t returnAddress;
  volatile LONG returnArmed;
};

struct DumpRecord {
  std::uint64_t sequence;
  std::int32_t commandId;
  std::int32_t index;
  char kind[24];
  char detail[640];
};

struct QueueSlot {
  volatile LONG state;
  DumpRecord record;
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
volatile LONG64 g_responseCount = 0;
volatile LONG64 g_commandCount = 0;
volatile LONG64 g_payloadCount = 0;
volatile LONG64 g_lineCount = 0;
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

// 转义 JSON 字符串。
std::string jsonEscape(const char* value) {
  std::string output;
  for (const unsigned char* cursor = reinterpret_cast<const unsigned char*>(value); *cursor != 0; cursor += 1) {
    if (*cursor < 0x20) continue;
    if (*cursor == '"' || *cursor == '\\') output.push_back('\\');
    output.push_back(static_cast<char>(*cursor));
  }
  return output;
}

// 向输出句柄写满指定字节数。
bool writeDumpBytes(const char* data, DWORD size) {
  DWORD total = 0;
  while (total < size) {
    DWORD written = 0;
    if (!WriteFile(g_dumpFile, data + total, size - total, &written, nullptr) || written == 0) return false;
    total += written;
  }
  return true;
}

// 写一行并立即刷新，保证宿主崩溃时已产出的记录不丢。
void writeLine(const std::string& line) {
  if (g_dumpFile == INVALID_HANDLE_VALUE) return;
  static constexpr char newline[] = "\r\n";
  const bool success = writeDumpBytes(line.data(), static_cast<DWORD>(line.size())) && writeDumpBytes(newline, 2);
  if (success) {
    FlushFileBuffers(g_dumpFile);
    InterlockedIncrement64(&g_lineCount);
  } else {
    InterlockedExchange(&g_outputBroken, 1);
  }
}

// 输出握手记录，告知宿主诊断 DLL 已就绪及其目标偏移。
void writePipeHandshake(const char* detailCode, const std::string& message) {
  if (g_outputFile == INVALID_HANDLE_VALUE) return;
  const std::string line = "{\"type\":\"status\",\"status\":\"HEALTHY\",\"detailCode\":\"" + std::string(detailCode) +
                           "\",\"message\":\"" + jsonEscape(message.c_str()) + "\",\"observedAt\":" +
                           std::to_string(unixTimeMilliseconds()) + "}\r\n";
  DWORD written = 0;
  WriteFile(g_outputFile, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
}

// 输出一行状态记录。
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

// 连接宿主创建的命名管道，带重试。
bool connectNamedPipe(const wchar_t* pipeName) {
  for (int attempt = 0; attempt < 300; attempt += 1) {
    g_outputFile = CreateFileW(pipeName, GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (g_outputFile != INVALID_HANDLE_VALUE) return true;
    if (GetLastError() != ERROR_PIPE_BUSY || !WaitNamedPipeW(pipeName, 100)) Sleep(50);
  }
  return false;
}

// 取目标模块的基址与结束地址。
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

// 把文本脱敏成形态串（只保留字符类别与长度），诊断输出不含原文。
std::string maskedShape(const char* value) {
  std::string output;
  for (const unsigned char* cursor = reinterpret_cast<const unsigned char*>(value); *cursor != 0; cursor += 1) {
    if (output.size() >= 72) break;
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

// 把一条转储记录放进固定大小的环形队列，绝不阻塞微信线程。
bool enqueueRecord(const DumpRecord& record) {
  if (InterlockedCompareExchange64(&g_lineCount, 0, 0) >= kMaxDumpLines) return false;
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

// 把绝对地址换算成相对模块基址的 RVA 文本，避免输出绝对地址。
std::string rvaText(std::uintptr_t address) {
  char buffer[32] = {};
  if (address >= g_moduleBase && address < g_moduleEnd) {
    std::snprintf(buffer, sizeof(buffer), "0x%llx", static_cast<unsigned long long>(address - g_moduleBase));
    return std::string("module+") + buffer;
  }
  std::snprintf(buffer, sizeof(buffer), "0x%llx", static_cast<unsigned long long>(address));
  return buffer;
}

// 扫描 CmdItem 载荷对象的字段布局，输出各偏移处的类型与形态。
// 这是定位 paymsg 结构化字段时用的研究工具，只输出形态与计数，不输出消息内容。
void scanPayloadObject(std::uintptr_t objectAddress, std::uint64_t sequence, std::int32_t commandId) {
  std::uintptr_t vtable = 0;
  if (!readMemory(reinterpret_cast<const void*>(objectAddress), &vtable, sizeof(vtable))) return;
  DumpRecord head{};
  head.sequence = sequence;
  head.commandId = commandId;
  head.index = -1;
  std::strncpy(head.kind, "payload_object", sizeof(head.kind) - 1);
  std::snprintf(head.detail, sizeof(head.detail), "object=%s vtable=%s", rvaText(objectAddress).c_str(),
                rvaText(vtable).c_str());
  enqueueRecord(head);
  if (vtable < g_moduleBase || vtable >= g_moduleEnd) return;

  // 布局取证：先输出载荷对象头部原始字节，再对其中每个堆指针做一层解引用转储，
  // 供离线还原真实字段位置。全部只读、只写本机文件，不接触原始网络流量。
  {
    std::uint8_t head[0x60] = {};
    if (readMemory(reinterpret_cast<const void*>(objectAddress), head, sizeof(head))) {
      for (std::uint32_t offset = 0; offset < sizeof(head); offset += 32) {
        DumpRecord record{};
        record.sequence = sequence;
        record.commandId = commandId;
        record.index = static_cast<std::int32_t>(offset);
        std::strncpy(record.kind, "payload_raw", sizeof(record.kind) - 1);
        static constexpr char hexDigits[] = "0123456789abcdef";
        std::size_t position = 0;
        for (std::uint32_t byteIndex = 0; byteIndex < 32 && position + 2 < sizeof(record.detail); byteIndex += 1) {
          record.detail[position++] = hexDigits[head[offset + byteIndex] >> 4];
          record.detail[position++] = hexDigits[head[offset + byteIndex] & 0x0f];
        }
        record.detail[position] = '\0';
        enqueueRecord(record);
      }
      for (std::uint32_t offset = 0; offset + 8 <= sizeof(head); offset += 8) {
        std::uintptr_t candidate = 0;
        std::memcpy(&candidate, head + offset, sizeof(candidate));
        if (candidate < 0x10000 || (candidate >= g_moduleBase && candidate < g_moduleEnd)) continue;
        std::uint8_t window[128] = {};
        if (!readMemory(reinterpret_cast<const void*>(candidate), window, sizeof(window))) continue;
        for (std::uint32_t chunk = 0; chunk < sizeof(window); chunk += 64) {
          DumpRecord record{};
          record.sequence = sequence;
          record.commandId = commandId;
          record.index = static_cast<std::int32_t>(offset);
          std::strncpy(record.kind, chunk == 0 ? "deref_hex" : "deref_hex2", sizeof(record.kind) - 1);
          static constexpr char hexDigits[] = "0123456789abcdef";
          std::size_t position = 0;
          for (std::uint32_t byteIndex = 0; byteIndex < 64 && position + 2 < sizeof(record.detail); byteIndex += 1) {
            record.detail[position++] = hexDigits[window[chunk + byteIndex] >> 4];
            record.detail[position++] = hexDigits[window[chunk + byteIndex] & 0x0f];
          }
          record.detail[position] = '\0';
          enqueueRecord(record);
        }
      }
    }
  }

  // CmdItem 载荷对象（Ghidra 还原）：+0x00 虚表，+0x08 指向 std::string 对象或空串哨兵，
  // +0x10 嵌套消息 int32 字段，+0x18 存在位图；std::string 对象内 +0x10 为长度、+0x18 为容量，
  // 容量 >= 16 时 +0x00 才是真实数据指针。必须多解一层引用才能取到 protobuf 字节。
  std::uintptr_t stringObject = 0;
  std::uintptr_t bufferAddress = 0;
  std::uint64_t bufferLength64 = 0;
  std::uint64_t bufferCapacity = 0;
  if (readMemory(reinterpret_cast<const void*>(objectAddress + 0x08), &stringObject, sizeof(stringObject)) &&
      stringObject > 0x10000 && stringObject != g_moduleBase + kEmptyStringSentinelRva &&
      readMemory(reinterpret_cast<const void*>(stringObject + 0x10), &bufferLength64, sizeof(bufferLength64)) &&
      readMemory(reinterpret_cast<const void*>(stringObject + 0x18), &bufferCapacity, sizeof(bufferCapacity)) &&
      bufferLength64 > 0 && bufferLength64 <= 65536 && bufferCapacity >= bufferLength64) {
    bufferAddress = stringObject;
    if (bufferCapacity >= 0x10) {
      readMemory(reinterpret_cast<const void*>(stringObject), &bufferAddress, sizeof(bufferAddress));
    }
  }
  if (bufferAddress > 0x10000 && bufferLength64 > 0) {
    const std::uint32_t bufferLength = static_cast<std::uint32_t>(bufferLength64);
    DumpRecord meta{};
    meta.sequence = sequence;
    meta.commandId = commandId;
    meta.index = -2;
    std::strncpy(meta.kind, "cmd_payload", sizeof(meta.kind) - 1);
    std::snprintf(meta.detail, sizeof(meta.detail), "stringObject=%s data=%s length=%u capacity=%llu",
                  rvaText(stringObject).c_str(), rvaText(bufferAddress).c_str(), bufferLength,
                  static_cast<unsigned long long>(bufferCapacity));
    enqueueRecord(meta);
    const std::uint32_t dumpLength = bufferLength > 4096 ? 4096 : bufferLength;
    for (std::uint32_t offset = 0; offset < dumpLength; offset += 128) {
      std::uint8_t chunk[128] = {};
      const std::uint32_t chunkSize = (dumpLength - offset) < 128 ? (dumpLength - offset) : 128;
      if (!readMemory(reinterpret_cast<const void*>(bufferAddress + offset), chunk, chunkSize)) {
        InterlockedIncrement64(&g_readFailureCount);
        break;
      }
      DumpRecord record{};
      record.sequence = sequence;
      record.commandId = commandId;
      record.index = static_cast<std::int32_t>(offset);
      std::strncpy(record.kind, "cmd_payload_hex", sizeof(record.kind) - 1);
      static constexpr char hexDigits[] = "0123456789abcdef";
      std::size_t position = 0;
      for (std::uint32_t byteIndex = 0; byteIndex < chunkSize && position + 2 < sizeof(record.detail);
           byteIndex += 1) {
        record.detail[position++] = hexDigits[chunk[byteIndex] >> 4];
        record.detail[position++] = hexDigits[chunk[byteIndex] & 0x0f];
      }
      record.detail[position] = '\0';
      enqueueRecord(record);
    }
  }

  const std::uint64_t nowMs = unixTimeMilliseconds();
  const std::uint64_t nowSeconds = nowMs / 1000;
  for (std::uint32_t offset = 0; offset + 8 <= kPayloadScanSize; offset += 8) {
    char value[1024] = {};
    if (readMsvcStringAt(objectAddress + offset, value, sizeof(value))) {
      DumpRecord record{};
      record.sequence = sequence;
      record.commandId = commandId;
      record.index = static_cast<std::int32_t>(offset);
      std::strncpy(record.kind, "payload_string", sizeof(record.kind) - 1);
      const std::string shape = maskedShape(value);
      std::snprintf(record.detail, sizeof(record.detail), "bytes=%zu shape=%s", std::strlen(value), shape.c_str());
      enqueueRecord(record);
      // 协议体全文只写本机文件，用于确认是否存在结构化命名字段；分析完成后删除转储文件。
      if (std::strlen(value) >= 32) {
        DumpRecord text{};
        text.sequence = sequence;
        text.commandId = commandId;
        text.index = static_cast<std::int32_t>(offset);
        std::strncpy(text.kind, "payload_text", sizeof(text.kind) - 1);
        std::strncpy(text.detail, value, sizeof(text.detail) - 1);
        enqueueRecord(text);
      }
      continue;
    }
    std::uint64_t quad = 0;
    if (!readMemory(reinterpret_cast<const void*>(objectAddress + offset), &quad, sizeof(quad))) continue;
    const char* kind = nullptr;
    if (quad > nowSeconds - 172800 && quad < nowSeconds + 3600) kind = "payload_ts_s";
    else if (quad > nowMs - 172800000ULL && quad < nowMs + 3600000ULL) kind = "payload_ts_ms";
    if (kind != nullptr) {
      DumpRecord record{};
      record.sequence = sequence;
      record.commandId = commandId;
      record.index = static_cast<std::int32_t>(offset);
      std::strncpy(record.kind, kind, sizeof(record.kind) - 1);
      std::snprintf(record.detail, sizeof(record.detail), "value=%llu", static_cast<unsigned long long>(quad));
      enqueueRecord(record);
      continue;
    }
    std::uint32_t dword = static_cast<std::uint32_t>(quad & 0xffffffffULL);
    if (dword > 0 && dword < 10000000 && (quad >> 32) == 0) {
      DumpRecord record{};
      record.sequence = sequence;
      record.commandId = commandId;
      record.index = static_cast<std::int32_t>(offset);
      std::strncpy(record.kind, "payload_i32", sizeof(record.kind) - 1);
      std::snprintf(record.detail, sizeof(record.detail), "value=%u", dword);
      enqueueRecord(record);
    }
  }
}

// 为一次 NewSyncResponse 命中分配序号，遍历命令列表并逐个转储载荷对象。
void dumpResponse(std::uintptr_t responseAddress) {
  const std::uint64_t sequence = static_cast<std::uint64_t>(InterlockedIncrement64(&g_sequence));
  ResponseLayout response{};
  if (!readMemory(reinterpret_cast<const void*>(responseAddress), &response, sizeof(response))) {
    InterlockedIncrement64(&g_readFailureCount);
    return;
  }
  if (response.vtable != g_moduleBase + kNewSyncResponseVtableRva) return;
  InterlockedIncrement64(&g_responseCount);

  DumpRecord head{};
  head.sequence = sequence;
  head.commandId = 0;
  head.index = -1;
  std::strncpy(head.kind, "sync_response", sizeof(head.kind) - 1);
  std::snprintf(head.detail, sizeof(head.detail), "retCode=%d continueFlag=%u status=%u serverTime=%u list=%s",
                response.retCode, response.continueFlag, response.status, response.serverTime,
                rvaText(response.commandList).c_str());
  enqueueRecord(head);
  if (response.commandList == 0) return;

  CommandListLayout commands{};
  if (!readMemory(reinterpret_cast<const void*>(response.commandList), &commands, sizeof(commands)) ||
      commands.itemCount < 0 || commands.itemCount > 4096 || commands.items == 0) {
    InterlockedIncrement64(&g_readFailureCount);
    return;
  }
  const std::uint32_t captureCount = static_cast<std::uint32_t>(
      commands.itemCount < static_cast<std::int32_t>(kMaxCommandsPerResponse)
          ? commands.itemCount
          : static_cast<std::int32_t>(kMaxCommandsPerResponse));
  for (std::uint32_t index = 0; index < captureCount; index += 1) {
    std::uintptr_t commandAddress = 0;
    CommandLayout command{};
    if (!readMemory(reinterpret_cast<const void*>(commands.items + index * sizeof(std::uintptr_t)), &commandAddress,
                    sizeof(commandAddress)) ||
        commandAddress == 0 || !readMemory(reinterpret_cast<const void*>(commandAddress), &command, sizeof(command))) {
      InterlockedIncrement64(&g_readFailureCount);
      break;
    }
    InterlockedIncrement64(&g_commandCount);
    DumpRecord record{};
    record.sequence = sequence;
    record.commandId = command.commandId;
    record.index = static_cast<std::int32_t>(index);
    std::strncpy(record.kind, "sync_command", sizeof(record.kind) - 1);
    const bool payloadInModule = command.payload >= g_moduleBase && command.payload < g_moduleEnd;
    std::snprintf(record.detail, sizeof(record.detail), "vtable=%s payload=%s payloadKind=%s presentMask=%u",
                  rvaText(command.vtable).c_str(), rvaText(command.payload).c_str(),
                  command.payload == 0 ? "null" : (payloadInModule ? "module" : "heap"), command.presentMask);
    enqueueRecord(record);
    if (command.payload != 0 && !payloadInModule) {
      InterlockedIncrement64(&g_payloadCount);
      scanPayloadObject(command.payload, sequence, command.commandId);
    }
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
        if (state != nullptr && InterlockedCompareExchange(&state->returnArmed, 1, 1) == 1 &&
            state->returnAddress != 0) {
          context.Dr1 = static_cast<DWORD64>(state->returnAddress);
          context.Dr7 &= ~(kSlot1EnableMask | kSlot1ControlMask);
          context.Dr7 |= 0x4;
        }
      } else {
        if (context.Dr0 == reinterpret_cast<DWORD64>(g_hookAddress)) {
          context.Dr0 = 0;
          context.Dr7 &= ~(kSlot0EnableMask | kSlot0ControlMask);
        }
        if (state != nullptr && context.Dr1 == static_cast<DWORD64>(state->returnAddress)) {
          context.Dr1 = 0;
          context.Dr7 &= ~(kSlot1EnableMask | kSlot1ControlMask);
        }
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

// 向量化异常处理器：只处理单步异常并转储命中的响应对象。
LONG CALLBACK vectoredHandler(PEXCEPTION_POINTERS exception) {
  if (exception->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP) return EXCEPTION_CONTINUE_SEARCH;
  CONTEXT* context = exception->ContextRecord;
  ThreadState* state = findThreadState(GetCurrentThreadId());
  if (state == nullptr) return EXCEPTION_CONTINUE_SEARCH;
  bool handled = false;

  if ((context->Dr6 & 0x1) != 0 && exception->ExceptionRecord->ExceptionAddress == g_hookAddress) {
    std::uintptr_t returnAddress = 0;
    if (readMemory(reinterpret_cast<const void*>(context->Rsp), &returnAddress, sizeof(returnAddress)) &&
        returnAddress != 0) {
      state->responseAddress = static_cast<std::uintptr_t>(context->Rcx);
      state->returnAddress = returnAddress;
      InterlockedExchange(&state->returnArmed, 1);
      context->Dr1 = static_cast<DWORD64>(returnAddress);
      context->Dr7 &= ~(kSlot1EnableMask | kSlot1ControlMask);
      context->Dr7 |= 0x4;
    } else {
      InterlockedIncrement64(&g_readFailureCount);
    }
    handled = true;
  }

  if ((context->Dr6 & 0x2) != 0 && InterlockedCompareExchange(&state->returnArmed, 1, 1) == 1 &&
      reinterpret_cast<std::uintptr_t>(exception->ExceptionRecord->ExceptionAddress) == state->returnAddress) {
    dumpResponse(state->responseAddress);
    context->Dr1 = 0;
    context->Dr7 &= ~(kSlot1EnableMask | kSlot1ControlMask);
    state->returnAddress = 0;
    state->responseAddress = 0;
    InterlockedExchange(&state->returnArmed, 0);
    handled = true;
  }

  if (!handled) return EXCEPTION_CONTINUE_SEARCH;
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
        writeLine("{\"type\":\"" + std::string(slot.record.kind) + "\",\"seq\":" +
                  std::to_string(slot.record.sequence) + ",\"cmd\":" + std::to_string(slot.record.commandId) +
                  ",\"index\":" + std::to_string(slot.record.index) + ",\"detail\":\"" +
                  jsonEscape(slot.record.detail) + "\",\"at\":" + std::to_string(unixTimeMilliseconds()) + "}");
        InterlockedExchange(&slot.state, 0);
      }
    }
    const std::uint64_t now = unixTimeMilliseconds();
    if (InterlockedCompareExchange(&g_running, 1, 1) != 0 && now >= nextCoverageCheck) {
      updateAllThreadBreakpoints(true);
      nextCoverageCheck = now + kCoverageIntervalMs;
    }
    if (InterlockedCompareExchange(&g_running, 1, 1) != 0 && now >= nextHealthReport) {
      int threadCount = 0;
      for (auto& state : g_threads) {
        if (InterlockedCompareExchange(&state.threadId, 0, 0) != 0) threadCount += 1;
      }
      writeStatus("HEALTHY", "SYNC_DUMP_ACTIVE",
                  "NewSync命令转储运行中; responses=" +
                      std::to_string(InterlockedCompareExchange64(&g_responseCount, 0, 0)) + "; commands=" +
                      std::to_string(InterlockedCompareExchange64(&g_commandCount, 0, 0)) + "; payloads=" +
                      std::to_string(InterlockedCompareExchange64(&g_payloadCount, 0, 0)) + "; lines=" +
                      std::to_string(InterlockedCompareExchange64(&g_lineCount, 0, 0)) + "; readFailures=" +
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

  HMODULE weixin = GetModuleHandleW(L"Weixin.dll");
  if (weixin == nullptr || !moduleBounds(weixin, &g_moduleBase, &g_moduleEnd)) {
    writePipeHandshake("PROCESS_NOT_FOUND", "目标进程未加载固定版本 Weixin.dll");
    return false;
  }
  std::array<std::uint8_t, kNewSyncParseSignature.size()> signature{};
  if (!readMemory(reinterpret_cast<const void*>(g_moduleBase + kNewSyncParseRva), signature.data(),
                  signature.size()) ||
      signature != kNewSyncParseSignature) {
    writePipeHandshake("SIGNATURE_MISMATCH", "NewSyncResponse解析入口签名不匹配");
    return false;
  }
  wchar_t modulePath[MAX_PATH] = {};
  HMODULE self = GetModuleHandleW(L"cfpay-hook-sync-diagnostic.dll");
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
  static constexpr wchar_t kDumpFileName[] = L"sync-dump.jsonl";
  wchar_t dumpPath[MAX_PATH + 64] = {};
  const std::size_t directoryLength = std::wcslen(modulePath);
  if (directoryLength + (sizeof(kDumpFileName) / sizeof(wchar_t)) >= MAX_PATH + 64) {
    writePipeHandshake("HOOK_INSTALL_FAILED", "诊断 DLL 路径过长");
    return false;
  }
  std::memcpy(dumpPath, modulePath, directoryLength * sizeof(wchar_t));
  std::memcpy(dumpPath + directoryLength, kDumpFileName, sizeof(kDumpFileName));
  g_dumpFile = CreateFileW(dumpPath, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                           nullptr);
  if (g_dumpFile == INVALID_HANDLE_VALUE) {
    writePipeHandshake("HOOK_INSTALL_FAILED",
                       "NewSync 转储文件无法创建 winError=" + std::to_string(GetLastError()));
    return false;
  }
  g_hookAddress = reinterpret_cast<void*>(g_moduleBase + kNewSyncParseRva);
  char dumpPathUtf8[MAX_PATH * 3] = {};
  WideCharToMultiByte(CP_UTF8, 0, dumpPath, -1, dumpPathUtf8, sizeof(dumpPathUtf8) - 1, nullptr, nullptr);
  writeStatus("HEALTHY", "SYNC_DUMP_READY", std::string("转储文件已创建 path=") + dumpPathUtf8);
  writePipeHandshake("SYNC_DUMP_ACTIVE", std::string("NewSync命令转储断点已安装 path=") + dumpPathUtf8);
  return true;
}

// 安装流程：连接输出、校验、注册异常处理器、装断点、启动工作线程。
DWORD WINAPI bootstrapMain(void*) {
  if (InterlockedCompareExchange(&g_lifecycle, kLifecycleStarting, kLifecycleStopped) != kLifecycleStopped) return 4;
  InterlockedExchange(&g_running, 0);
  InterlockedExchange(&g_stopResult, FALSE);
  InterlockedExchange(&g_outputBroken, 0);
  InterlockedExchange64(&g_sequence, 0);
  InterlockedExchange64(&g_responseCount, 0);
  InterlockedExchange64(&g_commandCount, 0);
  InterlockedExchange64(&g_payloadCount, 0);
  InterlockedExchange64(&g_lineCount, 0);
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
    writeStatus("ERROR", "HOOK_INSTALL_FAILED", "NewSync命令转储断点安装失败");
    if (g_veh != nullptr) {
      RemoveVectoredExceptionHandler(g_veh);
      g_veh = nullptr;
    }
    if (g_queueEvent != nullptr) {
      CloseHandle(g_queueEvent);
      g_queueEvent = nullptr;
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
    writeStatus("ERROR", "HOOK_INSTALL_FAILED", "NewSync命令转储工作线程创建失败");
    CloseHandle(g_queueEvent);
    g_queueEvent = nullptr;
    InterlockedExchange(&g_running, 0);
    InterlockedExchange(&g_lifecycle, kLifecycleStopped);
    return 3;
  }

  InterlockedExchange(&g_lifecycle, kLifecycleRunning);
  writeStatus("HEALTHY", "SYNC_DUMP_ACTIVE", "NewSync命令转储断点已安装");
  return 0;
}

// 停止流程：清除所有断点并摘除异常处理器。
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
              cleared ? "NewSync命令转储断点已清理" : "部分线程断点未能清理");
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
  return 0x000b0000;
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
