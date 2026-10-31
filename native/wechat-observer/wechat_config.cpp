#include <windows.h>
#include <bcrypt.h>
#include <commctrl.h>
#include <objbase.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <winhttp.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cwctype>
#include <filesystem>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"
#include "wechat_embedded.h"
#include "wechat_monitor_version.h"
#include "wechat_target.h"

namespace {

using json = nlohmann::json;

constexpr wchar_t kWindowClass[] = L"CFPayWeChatConfigWindow";
constexpr wchar_t kWindowTitle[] = L"CFPay 微信监控端配置";

enum ControlId {
  kBaseUrl = 1001,
  kMonitorId,
  kGenerateMonitorId,
  kMonitorSecret,
  kWeixinPath,
  kBrowseWeixin,
  kWeixinDllPath,
  kReceiverAccount,
  kRefreshReceiverAccount,
  kDebugLogging,
  kOpenDebugLog,
  kValidateTarget,
  kTestConnection,
  kSaveConfig,
  kStatus,
};

struct AppState {
  HWND window = nullptr;
  HWND baseUrl = nullptr;
  HWND monitorId = nullptr;
  HWND monitorSecret = nullptr;
  HWND weixinPath = nullptr;
  HWND weixinDllPath = nullptr;
  HWND receiverAccount = nullptr;
  HWND debugLogging = nullptr;
  HWND status = nullptr;
  HFONT font = nullptr;
  HBRUSH background = nullptr;
  HFONT titleFont = nullptr;
  std::wstring configPath;
  std::wstring packageDirectory;
  json config = json::object();
  std::wstring validatedExecutablePath;
  struct AccountOption {
    std::wstring username;
    std::wstring nickname;
  };
  std::vector<AccountOption> accountOptions;
  bool targetValidated = false;
  bool embedded = false;
  int verticalOffset = 0;
};

AppState gState;

// 宽字符转 UTF-8，配置文件以 UTF-8 存储。
std::string wideToUtf8(const std::wstring& value) {
  if (value.empty()) return {};
  const int size = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
  if (size <= 0) return {};
  std::string output(static_cast<std::size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), output.data(), size, nullptr, nullptr);
  return output;
}

// UTF-8 转宽字符，界面控件只接受宽字符。
std::wstring utf8ToWide(const std::string& value) {
  if (value.empty()) return {};
  const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
  if (size <= 0) return {};
  std::wstring output(static_cast<std::size_t>(size), L'\0');
  MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), output.data(), size);
  return output;
}

// 读取控件文本。
std::wstring getText(HWND control) {
  const int length = GetWindowTextLengthW(control);
  std::wstring value(static_cast<std::size_t>(length + 1), L'\0');
  GetWindowTextW(control, value.data(), length + 1);
  value.resize(static_cast<std::size_t>(length));
  return value;
}

// 设置控件文本。
void setText(HWND control, const std::wstring& value) {
  SetWindowTextW(control, value.c_str());
}

// 在状态栏显示一行提示，出错时用红色；界面所有反馈都走这里，不弹多余的对话框。
void setStatus(const std::wstring& value, bool error = false) {
  setText(gState.status, value);
  if (!gState.embedded) SetWindowTextW(gState.window, error ? L"CFPay 微信监控端配置 - 操作失败" : kWindowTitle);
}

