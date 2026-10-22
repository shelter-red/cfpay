#include "wechat_login_assistant.h"

#include <tlhelp32.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <string>
#include <vector>

extern "C" {
#include "quirc/quirc.h"
}

#include "test_login_qr.h"

namespace {

constexpr DWORD kPrintWindowRenderFullContent = 0x00000002;
constexpr int kMaxCaptureDimension = 4096;
constexpr wchar_t kWechatLoginWindowClass[] = L"mmui::LoginWindow";
constexpr wchar_t kWechatQtWindowClass[] = L"Qt51514QWindowIcon";

struct WindowCandidate {
  HWND window = nullptr;
  std::uint64_t area = 0;
};

struct WindowSearch {
  DWORD processId = 0;
  std::vector<WindowCandidate> windows;
};

// EnumWindows 回调：收集属于指定进程的可见顶层窗口。
BOOL CALLBACK findProcessWindow(HWND window, LPARAM parameter) {
  auto* search = reinterpret_cast<WindowSearch*>(parameter);
  DWORD processId = 0;
  GetWindowThreadProcessId(window, &processId);
  if (processId != search->processId || !IsWindowVisible(window) || GetWindow(window, GW_OWNER) != nullptr) return TRUE;
  RECT bounds{};
  if (!GetWindowRect(window, &bounds)) return TRUE;
  const LONG width = bounds.right - bounds.left;
  const LONG height = bounds.bottom - bounds.top;
  if (width <= 0 || height <= 0) return TRUE;
  const std::uint64_t area = static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height);
  search->windows.push_back({window, area});
  return TRUE;
}

// 枚举目标进程的所有顶层窗口候选，供登录识别与按钮点击使用。
std::vector<WindowCandidate> findProcessWindows(DWORD processId) {
  WindowSearch search;
  search.processId = processId;
  EnumWindows(findProcessWindow, reinterpret_cast<LPARAM>(&search));
  std::sort(search.windows.begin(), search.windows.end(), [](const WindowCandidate& left, const WindowCandidate& right) {
    return left.area > right.area;
  });
  return search.windows;
}

// 截取窗口客户区并转成灰度像素，供二维码解码使用。
// 只在识别到疑似登录二维码窗口时才截屏，截图不落盘、不上传，解码完即丢弃。
bool captureWindowGrayscale(HWND window, std::vector<std::uint8_t>* pixels, int* width, int* height) {
  RECT bounds{};
  if (!GetWindowRect(window, &bounds)) return false;
  const int captureWidth = bounds.right - bounds.left;
  const int captureHeight = bounds.bottom - bounds.top;
  if (captureWidth < 80 || captureHeight < 80 || captureWidth > kMaxCaptureDimension ||
      captureHeight > kMaxCaptureDimension) {
    return false;
  }

  HDC windowDc = GetWindowDC(window);
  HDC memoryDc = windowDc == nullptr ? nullptr : CreateCompatibleDC(windowDc);
  BITMAPINFO bitmapInfo{};
  bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bitmapInfo.bmiHeader.biWidth = captureWidth;
  bitmapInfo.bmiHeader.biHeight = -captureHeight;
  bitmapInfo.bmiHeader.biPlanes = 1;
  bitmapInfo.bmiHeader.biBitCount = 32;
  bitmapInfo.bmiHeader.biCompression = BI_RGB;
  void* bitmapPixels = nullptr;
  HBITMAP bitmap = memoryDc == nullptr
                       ? nullptr
                       : CreateDIBSection(memoryDc, &bitmapInfo, DIB_RGB_COLORS, &bitmapPixels, nullptr, 0);
  HGDIOBJ previous = bitmap == nullptr ? nullptr : SelectObject(memoryDc, bitmap);
  bool captured = false;
  if (bitmap != nullptr && bitmapPixels != nullptr) {
    captured = PrintWindow(window, memoryDc, kPrintWindowRenderFullContent) != FALSE;
    if (!captured) {
      HDC screenDc = GetDC(nullptr);
      if (screenDc != nullptr) {
        captured = BitBlt(memoryDc, 0, 0, captureWidth, captureHeight, screenDc, bounds.left, bounds.top, SRCCOPY) != FALSE;
        ReleaseDC(nullptr, screenDc);
      }
    }
  }
  if (captured) {
    pixels->resize(static_cast<std::size_t>(captureWidth) * static_cast<std::size_t>(captureHeight));
    const auto* source = static_cast<const std::uint8_t*>(bitmapPixels);
    for (std::size_t index = 0; index < pixels->size(); index += 1) {
      const std::uint8_t blue = source[index * 4];
      const std::uint8_t green = source[index * 4 + 1];
      const std::uint8_t red = source[index * 4 + 2];
      (*pixels)[index] = static_cast<std::uint8_t>((red * 77 + green * 150 + blue * 29) >> 8);
    }
    *width = captureWidth;
    *height = captureHeight;
  }
  if (previous != nullptr) SelectObject(memoryDc, previous);
  if (bitmap != nullptr) DeleteObject(bitmap);
  if (memoryDc != nullptr) DeleteDC(memoryDc);
  if (windowDc != nullptr) ReleaseDC(window, windowDc);
  return captured;
}

