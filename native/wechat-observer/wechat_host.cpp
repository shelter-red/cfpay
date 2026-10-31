#include <windows.h>
#include <bcrypt.h>
#include <objbase.h>
#include <shobjidl.h>
#include <tlhelp32.h>
#include <winver.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>

#include "wechat_target.h"

namespace {

// 目标版本、文件指纹与 RVA 全部来自 wechat_target.h，这里只做本地别名，
// 避免宿主与观察器各写一份、升级微信版本时漏改导致校验与断点不是同一个目标。
constexpr std::uint64_t kExpectedFileSize = cfpay::target::kWeixinFileSize;
constexpr const wchar_t* kExpectedVersion = cfpay::target::kWeixinVersion;
constexpr const char* kExpectedSha256 = cfpay::target::kWeixinSha256;
constexpr std::uint64_t kExpectedWmpfFileSize = cfpay::target::kWmpfFileSize;
constexpr const char* kExpectedWmpfSha256 = cfpay::target::kWmpfSha256;
constexpr std::uint32_t kParserRva = cfpay::target::kNewSyncParseRva;
constexpr std::uint32_t kAccountRva = cfpay::target::kAccountRva;
constexpr std::uint32_t kAccountCallsiteRva = cfpay::target::kAccountCallsiteRva;
constexpr std::uint32_t kNicknameRva = cfpay::target::kNicknameRva;
constexpr DWORD kObserverApiVersion = 0x000a0000;
constexpr std::uint64_t kParserFileOffset = 0x10af630;
constexpr std::uint64_t kAccountFileOffset = 0x0e07370;
constexpr std::uint64_t kAccountCallsiteFileOffset = 0x291503e;
constexpr std::uint64_t kNicknameFileOffset = 0x004aa90;
constexpr std::array<std::uint64_t, 4> kWmpfCandidateFileOffsets = {
    0x2d16df0, 0x2d21500, 0x2d4aec0, 0x2d59800};
constexpr std::array<std::uint8_t, 32> kParserSignature = {
    0x55, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x56, 0x57, 0x53, 0x48, 0x83, 0xec, 0x48,
    0x48, 0x8d, 0x6c, 0x24, 0x40, 0x48, 0xc7, 0x45, 0x00, 0xfe, 0xff, 0xff, 0xff, 0x48, 0x89, 0xd6,
};
constexpr std::array<std::uint8_t, 32> kAccountSignature = {
    0x55, 0x56, 0x57, 0x53, 0x48, 0x81, 0xec, 0xd8, 0x00, 0x00, 0x00, 0x48, 0x8d, 0xac, 0x24, 0x80,
    0x00, 0x00, 0x00, 0x48, 0xc7, 0x45, 0x50, 0xfe, 0xff, 0xff, 0xff, 0x48, 0x89, 0xd6, 0x48, 0x89,
};
constexpr std::array<std::uint8_t, 5> kAccountCallsiteSignature = {0xe8, 0x2d, 0x23, 0x4f, 0xfe};
constexpr std::array<std::uint8_t, 32> kNicknameSignature = {
    0x55, 0x41, 0x57, 0x41, 0x56, 0x41, 0x54, 0x56, 0x57, 0x53, 0x48, 0x83, 0xec, 0x50, 0x48, 0x8d,
    0x6c, 0x24, 0x50, 0x48, 0xc7, 0x45, 0xf8, 0xfe, 0xff, 0xff, 0xff, 0x48, 0x83, 0x7a, 0x38, 0x00,
};
constexpr std::array<std::array<std::uint8_t, 32>, 4> kWmpfCandidateSignatures = {{
    {0x41, 0x56, 0x56, 0x57, 0x53, 0x48, 0x81, 0xec, 0x78, 0x01, 0x00, 0x00, 0x4c, 0x89, 0xce, 0x4c,
     0x89, 0xc3, 0x48, 0x89, 0xd7, 0x4c, 0x8b, 0xb4, 0x24, 0xc0, 0x01, 0x00, 0x00, 0xe8, 0x4e, 0xf6},
    {0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x56, 0x57, 0x55, 0x53, 0x48, 0x81, 0xec, 0xa8,
     0x01, 0x00, 0x00, 0x4d, 0x89, 0xc6, 0x48, 0x89, 0xd7, 0x4c, 0x8b, 0xa9, 0x98, 0x00, 0x00, 0x00},
    {0x41, 0x57, 0x41, 0x56, 0x56, 0x57, 0x53, 0x48, 0x81, 0xec, 0x90, 0x01, 0x00, 0x00, 0x4c, 0x89,
     0xc6, 0x48, 0x89, 0xd7, 0x48, 0x89, 0xcb, 0x83, 0x3d, 0xb2, 0x6f, 0xf3, 0x0a, 0x02, 0x0f, 0x8f},
    {0x41, 0x57, 0x41, 0x56, 0x56, 0x57, 0x53, 0x48, 0x81, 0xec, 0x70, 0x01, 0x00, 0x00, 0x4c, 0x89,
     0xce, 0x4c, 0x89, 0xc7, 0x48, 0x8b, 0x41, 0x18, 0x48, 0x8b, 0x80, 0x40, 0x06, 0x00, 0x00, 0x48},
}};

// 转义 JSON 字符串里的引号、反斜杠与控制字符，观察记录通过管道以 JSONL 传给监控端，转义不完整会整行解析失败。
std::string jsonEscape(const std::string& value) {
  std::string output;
  for (const unsigned char character : value) {
    if (character == '\\') output += "\\\\";
    else if (character == '"') output += "\\\"";
    else if (character == '\n') output += "\\n";
    else if (character == '\r') output += "\\r";
    else if (character == '\t') output += "\\t";
    else output.push_back(static_cast<char>(character));
  }
  return output;
}

// 向标准输出写一行状态 JSONL；宿主与监控端之间只通过这条单向管道通信，不共享内存。
void emitStatus(const char* status, const char* detailCode, const std::string& message) {
  std::printf(
      "{\"type\":\"status\",\"component\":\"wechat_hook\",\"status\":\"%s\",\"detailCode\":\"%s\",\"message\":\"%s\"}\n",
      status,
      detailCode,
      jsonEscape(message).c_str());
}

// 宽字符转 UTF-8，空指针与空串统一返回空字符串。
std::string utf8FromWide(const wchar_t* value) {
  if (value == nullptr || *value == L'\0') return {};
  const int size = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
  if (size <= 1) return {};
  std::string output(static_cast<std::size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, value, -1, output.data(), size, nullptr, nullptr);
  output.resize(static_cast<std::size_t>(size - 1));
  return output;
}

// 弹出系统文件选择框让管理员本人挑选 Weixin.exe；只在配置模式下使用，进程路径不靠猜测或注册表扫描。
int selectWeixinExecutable() {
  const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
  if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE) return 20;
  const bool uninitialize = SUCCEEDED(initialized);

  IFileOpenDialog* dialog = nullptr;
  HRESULT result = CoCreateInstance(
      CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_IFileOpenDialog, reinterpret_cast<void**>(&dialog));
  if (SUCCEEDED(result)) {
    const COMDLG_FILTERSPEC filters[] = {{L"Weixin.exe", L"Weixin.exe"}};
    FILEOPENDIALOGOPTIONS options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST | FOS_FORCEFILESYSTEM | FOS_NOCHANGEDIR);
    dialog->SetFileTypes(1, filters);
    dialog->SetFileTypeIndex(1);
    dialog->SetDefaultExtension(L"exe");
    dialog->SetFileName(L"Weixin.exe");
    dialog->SetTitle(L"Select Weixin.exe");
    result = dialog->Show(nullptr);
  }

  int exitCode = 20;
  if (SUCCEEDED(result)) {
    IShellItem* item = nullptr;
    PWSTR selectedPath = nullptr;
    result = dialog->GetResult(&item);
    if (SUCCEEDED(result)) result = item->GetDisplayName(SIGDN_FILESYSPATH, &selectedPath);
    const wchar_t* selectedName = selectedPath == nullptr ? nullptr : std::wcsrchr(selectedPath, L'\\');
    selectedName = selectedName == nullptr ? selectedPath : selectedName + 1;
    if (SUCCEEDED(result) && selectedName != nullptr && _wcsicmp(selectedName, L"Weixin.exe") == 0) {
      std::printf("%s\n", utf8FromWide(selectedPath).c_str());
      exitCode = 0;
    }
    if (selectedPath != nullptr) CoTaskMemFree(selectedPath);
    if (item != nullptr) item->Release();
  } else if (result == HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
    exitCode = 21;
  }

  if (dialog != nullptr) dialog->Release();
  if (uninitialize) CoUninitialize();
  return exitCode;
}

// 从文件指定偏移精确读取固定字节数，读不满即失败——用于校验函数签名字节。
bool readExact(HANDLE file, std::uint64_t offset, void* output, DWORD size) {
  LARGE_INTEGER position{};
  position.QuadPart = static_cast<LONGLONG>(offset);
  if (!SetFilePointerEx(file, position, nullptr, FILE_BEGIN)) return false;
  DWORD bytesRead = 0;
  return ReadFile(file, output, size, &bytesRead, nullptr) != FALSE && bytesRead == size;
}

// 分块计算整个文件的 SHA-256；以共享读方式打开，微信正在运行时也能校验磁盘文件。
bool calculateSha256(const wchar_t* path, std::string* output) {
  HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;

  BCRYPT_ALG_HANDLE algorithm = nullptr;
  BCRYPT_HASH_HANDLE hash = nullptr;
  DWORD objectLength = 0;
  DWORD hashLength = 0;
  DWORD resultLength = 0;
  bool success = false;
  std::vector<std::uint8_t> object;
  std::vector<std::uint8_t> digest;
  std::vector<std::uint8_t> buffer(1024 * 1024);

  if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0 ||
      BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectLength), sizeof(objectLength),
                        &resultLength, 0) < 0 ||
      BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&hashLength), sizeof(hashLength), &resultLength,
                        0) < 0) {
    goto cleanup;
  }

  object.resize(objectLength);
  digest.resize(hashLength);
  if (BCryptCreateHash(algorithm, &hash, object.data(), objectLength, nullptr, 0, 0) < 0) goto cleanup;

  while (true) {
    DWORD bytesRead = 0;
    if (!ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead, nullptr)) goto cleanup;
    if (bytesRead == 0) break;
    if (BCryptHashData(hash, buffer.data(), bytesRead, 0) < 0) goto cleanup;
  }
  if (BCryptFinishHash(hash, digest.data(), hashLength, 0) < 0) goto cleanup;

  output->clear();
  static constexpr char hex[] = "0123456789abcdef";
  for (const std::uint8_t byte : digest) {
    output->push_back(hex[byte >> 4]);
    output->push_back(hex[byte & 0x0f]);
  }
  success = true;