// 判断文件是否存在且不是目录。
bool fileExists(const std::wstring& path) {
  const DWORD attributes = GetFileAttributesW(path.c_str());
  return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

// 取路径的父目录，兼容正反斜杠。
std::wstring parentDirectory(const std::wstring& path) {
  const std::size_t index = path.find_last_of(L"\\/");
  return index == std::wstring::npos ? L"" : path.substr(0, index);
}

// 拼接路径，自动处理分隔符。
std::wstring joinPath(const std::wstring& left, const std::wstring& right) {
  if (left.empty()) return right;
  if (left.back() == L'\\' || left.back() == L'/') return left + right;
  return left + L"\\" + right;
}

// 取当前 EXE 所在目录；便携模式下配置就放在这里。
std::wstring moduleDirectory() {
  std::vector<wchar_t> buffer(32768);
  const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
  if (length == 0 || length >= buffer.size()) return L"";
  return parentDirectory(std::wstring(buffer.data(), length));
}

// 读取环境变量（如 LOCALAPPDATA），不存在返回空。
std::wstring environmentValue(const wchar_t* name) {
  const DWORD size = GetEnvironmentVariableW(name, nullptr, 0);
  if (size == 0) return L"";
  std::wstring value(static_cast<std::size_t>(size), L'\0');
  const DWORD written = GetEnvironmentVariableW(name, value.data(), size);
  if (written == 0 || written >= size) return L"";
  value.resize(written);
  return value;
}

// 递归创建目录，空路径直接失败。
bool ensureDirectory(const std::wstring& directory) {
  if (directory.empty()) return false;
  std::error_code error;
  std::filesystem::create_directories(std::filesystem::path(directory), error);
  return !error;
}

// 读取整个文件内容。
bool readFile(const std::wstring& path, std::string* output) {
  HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  LARGE_INTEGER size{};
  if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 || size.QuadPart > 4 * 1024 * 1024) {
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

// 先写 .tmp 再原子替换，避免保存中途失败留下半个 config.json。
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

// 从已加载的配置 JSON 里取字符串字段，缺失或类型不符时返回给定兜底值。
std::wstring jsonString(const char* key, const std::wstring& fallback = L"") {
  if (!gState.config.contains(key) || !gState.config[key].is_string()) return fallback;
  return utf8ToWide(gState.config[key].get<std::string>());
}

// 用 BCryptGenRandom 生成随机监控端 ID。
// ID 由程序生成而不是让用户手写，避免多台机器 accidentally 用同一个 ID 互相覆盖状态。
std::wstring randomIdentifier() {
  std::array<std::uint8_t, 12> bytes{};
  if (BCryptGenRandom(nullptr, bytes.data(), static_cast<ULONG>(bytes.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) {
    GUID guid{};
    if (CoCreateGuid(&guid) != S_OK) return L"";
    const auto* guidBytes = reinterpret_cast<const std::uint8_t*>(&guid);
    std::copy_n(guidBytes, bytes.size(), bytes.begin());
  }
  static constexpr wchar_t hex[] = L"0123456789abcdef";
  std::wstring value = L"wm_";
  for (const std::uint8_t byte : bytes) {
    value.push_back(hex[byte >> 4]);
    value.push_back(hex[byte & 0x0f]);
  }
  return value;
}

// 程序创建的所有文件统一放在这个目录：配置、释放出的观察 DLL、收款队列状态与日志。
// EXE 本身可以从任意位置运行（安装目录、下载目录、构建输出目录），把数据写在 EXE 旁边
// 既难管理，也更容易被杀软当成「随程序释放的可执行文件」。取不到 LOCALAPPDATA 时退回 EXE 同目录。
std::wstring cfpayDataDirectory() {
  const std::wstring localAppData = environmentValue(L"LOCALAPPDATA");
  if (localAppData.empty()) return moduleDirectory();
  return joinPath(joinPath(localAppData, L"CFPay"), L"WeChatMonitor");
}

// 首次运行时尽力找一个微信主程序，找不到就留空由用户在配置页选择，绝不猜一个不存在的路径。
std::wstring detectWeixinExecutable() {
  for (const wchar_t* name : {L"ProgramW6432", L"ProgramFiles", L"ProgramFiles(x86)"}) {
    const std::wstring root = environmentValue(name);
    if (root.empty()) continue;
    const std::wstring candidate = joinPath(joinPath(root, L"Tencent\\Weixin"), L"Weixin.exe");
    if (fileExists(candidate)) return candidate;
  }
  return {};
}

// 配置文件的默认内容：默认值写在代码里，与内嵌观察 DLL 的释放方式一致，
// 仓库不再维护一份需要人工同步的 example JSON。
// 网关地址与监控端密钥刻意留空——它们必须由管理员在后台生成后填进来，程序不猜。
json defaultConfigJson() {
  const std::wstring dataDirectory = cfpayDataDirectory();
  const std::wstring identifier = randomIdentifier();
  json config;
  config["baseUrl"] = "";
  config["monitorId"] = wideToUtf8(identifier.empty() ? std::wstring(L"wm_000000000000000000000000") : identifier);
  config["monitorSecret"] = "";
  config["expectedReceiverUsername"] = "";
  config["weixinExecutablePath"] = wideToUtf8(detectWeixinExecutable());
  config["weixinDllPath"] = "";
  config["observerDllPath"] = wideToUtf8(joinPath(dataDirectory, L"cfpay-hook-observer.dll"));
  config["statePath"] = wideToUtf8(joinPath(dataDirectory, L"wechat-monitor-state.json"));
  config["logPath"] = wideToUtf8(joinPath(dataDirectory, L"wechat-monitor.log"));
  config["debugLogging"] = false;
  config["processPollSeconds"] = 5;
  config["queuePollSeconds"] = 1;
  config["maxEventAgeSeconds"] = 600;
  return config;
}

// 把早期版本散落在 %LOCALAPPDATA%\CFPay 根目录下的状态与日志收进数据目录。
// 只在当前值恰好等于旧默认值时才迁移，用户改过的路径一律不动；
// 状态文件里存着已确认的收款账号与待投递队列，迁移不成功就保持原样，绝不删除原文件。
bool migrateLegacyDataPaths(json* config) {
  const std::wstring localAppData = environmentValue(L"LOCALAPPDATA");
  if (localAppData.empty()) return false;
  const std::wstring dataDirectory = cfpayDataDirectory();
  const std::pair<const char*, const wchar_t*> legacy[] = {{"statePath", L"wechat-monitor-state.json"},
                                                           {"logPath", L"wechat-monitor.log"}};
  bool changed = false;
  for (const auto& item : legacy) {
    if (!config->contains(item.first) || !config->at(item.first).is_string()) continue;
    const std::wstring current = utf8ToWide(config->at(item.first).get<std::string>());
    const std::wstring oldPath = joinPath(joinPath(localAppData, L"CFPay"), item.second);
    if (_wcsicmp(current.c_str(), oldPath.c_str()) != 0) continue;
    const std::wstring newPath = joinPath(dataDirectory, item.second);
    if (GetFileAttributesW(newPath.c_str()) != INVALID_FILE_ATTRIBUTES) continue;
    if (!ensureDirectory(dataDirectory)) continue;
    if (MoveFileExW(oldPath.c_str(), newPath.c_str(), 0) == FALSE) continue;
    config->at(item.first) = wideToUtf8(newPath);
    changed = true;
  }
  return changed;
}

// 首次运行时按代码里的默认值创建配置文件；文件已存在时只补缺失的键，用户填过的值一律不覆盖。
// 同时清掉程序早已不再读取的废弃键（hostPath、locatorScriptPath、heartbeatIntervalSeconds 等），
// 免得配置里留着一个看起来还能调、实际被忽略的「心跳间隔 30 秒」。
// detail 返回机器可读的结果码，供监控端写日志与界面提示。
bool ensureConfigFile(const std::wstring& configPath, std::string* detail) {
  const auto report = [detail](const char* code) {
    if (detail != nullptr) *detail = code;
  };
  const std::wstring directory = parentDirectory(configPath);
  if (!directory.empty() && !ensureDirectory(directory)) {
    report("CONFIG_DIRECTORY_UNAVAILABLE");
    return false;
  }
  std::string raw;
  if (!readFile(configPath, &raw)) {
    if (!writeFileAtomically(configPath, defaultConfigJson().dump(2) + "\n")) {
      report("CONFIG_WRITE_FAILED");
      return false;
    }
    report("CONFIG_CREATED");
    return true;
  }
  json config = json::parse(raw, nullptr, false);
  if (config.is_discarded() || !config.is_object()) {
    // 配置坏了不等于可以直接丢掉：先留一份带时间戳的备份再按默认值重建，
    // 否则程序起不来、用户也看不到自己原来写了什么。
    SYSTEMTIME stamp{};
    GetLocalTime(&stamp);
    wchar_t suffix[64] = {};
    std::swprintf(suffix, std::size(suffix), L".invalid-%04u%02u%02u-%02u%02u%02u", stamp.wYear, stamp.wMonth,
                  stamp.wDay, stamp.wHour, stamp.wMinute, stamp.wSecond);
    MoveFileExW(configPath.c_str(), (configPath + suffix).c_str(), MOVEFILE_REPLACE_EXISTING);
    if (!writeFileAtomically(configPath, defaultConfigJson().dump(2) + "\n")) {
      report("CONFIG_WRITE_FAILED");
      return false;
    }
    report("CONFIG_REBUILT_FROM_INVALID_FILE");
    return true;
  }
  bool changed = false;
  const json defaults = defaultConfigJson();
  for (auto it = defaults.begin(); it != defaults.end(); ++it) {
    if (config.contains(it.key())) continue;
    config[it.key()] = it.value();
    changed = true;
  }
  for (const char* obsolete : {"hostPath", "locatorScriptPath", "heartbeatIntervalSeconds",
                               "allowedPayMsgTypes", "allowedTransferMsgTypes"}) {
    if (!config.contains(obsolete)) continue;
    config.erase(obsolete);
    changed = true;
  }
  if (migrateLegacyDataPaths(&config)) changed = true;
  if (changed && !writeFileAtomically(configPath, config.dump(2) + "\n")) {
    report("CONFIG_WRITE_FAILED");
    return false;
  }
  report(changed ? "CONFIG_UPDATED" : "CONFIG_UNCHANGED");
  return true;
}

// 校验监控端 ID 格式，与监控端运行时的校验规则保持一致。
bool validMonitorId(const std::wstring& value) {
  if (value.empty() || value.size() > 80) return false;
  return std::all_of(value.begin(), value.end(), [](wchar_t character) {
    return (character >= L'a' && character <= L'z') || (character >= L'A' && character <= L'Z') ||
           (character >= L'0' && character <= L'9') || character == L'.' || character == L'_' || character == L'-';
  });
}

// 调用系统文件选择对话框让管理员本人挑选 Weixin.exe。
// 刻意不做自动扫描：微信安装路径因机器而异，猜错会导致注入到错误的进程。
std::wstring chooseWeixinExecutable(HWND owner, HRESULT* error) {
  *error = S_OK;
  const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
  if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE) {
    *error = initialized;
    return L"";
  }
  const bool uninitialize = SUCCEEDED(initialized);
  IFileOpenDialog* dialog = nullptr;
  std::wstring selected;
  HRESULT result = CoCreateInstance(
      CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_IFileOpenDialog, reinterpret_cast<void**>(&dialog));
  if (SUCCEEDED(result)) {
    const COMDLG_FILTERSPEC filters[] = {{L"Weixin.exe", L"Weixin.exe"}};
    FILEOPENDIALOGOPTIONS options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST | FOS_FORCEFILESYSTEM | FOS_NOCHANGEDIR);
    dialog->SetFileTypes(1, filters);
    dialog->SetFileName(L"Weixin.exe");
    dialog->SetTitle(L"选择微信主程序 Weixin.exe");
    result = dialog->Show(owner);
  }
  if (SUCCEEDED(result)) {
    IShellItem* item = nullptr;
    PWSTR path = nullptr;
    if (SUCCEEDED(dialog->GetResult(&item)) && SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path != nullptr) {
      selected = path;
    }
    if (path != nullptr) CoTaskMemFree(path);
    if (item != nullptr) item->Release();
  }
  if (dialog != nullptr) dialog->Release();
  if (uninitialize) CoUninitialize();
  if (FAILED(result) && result != HRESULT_FROM_WIN32(ERROR_CANCELLED)) *error = result;
  return selected;
}

// 由已选的 Weixin.exe 推算同版本的 Weixin.dll 路径。
// 依次尝试 <目录>\<版本号>\Weixin.dll 与 <目录>\Weixin.dll，都找不到就留空让用户自己填。
std::wstring inferWeixinDll(const std::wstring& executablePath) {
  const std::wstring directory = parentDirectory(executablePath);
  const std::array<std::wstring, 2> directCandidates = {
      joinPath(joinPath(directory, cfpay::target::kWeixinVersion), L"Weixin.dll"), joinPath(directory, L"Weixin.dll")};
  for (const auto& candidate : directCandidates) {
    if (fileExists(candidate)) return candidate;
  }
  WIN32_FIND_DATAW data{};
  HANDLE search = FindFirstFileW(joinPath(directory, L"*").c_str(), &data);
  if (search == INVALID_HANDLE_VALUE) return L"";
  do {
    if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 || std::wcscmp(data.cFileName, L".") == 0 ||
        std::wcscmp(data.cFileName, L"..") == 0) {
      continue;
    }
    const std::wstring candidate = joinPath(joinPath(directory, data.cFileName), L"Weixin.dll");
    if (fileExists(candidate)) {
      FindClose(search);
      return candidate;
    }
  } while (FindNextFileW(search, &data));
  FindClose(search);
  return L"";
}

// 按 Windows 命令行规则转义并加引号，路径含空格时不会被拆开。
std::wstring quoteArgument(const std::wstring& value) {
  std::wstring output = L"\"";
  std::size_t backslashes = 0;
  for (const wchar_t character : value) {
    if (character == L'\\') {
      ++backslashes;
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

// 取调试日志路径：优先配置里的 logPath，否则用 %LOCALAPPDATA%\CFPay 下的默认位置。
std::wstring debugLogPath() {
  std::wstring path = jsonString("logPath");
  if (path.empty()) path = joinPath(environmentValue(L"LOCALAPPDATA"), L"CFPay\\wechat-monitor.log");
  if (std::filesystem::path(path).is_absolute()) return path;
  return joinPath(parentDirectory(gState.configPath), path);
}

// 打开调试日志目录供用户查看；目录创建失败时给出明确中文提示而不是静默无反应。
void openDebugLog() {
  const std::wstring path = debugLogPath();
  if (path.empty() || !ensureDirectory(parentDirectory(path))) {
    setStatus(L"无法创建调试日志目录", true);
    MessageBoxW(gState.window, L"无法创建调试日志目录。", L"打开日志失败", MB_OK | MB_ICONERROR);
    return;
  }
  HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    setStatus(L"无法创建或打开调试日志文件", true);
    MessageBoxW(gState.window, L"无法创建或打开调试日志文件。", L"打开日志失败", MB_OK | MB_ICONERROR);
    return;
  }
  CloseHandle(file);
  const std::wstring parameters = quoteArgument(path);
  const HINSTANCE result = ShellExecuteW(gState.window, L"open", L"notepad.exe", parameters.c_str(),
                                         parentDirectory(path).c_str(), SW_SHOWNORMAL);
  if (reinterpret_cast<INT_PTR>(result) <= 32) {
    setStatus(L"启动记事本失败", true);
    MessageBoxW(gState.window, L"无法使用记事本打开调试日志。", L"打开日志失败", MB_OK | MB_ICONERROR);
    return;
  }
  setStatus(L"已使用记事本打开调试日志");
}

// 把配置界面的操作与结果追加进调试日志，便于事后排查“点了没反应”这类问题。
void appendConfigDiagnostic(const std::wstring& message) {
  const std::wstring path = debugLogPath();
  if (path.empty() || !ensureDirectory(parentDirectory(path))) return;
  HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return;
  SYSTEMTIME now{};
  GetLocalTime(&now);
  wchar_t prefix[96]{};
  swprintf(prefix, std::size(prefix), L"%04u-%02u-%02u %02u:%02u:%02u.%03u CONFIG ", now.wYear, now.wMonth,
           now.wDay, now.wHour, now.wMinute, now.wSecond, now.wMilliseconds);
  const std::string line = wideToUtf8(std::wstring(prefix) + message + L"\r\n");
  DWORD written = 0;
  WriteFile(file, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
  CloseHandle(file);
}

// 以子进程方式运行外部程序并等待退出。
// 用于调起记事本打开日志、以及调用自身 EXE 的受限宿主命令；继承句柄但只读取退出码。
bool runProcess(const std::wstring& program, const std::vector<std::wstring>& arguments, DWORD* exitCode, std::string* output) {
  SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
  HANDLE readPipe = nullptr;
  HANDLE writePipe = nullptr;
  if (!CreatePipe(&readPipe, &writePipe, &security, 0)) return false;
  SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

  std::wstring command = quoteArgument(program);
  for (const auto& argument : arguments) command += L" " + quoteArgument(argument);
  std::vector<wchar_t> mutableCommand(command.begin(), command.end());
  mutableCommand.push_back(L'\0');

  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  startup.dwFlags = STARTF_USESTDHANDLES;
  startup.hStdOutput = writePipe;
  startup.hStdError = writePipe;
  startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  PROCESS_INFORMATION process{};
  const BOOL created = CreateProcessW(program.c_str(), mutableCommand.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                                      gState.packageDirectory.c_str(), &startup, &process);
  CloseHandle(writePipe);
  if (!created) {
    CloseHandle(readPipe);
    return false;
  }

  output->clear();
  std::array<char, 4096> buffer{};
  while (true) {
    DWORD bytesRead = 0;
    if (!ReadFile(readPipe, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead, nullptr) || bytesRead == 0) break;
    output->append(buffer.data(), bytesRead);
  }
  WaitForSingleObject(process.hProcess, 60'000);
  GetExitCodeProcess(process.hProcess, exitCode);
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  CloseHandle(readPipe);
  return true;
}

// 刷新“当前微信账号”下拉框。
// 账号与昵称不靠读文件夹名（那是用户可改的、换机器就不一样），
// 而是通过受限宿主命令从微信进程内的账号 Hook 读取 wxid 与昵称。
bool refreshAccounts() {
  std::wstring selectedUsername;
  const int previousIndex = static_cast<int>(SendMessageW(gState.receiverAccount, CB_GETCURSEL, 0, 0));
  if (previousIndex >= 0 && static_cast<std::size_t>(previousIndex) < gState.accountOptions.size()) {
    selectedUsername = gState.accountOptions[static_cast<std::size_t>(previousIndex)].username;
  }
  SendMessageW(gState.receiverAccount, CB_RESETCONTENT, 0, 0);
  gState.accountOptions.clear();
  const std::wstring configured = jsonString("expectedReceiverUsername");
  auto addAccount = [](const std::wstring& username, const std::wstring& nickname) {
    if (username.empty()) return;
    for (const auto& option : gState.accountOptions) {
      if (option.username == username) return;
    }
    gState.accountOptions.push_back({username, nickname});
  };
  if (!configured.empty()) addAccount(configured, L"");

  std::wstring statePath = jsonString("statePath");
  if (statePath.empty()) {
    statePath = joinPath(environmentValue(L"LOCALAPPDATA"), L"CFPay\\wechat-monitor-state.json");
  }
  std::string stateText;
  const bool stateRead = readFile(statePath, &stateText);
  std::size_t stateCandidateCount = 0;
  std::size_t stateNicknameCount = 0;
  if (stateRead) {
    const json state = json::parse(stateText, nullptr, false);
    if (!state.is_discarded() && state.contains("receiverCandidates") && state["receiverCandidates"].is_array()) {
      stateCandidateCount = state["receiverCandidates"].size();
      for (const auto& candidate : state["receiverCandidates"]) {
        if (!candidate.is_object() || !candidate.contains("username") || !candidate["username"].is_string()) continue;
        const std::wstring username = utf8ToWide(candidate["username"].get<std::string>());
        const std::wstring nickname = candidate.contains("nickname") && candidate["nickname"].is_string()
                                          ? utf8ToWide(candidate["nickname"].get<std::string>())
                                          : L"";
        if (!nickname.empty()) stateNicknameCount += 1;
        if (username.empty()) continue;
        auto existing = std::find_if(gState.accountOptions.begin(), gState.accountOptions.end(),
                                     [&](const AppState::AccountOption& option) { return option.username == username; });
        if (existing == gState.accountOptions.end()) {
          addAccount(username, nickname);
        } else if (existing->nickname.empty()) {
          existing->nickname = nickname;
        }
      }
    }
  }

  int selectedIndex = -1;
  bool hasNickname = false;
  for (std::size_t index = 0; index < gState.accountOptions.size(); index += 1) {
    const auto& option = gState.accountOptions[index];
    hasNickname = hasNickname || !option.nickname.empty();
    const std::wstring label = option.nickname.empty() ? option.username : option.nickname + L"（" + option.username + L"）";
    SendMessageW(gState.receiverAccount, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
    if ((!selectedUsername.empty() && option.username == selectedUsername) ||
        (selectedUsername.empty() && option.username == configured)) {
      selectedIndex = static_cast<int>(index);
    }
  }
  if (selectedIndex >= 0) {
    SendMessageW(gState.receiverAccount, CB_SETCURSEL, selectedIndex, 0);
  } else if (gState.accountOptions.size() == 1) {
    SendMessageW(gState.receiverAccount, CB_SETCURSEL, 0, 0);
  }
  appendConfigDiagnostic(L"配置页重新获取Hook账号候选 状态读取=" + std::wstring(stateRead ? L"成功" : L"失败") +
                         L" 状态候选数=" + std::to_wstring(stateCandidateCount) +
                         L" 状态昵称数=" + std::to_wstring(stateNicknameCount) +
                         L" 下拉选项数=" + std::to_wstring(gState.accountOptions.size()) +
                         L" 昵称选项=" + std::wstring(hasNickname ? L"有" : L"无"));
  return hasNickname;
}

// 校验微信目标：主程序必须存在，Weixin.dll 必须通过版本、大小、SHA-256 与函数签名校验。
// 不通过时在界面给出明确原因，并拒绝保存，避免装到不受支持的微信版本上。
bool validateTarget(bool showMessage) {
  const std::wstring executablePath = getText(gState.weixinPath);
  if (!fileExists(executablePath) || _wcsicmp(std::filesystem::path(executablePath).filename().c_str(), L"Weixin.exe") != 0) {
    if (showMessage) MessageBoxW(gState.window, L"请选择有效的 Weixin.exe。", L"目标校验失败", MB_OK | MB_ICONERROR);
    setStatus(L"微信目标校验失败：未找到有效的 Weixin.exe", true);
    gState.targetValidated = false;
    return false;
  }
  const std::wstring dllPath = inferWeixinDll(executablePath);
  if (dllPath.empty()) {
    if (showMessage) MessageBoxW(gState.window, L"未找到对应的 Weixin.dll。", L"目标校验失败", MB_OK | MB_ICONERROR);
    setStatus(L"微信目标校验失败：未在同目录版本文件夹中找到 Weixin.dll", true);
    gState.targetValidated = false;
    return false;
  }
  setText(gState.weixinDllPath, dllPath);
  if (showMessage) setStatus(L"正在校验微信版本、SHA-256 和函数签名...");
  DWORD exitCode = 0;
  std::string processOutput;
  const std::wstring monitorPath = joinPath(gState.packageDirectory, L"cfpay-wechat-monitor.exe");
  if (!runProcess(monitorPath, {L"--internal-host", L"--inspect-weixin", dllPath}, &exitCode, &processOutput) ||
      exitCode != 0) {
    if (showMessage) MessageBoxW(gState.window, L"微信目标不受支持或文件校验失败。", L"目标校验失败", MB_OK | MB_ICONERROR);
    setStatus(L"微信目标校验失败：版本、SHA-256 或函数签名不匹配，仅支持 4.2.0.11", true);
    gState.targetValidated = false;
    return false;
  }
  gState.targetValidated = true;
  gState.validatedExecutablePath = executablePath;
  refreshAccounts();
  setStatus(showMessage ? L"微信目标校验通过" : L"配置已加载，微信目标自动校验通过");
  return true;
}

// 用 BCrypt 计算 SHA-256。
std::vector<std::uint8_t> hashSha256(const std::string& value) {
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
  if (BCryptCreateHash(algorithm, &hash, object.data(), objectLength, nullptr, 0, 0) >= 0 &&
      BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<char*>(value.data())), static_cast<ULONG>(value.size()), 0) >= 0 &&
      BCryptFinishHash(hash, digest.data(), hashLength, 0) >= 0) {
  } else {
    digest.clear();
  }
  if (hash != nullptr) BCryptDestroyHash(hash);
  BCryptCloseAlgorithmProvider(algorithm, 0);
  return digest;
}

// 字节转小写十六进制。
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

// 用 BCrypt 计算 HMAC-SHA256，构造连接测试请求的签名。
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

// 字节转 base64url，用于生成一次性 nonce。
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

// 取当前生效的监控端密钥：输入框有值就用输入框的，否则用配置里已保存的；不会把已保存的密钥回显到界面。
std::string currentSecret() {
  const std::wstring entered = getText(gState.monitorSecret);
  if (!entered.empty()) return wideToUtf8(entered);
  if (gState.config.contains("monitorSecret") && gState.config["monitorSecret"].is_string()) {
    return gState.config["monitorSecret"].get<std::string>();
  }
  return {};
}

// 读取响应体并取出服务端返回的中文说明（形如 {"code":-1,"msg":"监控端签名错误"}）。
// 只取 msg、不把整个响应体贴到界面上；读取上限 8 KiB，防止异常响应把内存撑爆。
// 没有它，测试失败时界面只剩一个 HTTP 状态码：400 究竟是「监控组件名称无效」
// 还是「监控端密钥尚未配置」根本无从判断。
std::wstring serverErrorMessage(HINTERNET request) {
  constexpr std::size_t kMaxBodyBytes = 8 * 1024;
  std::string body;
  while (body.size() < kMaxBodyBytes) {
    DWORD available = 0;
    if (WinHttpQueryDataAvailable(request, &available) == FALSE || available == 0) break;
    const std::size_t offset = body.size();
    const DWORD wanted = static_cast<DWORD>(std::min<std::size_t>(available, kMaxBodyBytes - offset));
    body.resize(offset + wanted);
    DWORD bytesRead = 0;
    if (WinHttpReadData(request, body.data() + offset, wanted, &bytesRead) == FALSE) {
      body.resize(offset);
      break;
    }
    body.resize(offset + bytesRead);
    if (bytesRead == 0) break;
  }
  const json parsed = json::parse(body, nullptr, false);
  if (parsed.is_discarded() || !parsed.is_object()) return {};
  if (!parsed.contains("msg") || !parsed["msg"].is_string()) return {};
  return utf8ToWide(parsed["msg"].get<std::string>());
}

// 用当前表单内容向 Worker 的状态上报接口发一次真实请求，验证网关地址、监控端 ID 与密钥是否匹配。
// 失败只显示截断后的接口错误，绝不输出密钥；请求走 WinHTTP 并要求 HTTPS。
bool testConnection() {
  const std::wstring baseUrl = getText(gState.baseUrl);
  const std::wstring monitorId = getText(gState.monitorId);
  const std::string secret = currentSecret();
  if (baseUrl.rfind(L"https://", 0) != 0 || !validMonitorId(monitorId) || secret.size() < 32) {
    MessageBoxW(gState.window, L"请先填写 HTTPS 网关、监控端 ID 和至少 32 位密钥。", L"连接测试", MB_OK | MB_ICONERROR);
    return false;
  }

  URL_COMPONENTSW components{};
  components.dwStructSize = sizeof(components);
  components.dwSchemeLength = static_cast<DWORD>(-1);
  components.dwHostNameLength = static_cast<DWORD>(-1);
  components.dwUrlPathLength = static_cast<DWORD>(-1);
  if (!WinHttpCrackUrl(baseUrl.c_str(), 0, 0, &components) || components.nScheme != INTERNET_SCHEME_HTTPS) return false;
  const std::wstring host(components.lpszHostName, components.dwHostNameLength);
  std::wstring requestPath(components.lpszUrlPath, components.dwUrlPathLength);
  while (!requestPath.empty() && requestPath.back() == L'/') requestPath.pop_back();
  requestPath += L"/api/monitor/status";

  // 探测请求里的版本号与监控端实际上报的完全同源：两边各写一份时，
  // 配置界面测通了、真实上报却报另一个版本，这类错配从日志里根本看不出来。
  const json probe{{"name", "Windows Native Monitor"}, {"version", cfpay::monitor::kVersion}, {"platform", "windows"}};
  const std::string body = probe.dump();
  const auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(
                             std::chrono::system_clock::now().time_since_epoch())
                             .count();
  std::array<std::uint8_t, 24> nonceBytes{};
  if (BCryptGenRandom(nullptr, nonceBytes.data(), static_cast<ULONG>(nonceBytes.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) {
    return false;
  }
  const std::string nonce = base64Url(std::vector<std::uint8_t>(nonceBytes.begin(), nonceBytes.end()));
  const std::string timestampText = std::to_string(timestamp);
  const std::string signedValue = timestampText + "\n" + nonce + "\n" + hexValue(hashSha256(body));
  const std::string signature = hmacSha256(secret, signedValue);
  if (signature.empty()) return false;

  const std::wstring userAgent = utf8ToWide("CFPay-WeChat-Config/" + std::string(cfpay::monitor::kVersion));
  HINTERNET session = WinHttpOpen(userAgent.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (session == nullptr) return false;
  WinHttpSetTimeouts(session, 5000, 5000, 5000, 10000);
  HINTERNET connection = WinHttpConnect(session, host.c_str(), components.nPort, 0);
  HINTERNET request = connection == nullptr
                          ? nullptr
                          : WinHttpOpenRequest(connection, L"POST", requestPath.c_str(), nullptr, WINHTTP_NO_REFERER,
                                               WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
  const std::wstring headers = L"Content-Type: application/json\r\nX-Monitor-Id: " + monitorId +
                               L"\r\nX-Timestamp: " + utf8ToWide(timestampText) + L"\r\nX-Nonce: " + utf8ToWide(nonce) +
                               L"\r\nX-Signature: " + utf8ToWide(signature) + L"\r\n";
  bool success = request != nullptr &&
                 WinHttpSendRequest(request, headers.c_str(), static_cast<DWORD>(-1),
                                    const_cast<char*>(body.data()), static_cast<DWORD>(body.size()),
                                    static_cast<DWORD>(body.size()), 0) != FALSE &&
                 WinHttpReceiveResponse(request, nullptr) != FALSE;
  DWORD statusCode = 0;
  DWORD statusSize = sizeof(statusCode);
  bool statusKnown = false;
  if (success) {
    statusKnown = WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                      WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize,
                                      WINHTTP_NO_HEADER_INDEX) != FALSE;
    success = statusKnown && statusCode >= 200 && statusCode < 300;
  }
  // 响应体必须在关闭句柄之前读完，否则拿不到服务端的拒绝原因。
  const std::wstring reason = success || !statusKnown ? std::wstring() : serverErrorMessage(request);
  if (request != nullptr) WinHttpCloseHandle(request);
  if (connection != nullptr) WinHttpCloseHandle(connection);
  WinHttpCloseHandle(session);
  if (success) {
    setStatus(L"Worker 连接和监控密钥验证通过");
    return true;
  }
  setStatus(L"Worker 连接测试失败，HTTP " + std::to_wstring(statusCode) + (reason.empty() ? L"" : L"：" + reason),
            true);
  return false;
}

// 保存配置：先做完整校验（HTTPS 地址、ID 与密钥格式、微信目标），再原子写入 config.json。
// 已保存的密钥字段留空表示保持不变，不会因为界面没回显就把密钥清空。
bool saveConfiguration() {
  std::wstring baseUrl = getText(gState.baseUrl);
  while (!baseUrl.empty() && baseUrl.back() == L'/') baseUrl.pop_back();
  const std::wstring monitorId = getText(gState.monitorId);
  const std::string secret = currentSecret();
  if (baseUrl.rfind(L"https://", 0) != 0) {
    MessageBoxW(gState.window, L"Worker 网关必须使用 HTTPS。", L"保存失败", MB_OK | MB_ICONERROR);
    return false;
  }
  if (!validMonitorId(monitorId)) {
    MessageBoxW(gState.window, L"监控端 ID 格式无效。", L"保存失败", MB_OK | MB_ICONERROR);
    return false;
  }
  if (secret.size() < 32) {
    MessageBoxW(gState.window, L"PC 监控端密钥至少需要 32 位。", L"保存失败", MB_OK | MB_ICONERROR);
    return false;
  }
  if ((!gState.targetValidated || gState.validatedExecutablePath != getText(gState.weixinPath)) &&
      !validateTarget(true)) {
    return false;
  }

  const int accountIndex = static_cast<int>(SendMessageW(gState.receiverAccount, CB_GETCURSEL, 0, 0));
  std::wstring account;
  if (accountIndex >= 0 && static_cast<std::size_t>(accountIndex) < gState.accountOptions.size()) {
    account = gState.accountOptions[static_cast<std::size_t>(accountIndex)].username;
  }

  gState.config["baseUrl"] = wideToUtf8(baseUrl);
  gState.config["monitorId"] = wideToUtf8(monitorId);
  gState.config["monitorSecret"] = secret;
  gState.config["expectedReceiverUsername"] = wideToUtf8(account);
  gState.config["weixinExecutablePath"] = wideToUtf8(getText(gState.weixinPath));
  gState.config["weixinDllPath"] = wideToUtf8(getText(gState.weixinDllPath));
  // 数据文件统一放在 %LOCALAPPDATA%\CFPay\WeChatMonitor；已经存在的值保留不动，
  // 只有缺失时才按默认位置补上，避免把用户手动指定过的路径改回默认值。
  const std::wstring dataDirectory = cfpayDataDirectory();
  const auto keepOr = [](const char* key, const std::wstring& fallback) {
    const std::string existing = gState.config.contains(key) && gState.config[key].is_string()
                                     ? gState.config[key].get<std::string>()
                                     : std::string();
    gState.config[key] = existing.empty() ? wideToUtf8(fallback) : existing;
  };
  keepOr("observerDllPath", joinPath(dataDirectory, L"cfpay-hook-observer.dll"));
  keepOr("statePath", joinPath(dataDirectory, L"wechat-monitor-state.json"));
  keepOr("logPath", joinPath(dataDirectory, L"wechat-monitor.log"));
  gState.config.erase("hostPath");
  gState.config.erase("locatorScriptPath");
  gState.config.erase("heartbeatIntervalSeconds");
  gState.config["debugLogging"] = SendMessageW(gState.debugLogging, BM_GETCHECK, 0, 0) == BST_CHECKED;
  if (!gState.config.contains("processPollSeconds")) gState.config["processPollSeconds"] = 5;
  if (!gState.config.contains("queuePollSeconds")) gState.config["queuePollSeconds"] = 1;
  if (!gState.config.contains("maxEventAgeSeconds")) gState.config["maxEventAgeSeconds"] = 600;
  gState.config.erase("allowedPayMsgTypes");
  gState.config.erase("allowedTransferMsgTypes");

  const std::string serialized = gState.config.dump(2) + "\n";
  if (!writeFileAtomically(gState.configPath, serialized)) {
    MessageBoxW(gState.window, L"配置文件写入失败。", L"保存失败", MB_OK | MB_ICONERROR);
    return false;
  }
  setText(gState.monitorSecret, L"");
  SendMessageW(gState.monitorSecret, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"已配置，留空不修改"));
  setStatus(L"配置已保存");
  MessageBoxW(gState.window, L"监控端配置已保存。", L"CFPay", MB_OK | MB_ICONINFORMATION);
  return true;
}

// 创建子控件的封装，统一字体与位置参数。
HWND createControl(const wchar_t* className, const wchar_t* text, DWORD style, int x, int y, int width, int height,
                   int id = 0, DWORD extendedStyle = 0) {
  HWND control = CreateWindowExW(extendedStyle, className, text, WS_CHILD | WS_VISIBLE | style, x, y + gState.verticalOffset,
                                 width, height,
                                 gState.window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
  if (control != nullptr && gState.font != nullptr) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(gState.font), TRUE);
  return control;
}

// 创建一行说明文字。
void createLabel(const wchar_t* text, int y) {
  createControl(L"STATIC", text, SS_LEFT, 28, y + 5, 170, 26);
}

// 构建整个配置界面：单窗口内包含网关地址、监控端 ID 与密钥、微信主程序与 DLL 路径、
// 当前微信账号下拉框与重新获取按钮、各项轮询间隔，以及保存/测试/打开日志等操作。
void createUi() {
  gState.font = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                            CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
  HWND title = createControl(L"STATIC", L"微信监控端配置", SS_LEFT, 28, 24, 500, 38);
  gState.titleFont = CreateFontW(-26, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                 CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
  SendMessageW(title, WM_SETFONT, reinterpret_cast<WPARAM>(gState.titleFont), TRUE);

  int y = 78;
  createLabel(L"微信主程序路径", y);
  gState.weixinPath = createControl(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | ES_READONLY, 198, y, 430, 32, kWeixinPath,
                                    WS_EX_CLIENTEDGE);
  createControl(L"BUTTON", L"浏览", BS_PUSHBUTTON, 640, y, 90, 32, kBrowseWeixin);
  y += 46;
  createLabel(L"Weixin.dll 路径", y);
  gState.weixinDllPath = createControl(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | ES_READONLY, 198, y, 532, 32,
                                       kWeixinDllPath, WS_EX_CLIENTEDGE);
  y += 58;
  createLabel(L"Worker 网关", y);
  gState.baseUrl = createControl(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, 198, y, 532, 32, kBaseUrl, WS_EX_CLIENTEDGE);
  y += 46;
  createLabel(L"本机 ID", y);
  gState.monitorId = createControl(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, 198, y, 430, 32, kMonitorId, WS_EX_CLIENTEDGE);
  createControl(L"BUTTON", L"生成", BS_PUSHBUTTON, 640, y, 90, 32, kGenerateMonitorId);
  y += 46;
  createLabel(L"接入密钥", y);
  gState.monitorSecret = createControl(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | ES_PASSWORD, 198, y, 532, 32,
                                       kMonitorSecret, WS_EX_CLIENTEDGE);
  y += 46;
  createLabel(L"当前微信账号", y);
  gState.receiverAccount = createControl(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, 198, y, 430, 180,
                                         kReceiverAccount, WS_EX_CLIENTEDGE);
  createControl(L"BUTTON", L"重新获取", BS_PUSHBUTTON, 640, y, 90, 32, kRefreshReceiverAccount);

  y += 46;
  createLabel(L"调试日志", y);
  gState.debugLogging = createControl(L"BUTTON", L"写入本地调试日志", BS_AUTOCHECKBOX, 198, y, 260, 28,
                                      kDebugLogging);
  createControl(L"BUTTON", L"打开日志", BS_PUSHBUTTON, 640, y, 90, 32, kOpenDebugLog);

  y += 46;
  createControl(L"BUTTON", L"校验目标", BS_PUSHBUTTON, 198, y, 120, 36, kValidateTarget);
  createControl(L"BUTTON", L"测试连接", BS_PUSHBUTTON, 330, y, 120, 36, kTestConnection);
  createControl(L"BUTTON", L"保存配置", BS_DEFPUSHBUTTON, 610, y, 120, 36, kSaveConfig);
  y += 54;
  gState.status = createControl(L"STATIC", L"", SS_LEFT, 28, y, 702, 40, kStatus);
}

// 从 config.json 载入各项配置并填充界面；文件不存在时先按代码里的默认值创建，密钥类字段一律不回显。
void loadConfiguration() {
  std::string ensureDetail;
  ensureConfigFile(gState.configPath, &ensureDetail);
  std::string raw;
  if (readFile(gState.configPath, &raw)) {
    try {
      gState.config = json::parse(raw);
      if (!gState.config.is_object()) gState.config = json::object();
    } catch (...) {
      gState.config = json::object();
      setStatus(L"现有配置文件格式无效，将在保存时覆盖", true);
    }
  }
  setText(gState.baseUrl, jsonString("baseUrl", L"https://pay.example.com"));
  std::wstring monitorId = jsonString("monitorId");
  if (monitorId.empty() || monitorId == L"windows-main") monitorId = randomIdentifier();
  setText(gState.monitorId, monitorId);
  setText(gState.weixinPath, jsonString("weixinExecutablePath"));
  setText(gState.weixinDllPath, jsonString("weixinDllPath"));
  const bool secretConfigured = gState.config.contains("monitorSecret") && gState.config["monitorSecret"].is_string() &&
                                gState.config["monitorSecret"].get<std::string>().size() >= 32;
  SendMessageW(gState.monitorSecret, EM_SETCUEBANNER, TRUE,
               reinterpret_cast<LPARAM>(secretConfigured ? L"已配置，留空不修改" : L"至少 32 位"));
  refreshAccounts();
  const bool debugLogging = gState.config.contains("debugLogging") && gState.config["debugLogging"].is_boolean() &&
                            gState.config["debugLogging"].get<bool>();
  SendMessageW(gState.debugLogging, BM_SETCHECK, debugLogging ? BST_CHECKED : BST_UNCHECKED, 0);
}

// 处理界面按钮命令：选择微信主程序、重新获取账号、测试连接、保存、打开日志等。
bool handleCommand(HWND window, int command) {
  switch (command) {
    case kBrowseWeixin: {
      HRESULT error = S_OK;
      const std::wstring path = chooseWeixinExecutable(window, &error);
      if (!path.empty()) {
        setText(gState.weixinPath, path);
        gState.targetValidated = false;
        validateTarget(false);
      } else if (FAILED(error)) {
        wchar_t message[160]{};
        swprintf(message, std::size(message), L"打开文件选择器失败，错误 0x%08lX。", static_cast<unsigned long>(error));
        setStatus(message, true);
        MessageBoxW(window, message, L"选择微信主程序", MB_OK | MB_ICONERROR);
      }
      return true;
    }
    case kGenerateMonitorId:
      setText(gState.monitorId, randomIdentifier());
      setStatus(L"已生成新的监控端 ID，保存后生效");
      return true;
    case kOpenDebugLog:
      openDebugLog();
      return true;
    case kRefreshReceiverAccount:
      if (refreshAccounts()) setStatus(L"已重新获取 Hook 捕获的微信账号和昵称");
      else setStatus(L"Hook 尚未捕获微信账号或昵称，请确认微信已登录后重试", true);
      return true;
    case kValidateTarget:
      validateTarget(true);
      return true;
    case kTestConnection:
      setStatus(L"正在测试 Worker 连接...");
      if (!testConnection()) {
        MessageBoxW(window, L"Worker 连接或密钥验证失败。", L"连接测试", MB_OK | MB_ICONERROR);
      }
      return true;
    case kSaveConfig:
      saveConfiguration();
      return true;
    default:
      return false;
  }
}

// 独立配置窗口的消息循环；内嵌模式下不会走到这里。
LRESULT CALLBACK windowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
  switch (message) {
    case WM_CREATE:
      gState.window = window;
      createUi();
      loadConfiguration();
      return 0;
    case WM_COMMAND:
      if (handleCommand(window, LOWORD(wParam))) return 0;
      break;
    case WM_CTLCOLORSTATIC:
      SetBkMode(reinterpret_cast<HDC>(wParam), TRANSPARENT);
      return reinterpret_cast<LRESULT>(gState.background);
    case WM_DESTROY:
      DestroyWechatConfigPanel();
      if (gState.background != nullptr) DeleteObject(gState.background);
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcW(window, message, wParam, lParam);
}

#ifndef CFPAY_EMBED_CONFIG
// 从命令行取配置文件路径，未指定时用默认位置。
std::wstring configPathFromCommandLine() {
  int argumentCount = 0;
  LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
  std::wstring result = joinPath(moduleDirectory(), L"config.json");
  if (arguments != nullptr) {
    for (int index = 1; index + 1 < argumentCount; ++index) {
      if (std::wcscmp(arguments[index], L"--config") == 0) {
        result = std::filesystem::absolute(arguments[index + 1]).wstring();
        break;
      }
    }
    LocalFree(arguments);
  }
  return result;
}
#endif

}  // namespace

// 以下两个函数在头文件里声明、供监控端主程序调用：实现放在匿名命名空间内，
// 这里只做一层导出包装，避免把配置文件的内部细节暴露成全局符号。

// 程序创建的所有文件统一存放的目录，即 %LOCALAPPDATA%\CFPay\WeChatMonitor。
std::wstring CfpayDataDirectory() { return cfpayDataDirectory(); }

// 首次运行创建配置文件；已存在时只补缺失字段，不覆盖用户填过的值。
bool EnsureWechatConfigFile(const std::wstring& configPath, std::string* detail) {
  return ensureConfigFile(configPath, detail);
}

// 把配置界面内嵌进监控端主窗口（而不是另开一个 EXE 窗口）。
// 这样整个监控端只有一个窗口，不会出现主程序退了配置页还留着的情况。
bool InitializeWechatConfigPanel(HWND parent, const std::wstring& configPath, int verticalOffset) {
  if (parent == nullptr) return false;
  gState.window = parent;
  gState.configPath = configPath;
  gState.packageDirectory = moduleDirectory();
  gState.config = json::object();
  gState.targetValidated = false;
  gState.embedded = true;
  gState.verticalOffset = verticalOffset;
  createUi();
  loadConfiguration();
  return gState.status != nullptr;
}

// 启动后由窗口消息触发：程序自行完成目标校验，只有失败时才在状态栏报错。
void RunWechatConfigPanelAutoValidate() {
  if (getText(gState.weixinPath).empty()) {
    setStatus(L"尚未配置微信主程序，请选择 Weixin.exe", true);
    return;
  }
  validateTarget(false);
}

// 内嵌模式下把主窗口的命令消息转给配置面板处理。
bool HandleWechatConfigPanelCommand(WPARAM wParam, LPARAM) {
  return gState.embedded && gState.window != nullptr && handleCommand(gState.window, LOWORD(wParam));
}

// 释放配置面板的字体与控件资源。
void DestroyWechatConfigPanel() {
  if (gState.font != nullptr) DeleteObject(gState.font);
  if (gState.titleFont != nullptr) DeleteObject(gState.titleFont);
  gState.font = nullptr;
  gState.titleFont = nullptr;
  gState.window = nullptr;
  gState.status = nullptr;
  gState.embedded = false;
  gState.verticalOffset = 0;
}

// 以独立窗口方式运行配置界面（兼容入口），设置 DPI 感知后进入消息循环。
int RunWechatConfigWindow(HINSTANCE instance, const std::wstring& configPath, int showCommand) {
  SetProcessDPIAware();
  INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES};
  InitCommonControlsEx(&controls);
  gState.packageDirectory = moduleDirectory();
  gState.configPath = configPath;
  gState.background = CreateSolidBrush(RGB(246, 247, 249));

  WNDCLASSEXW windowClass{};
  windowClass.cbSize = sizeof(windowClass);
  windowClass.lpfnWndProc = windowProcedure;
  windowClass.hInstance = instance;
  windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  windowClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
  windowClass.hbrBackground = gState.background;
  windowClass.lpszClassName = kWindowClass;
  if (!RegisterClassExW(&windowClass)) return 1;

  HWND window = CreateWindowExW(0, kWindowClass, kWindowTitle, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                                CW_USEDEFAULT, CW_USEDEFAULT, 780, 620, nullptr, nullptr, instance, nullptr);
  if (window == nullptr) return 2;
  ShowWindow(window, showCommand);
  UpdateWindow(window);

  MSG message{};
  while (GetMessageW(&message, nullptr, 0, 0) > 0) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
  return static_cast<int>(message.wParam);
}

#ifndef CFPAY_EMBED_CONFIG
// 独立配置程序的入口；被监控端内嵌编译时不会用到。
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
  return RunWechatConfigWindow(instance, configPathFromCommandLine(), showCommand);
}
#endif