// 校验解码出的二维码内容：长度受限且只允许可见 ASCII，过滤掉误识别的噪点结果。
bool validQrPayload(const std::string& payload) {
  if (payload.size() < 8 || payload.size() > 2048) return false;
  for (const unsigned char character : payload) {
    if (character < 0x20 || character == 0x7f) return false;
  }
  std::string normalized = payload.substr(0, std::min<std::size_t>(payload.size(), 16));
  std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char character) {
    return static_cast<char>(std::tolower(character));
  });
  const std::array<const char*, 4> prefixes = {"http://", "https://", "weixin://", "wxp://"};
  return std::any_of(prefixes.begin(), prefixes.end(), [&](const char* prefix) { return normalized.rfind(prefix, 0) == 0; });
}

// 用 quirc 在灰度像素上识别二维码，失败返回空串。
std::string decodeQrPixels(const std::uint8_t* pixels, int width, int height) {
  struct quirc* decoder = quirc_new();
  if (decoder == nullptr) return {};
  std::string payload;
  if (quirc_resize(decoder, width, height) == 0) {
    int imageWidth = 0;
    int imageHeight = 0;
    std::uint8_t* image = quirc_begin(decoder, &imageWidth, &imageHeight);
    if (image != nullptr && imageWidth == width && imageHeight == height) {
      std::memcpy(image, pixels, static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
      quirc_end(decoder);
      const int count = quirc_count(decoder);
      for (int index = 0; index < count; index += 1) {
        struct quirc_code code{};
        struct quirc_data data{};
        quirc_extract(decoder, index, &code);
        if (quirc_decode(&code, &data) != QUIRC_SUCCESS || data.payload_len <= 0) continue;
        const std::string candidate(reinterpret_cast<const char*>(data.payload), static_cast<std::size_t>(data.payload_len));
        if (validQrPayload(candidate)) {
          payload = candidate;
          break;
        }
      }
    }
  }
  quirc_destroy(decoder);
  return payload;
}

// 判断控件文本是否是“进入微信”一类登录按钮文案；做大小写与空白归一后比较。
bool isLoginButtonName(const std::wstring& value) {
  std::wstring normalized;
  normalized.reserve(value.size());
  for (const wchar_t character : value) {
    if (!iswspace(character)) normalized.push_back(static_cast<wchar_t>(towlower(character)));
  }
  return normalized == L"\u767b\u5f55" || normalized == L"\u8fdb\u5165\u5fae\u4fe1" || normalized == L"login" ||
         normalized == L"log-in" ||
         (normalized.size() <= 12 && normalized.find(L"\u767b\u5f55") != std::wstring::npos);
}

struct NativeButtonSearch {
  HWND button = nullptr;
};

// EnumChildWindows 回调：按类名与文本找原生登录按钮。
BOOL CALLBACK findNativeLoginButton(HWND window, LPARAM parameter) {
  auto* search = reinterpret_cast<NativeButtonSearch*>(parameter);
  wchar_t className[64]{};
  wchar_t text[128]{};
  GetClassNameW(window, className, static_cast<int>(std::size(className)));
  GetWindowTextW(window, text, static_cast<int>(std::size(text)));
  if (_wcsicmp(className, L"Button") == 0 && isLoginButtonName(text) && IsWindowEnabled(window)) {
    search->button = window;
    return FALSE;
  }
  return TRUE;
}

// 直接给登录窗口发点击消息，不使用无障碍接口。
// invoke 为 false 时只探测不点击，用于先确认“确实停在登录页”再决定动作。
bool directClickLoginWindow(HWND window, bool invoke, bool* invoked) {
  wchar_t className[128] = {};
  if (GetClassNameW(window, className, static_cast<int>(std::size(className))) <= 0 ||
      (_wcsicmp(className, kWechatLoginWindowClass) != 0 && _wcsicmp(className, kWechatQtWindowClass) != 0)) {
    return false;
  }
  if (!IsWindowEnabled(window)) return false;
  RECT client{};
  if (!GetClientRect(window, &client) || client.right <= 0 || client.bottom <= 0) return false;
  const LONG width = client.right - client.left;
  const LONG height = client.bottom - client.top;
  if (height <= width || width < 220 || width > 480 || height < 300 || height > 680) return false;
  const LONG x = client.right / 2;
  const LONG y = client.bottom * 295 / 380;
  const LPARAM point = MAKELPARAM(static_cast<short>(x), static_cast<short>(y));
  if (invoke) {
    DWORD_PTR result = 0;
    const bool activated = SendMessageTimeoutW(window, WM_MOUSEACTIVATE, reinterpret_cast<WPARAM>(window),
                                                MAKELPARAM(HTCLIENT, WM_LBUTTONDOWN),
                                                SMTO_ABORTIFHUNG | SMTO_BLOCK, 3'000, &result) != 0;
    const bool focused = SendMessageTimeoutW(window, WM_SETFOCUS, 0, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 3'000, &result) != 0;
    const bool cursor = SendMessageTimeoutW(window, WM_SETCURSOR, reinterpret_cast<WPARAM>(window),
                                             MAKELPARAM(HTCLIENT, WM_MOUSEMOVE),
                                             SMTO_ABORTIFHUNG | SMTO_BLOCK, 3'000, &result) != 0;
    const bool moved = activated && focused && cursor &&
                       SendMessageTimeoutW(window, WM_MOUSEMOVE, 0, point, SMTO_ABORTIFHUNG | SMTO_BLOCK, 3'000, &result) != 0;
    const bool pressed = moved &&
                         SendMessageTimeoutW(window, WM_LBUTTONDOWN, MK_LBUTTON, point,
                                              SMTO_ABORTIFHUNG | SMTO_BLOCK, 3'000, &result) != 0;
    const bool released = pressed &&
                          SendMessageTimeoutW(window, WM_LBUTTONUP, 0, point,
                                               SMTO_ABORTIFHUNG | SMTO_BLOCK, 3'000, &result) != 0;
    *invoked = released;
  }
  return true;
}

// 直接点击账号切换入口；用于管理员要求换收款账号时让微信回到登录页。
bool directClickAccountSwitch(HWND window, bool invoke, bool* invoked) {
  wchar_t className[128] = {};
  if (GetClassNameW(window, className, static_cast<int>(std::size(className))) <= 0 ||
      (_wcsicmp(className, kWechatLoginWindowClass) != 0 && _wcsicmp(className, kWechatQtWindowClass) != 0)) {
    return false;
  }
  if (!IsWindowEnabled(window)) return false;
  RECT client{};
  if (!GetClientRect(window, &client) || client.right <= 0 || client.bottom <= 0) return false;
  const LONG width = client.right - client.left;
  const LONG height = client.bottom - client.top;
  if (height <= width || width < 220 || width > 480 || height < 300 || height > 680) return false;
  const LONG x = client.right * 88 / 280;
  const LONG y = client.bottom * 346 / 380;
  const LPARAM point = MAKELPARAM(static_cast<short>(x), static_cast<short>(y));
  if (invoke) {
    DWORD_PTR result = 0;
    const bool activated = SendMessageTimeoutW(window, WM_MOUSEACTIVATE, reinterpret_cast<WPARAM>(window),
                                                MAKELPARAM(HTCLIENT, WM_LBUTTONDOWN),
                                                SMTO_ABORTIFHUNG | SMTO_BLOCK, 3'000, &result) != 0;
    const bool focused = activated &&
                         SendMessageTimeoutW(window, WM_SETFOCUS, 0, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 3'000, &result) != 0;
    const bool cursor = focused && SendMessageTimeoutW(window, WM_SETCURSOR, reinterpret_cast<WPARAM>(window),
                                                        MAKELPARAM(HTCLIENT, WM_MOUSEMOVE),
                                                        SMTO_ABORTIFHUNG | SMTO_BLOCK, 3'000, &result) != 0;
    const bool pressed = cursor && SendMessageTimeoutW(window, WM_LBUTTONDOWN, MK_LBUTTON, point,
                                                        SMTO_ABORTIFHUNG | SMTO_BLOCK, 3'000, &result) != 0;
    *invoked = pressed && SendMessageTimeoutW(window, WM_LBUTTONUP, 0, point,
                                               SMTO_ABORTIFHUNG | SMTO_BLOCK, 3'000, &result) != 0;
  }
  return true;
}

// 先找原生登录按钮并点击，找不到再退回窗口级点击。
bool findAndInvokeLoginButton(HWND window, bool invoke, bool* invoked) {
  NativeButtonSearch native;
  EnumChildWindows(window, findNativeLoginButton, reinterpret_cast<LPARAM>(&native));
  if (native.button != nullptr) {
    if (invoke) {
      DWORD_PTR result = 0;
      *invoked = SendMessageTimeoutW(native.button, BM_CLICK, 0, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 3'000, &result) != 0;
    }
    return true;
  }
  return directClickLoginWindow(window, invoke, invoked);
}

// 取进程可执行文件完整路径，只用受限查询权限。
bool processPath(DWORD processId, std::wstring* output) {
  HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
  if (process == nullptr) return false;
  std::vector<wchar_t> path(32768);
  DWORD length = static_cast<DWORD>(path.size());
  const bool success = QueryFullProcessImageNameW(process, 0, path.data(), &length) != FALSE;
  CloseHandle(process);
  if (success) output->assign(path.data(), length);
  return success;
}

// 按可执行文件绝对路径精确匹配进程 ID，避免误伤同名的其它程序。
std::vector<DWORD> exactProcessIds(const std::wstring& executablePath) {
  std::vector<DWORD> output;
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot == INVALID_HANDLE_VALUE) return output;
  PROCESSENTRY32W entry{};
  entry.dwSize = sizeof(entry);
  if (Process32FirstW(snapshot, &entry)) {
    do {
      if (_wcsicmp(entry.szExeFile, L"Weixin.exe") != 0) continue;
      std::wstring path;
      if (processPath(entry.th32ProcessID, &path) && _wcsicmp(path.c_str(), executablePath.c_str()) == 0) {
        output.push_back(entry.th32ProcessID);
      }
    } while (Process32NextW(snapshot, &entry));
  }
  CloseHandle(snapshot);
  return output;
}

// EnumWindows 回调：向精确匹配到的进程窗口发送关闭消息。
BOOL CALLBACK closeExactProcessWindow(HWND window, LPARAM parameter) {
  const auto* processIds = reinterpret_cast<const std::vector<DWORD>*>(parameter);
  DWORD processId = 0;
  GetWindowThreadProcessId(window, &processId);
  if (std::find(processIds->begin(), processIds->end(), processId) != processIds->end()) PostMessageW(window, WM_CLOSE, 0, 0);
  return TRUE;
}

// 判断进程是否已退出；打不开句柄也视为已退出。
bool processExited(DWORD processId) {
  HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, processId);
  if (process == nullptr) return true;
  const bool exited = WaitForSingleObject(process, 0) == WAIT_OBJECT_0;
  CloseHandle(process);
  return exited;
}

// 取路径的父目录，用于在同一目录下定位 Weixin.exe。
std::wstring parentDirectory(const std::wstring& path) {
  const std::size_t separator = path.find_last_of(L"\\/");
  return separator == std::wstring::npos ? L"" : path.substr(0, separator);
}

}  // namespace

// 识别微信登录界面并按需点击登录。
// 只有明确拿到“未登录”证据时才动作：先找登录按钮点击，找不到再尝试解析登录二维码上报给后台，
// 两者都失败就只报告状态，绝不循环点击或循环重启。
WechatLoginUiResult InspectWechatLoginUi(DWORD processId, bool invokeLoginButton) {
  WechatLoginUiResult result;
  const std::vector<WindowCandidate> windows = findProcessWindows(processId);
  if (windows.empty()) {
    result.diagnostic = "WINDOW_NOT_FOUND";
    return result;
  }

  result.window = windows.front().window;
  for (const WindowCandidate& candidate : windows) {
    std::vector<std::uint8_t> pixels;
    int width = 0;
    int height = 0;
    if (captureWindowGrayscale(candidate.window, &pixels, &width, &height)) {
      result.qrPayload = decodeQrPixels(pixels.data(), width, height);
      if (!result.qrPayload.empty()) {
        result.window = candidate.window;
        result.kind = WechatLoginUiKind::QrReady;
        result.diagnostic = "QR_DECODED";
        return result;
      }
    }
  }

  for (const WindowCandidate& candidate : windows) {
    bool invoked = false;
    if (findAndInvokeLoginButton(candidate.window, invokeLoginButton, &invoked)) {
      result.window = candidate.window;
      result.kind = invoked ? WechatLoginUiKind::LoginClicked : WechatLoginUiKind::LoginRequired;
      result.diagnostic = invoked ? "LOGIN_BUTTON_INVOKED" : "LOGIN_BUTTON_FOUND";
      return result;
    }
  }
  result.diagnostic = "LOGIN_UI_NOT_RECOGNIZED";
  return result;
}

// 请求切换微信账号：遍历目标进程窗口并点击账号切换入口，成功返回 true。
bool InvokeWechatAccountSwitch(DWORD processId) {
  for (const WindowCandidate& candidate : findProcessWindows(processId)) {
    bool invoked = false;
    if (directClickAccountSwitch(candidate.window, true, &invoked) && invoked) return true;
  }
  return false;
}

// 重启指定绝对路径的 Weixin.exe：先关窗口再等退出，必要时结束进程，然后从同一路径重新启动。
// 只针对配置里那一个可执行文件，不会碰其它微信进程。
bool RestartExactWeixinExecutable(const std::wstring& executablePath, DWORD* processId, DWORD* errorCode) {
  if (processId != nullptr) *processId = 0;
  if (errorCode != nullptr) *errorCode = 0;
  const std::size_t separator = executablePath.find_last_of(L"\\/");
  const wchar_t* filename = separator == std::wstring::npos ? executablePath.c_str() : executablePath.c_str() + separator + 1;
  if (_wcsicmp(filename, L"Weixin.exe") != 0 || GetFileAttributesW(executablePath.c_str()) == INVALID_FILE_ATTRIBUTES) {
    if (errorCode != nullptr) *errorCode = ERROR_INVALID_NAME;
    return false;
  }

  const std::vector<DWORD> processes = exactProcessIds(executablePath);
  EnumWindows(closeExactProcessWindow, reinterpret_cast<LPARAM>(&processes));
  const std::uint64_t deadline = GetTickCount64() + 5'000;
  while (GetTickCount64() < deadline) {
    if (std::all_of(processes.begin(), processes.end(), processExited)) break;
    Sleep(100);
  }
  for (const DWORD existingProcessId : processes) {
    if (processExited(existingProcessId)) continue;
    std::wstring actualPath;
    if (!processPath(existingProcessId, &actualPath) || _wcsicmp(actualPath.c_str(), executablePath.c_str()) != 0) continue;
    HANDLE process = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, existingProcessId);
    if (process == nullptr || !TerminateProcess(process, 1)) {
      if (errorCode != nullptr) *errorCode = GetLastError();
      if (process != nullptr) CloseHandle(process);
      return false;
    }
    WaitForSingleObject(process, 5'000);
    CloseHandle(process);
  }

  std::wstring command = L"\"" + executablePath + L"\"";
  std::vector<wchar_t> mutableCommand(command.begin(), command.end());
  mutableCommand.push_back(L'\0');
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  if (!CreateProcessW(executablePath.c_str(), mutableCommand.data(), nullptr, nullptr, FALSE, 0, nullptr,
                      parentDirectory(executablePath).c_str(), &startup, &process)) {
    if (errorCode != nullptr) *errorCode = GetLastError();
    return false;
  }
  if (processId != nullptr) *processId = process.dwProcessId;
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  return true;
}

// 登录辅助的自检入口：按模式验证二维码识别或按钮点击链路，返回不同的非零码定位失败环节。
int RunWechatLoginAssistantSelfTest(const wchar_t* mode, DWORD processId) {
  if (mode == nullptr || processId == 0) return 20;
  if (std::wcscmp(mode, L"qr") == 0) {
    constexpr int quiet = 4;
    constexpr int scale = 8;
    constexpr int imageSize = static_cast<int>(kTestLoginQrSize + quiet * 2) * scale;
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(imageSize) * imageSize, 255);
    for (std::size_t row = 0; row < kTestLoginQrSize; row += 1) {
      for (std::size_t column = 0; column < kTestLoginQrSize; column += 1) {
        if (kTestLoginQrRows[row][column] != '1') continue;
        for (int y = 0; y < scale; y += 1) {
          for (int x = 0; x < scale; x += 1) {
            const int targetX = static_cast<int>(column + quiet) * scale + x;
            const int targetY = static_cast<int>(row + quiet) * scale + y;
            pixels[static_cast<std::size_t>(targetY) * imageSize + targetX] = 0;
          }
        }
      }
    }
    if (decodeQrPixels(pixels.data(), imageSize, imageSize) != kTestLoginQrPayload) return 24;
    const WechatLoginUiResult result = InspectWechatLoginUi(processId, false);
    return result.kind == WechatLoginUiKind::QrReady && result.qrPayload == kTestLoginQrPayload ? 0 : 21;
  }
  if (std::wcscmp(mode, L"button") == 0 || std::wcscmp(mode, L"direct") == 0) {
    const WechatLoginUiResult result = InspectWechatLoginUi(processId, true);
    if (result.kind == WechatLoginUiKind::LoginClicked) return 0;
    if (result.kind == WechatLoginUiKind::LoginRequired && result.diagnostic == "LOGIN_BUTTON_FOUND") return 22;
    if (result.kind == WechatLoginUiKind::LoginRequired && result.diagnostic == "WINDOW_NOT_FOUND") return 24;
    return 25;
  }
  if (std::wcscmp(mode, L"switch") == 0) {
    return InvokeWechatAccountSwitch(processId) ? 0 : 26;
  }
  return 23;
}