cleanup:
  if (hash != nullptr) BCryptDestroyHash(hash);
  if (algorithm != nullptr) BCryptCloseAlgorithmProvider(algorithm, 0);
  CloseHandle(file);
  return success;
}

// 读取 PE 版本资源里的文件版本号，用于与受支持版本逐字比对。
bool getFileVersion(const wchar_t* path, std::wstring* output) {
  DWORD unused = 0;
  const DWORD size = GetFileVersionInfoSizeW(path, &unused);
  if (size == 0) return false;
  std::vector<std::uint8_t> data(size);
  if (!GetFileVersionInfoW(path, 0, size, data.data())) return false;

  VS_FIXEDFILEINFO* info = nullptr;
  UINT infoSize = 0;
  if (!VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(&info), &infoSize) || infoSize < sizeof(*info)) {
    return false;
  }

  wchar_t version[64] = {};
  ::swprintf(version,
             L"%u.%u.%u.%u",
             HIWORD(info->dwFileVersionMS),
             LOWORD(info->dwFileVersionMS),
             HIWORD(info->dwFileVersionLS),
             LOWORD(info->dwFileVersionLS));
  *output = version;
  return true;
}

// 安装前对磁盘上的 Weixin.dll 做全量静态校验：文件大小、版本资源、SHA-256，以及四个 Hook 目标处的签名字节。
// 任何一项不符都返回 UNSUPPORTED 类诊断码并拒绝注入，避免偏移对不上时把断点打到错误地址导致微信崩溃。
bool inspectWeixinFile(const wchar_t* path, std::string* detailCode, std::string* message) {
  HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    *detailCode = "FILE_NOT_FOUND";
    *message = "Weixin.dll could not be opened";
    return false;
  }

  LARGE_INTEGER size{};
  if (!GetFileSizeEx(file, &size) || static_cast<std::uint64_t>(size.QuadPart) != kExpectedFileSize) {
    CloseHandle(file);
    *detailCode = "SIGNATURE_MISMATCH";
    *message = "Weixin.dll file size does not match";
    return false;
  }

  std::array<std::uint8_t, 32> parserBytes{};
  std::array<std::uint8_t, 32> accountBytes{};
  std::array<std::uint8_t, 5> accountCallsiteBytes{};
  std::array<std::uint8_t, 32> nicknameBytes{};
  const bool bytesMatch = readExact(file, kParserFileOffset, parserBytes.data(), static_cast<DWORD>(parserBytes.size())) &&
                          readExact(file, kAccountFileOffset, accountBytes.data(), static_cast<DWORD>(accountBytes.size())) &&
                          readExact(file, kAccountCallsiteFileOffset, accountCallsiteBytes.data(),
                                    static_cast<DWORD>(accountCallsiteBytes.size())) &&
                          readExact(file, kNicknameFileOffset, nicknameBytes.data(),
                                    static_cast<DWORD>(nicknameBytes.size())) &&
                          parserBytes == kParserSignature &&
                          accountBytes == kAccountSignature && accountCallsiteBytes == kAccountCallsiteSignature &&
                          nicknameBytes == kNicknameSignature;
  CloseHandle(file);
  if (!bytesMatch) {
    *detailCode = "SIGNATURE_MISMATCH";
    *message = "NewSync parser, account reader, nickname reader, or account callsite bytes do not match";
    return false;
  }

  std::int32_t accountRelativeTarget = 0;
  std::memcpy(&accountRelativeTarget, accountCallsiteBytes.data() + 1, sizeof(accountRelativeTarget));
  if (static_cast<std::uint32_t>(kAccountCallsiteRva + accountCallsiteBytes.size() + accountRelativeTarget) !=
      kAccountRva) {
    *detailCode = "SIGNATURE_MISMATCH";
    *message = "account callsite target does not resolve to the expected RVA";
    return false;
  }

  std::wstring version;
  if (!getFileVersion(path, &version) || version != kExpectedVersion) {
    *detailCode = "VERSION_MISMATCH";
    *message = "Weixin.dll version does not match the supported target";
    return false;
  }

  std::string sha256;
  if (!calculateSha256(path, &sha256) || sha256 != kExpectedSha256) {
    *detailCode = "SIGNATURE_MISMATCH";
    *message = "Weixin.dll SHA-256 does not match";
    return false;
  }

  *detailCode = "TARGET_VALIDATED";
  *message = "Weixin.dll version, SHA-256, NewSync parser, account, and nickname signatures are valid";
  return true;
}

// 对小程序渲染进程的 flue.dll 做同样的静态校验，仅诊断构建使用。
bool inspectWmpfFile(const wchar_t* path, std::string* detailCode, std::string* message) {
  HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    *detailCode = "FILE_NOT_FOUND";
    *message = "flue.dll could not be opened";
    return false;
  }

  LARGE_INTEGER size{};
  bool signaturesMatch = GetFileSizeEx(file, &size) != FALSE &&
                         static_cast<std::uint64_t>(size.QuadPart) == kExpectedWmpfFileSize;
  for (std::size_t index = 0; signaturesMatch && index < kWmpfCandidateFileOffsets.size(); index += 1) {
    std::array<std::uint8_t, 32> bytes{};
    signaturesMatch = readExact(file,
                                kWmpfCandidateFileOffsets[index],
                                bytes.data(),
                                static_cast<DWORD>(bytes.size())) &&
                      bytes == kWmpfCandidateSignatures[index];
  }
  CloseHandle(file);
  if (!signaturesMatch) {
    *detailCode = "SIGNATURE_MISMATCH";
    *message = "flue.dll size or WMPF render candidate signatures do not match";
    return false;
  }

  std::string sha256;
  if (!calculateSha256(path, &sha256) || sha256 != kExpectedWmpfSha256) {
    *detailCode = "SIGNATURE_MISMATCH";
    *message = "flue.dll SHA-256 does not match";
    return false;
  }

  *detailCode = "TARGET_VALIDATED";
  *message = "flue.dll SHA-256 and WMPF render candidate signatures are valid";
  return true;
}

// 取进程可执行文件的完整路径；只用 PROCESS_QUERY_LIMITED_INFORMATION，不申请更高权限。
bool getProcessPath(DWORD processId, std::wstring* output) {
  HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
  if (!process) return false;
  std::vector<wchar_t> path(32768);
  DWORD length = static_cast<DWORD>(path.size());
  const bool success = QueryFullProcessImageNameW(process, 0, path.data(), &length) != FALSE;
  CloseHandle(process);
  if (!success) return false;
  output->assign(path.data(), length);
  return true;
}

// 从完整路径里取文件名部分，兼容反斜杠分隔。
const wchar_t* baseName(const std::wstring& path) {
  const wchar_t* slash = std::wcsrchr(path.c_str(), L'\\');
  return slash ? slash + 1 : path.c_str();
}

// 在目标进程的模块快照里按名字找模块基址，找不到返回 0。
std::uintptr_t findRemoteModuleBase(DWORD processId, const wchar_t* moduleName) {
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, processId);
  if (snapshot == INVALID_HANDLE_VALUE) return 0;

  MODULEENTRY32W entry{};
  entry.dwSize = sizeof(entry);
  std::uintptr_t result = 0;
  if (Module32FirstW(snapshot, &entry)) {
    do {
      if (_wcsicmp(entry.szModule, moduleName) == 0) {
        result = reinterpret_cast<std::uintptr_t>(entry.modBaseAddr);
        break;
      }
    } while (Module32NextW(snapshot, &entry));
  }
  CloseHandle(snapshot);
  return result;
}

// 在目标进程的模块快照里按名字找模块的磁盘路径，用于确认已加载的观察 DLL 就是我们释放的那一份。
bool findRemoteModulePath(DWORD processId, const wchar_t* moduleName, std::wstring* output) {
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, processId);
  if (snapshot == INVALID_HANDLE_VALUE) return false;

  MODULEENTRY32W entry{};
  entry.dwSize = sizeof(entry);
  bool found = false;
  if (Module32FirstW(snapshot, &entry)) {
    do {
      if (_wcsicmp(entry.szModule, moduleName) == 0) {
        *output = entry.szExePath;
        found = true;
        break;
      }
    } while (Module32NextW(snapshot, &entry));
  }
  CloseHandle(snapshot);
  return found;
}

bool injectRestrictedDll(
    DWORD processId,
    const wchar_t* dllPath,
    const wchar_t* expectedDllName,
    const wchar_t* expectedProcessName,
    const char* restrictionMessage,
    const char* successMessage,
    const char* failureMessage,
    std::string* message) {
  std::wstring processPath;
  if (!getProcessPath(processId, &processPath) || _wcsicmp(baseName(processPath), expectedProcessName) != 0) {
    *message = restrictionMessage;
    return false;
  }

  wchar_t absoluteDllPath[32768] = {};
  if (GetFullPathNameW(dllPath, 32768, absoluteDllPath, nullptr) == 0 || GetFileAttributesW(absoluteDllPath) == INVALID_FILE_ATTRIBUTES) {
    *message = "observer DLL path is invalid";
    return false;
  }
  if (_wcsicmp(baseName(absoluteDllPath), expectedDllName) != 0) {
    *message = "observer DLL filename is not allowed for this mode";
    return false;
  }

  HANDLE process = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION | PROCESS_VM_WRITE |
                                   PROCESS_VM_READ | SYNCHRONIZE,
                               FALSE,
                               processId);
  if (!process) {
    *message = "OpenProcess failed; win32=" + std::to_string(GetLastError());
    return false;
  }

  const SIZE_T pathBytes = (std::wcslen(absoluteDllPath) + 1) * sizeof(wchar_t);
  void* remotePath = VirtualAllocEx(process, nullptr, pathBytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
  bool success = false;
  HANDLE remoteThread = nullptr;
  HMODULE kernel32 = nullptr;
  FARPROC loadLibrary = nullptr;
  LPTHREAD_START_ROUTINE startRoutine = nullptr;
  DWORD remoteModule = 0;
  std::uintptr_t remoteKernel32 = 0;
  std::uintptr_t remoteLoadLibrary = 0;
  const char* failureStage = "unknown";
  DWORD failureError = ERROR_SUCCESS;
  if (remotePath == nullptr) {
    failureStage = "VirtualAllocEx";
    failureError = GetLastError();
    goto cleanup;
  }
  if (!WriteProcessMemory(process, remotePath, absoluteDllPath, pathBytes, nullptr)) {
    failureStage = "WriteProcessMemory";
    failureError = GetLastError();
    goto cleanup;
  }

  kernel32 = GetModuleHandleW(L"kernel32.dll");
  loadLibrary = GetProcAddress(kernel32, "LoadLibraryW");
  if (kernel32 == nullptr || loadLibrary == nullptr) {
    failureStage = "GetProcAddress";
    failureError = GetLastError();
    goto cleanup;
  }

  remoteKernel32 = findRemoteModuleBase(processId, L"kernel32.dll");
  if (remoteKernel32 == 0) {
    failureStage = "RemoteKernel32";
    failureError = GetLastError();
    goto cleanup;
  }
  remoteLoadLibrary = remoteKernel32 + reinterpret_cast<std::uintptr_t>(loadLibrary) -
                      reinterpret_cast<std::uintptr_t>(kernel32);

  static_assert(sizeof(startRoutine) == sizeof(loadLibrary));
  std::memcpy(&startRoutine, &remoteLoadLibrary, sizeof(startRoutine));
  remoteThread = CreateRemoteThread(process, nullptr, 0, startRoutine, remotePath, 0, nullptr);
  if (remoteThread == nullptr) {
    failureStage = "CreateRemoteThread";
    failureError = GetLastError();
    goto cleanup;
  }
  if (WaitForSingleObject(remoteThread, 10'000) != WAIT_OBJECT_0) {
    failureStage = "WaitForSingleObject";
    failureError = GetLastError();
    goto cleanup;
  }

  if (!GetExitCodeThread(remoteThread, &remoteModule)) {
    failureStage = "GetExitCodeThread";
    failureError = GetLastError();
    goto cleanup;
  }
  if (remoteModule == 0) {
    failureStage = "LoadLibraryW";
    failureError = ERROR_MOD_NOT_FOUND;
    goto cleanup;
  }
  success = true;

cleanup:
  if (remoteThread != nullptr) CloseHandle(remoteThread);
  if (remotePath != nullptr) VirtualFreeEx(process, remotePath, 0, MEM_RELEASE);
  CloseHandle(process);
  *message = success ? successMessage
                     : std::string(failureMessage) + "; stage=" + failureStage +
                           "; win32=" + std::to_string(failureError);
  return success;
}

// 把 DLL 注入自有测试目标；走受限注入路径，目标进程名必须完全匹配白名单。
bool injectTestDll(DWORD processId, const wchar_t* dllPath, std::string* message) {
  return injectRestrictedDll(
      processId,
      dllPath,
      L"cfpay-hook-observer.dll",
      L"cfpay-hook-test-target.exe",
      "test injection is restricted to cfpay-hook-test-target.exe",
      "observer DLL loaded into restricted test target",
      "test DLL injection failed",
      message);
}

// 确认目标进程已加载的观察 DLL 与磁盘上期望的那一份是同一个文件：
// 比较绝对路径与文件内容哈希，避免微信里还挂着旧版观察器却被当成新版继续使用。
bool observerDllMatches(
    DWORD processId, const wchar_t* expectedDllPath, const wchar_t* moduleName, std::string* message) {
  wchar_t absoluteExpectedPath[32768] = {};
  if (GetFullPathNameW(expectedDllPath, 32768, absoluteExpectedPath, nullptr) == 0 ||
      GetFileAttributesW(absoluteExpectedPath) == INVALID_FILE_ATTRIBUTES ||
      _wcsicmp(baseName(absoluteExpectedPath), moduleName) != 0) {
    *message = "observer DLL path is invalid";
    return false;
  }

  std::wstring loadedPath;
  if (!findRemoteModulePath(processId, moduleName, &loadedPath)) {
    *message = "observer DLL is not loaded in the target process";
    return false;
  }

  std::string expectedHash;
  std::string loadedHash;
  if (!calculateSha256(absoluteExpectedPath, &expectedHash) || !calculateSha256(loadedPath.c_str(), &loadedHash) ||
      expectedHash != loadedHash) {
    *message = "loaded observer DLL does not match the current package";
    return false;
  }
  return true;
}

// 从远端进程精确读取固定字节数，读不满即失败。
bool readRemoteExact(HANDLE process, std::uintptr_t address, void* output, SIZE_T size) {
  SIZE_T bytesRead = 0;
  return ReadProcessMemory(process, reinterpret_cast<const void*>(address), output, size, &bytesRead) != FALSE &&
         bytesRead == size;
}

// 解析远端模块的导出表找指定导出函数的绝对地址。
// 停止观察器时用它取 CfpayObserverStop 入口，只调用目标自己导出的清理函数，不做代码补丁。
std::uintptr_t findRemoteExport(HANDLE process, std::uintptr_t moduleBase, const char* exportName) {
  IMAGE_DOS_HEADER dosHeader{};
  if (!readRemoteExact(process, moduleBase, &dosHeader, sizeof(dosHeader)) || dosHeader.e_magic != IMAGE_DOS_SIGNATURE ||
      dosHeader.e_lfanew <= 0) {
    return 0;
  }

  IMAGE_NT_HEADERS64 ntHeaders{};
  const std::uintptr_t ntAddress = moduleBase + static_cast<std::uintptr_t>(dosHeader.e_lfanew);
  if (!readRemoteExact(process, ntAddress, &ntHeaders, sizeof(ntHeaders)) ||
      ntHeaders.Signature != IMAGE_NT_SIGNATURE || ntHeaders.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
    return 0;
  }

  const IMAGE_DATA_DIRECTORY exportData = ntHeaders.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
  if (exportData.VirtualAddress == 0 || exportData.Size < sizeof(IMAGE_EXPORT_DIRECTORY)) return 0;

  IMAGE_EXPORT_DIRECTORY exports{};
  if (!readRemoteExact(process, moduleBase + exportData.VirtualAddress, &exports, sizeof(exports)) ||
      exports.NumberOfNames == 0 || exports.NumberOfNames > 4096 || exports.NumberOfFunctions == 0 ||
      exports.NumberOfFunctions > 4096) {
    return 0;
  }

  std::vector<DWORD> nameRvas(exports.NumberOfNames);
  std::vector<WORD> nameOrdinals(exports.NumberOfNames);
  std::vector<DWORD> functionRvas(exports.NumberOfFunctions);
  if (!readRemoteExact(process,
                       moduleBase + exports.AddressOfNames,
                       nameRvas.data(),
                       nameRvas.size() * sizeof(nameRvas[0])) ||
      !readRemoteExact(process,
                       moduleBase + exports.AddressOfNameOrdinals,
                       nameOrdinals.data(),
                       nameOrdinals.size() * sizeof(nameOrdinals[0])) ||
      !readRemoteExact(process,
                       moduleBase + exports.AddressOfFunctions,
                       functionRvas.data(),
                       functionRvas.size() * sizeof(functionRvas[0]))) {
    return 0;
  }

  for (std::size_t index = 0; index < nameRvas.size(); index += 1) {
    std::array<char, 128> name{};
    if (!readRemoteExact(process, moduleBase + nameRvas[index], name.data(), name.size() - 1)) continue;
    if (std::strcmp(name.data(), exportName) != 0 || nameOrdinals[index] >= functionRvas.size()) continue;
    const DWORD functionRva = functionRvas[nameOrdinals[index]];
    if (functionRva >= exportData.VirtualAddress && functionRva < exportData.VirtualAddress + exportData.Size) return 0;
    return moduleBase + functionRva;
  }
  return 0;
}

bool invokeRemoteObserverExport(
    DWORD processId,
    const wchar_t* moduleName,
    const char* exportName,
    DWORD timeoutMilliseconds,
    DWORD* remoteResult,
    std::string* message) {
  const std::uintptr_t remoteObserver = findRemoteModuleBase(processId, moduleName);
  if (remoteObserver == 0) {
    *message = "observer DLL is not loaded in the target process";
    return false;
  }

  HANDLE process = OpenProcess(
      PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | SYNCHRONIZE, FALSE, processId);
  if (process == nullptr) {
    *message = "target process could not be opened for observer lifecycle call";
    return false;
  }

  const std::uintptr_t remoteProcedure = findRemoteExport(process, remoteObserver, exportName);
  if (remoteProcedure == 0) {
    CloseHandle(process);
    *message = "observer lifecycle export was not found";
    return false;
  }

  LPTHREAD_START_ROUTINE startRoutine = nullptr;
  static_assert(sizeof(startRoutine) == sizeof(remoteProcedure));
  std::memcpy(&startRoutine, &remoteProcedure, sizeof(startRoutine));

  HANDLE remoteThread = CreateRemoteThread(process, nullptr, 0, startRoutine, nullptr, 0, nullptr);
  DWORD result = FALSE;
  const bool completed = remoteThread != nullptr && WaitForSingleObject(remoteThread, timeoutMilliseconds) == WAIT_OBJECT_0 &&
                         GetExitCodeThread(remoteThread, &result) != FALSE;

  if (remoteThread != nullptr) CloseHandle(remoteThread);
  CloseHandle(process);
  if (remoteResult != nullptr) *remoteResult = result;
  if (!completed) *message = "observer lifecycle call did not complete";
  return completed;
}

// 调用远端观察器的版本导出，确认进程内运行的观察器 API 版本与本宿主期望的一致。
bool observerRuntimeMatches(DWORD processId, const wchar_t* moduleName, std::string* message) {
  DWORD version = 0;
  if (!invokeRemoteObserverExport(processId, moduleName, "CfpayObserverApiVersion", 5'000, &version, message) ||
      version != kObserverApiVersion) {
    if (message->empty()) *message = "loaded observer runtime API version does not match the current package";
    return false;
  }
  return true;
}

// 请求远端模块卸载；模块不存在视为成功。生产观察器保持惰性加载，不做动态卸载。
bool unloadRemoteModule(DWORD processId, const wchar_t* moduleName, std::string* message) {
  const std::uintptr_t moduleBase = findRemoteModuleBase(processId, moduleName);
  if (moduleBase == 0) return true;

  HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
  FARPROC freeLibrary = kernel32 == nullptr ? nullptr : GetProcAddress(kernel32, "FreeLibrary");
  const std::uintptr_t remoteKernel32 = findRemoteModuleBase(processId, L"kernel32.dll");
  if (kernel32 == nullptr || freeLibrary == nullptr || remoteKernel32 == 0) {
    *message = "unable to resolve remote FreeLibrary";
    return false;
  }

  const std::uintptr_t remoteFreeLibrary = remoteKernel32 + reinterpret_cast<std::uintptr_t>(freeLibrary) -
                                           reinterpret_cast<std::uintptr_t>(kernel32);
  LPTHREAD_START_ROUTINE startRoutine = nullptr;
  static_assert(sizeof(startRoutine) == sizeof(remoteFreeLibrary));
  std::memcpy(&startRoutine, &remoteFreeLibrary, sizeof(startRoutine));

  HANDLE process = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION | PROCESS_VM_READ,
                               FALSE,
                               processId);
  if (process == nullptr) {
    *message = "OpenProcess for diagnostic unload failed; win32=" + std::to_string(GetLastError());
    return false;
  }

  HANDLE remoteThread =
      CreateRemoteThread(process, nullptr, 0, startRoutine, reinterpret_cast<void*>(moduleBase), 0, nullptr);
  DWORD result = FALSE;
  const bool unloaded = remoteThread != nullptr && WaitForSingleObject(remoteThread, 10'000) == WAIT_OBJECT_0 &&
                        GetExitCodeThread(remoteThread, &result) != FALSE && result != FALSE;
  if (remoteThread != nullptr) CloseHandle(remoteThread);
  CloseHandle(process);
  if (!unloaded) *message = "diagnostic DLL unload did not complete";
  return unloaded;
}

// 停止微信主进程里的生产观察器：先校验进程名是 Weixin.exe，再请观察器自行清除断点与异常处理器。
bool stopWeixinObserver(DWORD processId, std::string* detailCode, std::string* message) {
  std::wstring processPath;
  if (!getProcessPath(processId, &processPath) || _wcsicmp(baseName(processPath), L"Weixin.exe") != 0) {
    *detailCode = "PROCESS_NOT_FOUND";
    *message = "stop mode is restricted to Weixin.exe";
    return false;
  }

  std::wstring weixinModulePath;
  if (!findRemoteModulePath(processId, L"Weixin.dll", &weixinModulePath) ||
      !inspectWeixinFile(weixinModulePath.c_str(), detailCode, message)) {
    return false;
  }

  if (findRemoteModuleBase(processId, L"cfpay-hook-observer.dll") == 0) {
    *detailCode = "HOOK_NOT_ACTIVE";
    *message = "observer DLL is not loaded in the target process";
    return false;
  }

  DWORD stopResult = FALSE;
  const bool stopped = invokeRemoteObserverExport(
                           processId, L"cfpay-hook-observer.dll", "CfpayObserverStop", 15'000, &stopResult, message) &&
                       stopResult != FALSE;

  *detailCode = stopped ? "HOOK_STOPPED" : "HOOK_STOP_FAILED";
  *message = stopped ? "observer hardware breakpoints and exception handler were cleared"
                     : "observer cleanup did not complete successfully";
  return stopped;
}

// 停止会话摘要诊断观察器（历史研究用），校验流程与生产一致。
bool stopWeixinDiagnosticObserver(DWORD processId, std::string* detailCode, std::string* message) {
  std::wstring processPath;
  if (!getProcessPath(processId, &processPath) || _wcsicmp(baseName(processPath), L"Weixin.exe") != 0) {
    *detailCode = "PROCESS_NOT_FOUND";
    *message = "session preview diagnostic stop mode is restricted to Weixin.exe";
    return false;
  }

  std::wstring weixinModulePath;
  if (!findRemoteModulePath(processId, L"Weixin.dll", &weixinModulePath) ||
      !inspectWeixinFile(weixinModulePath.c_str(), detailCode, message)) {
    return false;
  }

  constexpr wchar_t moduleName[] = L"cfpay-hook-diagnostic.dll";
  if (findRemoteModuleBase(processId, moduleName) == 0) {
    *detailCode = "HOOK_NOT_ACTIVE";
    *message = "session preview diagnostic DLL is not loaded in the target process";
    return false;
  }

  DWORD stopResult = FALSE;
  bool stopped =
      invokeRemoteObserverExport(processId, moduleName, "CfpayObserverStop", 15'000, &stopResult, message) &&
      stopResult != FALSE;
  if (stopped) stopped = unloadRemoteModule(processId, moduleName, message);
  *detailCode = stopped ? "HOOK_STOPPED" : "HOOK_STOP_FAILED";
  *message = stopped ? "session preview diagnostic breakpoints were cleared and the DLL was unloaded"
                     : "session preview diagnostic cleanup did not complete successfully";
  return stopped;
}

// 停止网络解包溯源诊断观察器。
bool stopWeixinNetworkDiagnosticObserver(DWORD processId, std::string* detailCode, std::string* message) {
  std::wstring processPath;
  if (!getProcessPath(processId, &processPath) || _wcsicmp(baseName(processPath), L"Weixin.exe") != 0) {
    *detailCode = "PROCESS_NOT_FOUND";
    *message = "network provenance diagnostic stop mode is restricted to Weixin.exe";
    return false;
  }

  std::wstring weixinModulePath;
  if (!findRemoteModulePath(processId, L"Weixin.dll", &weixinModulePath) ||
      !inspectWeixinFile(weixinModulePath.c_str(), detailCode, message)) {
    return false;
  }

  constexpr wchar_t moduleName[] = L"cfpay-hook-network-diagnostic.dll";
  if (findRemoteModuleBase(processId, moduleName) == 0) {
    *detailCode = "HOOK_NOT_ACTIVE";
    *message = "network provenance diagnostic DLL is not loaded in the target process";
    return false;
  }

  DWORD stopResult = FALSE;
  bool stopped =
      invokeRemoteObserverExport(processId, moduleName, "CfpayObserverStop", 15'000, &stopResult, message) &&
      stopResult != FALSE;
  if (stopped) stopped = unloadRemoteModule(processId, moduleName, message);
  *detailCode = stopped ? "HOOK_STOPPED" : "HOOK_STOP_FAILED";
  *message = stopped ? "network provenance diagnostic breakpoints were cleared and the DLL was unloaded"
                     : "network provenance diagnostic cleanup did not complete successfully";
  return stopped;
}

// 停止结构化记录槽位转储诊断观察器。
bool stopWeixinRecordDiagnosticObserver(DWORD processId, std::string* detailCode, std::string* message) {
  std::wstring processPath;
  if (!getProcessPath(processId, &processPath) || _wcsicmp(baseName(processPath), L"Weixin.exe") != 0) {
    *detailCode = "PROCESS_NOT_FOUND";
    *message = "structured record dump diagnostic stop mode is restricted to Weixin.exe";
    return false;
  }

  std::wstring weixinModulePath;
  if (!findRemoteModulePath(processId, L"Weixin.dll", &weixinModulePath) ||
      !inspectWeixinFile(weixinModulePath.c_str(), detailCode, message)) {
    return false;
  }

  constexpr wchar_t moduleName[] = L"cfpay-hook-record-diagnostic.dll";
  if (findRemoteModuleBase(processId, moduleName) == 0) {
    *detailCode = "HOOK_NOT_ACTIVE";
    *message = "structured record dump diagnostic DLL is not loaded in the target process";
    return false;
  }

  DWORD stopResult = FALSE;
  bool stopped =
      invokeRemoteObserverExport(processId, moduleName, "CfpayObserverStop", 15'000, &stopResult, message) &&
      stopResult != FALSE;
  if (stopped) stopped = unloadRemoteModule(processId, moduleName, message);
  *detailCode = stopped ? "HOOK_STOPPED" : "HOOK_STOP_FAILED";
  *message = stopped ? "structured record dump diagnostic breakpoints were cleared and the DLL was unloaded"
                     : "structured record dump diagnostic cleanup did not complete successfully";
  return stopped;
}

// 停止 NewSync 命令与载荷转储诊断观察器。
bool stopWeixinSyncDiagnosticObserver(DWORD processId, std::string* detailCode, std::string* message) {
  std::wstring processPath;
  if (!getProcessPath(processId, &processPath) || _wcsicmp(baseName(processPath), L"Weixin.exe") != 0) {
    *detailCode = "PROCESS_NOT_FOUND";
    *message = "NewSync command dump diagnostic stop mode is restricted to Weixin.exe";
    return false;
  }

  std::wstring weixinModulePath;
  if (!findRemoteModulePath(processId, L"Weixin.dll", &weixinModulePath) ||
      !inspectWeixinFile(weixinModulePath.c_str(), detailCode, message)) {
    return false;
  }

  constexpr wchar_t moduleName[] = L"cfpay-hook-sync-diagnostic.dll";
  if (findRemoteModuleBase(processId, moduleName) == 0) {
    *detailCode = "HOOK_NOT_ACTIVE";
    *message = "NewSync command dump diagnostic DLL is not loaded in the target process";
    return false;
  }

  DWORD stopResult = FALSE;
  bool stopped =
      invokeRemoteObserverExport(processId, moduleName, "CfpayObserverStop", 15'000, &stopResult, message) &&
      stopResult != FALSE;
  if (stopped) stopped = unloadRemoteModule(processId, moduleName, message);
  *detailCode = stopped ? "HOOK_STOPPED" : "HOOK_STOP_FAILED";
  *message = stopped ? "NewSync command dump diagnostic breakpoints were cleared and the DLL was unloaded"
                     : "NewSync command dump diagnostic cleanup did not complete successfully";
  return stopped;
}

// 停止小程序渲染进程（WeChatAppEx.exe）里的 WMPF 诊断观察器。
bool stopWmpfObserver(DWORD processId, std::string* detailCode, std::string* message) {
  std::wstring processPath;
  if (!getProcessPath(processId, &processPath) || _wcsicmp(baseName(processPath), L"WeChatAppEx.exe") != 0) {
    *detailCode = "PROCESS_NOT_FOUND";
    *message = "WMPF stop mode is restricted to WeChatAppEx.exe";
    return false;
  }

  std::wstring modulePath;
  if (!findRemoteModulePath(processId, L"flue.dll", &modulePath) ||
      !inspectWmpfFile(modulePath.c_str(), detailCode, message)) {
    return false;
  }
  if (findRemoteModuleBase(processId, L"cfpay-hook-observer.dll") == 0) {
    *detailCode = "HOOK_NOT_ACTIVE";
    *message = "observer DLL is not loaded in the WMPF target process";
    return false;
  }

  DWORD stopResult = FALSE;
  const bool stopped = invokeRemoteObserverExport(
                           processId, L"cfpay-hook-observer.dll", "CfpayObserverStop", 15'000, &stopResult, message) &&
                       stopResult != FALSE;
  *detailCode = stopped ? "HOOK_STOPPED" : "HOOK_STOP_FAILED";
  *message = stopped ? "WMPF diagnostic hardware breakpoints and exception handler were cleared"
                     : "WMPF diagnostic cleanup did not complete successfully";
  return stopped;
}

// 把命名管道的内容原样转发到标准输出，直到对端关闭；宿主自己完全不解析业务内容。
bool relayPipe(HANDLE pipe, std::string* message) {
  std::array<char, 8192> buffer{};
  bool received = false;
  while (true) {
    DWORD bytesRead = 0;
    if (!ReadFile(pipe, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead, nullptr)) {
      if (GetLastError() == ERROR_BROKEN_PIPE) break;
      *message = "named pipe read failed";
      return false;
    }
    if (bytesRead == 0) break;
    received = true;
    std::fwrite(buffer.data(), 1, bytesRead, stdout);
    std::fflush(stdout);
  }
  *message = received ? "named pipe stream completed" : "named pipe produced no output";
  return received;
}

// 对自有测试目标建立管道并转发输出，用来在不接触微信的前提下验证 Hook 与管道协议。
bool streamTestPipe(DWORD processId, const wchar_t* dllPath, std::string* message) {
  std::wstring processPath;
  if (!getProcessPath(processId, &processPath) || _wcsicmp(baseName(processPath), L"cfpay-hook-test-target.exe") != 0) {
    *message = "test pipe mode is restricted to cfpay-hook-test-target.exe";
    return false;
  }

  wchar_t pipeName[128] = {};
  ::swprintf(pipeName, L"\\\\.\\pipe\\cfpay-hook-test-%lu", processId);
  HANDLE pipe = CreateNamedPipeW(pipeName,
                                 PIPE_ACCESS_INBOUND,
                                 PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                                 1,
                                 64 * 1024,
                                 64 * 1024,
                                 0,
                                 nullptr);
  if (pipe == INVALID_HANDLE_VALUE) {
    *message = "test named pipe creation failed";
    return false;
  }

  const bool observerLoaded = findRemoteModuleBase(processId, L"cfpay-hook-observer.dll") != 0;
  std::string lifecycleMessage;
  if (observerLoaded) {
    DWORD startResult = FALSE;
    if (!observerDllMatches(processId, dllPath, L"cfpay-hook-observer.dll", &lifecycleMessage) ||
        !observerRuntimeMatches(processId, L"cfpay-hook-observer.dll", &lifecycleMessage) ||
        !invokeRemoteObserverExport(processId, L"cfpay-hook-observer.dll", "CfpayObserverStart", 15'000, &startResult,
                                    &lifecycleMessage) ||
        startResult == FALSE) {
      CloseHandle(pipe);
      *message = lifecycleMessage.empty() ? "observer reactivation failed" : lifecycleMessage;
      return false;
    }
    emitStatus("STARTING", "TEST_DLL_REACTIVATED", "observer DLL reactivated in restricted test target");
  } else {
    if (!injectTestDll(processId, dllPath, &lifecycleMessage)) {
      CloseHandle(pipe);
      *message = lifecycleMessage;
      return false;
    }
    emitStatus("STARTING", "TEST_DLL_INJECTED", lifecycleMessage);
  }

  const BOOL connected = ConnectNamedPipe(pipe, nullptr);
  if (!connected && GetLastError() != ERROR_PIPE_CONNECTED) {
    CloseHandle(pipe);
    *message = "observer did not connect to test named pipe";
    return false;
  }

  const bool relayed = relayPipe(pipe, message);
  CloseHandle(pipe);
  return relayed;
}

// 同上，但建立管道后由宿主提前断开，用于验证观察器在对端消失后能自行清理断点。
bool dropTestPipe(DWORD processId, const wchar_t* dllPath, std::string* message) {
  std::wstring processPath;
  if (!getProcessPath(processId, &processPath) || _wcsicmp(baseName(processPath), L"cfpay-hook-test-target.exe") != 0) {
    *message = "test pipe drop is restricted to cfpay-hook-test-target.exe";
    return false;
  }

  wchar_t pipeName[128] = {};
  ::swprintf(pipeName, L"\\\\.\\pipe\\cfpay-hook-test-%lu", processId);
  HANDLE pipe = CreateNamedPipeW(pipeName,
                                 PIPE_ACCESS_INBOUND,
                                 PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                                 1,
                                 64 * 1024,
                                 64 * 1024,
                                 0,
                                 nullptr);
  if (pipe == INVALID_HANDLE_VALUE) {
    *message = "test drop pipe creation failed";
    return false;
  }

  std::string injectionMessage;
  if (!injectTestDll(processId, dllPath, &injectionMessage)) {
    CloseHandle(pipe);
    *message = injectionMessage;
    return false;
  }
  const BOOL connected = ConnectNamedPipe(pipe, nullptr);
  if (!connected && GetLastError() != ERROR_PIPE_CONNECTED) {
    CloseHandle(pipe);
    *message = "observer did not connect before drop test";
    return false;
  }

  std::array<char, 1024> buffer{};
  DWORD bytesRead = 0;
  const bool received = ReadFile(pipe, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead, nullptr) != FALSE &&
                        bytesRead > 0;
  CloseHandle(pipe);
  *message = received ? "test pipe closed after first observer record" : "observer produced no record before drop";
  return received;
}

// 生产链路：校验目标是 Weixin.exe 后注入观察器、建立命名管道并把 JSONL 转发给监控端。
// 宿主只做校验与转发，不解析收款内容，业务判定全部在监控端完成。
bool streamWeixinPipe(DWORD processId, const wchar_t* dllPath, std::string* detailCode, std::string* message) {
  std::wstring processPath;
  if (!getProcessPath(processId, &processPath) || _wcsicmp(baseName(processPath), L"Weixin.exe") != 0) {
    *detailCode = "PROCESS_NOT_FOUND";
    *message = "production mode is restricted to Weixin.exe";
    return false;
  }

  std::wstring modulePath;
  if (!findRemoteModulePath(processId, L"Weixin.dll", &modulePath)) {
    *detailCode = "PROCESS_NOT_FOUND";
    *message = "target process does not contain Weixin.dll";
    return false;
  }
  if (!inspectWeixinFile(modulePath.c_str(), detailCode, message)) return false;
  const bool observerLoaded = findRemoteModuleBase(processId, L"cfpay-hook-observer.dll") != 0;
  if (observerLoaded &&
      (!observerDllMatches(processId, dllPath, L"cfpay-hook-observer.dll", message) ||
       !observerRuntimeMatches(processId, L"cfpay-hook-observer.dll", message))) {
    *detailCode = "HOOK_INSTALL_FAILED";
    return false;
  }

  wchar_t pipeName[128] = {};
  ::swprintf(pipeName, L"\\\\.\\pipe\\cfpay-wechat-observer-%lu", processId);
  HANDLE pipe = CreateNamedPipeW(pipeName,
                                 PIPE_ACCESS_INBOUND,
                                 PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                                 1,
                                 64 * 1024,
                                 64 * 1024,
                                 0,
                                 nullptr);
  if (pipe == INVALID_HANDLE_VALUE) {
    *detailCode = "HOOK_INSTALL_FAILED";
    *message = "production named pipe creation failed";
    return false;
  }

  emitStatus("STARTING", "TARGET_VALIDATED", "Weixin target passed file validation");
  if (observerLoaded) {
    DWORD startResult = FALSE;
    if (!invokeRemoteObserverExport(processId, L"cfpay-hook-observer.dll", "CfpayObserverStart", 15'000, &startResult,
                                    message) ||
        startResult == FALSE) {
      CloseHandle(pipe);
      *detailCode = "HOOK_INSTALL_FAILED";
      if (message->empty()) *message = "observer DLL is loaded but could not be reactivated";
      return false;
    }
    emitStatus("STARTING", "HOOK_REACTIVATING", "validated observer DLL reactivated in Weixin process");
  } else {
    std::string injectionMessage;
    if (!injectRestrictedDll(processId,
                             dllPath,
                             L"cfpay-hook-observer.dll",
                             L"Weixin.exe",
                             "production mode is restricted to Weixin.exe",
                             "observer DLL loaded into validated Weixin process",
                             "observer DLL injection failed",
                             &injectionMessage)) {
      CloseHandle(pipe);
      *detailCode = "HOOK_INSTALL_FAILED";
      *message = injectionMessage;
      return false;
    }
    emitStatus("STARTING", "HOOK_INSTALLING", injectionMessage);
  }

  const BOOL connected = ConnectNamedPipe(pipe, nullptr);
  if (!connected && GetLastError() != ERROR_PIPE_CONNECTED) {
    CloseHandle(pipe);
    *detailCode = "HOOK_INSTALL_FAILED";
    *message = "observer DLL did not connect to production named pipe";
    return false;
  }

  const bool relayed = relayPipe(pipe, message);
  CloseHandle(pipe);
  *detailCode = relayed ? "PROCESS_EXITED" : "HOOK_INSTALL_FAILED";
  return relayed;
}

// 会话摘要诊断链路的管道转发（历史研究用）。
bool streamWeixinDiagnosticPipe(DWORD processId, const wchar_t* dllPath, std::string* detailCode, std::string* message) {
  std::wstring processPath;
  if (!getProcessPath(processId, &processPath) || _wcsicmp(baseName(processPath), L"Weixin.exe") != 0) {
    *detailCode = "PROCESS_NOT_FOUND";
    *message = "session preview diagnostic mode is restricted to Weixin.exe";
    return false;
  }

  std::wstring modulePath;
  if (!findRemoteModulePath(processId, L"Weixin.dll", &modulePath)) {
    *detailCode = "PROCESS_NOT_FOUND";
    *message = "target process does not contain Weixin.dll";
    return false;
  }
  if (!inspectWeixinFile(modulePath.c_str(), detailCode, message)) return false;

  constexpr wchar_t diagnosticModuleName[] = L"cfpay-hook-diagnostic.dll";
  const bool diagnosticLoaded = findRemoteModuleBase(processId, diagnosticModuleName) != 0;
  if (diagnosticLoaded &&
      (!observerDllMatches(processId, dllPath, diagnosticModuleName, message) ||
       !observerRuntimeMatches(processId, diagnosticModuleName, message))) {
    *detailCode = "HOOK_INSTALL_FAILED";
    return false;
  }

  wchar_t pipeName[128] = {};
  ::swprintf(pipeName, L"\\\\.\\pipe\\cfpay-wechat-observer-%lu", processId);
  HANDLE pipe = CreateNamedPipeW(pipeName,
                                 PIPE_ACCESS_INBOUND,
                                 PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                                 1,
                                 64 * 1024,
                                 64 * 1024,
                                 0,
                                 nullptr);
  if (pipe == INVALID_HANDLE_VALUE) {
    *detailCode = "HOOK_INSTALL_FAILED";
    *message = "session preview diagnostic named pipe creation failed";
    return false;
  }

  emitStatus("STARTING", "TARGET_VALIDATED", "Weixin session preview diagnostic target passed validation");
  if (diagnosticLoaded) {
    DWORD startResult = FALSE;
    if (!invokeRemoteObserverExport(
            processId, diagnosticModuleName, "CfpayObserverStart", 15'000, &startResult, message) ||
        startResult == FALSE) {
      CloseHandle(pipe);
      *detailCode = "HOOK_INSTALL_FAILED";
      if (message->empty()) *message = "session preview diagnostic could not be reactivated";
      return false;
    }
    emitStatus("STARTING", "HOOK_REACTIVATING", "validated session preview diagnostic reactivated");
  } else {
    std::string injectionMessage;
    if (!injectRestrictedDll(processId,
                             dllPath,
                             diagnosticModuleName,
                             L"Weixin.exe",
                             "session preview diagnostic mode is restricted to Weixin.exe",
                             "session preview diagnostic loaded into validated Weixin process",
                             "session preview diagnostic injection failed",
                             &injectionMessage)) {
      CloseHandle(pipe);
      *detailCode = "HOOK_INSTALL_FAILED";
      *message = injectionMessage;
      return false;
    }
    emitStatus("STARTING", "HOOK_INSTALLING", injectionMessage);
  }

  const BOOL connected = ConnectNamedPipe(pipe, nullptr);
  if (!connected && GetLastError() != ERROR_PIPE_CONNECTED) {
    CloseHandle(pipe);
    *detailCode = "HOOK_INSTALL_FAILED";
    *message = "session preview diagnostic did not connect to named pipe";
    return false;
  }

  const bool relayed = relayPipe(pipe, message);
  CloseHandle(pipe);
  *detailCode = relayed ? "PROCESS_EXITED" : "HOOK_INSTALL_FAILED";
  return relayed;
}

// 网络解包溯源诊断链路的管道转发。
bool streamWeixinNetworkDiagnosticPipe(
    DWORD processId,
    const wchar_t* dllPath,
    std::string* detailCode,
    std::string* message) {
  std::wstring processPath;
  if (!getProcessPath(processId, &processPath) || _wcsicmp(baseName(processPath), L"Weixin.exe") != 0) {
    *detailCode = "PROCESS_NOT_FOUND";
    *message = "network provenance diagnostic mode is restricted to Weixin.exe";
    return false;
  }

  std::wstring modulePath;
  if (!findRemoteModulePath(processId, L"Weixin.dll", &modulePath)) {
    *detailCode = "PROCESS_NOT_FOUND";
    *message = "target process does not contain Weixin.dll";
    return false;
  }
  if (!inspectWeixinFile(modulePath.c_str(), detailCode, message)) return false;

  constexpr wchar_t diagnosticModuleName[] = L"cfpay-hook-network-diagnostic.dll";
  if (findRemoteModuleBase(processId, L"cfpay-hook-observer.dll") != 0 ||
      findRemoteModuleBase(processId, L"cfpay-hook-diagnostic.dll") != 0) {
    *detailCode = "HOOK_INSTALL_FAILED";
    *message = "another Weixin observer is active; stop it before network provenance diagnostics";
    return false;
  }
  const bool diagnosticLoaded = findRemoteModuleBase(processId, diagnosticModuleName) != 0;
  if (diagnosticLoaded &&
      (!observerDllMatches(processId, dllPath, diagnosticModuleName, message) ||
       !observerRuntimeMatches(processId, diagnosticModuleName, message))) {
    *detailCode = "HOOK_INSTALL_FAILED";
    return false;
  }

  wchar_t pipeName[128] = {};
  ::swprintf(pipeName, L"\\\\.\\pipe\\cfpay-wechat-observer-%lu", processId);
  HANDLE pipe = CreateNamedPipeW(pipeName,
                                 PIPE_ACCESS_INBOUND,
                                 PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                                 1,
                                 64 * 1024,
                                 64 * 1024,
                                 0,
                                 nullptr);
  if (pipe == INVALID_HANDLE_VALUE) {
    *detailCode = "HOOK_INSTALL_FAILED";
    *message = "network provenance diagnostic named pipe creation failed";
    return false;
  }

  emitStatus("STARTING", "TARGET_VALIDATED", "Weixin network provenance diagnostic target passed validation");
  if (diagnosticLoaded) {
    DWORD startResult = FALSE;
    if (!invokeRemoteObserverExport(
            processId, diagnosticModuleName, "CfpayObserverStart", 15'000, &startResult, message) ||
        startResult == FALSE) {
      CloseHandle(pipe);
      *detailCode = "HOOK_INSTALL_FAILED";
      if (message->empty()) *message = "network provenance diagnostic could not be reactivated";
      return false;
    }
    emitStatus("STARTING", "HOOK_REACTIVATING", "validated network provenance diagnostic reactivated");
  } else {
    std::string injectionMessage;
    if (!injectRestrictedDll(processId,
                             dllPath,
                             diagnosticModuleName,
                             L"Weixin.exe",
                             "network provenance diagnostic mode is restricted to Weixin.exe",
                             "network provenance diagnostic loaded into validated Weixin process",
                             "network provenance diagnostic injection failed",
                             &injectionMessage)) {
      CloseHandle(pipe);
      *detailCode = "HOOK_INSTALL_FAILED";
      *message = injectionMessage;
      return false;
    }
    emitStatus("STARTING", "HOOK_INSTALLING", injectionMessage);
  }

  const BOOL connected = ConnectNamedPipe(pipe, nullptr);
  if (!connected && GetLastError() != ERROR_PIPE_CONNECTED) {
    CloseHandle(pipe);
    *detailCode = "HOOK_INSTALL_FAILED";
    *message = "network provenance diagnostic did not connect to named pipe";
    return false;
  }

  const bool relayed = relayPipe(pipe, message);
  CloseHandle(pipe);
  *detailCode = relayed ? "PROCESS_EXITED" : "HOOK_INSTALL_FAILED";
  return relayed;
}

// 结构化记录槽位转储诊断链路的管道转发。
bool streamWeixinRecordDiagnosticPipe(
    DWORD processId,
    const wchar_t* dllPath,
    std::string* detailCode,
    std::string* message) {
  std::wstring processPath;
  if (!getProcessPath(processId, &processPath) || _wcsicmp(baseName(processPath), L"Weixin.exe") != 0) {
    *detailCode = "PROCESS_NOT_FOUND";
    *message = "structured record dump diagnostic mode is restricted to Weixin.exe";
    return false;
  }

  std::wstring modulePath;
  if (!findRemoteModulePath(processId, L"Weixin.dll", &modulePath)) {
    *detailCode = "PROCESS_NOT_FOUND";
    *message = "target process does not contain Weixin.dll";
    return false;
  }
  if (!inspectWeixinFile(modulePath.c_str(), detailCode, message)) return false;

  constexpr wchar_t diagnosticModuleName[] = L"cfpay-hook-record-diagnostic.dll";
  if (findRemoteModuleBase(processId, L"cfpay-hook-observer.dll") != 0 ||
      findRemoteModuleBase(processId, L"cfpay-hook-diagnostic.dll") != 0 ||
      findRemoteModuleBase(processId, L"cfpay-hook-network-diagnostic.dll") != 0) {
    *detailCode = "HOOK_INSTALL_FAILED";
    *message = "another Weixin observer is active; stop it before structured record dump diagnostics";
    return false;
  }
  const bool diagnosticLoaded = findRemoteModuleBase(processId, diagnosticModuleName) != 0;
  if (diagnosticLoaded &&
      (!observerDllMatches(processId, dllPath, diagnosticModuleName, message) ||
       !observerRuntimeMatches(processId, diagnosticModuleName, message))) {
    *detailCode = "HOOK_INSTALL_FAILED";
    return false;
  }

  wchar_t pipeName[128] = {};
  ::swprintf(pipeName, L"\\\\.\\pipe\\cfpay-wechat-observer-%lu", processId);
  HANDLE pipe = CreateNamedPipeW(pipeName,
                                 PIPE_ACCESS_INBOUND,
                                 PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                                 1,
                                 64 * 1024,
                                 64 * 1024,
                                 0,
                                 nullptr);
  if (pipe == INVALID_HANDLE_VALUE) {
    *detailCode = "HOOK_INSTALL_FAILED";
    *message = "structured record dump diagnostic named pipe creation failed";
    return false;
  }

  emitStatus("STARTING", "TARGET_VALIDATED", "Weixin structured record dump diagnostic target passed validation");
  if (diagnosticLoaded) {
    DWORD startResult = FALSE;
    if (!invokeRemoteObserverExport(
            processId, diagnosticModuleName, "CfpayObserverStart", 15'000, &startResult, message) ||
        startResult == FALSE) {
      CloseHandle(pipe);
      *detailCode = "HOOK_INSTALL_FAILED";
      if (message->empty()) *message = "structured record dump diagnostic could not be reactivated";
      return false;
    }
    emitStatus("STARTING", "HOOK_REACTIVATING", "validated structured record dump diagnostic reactivated");
  } else {
    std::string injectionMessage;
    if (!injectRestrictedDll(processId,
                             dllPath,
                             diagnosticModuleName,
                             L"Weixin.exe",
                             "structured record dump diagnostic mode is restricted to Weixin.exe",
                             "structured record dump diagnostic loaded into validated Weixin process",
                             "structured record dump diagnostic injection failed",
                             &injectionMessage)) {
      CloseHandle(pipe);
      *detailCode = "HOOK_INSTALL_FAILED";
      *message = injectionMessage;
      return false;
    }
    emitStatus("STARTING", "HOOK_INSTALLING", injectionMessage);
  }

  const BOOL connected = ConnectNamedPipe(pipe, nullptr);
  if (!connected && GetLastError() != ERROR_PIPE_CONNECTED) {
    CloseHandle(pipe);
    *detailCode = "HOOK_INSTALL_FAILED";
    *message = "structured record dump diagnostic did not connect to named pipe";
    return false;
  }

  const bool relayed = relayPipe(pipe, message);
  CloseHandle(pipe);
  *detailCode = relayed ? "PROCESS_EXITED" : "HOOK_INSTALL_FAILED";
  return relayed;
}

// NewSync 命令与载荷转储诊断链路的管道转发。
bool streamWeixinSyncDiagnosticPipe(
    DWORD processId,
    const wchar_t* dllPath,
    std::string* detailCode,
    std::string* message) {
  std::wstring processPath;
  if (!getProcessPath(processId, &processPath) || _wcsicmp(baseName(processPath), L"Weixin.exe") != 0) {
    *detailCode = "PROCESS_NOT_FOUND";
    *message = "NewSync command dump diagnostic mode is restricted to Weixin.exe";
    return false;
  }

  std::wstring modulePath;
  if (!findRemoteModulePath(processId, L"Weixin.dll", &modulePath)) {
    *detailCode = "PROCESS_NOT_FOUND";
    *message = "target process does not contain Weixin.dll";
    return false;
  }
  if (!inspectWeixinFile(modulePath.c_str(), detailCode, message)) return false;

  constexpr wchar_t diagnosticModuleName[] = L"cfpay-hook-sync-diagnostic.dll";
  if (findRemoteModuleBase(processId, L"cfpay-hook-observer.dll") != 0 ||
      findRemoteModuleBase(processId, L"cfpay-hook-diagnostic.dll") != 0 ||
      findRemoteModuleBase(processId, L"cfpay-hook-network-diagnostic.dll") != 0 ||
      findRemoteModuleBase(processId, L"cfpay-hook-record-diagnostic.dll") != 0) {
    *detailCode = "HOOK_INSTALL_FAILED";
    *message = "another Weixin observer is active; stop it before NewSync command dump diagnostics";
    return false;
  }
  const bool diagnosticLoaded = findRemoteModuleBase(processId, diagnosticModuleName) != 0;
  if (diagnosticLoaded &&
      (!observerDllMatches(processId, dllPath, diagnosticModuleName, message) ||
       !observerRuntimeMatches(processId, diagnosticModuleName, message))) {
    *detailCode = "HOOK_INSTALL_FAILED";
    return false;
  }

  wchar_t pipeName[128] = {};
  ::swprintf(pipeName, L"\\\\.\\pipe\\cfpay-wechat-observer-%lu", processId);
  HANDLE pipe = CreateNamedPipeW(pipeName,
                                 PIPE_ACCESS_INBOUND,
                                 PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                                 1,
                                 64 * 1024,
                                 64 * 1024,
                                 0,
                                 nullptr);
  if (pipe == INVALID_HANDLE_VALUE) {
    *detailCode = "HOOK_INSTALL_FAILED";
    *message = "NewSync command dump diagnostic named pipe creation failed";
    return false;
  }

  emitStatus("STARTING", "TARGET_VALIDATED", "Weixin NewSync command dump diagnostic target passed validation");
  if (diagnosticLoaded) {
    DWORD startResult = FALSE;
    if (!invokeRemoteObserverExport(
            processId, diagnosticModuleName, "CfpayObserverStart", 15'000, &startResult, message) ||
        startResult == FALSE) {
      CloseHandle(pipe);
      *detailCode = "HOOK_INSTALL_FAILED";
      if (message->empty()) *message = "NewSync command dump diagnostic could not be reactivated";
      return false;
    }
    emitStatus("STARTING", "HOOK_REACTIVATING", "validated NewSync command dump diagnostic reactivated");
  } else {
    std::string injectionMessage;
    if (!injectRestrictedDll(processId,
                             dllPath,
                             diagnosticModuleName,
                             L"Weixin.exe",
                             "NewSync command dump diagnostic mode is restricted to Weixin.exe",
                             "NewSync command dump diagnostic loaded into validated Weixin process",
                             "NewSync command dump diagnostic injection failed",
                             &injectionMessage)) {
      CloseHandle(pipe);
      *detailCode = "HOOK_INSTALL_FAILED";
      *message = injectionMessage;
      return false;
    }
    emitStatus("STARTING", "HOOK_INSTALLING", injectionMessage);
  }

  const BOOL connected = ConnectNamedPipe(pipe, nullptr);
  if (!connected && GetLastError() != ERROR_PIPE_CONNECTED) {
    CloseHandle(pipe);
    *detailCode = "HOOK_INSTALL_FAILED";
    *message = "NewSync command dump diagnostic did not connect to named pipe";
    return false;
  }

  const bool relayed = relayPipe(pipe, message);
  CloseHandle(pipe);
  *detailCode = relayed ? "PROCESS_EXITED" : "HOOK_INSTALL_FAILED";
  return relayed;
}

// 小程序渲染进程诊断链路的管道转发。
bool streamWmpfPipe(DWORD processId, const wchar_t* dllPath, std::string* detailCode, std::string* message) {
  std::wstring processPath;
  if (!getProcessPath(processId, &processPath) || _wcsicmp(baseName(processPath), L"WeChatAppEx.exe") != 0) {
    *detailCode = "PROCESS_NOT_FOUND";
    *message = "WMPF diagnostic mode is restricted to WeChatAppEx.exe";
    return false;
  }

  std::wstring modulePath;
  if (!findRemoteModulePath(processId, L"flue.dll", &modulePath)) {
    *detailCode = "PROCESS_NOT_FOUND";
    *message = "target process does not contain flue.dll";
    return false;
  }
  if (!inspectWmpfFile(modulePath.c_str(), detailCode, message)) return false;

  const bool observerLoaded = findRemoteModuleBase(processId, L"cfpay-hook-observer.dll") != 0;
  if (observerLoaded &&
      (!observerDllMatches(processId, dllPath, L"cfpay-hook-observer.dll", message) ||
       !observerRuntimeMatches(processId, L"cfpay-hook-observer.dll", message))) {
    *detailCode = "HOOK_INSTALL_FAILED";
    return false;
  }

  wchar_t pipeName[128] = {};
  ::swprintf(pipeName, L"\\\\.\\pipe\\cfpay-wechat-observer-%lu", processId);
  HANDLE pipe = CreateNamedPipeW(pipeName,
                                 PIPE_ACCESS_INBOUND,
                                 PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                                 1,
                                 64 * 1024,
                                 64 * 1024,
                                 0,
                                 nullptr);
  if (pipe == INVALID_HANDLE_VALUE) {
    *detailCode = "HOOK_INSTALL_FAILED";
    *message = "WMPF diagnostic named pipe creation failed";
    return false;
  }

  emitStatus("STARTING", "TARGET_VALIDATED", "WMPF renderer target passed flue.dll validation");
  if (observerLoaded) {
    DWORD startResult = FALSE;
    if (!invokeRemoteObserverExport(processId, L"cfpay-hook-observer.dll", "CfpayObserverStart", 15'000, &startResult,
                                    message) ||
        startResult == FALSE) {
      CloseHandle(pipe);
      *detailCode = "HOOK_INSTALL_FAILED";
      if (message->empty()) *message = "WMPF observer DLL is loaded but could not be reactivated";
      return false;
    }
    emitStatus("STARTING", "HOOK_REACTIVATING", "validated WMPF diagnostic observer reactivated");
  } else {
    std::string injectionMessage;
    if (!injectRestrictedDll(processId,
                             dllPath,
                             L"cfpay-hook-observer.dll",
                             L"WeChatAppEx.exe",
                             "WMPF diagnostic mode is restricted to WeChatAppEx.exe",
                             "observer DLL loaded into validated WMPF renderer",
                             "WMPF diagnostic DLL injection failed",
                             &injectionMessage)) {
      CloseHandle(pipe);
      *detailCode = "HOOK_INSTALL_FAILED";
      *message = injectionMessage;
      return false;
    }
    emitStatus("STARTING", "HOOK_INSTALLING", injectionMessage);
  }

  const BOOL connected = ConnectNamedPipe(pipe, nullptr);
  if (!connected && GetLastError() != ERROR_PIPE_CONNECTED) {
    CloseHandle(pipe);
    *detailCode = "HOOK_INSTALL_FAILED";
    *message = "WMPF diagnostic observer did not connect to named pipe";
    return false;
  }

  const bool relayed = relayPipe(pipe, message);
  CloseHandle(pipe);
  *detailCode = relayed ? "PROCESS_EXITED" : "HOOK_INSTALL_FAILED";
  return relayed;
}

}  // namespace

// 宿主命令入口：按子命令分派到校验、注入、停止、管道转发等具体实现。
// 子命令、目标进程名与 DLL 文件名都走严格白名单，任意路径与任意进程都不会被接受。
int RunWechatHostCommand(int argc, wchar_t** argv) {
  if (argc == 2 && std::wcscmp(argv[1], L"--select-weixin") == 0) {
    return selectWeixinExecutable();
  }

  if (argc == 3 && std::wcscmp(argv[1], L"--inspect-weixin") == 0) {
    std::string detailCode;
    std::string message;
    const bool valid = inspectWeixinFile(argv[2], &detailCode, &message);
    emitStatus(valid ? "STARTING" : "UNSUPPORTED", detailCode.c_str(), message);
    return valid ? 0 : 10;
  }

  if (argc == 4 && std::wcscmp(argv[1], L"--test-inject") == 0) {
    const DWORD processId = std::wcstoul(argv[2], nullptr, 10);
    std::string message;
    const bool injected = processId != 0 && injectTestDll(processId, argv[3], &message);
    emitStatus(injected ? "STARTING" : "ERROR", injected ? "TEST_DLL_INJECTED" : "TEST_DLL_INJECTION_FAILED", message);
    return injected ? 0 : 11;
  }

  if (argc == 4 && std::wcscmp(argv[1], L"--test-pipe-run") == 0) {
    const DWORD processId = std::wcstoul(argv[2], nullptr, 10);
    std::string message;
    const bool completed = processId != 0 && streamTestPipe(processId, argv[3], &message);
    if (!completed) emitStatus("ERROR", "TEST_PIPE_FAILED", message);
    return completed ? 0 : 12;
  }

  if (argc == 4 && std::wcscmp(argv[1], L"--test-pipe-drop") == 0) {
    const DWORD processId = std::wcstoul(argv[2], nullptr, 10);
    std::string message;
    const bool completed = processId != 0 && dropTestPipe(processId, argv[3], &message);
    emitStatus(completed ? "DEGRADED" : "ERROR", completed ? "TEST_PIPE_DROPPED" : "TEST_PIPE_FAILED", message);
    return completed ? 0 : 14;
  }

  if (argc == 4 && std::wcscmp(argv[1], L"--run-weixin") == 0) {
    const DWORD processId = std::wcstoul(argv[2], nullptr, 10);
    std::string detailCode;
    std::string message;
    const bool completed = processId != 0 && streamWeixinPipe(processId, argv[3], &detailCode, &message);
    if (!completed) {
      const char* status = detailCode == "PROCESS_NOT_FOUND"
                               ? "STOPPED"
                               : (detailCode == "VERSION_MISMATCH" || detailCode == "SIGNATURE_MISMATCH" ? "UNSUPPORTED"
                                                                                                           : "ERROR");
      emitStatus(status, detailCode.empty() ? "HOOK_INSTALL_FAILED" : detailCode.c_str(), message);
    }
    return completed ? 0 : 13;
  }

  if (argc == 4 && std::wcscmp(argv[1], L"--run-weixin-diagnostic") == 0) {
    const DWORD processId = std::wcstoul(argv[2], nullptr, 10);
    std::string detailCode;
    std::string message;
    const bool completed = processId != 0 && streamWeixinDiagnosticPipe(processId, argv[3], &detailCode, &message);
    if (!completed) {
      const char* status = detailCode == "PROCESS_NOT_FOUND"
                               ? "STOPPED"
                               : (detailCode == "VERSION_MISMATCH" || detailCode == "SIGNATURE_MISMATCH" ? "UNSUPPORTED"
                                                                                                           : "ERROR");
      emitStatus(status, detailCode.empty() ? "HOOK_INSTALL_FAILED" : detailCode.c_str(), message);
    }
    return completed ? 0 : 18;
  }

  if (argc == 4 && std::wcscmp(argv[1], L"--run-weixin-network-diagnostic") == 0) {
    const DWORD processId = std::wcstoul(argv[2], nullptr, 10);
    std::string detailCode;
    std::string message;
    const bool completed = processId != 0 &&
                           streamWeixinNetworkDiagnosticPipe(processId, argv[3], &detailCode, &message);
    if (!completed) {
      const char* status = detailCode == "PROCESS_NOT_FOUND"
                               ? "STOPPED"
                               : (detailCode == "VERSION_MISMATCH" || detailCode == "SIGNATURE_MISMATCH" ? "UNSUPPORTED"
                                                                                                           : "ERROR");
      emitStatus(status, detailCode.empty() ? "HOOK_INSTALL_FAILED" : detailCode.c_str(), message);
    }
    return completed ? 0 : 20;
  }

  if (argc == 4 && std::wcscmp(argv[1], L"--run-weixin-record-diagnostic") == 0) {
    const DWORD processId = std::wcstoul(argv[2], nullptr, 10);
    std::string detailCode;
    std::string message;
    const bool completed = processId != 0 &&
                           streamWeixinRecordDiagnosticPipe(processId, argv[3], &detailCode, &message);
    if (!completed) {
      const char* status = detailCode == "PROCESS_NOT_FOUND"
                               ? "STOPPED"
                               : (detailCode == "VERSION_MISMATCH" || detailCode == "SIGNATURE_MISMATCH" ? "UNSUPPORTED"
                                                                                                           : "ERROR");
      emitStatus(status, detailCode.empty() ? "HOOK_INSTALL_FAILED" : detailCode.c_str(), message);
    }
    return completed ? 0 : 22;
  }

  if (argc == 4 && std::wcscmp(argv[1], L"--run-weixin-sync-diagnostic") == 0) {
    const DWORD processId = std::wcstoul(argv[2], nullptr, 10);
    std::string detailCode;
    std::string message;
    const bool completed = processId != 0 && streamWeixinSyncDiagnosticPipe(processId, argv[3], &detailCode, &message);
    if (!completed) {
      const char* status = detailCode == "PROCESS_NOT_FOUND"
                               ? "STOPPED"
                               : (detailCode == "VERSION_MISMATCH" || detailCode == "SIGNATURE_MISMATCH" ? "UNSUPPORTED"
                                                                                                           : "ERROR");
      emitStatus(status, detailCode.empty() ? "HOOK_INSTALL_FAILED" : detailCode.c_str(), message);
    }
    return completed ? 0 : 24;
  }

  if (argc == 4 && std::wcscmp(argv[1], L"--run-wmpf") == 0) {
    const DWORD processId = std::wcstoul(argv[2], nullptr, 10);
    std::string detailCode;
    std::string message;
    const bool completed = processId != 0 && streamWmpfPipe(processId, argv[3], &detailCode, &message);
    if (!completed) {
      const char* status = detailCode == "PROCESS_NOT_FOUND"
                               ? "STOPPED"
                               : (detailCode == "SIGNATURE_MISMATCH" ? "UNSUPPORTED" : "ERROR");
      emitStatus(status, detailCode.empty() ? "HOOK_INSTALL_FAILED" : detailCode.c_str(), message);
    }
    return completed ? 0 : 16;
  }

  if (argc == 3 && std::wcscmp(argv[1], L"--stop-weixin") == 0) {
    const DWORD processId = std::wcstoul(argv[2], nullptr, 10);
    std::string detailCode;
    std::string message;
    const bool stopped = processId != 0 && stopWeixinObserver(processId, &detailCode, &message);
    const char* status = stopped ? "STOPPED" : (detailCode == "PROCESS_NOT_FOUND" || detailCode == "HOOK_NOT_ACTIVE"
                                                   ? "STOPPED"
                                                   : "ERROR");
    emitStatus(status, detailCode.empty() ? "HOOK_STOP_FAILED" : detailCode.c_str(), message);
    return stopped ? 0 : 15;
  }

  if (argc == 3 && std::wcscmp(argv[1], L"--stop-weixin-diagnostic") == 0) {
    const DWORD processId = std::wcstoul(argv[2], nullptr, 10);
    std::string detailCode;
    std::string message;
    const bool stopped = processId != 0 && stopWeixinDiagnosticObserver(processId, &detailCode, &message);
    const char* status = stopped ? "STOPPED" : (detailCode == "PROCESS_NOT_FOUND" || detailCode == "HOOK_NOT_ACTIVE"
                                                   ? "STOPPED"
                                                   : "ERROR");
    emitStatus(status, detailCode.empty() ? "HOOK_STOP_FAILED" : detailCode.c_str(), message);
    return stopped ? 0 : 19;
  }

  if (argc == 3 && std::wcscmp(argv[1], L"--stop-weixin-network-diagnostic") == 0) {
    const DWORD processId = std::wcstoul(argv[2], nullptr, 10);
    std::string detailCode;
    std::string message;
    const bool stopped = processId != 0 &&
                         stopWeixinNetworkDiagnosticObserver(processId, &detailCode, &message);
    const char* status = stopped ? "STOPPED" : (detailCode == "PROCESS_NOT_FOUND" || detailCode == "HOOK_NOT_ACTIVE"
                                                   ? "STOPPED"
                                                   : "ERROR");
    emitStatus(status, detailCode.empty() ? "HOOK_STOP_FAILED" : detailCode.c_str(), message);
    return stopped ? 0 : 21;
  }

  if (argc == 3 && std::wcscmp(argv[1], L"--stop-weixin-record-diagnostic") == 0) {
    const DWORD processId = std::wcstoul(argv[2], nullptr, 10);
    std::string detailCode;
    std::string message;
    const bool stopped = processId != 0 &&
                         stopWeixinRecordDiagnosticObserver(processId, &detailCode, &message);
    const char* status = stopped ? "STOPPED" : (detailCode == "PROCESS_NOT_FOUND" || detailCode == "HOOK_NOT_ACTIVE"
                                                   ? "STOPPED"
                                                   : "ERROR");
    emitStatus(status, detailCode.empty() ? "HOOK_STOP_FAILED" : detailCode.c_str(), message);
    return stopped ? 0 : 23;
  }

  if (argc == 3 && std::wcscmp(argv[1], L"--stop-weixin-sync-diagnostic") == 0) {
    const DWORD processId = std::wcstoul(argv[2], nullptr, 10);
    std::string detailCode;
    std::string message;
    const bool stopped = processId != 0 && stopWeixinSyncDiagnosticObserver(processId, &detailCode, &message);
    const char* status = stopped ? "STOPPED" : (detailCode == "PROCESS_NOT_FOUND" || detailCode == "HOOK_NOT_ACTIVE"
                                                   ? "STOPPED"
                                                   : "ERROR");
    emitStatus(status, detailCode.empty() ? "HOOK_STOP_FAILED" : detailCode.c_str(), message);
    return stopped ? 0 : 25;
  }

  if (argc == 3 && std::wcscmp(argv[1], L"--stop-wmpf") == 0) {
    const DWORD processId = std::wcstoul(argv[2], nullptr, 10);
    std::string detailCode;
    std::string message;
    const bool stopped = processId != 0 && stopWmpfObserver(processId, &detailCode, &message);
    const char* status = stopped ? "STOPPED" : (detailCode == "PROCESS_NOT_FOUND" || detailCode == "HOOK_NOT_ACTIVE"
                                                   ? "STOPPED"
                                                   : "ERROR");
    emitStatus(status, detailCode.empty() ? "HOOK_STOP_FAILED" : detailCode.c_str(), message);
    return stopped ? 0 : 17;
  }

  ::fwprintf(stderr,
             L"usage:\n  cfpay-wechat-monitor.exe --internal-host --select-weixin\n  cfpay-wechat-monitor.exe --internal-host --inspect-weixin <Weixin.dll>\n  cfpay-wechat-monitor.exe --internal-host --test-inject <pid> <observer.dll>\n  cfpay-wechat-monitor.exe --internal-host --test-pipe-run <pid> <observer.dll>\n  cfpay-wechat-monitor.exe --internal-host --test-pipe-drop <pid> <observer.dll>\n  cfpay-wechat-monitor.exe --internal-host --run-weixin <pid> <observer.dll>\n  cfpay-wechat-monitor.exe --internal-host --stop-weixin <pid>\n"
             L"  cfpay-wechat-monitor.exe --internal-host --run-weixin-diagnostic <pid> <diagnostic.dll>\n"
             L"  cfpay-wechat-monitor.exe --internal-host --stop-weixin-diagnostic <pid>\n"
             L"  cfpay-wechat-monitor.exe --internal-host --run-weixin-network-diagnostic <pid> <diagnostic.dll>\n"
             L"  cfpay-wechat-monitor.exe --internal-host --stop-weixin-network-diagnostic <pid>\n"
             L"  cfpay-wechat-monitor.exe --internal-host --run-weixin-record-diagnostic <pid> <diagnostic.dll>\n"
             L"  cfpay-wechat-monitor.exe --internal-host --stop-weixin-record-diagnostic <pid>\n"
             L"  cfpay-wechat-monitor.exe --internal-host --run-weixin-sync-diagnostic <pid> <diagnostic.dll>\n"
             L"  cfpay-wechat-monitor.exe --internal-host --stop-weixin-sync-diagnostic <pid>\n"
             L"  cfpay-wechat-monitor.exe --internal-host --run-wmpf <pid> <observer.dll>\n"
             L"  cfpay-wechat-monitor.exe --internal-host --stop-wmpf <pid>\n");
  return 2;
}

#ifndef CFPAY_EMBED_HOST
// 独立宿主可执行文件的入口；被监控端内嵌编译时不会用到。
int wmain(int argc, wchar_t** argv) {
  return RunWechatHostCommand(argc, argv);
}
#endif
