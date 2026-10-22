#include <windows.h>
#include <windowsx.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <ctime>

#include "test_login_qr.h"

namespace {

constexpr wchar_t kLoginWindowClass[] = L"CfpayWechatLoginTestWindow";
constexpr wchar_t kDirectLoginWindowClass[] = L"Qt51514QWindowIcon";
constexpr int kLoginButtonId = 1001;
bool g_loginQrMode = false;
bool g_directLoginMode = false;
HANDLE g_loginButtonEvent = nullptr;
HANDLE g_proxyButtonEvent = nullptr;
HANDLE g_accountSwitchEvent = nullptr;

// 在自有测试窗口里画一个假登录二维码，用来验证登录辅助的截屏+解码链路，全程不接触真实微信。
void paintLoginQr(HWND window, HDC device) {
  RECT client{};
  GetClientRect(window, &client);
  FillRect(device, &client, reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
  if (!g_loginQrMode) return;
  constexpr int quiet = 4;
  const int totalModules = static_cast<int>(kTestLoginQrSize) + quiet * 2;
  const int availableWidth = static_cast<int>(client.right - client.left - 24);
  const int availableHeight = static_cast<int>(client.bottom - client.top - 24);
  const int scale = std::max(4, std::min(availableWidth / totalModules, availableHeight / totalModules));
  const int qrSize = totalModules * scale;
  const int left = (client.right - qrSize) / 2 + quiet * scale;
  const int top = (client.bottom - qrSize) / 2 + quiet * scale;
  HBRUSH black = reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
  for (std::size_t row = 0; row < kTestLoginQrSize; row += 1) {
    for (std::size_t column = 0; column < kTestLoginQrSize; column += 1) {
      if (kTestLoginQrRows[row][column] != '1') continue;
      RECT module = {left + static_cast<int>(column) * scale,
                     top + static_cast<int>(row) * scale,
                     left + static_cast<int>(column + 1) * scale,
                     top + static_cast<int>(row + 1) * scale};
      FillRect(device, &module, black);
    }
  }
}

// 画一个只有“进入微信”按钮的假登录窗口，用来验证原生按钮点击链路。
void paintDirectLogin(HWND window, HDC device) {
  RECT client{};
  GetClientRect(window, &client);
  FillRect(device, &client, reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
  RECT proxy = {192, 8, 260, 48};
  RECT login = {56, 277, 224, 313};
  FrameRect(device, &proxy, reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
  FrameRect(device, &login, reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
}

// 测试登录窗口的消息循环：按模式绘制二维码或按钮，并处理关闭。
LRESULT CALLBACK loginWindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
  if (message == WM_CREATE && !g_loginQrMode && !g_directLoginMode) {
    CreateWindowExW(0, L"BUTTON", L"\u767b\u5f55", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                    125, 170, 150, 48, window, reinterpret_cast<HMENU>(kLoginButtonId), GetModuleHandleW(nullptr), nullptr);
    return 0;
  }
  if (message == WM_COMMAND && LOWORD(wParam) == kLoginButtonId) {
    if (g_loginButtonEvent != nullptr) SetEvent(g_loginButtonEvent);
    SetWindowTextW(window, L"CFPay Login Button Clicked");
    return 0;
  }
  if (message == WM_PAINT) {
    PAINTSTRUCT paint{};
    HDC device = BeginPaint(window, &paint);
    if (g_directLoginMode) paintDirectLogin(window, device);
    else paintLoginQr(window, device);
    EndPaint(window, &paint);
    return 0;
  }
  if (message == WM_PRINTCLIENT) {
    if (g_directLoginMode) paintDirectLogin(window, reinterpret_cast<HDC>(wParam));
    else paintLoginQr(window, reinterpret_cast<HDC>(wParam));
    return 0;
  }
  if (message == WM_LBUTTONUP && g_directLoginMode) {
    const int x = GET_X_LPARAM(lParam);
    const int y = GET_Y_LPARAM(lParam);
    if (x >= 56 && x < 224 && y >= 277 && y < 313) {
      if (g_loginButtonEvent != nullptr) SetEvent(g_loginButtonEvent);
    } else if (x >= 56 && x < 120 && y >= 334 && y < 358) {
      if (g_accountSwitchEvent != nullptr) SetEvent(g_accountSwitchEvent);
    } else if (x >= 192 && x < 260 && y >= 8 && y < 48) {
      if (g_proxyButtonEvent != nullptr) SetEvent(g_proxyButtonEvent);
    }
    return 0;
  }
  if (message == WM_DESTROY) {
    PostQuitMessage(0);
    return 0;
  }
  return DefWindowProcW(window, message, wParam, lParam);
}

// 启动测试登录窗口并进入消息循环；qrMode 画二维码，directMode 画登录按钮。
int runLoginWindow(bool qrMode, bool directMode = false) {
  g_loginQrMode = qrMode;
  g_directLoginMode = directMode;
  wchar_t eventName[96]{};
  ::swprintf(eventName, L"Local\\CfpayLoginButtonClicked-%lu", GetCurrentProcessId());
  g_loginButtonEvent = CreateEventW(nullptr, TRUE, FALSE, eventName);
  ::swprintf(eventName, L"Local\\CfpayProxyButtonClicked-%lu", GetCurrentProcessId());
  g_proxyButtonEvent = CreateEventW(nullptr, TRUE, FALSE, eventName);
  ::swprintf(eventName, L"Local\\CfpayAccountSwitchClicked-%lu", GetCurrentProcessId());
  g_accountSwitchEvent = CreateEventW(nullptr, TRUE, FALSE, eventName);

  WNDCLASSEXW windowClass{};
  windowClass.cbSize = sizeof(windowClass);
  windowClass.lpfnWndProc = loginWindowProcedure;
  windowClass.hInstance = GetModuleHandleW(nullptr);
  windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  windowClass.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
  windowClass.lpszClassName = directMode ? kDirectLoginWindowClass : kLoginWindowClass;
  if (!RegisterClassExW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return 30;
  RECT outer = {0, 0, directMode ? 280 : 400, directMode ? 380 : 420};
  AdjustWindowRectEx(&outer, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, FALSE, 0);
  HWND window = CreateWindowExW(0, windowClass.lpszClassName,
                                qrMode ? L"CFPay Login QR" : directMode ? L"CFPay Direct Login" : L"CFPay Login Button",
                                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT,
                                outer.right - outer.left, outer.bottom - outer.top, nullptr, nullptr,
                                windowClass.hInstance, nullptr);
  if (window == nullptr) return 31;
  ShowWindow(window, SW_SHOW);
  UpdateWindow(window);
  MSG message{};
  while (GetMessageW(&message, nullptr, 0, 0) > 0) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
  if (g_loginButtonEvent != nullptr) CloseHandle(g_loginButtonEvent);
  if (g_proxyButtonEvent != nullptr) CloseHandle(g_proxyButtonEvent);
  if (g_accountSwitchEvent != nullptr) CloseHandle(g_accountSwitchEvent);
  return static_cast<int>(message.wParam);
}

}  // namespace

struct MsvcString {
  union {
    char inlineData[16];
    const char* heapData;
  } storage;
  std::uint64_t length;
  std::uint64_t capacity;
};

struct ReceiptOutput {
  std::uint8_t reserved0[0x1c8];
  std::int32_t paySubtype;
  std::int32_t reserved1;
  MsvcString feeDescription;
  MsvcString transactionId;
  MsvcString reserved2;
  std::int32_t beginTransferTime;
  std::int32_t effectiveDate;
  MsvcString memo;
  MsvcString receiverUsername;
};

struct AccountOutput {
  std::uint64_t uin;
  MsvcString username;
  bool loggedIn;
};

struct NicknameProfile {
  std::uint8_t reserved[0x28];
  MsvcString nickname;
};

struct NetworkServiceSession {
  std::uint8_t reserved[0x140];
  std::uintptr_t vectorEnd;
};

struct TestNewSyncCommand {
  std::uintptr_t vtable;
  std::uintptr_t payload;
  std::int32_t commandId;
  std::uint32_t cachedSize;
  std::uint8_t presentMask;
  std::uint8_t tail[7];
};

struct TestNewSyncCommandList {
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

struct TestNewSyncResponse {
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
  std::uint8_t tail[7];
};

struct TestAddMsg {
  std::uintptr_t vtable;
  std::uintptr_t field1;
  std::uintptr_t field3;
  std::int32_t messageType;
  std::uint32_t reserved;
  std::uint8_t presentMask;
  std::uint8_t tail[3];
};

volatile std::uint64_t g_testSink = 0;
TestNewSyncCommand g_testCommands[2]{};
std::uintptr_t g_testCommandPointers[2]{};
TestNewSyncCommandList g_testCommandList{};

static_assert(sizeof(MsvcString) == 0x20);
static_assert(offsetof(ReceiptOutput, paySubtype) == 0x1c8);
static_assert(offsetof(ReceiptOutput, feeDescription) == 0x1d0);
static_assert(offsetof(ReceiptOutput, transactionId) == 0x1f0);
static_assert(offsetof(ReceiptOutput, beginTransferTime) == 0x230);
static_assert(offsetof(ReceiptOutput, memo) == 0x238);
static_assert(offsetof(ReceiptOutput, receiverUsername) == 0x258);
static_assert(offsetof(AccountOutput, username) == 0x08);
static_assert(offsetof(AccountOutput, loggedIn) == 0x28);
static_assert(offsetof(NicknameProfile, nickname) == 0x28);
static_assert(offsetof(NetworkServiceSession, vectorEnd) == 0x140);
static_assert(sizeof(TestNewSyncCommand) == 0x20);
static_assert(sizeof(TestNewSyncCommandList) == 0x30);
static_assert(sizeof(TestNewSyncResponse) == 0x38);
static_assert(sizeof(TestAddMsg) == 0x28);

// 按 MSVC std::string 的内存布局构造一个假字符串对象，供测试线程“解析”出结构化字段。
void setString(MsvcString* output, const char* value) {
  const std::size_t length = std::strlen(value);
  std::memset(output, 0, sizeof(*output));
  output->length = length;
  if (length < sizeof(output->storage.inlineData)) {
    std::memcpy(output->storage.inlineData, value, length);
    output->capacity = sizeof(output->storage.inlineData) - 1;
  } else {
    output->storage.heapData = value;
    output->capacity = length;
  }
}

extern "C" __declspec(dllexport) __attribute__((noinline, noclone)) ReceiptOutput* CfpayTestParser(
    ReceiptOutput* output,
    const char* amount,
    const char* transactionId,
    const char* memo) {
  std::memset(output, 0, sizeof(*output));
  output->paySubtype = 1;
  setString(&output->feeDescription, amount);
  setString(&output->transactionId, transactionId);
  output->beginTransferTime = static_cast<std::int32_t>(std::time(nullptr));
  setString(&output->memo, memo);
  setString(&output->receiverUsername, "wxid_receiver_long_name");
  return output;
}

extern "C" __declspec(dllexport) __attribute__((noinline, noclone)) AccountOutput* CfpayTestAccount(
    void*,
    AccountOutput* output,
    const char* username) {
  std::memset(output, 0, sizeof(*output));
  output->loggedIn = username != nullptr && username[0] != '\0';
  if (output->loggedIn) {
    output->uin = 10001;
    setString(&output->username, username);
  }
  return output;
}

extern "C" __declspec(dllexport) __attribute__((noinline, noclone)) NicknameProfile* CfpayTestNickname(
    void*, NicknameProfile* profile, const char* nickname) {
  std::memset(profile, 0, sizeof(*profile));
  setString(&profile->nickname, nickname);
  return profile;
}

extern "C" __declspec(dllexport) __attribute__((noinline, noclone)) void CfpayTestNetworkRecordAppend(
    NetworkServiceSession* session,
    std::uintptr_t value) {
  session->vectorEnd = value;
  g_testSink ^= value;
}

extern "C" __declspec(dllexport) __attribute__((noinline, noclone)) void CfpayTestNewSyncResponseParse(
    NetworkServiceSession* response,
    std::uintptr_t value) {
  g_testCommands[0].commandId = 5;
  g_testCommands[1].commandId = 17;
  g_testCommandPointers[0] = reinterpret_cast<std::uintptr_t>(&g_testCommands[0]);
  g_testCommandPointers[1] = reinterpret_cast<std::uintptr_t>(&g_testCommands[1]);
  g_testCommandList.vtable = 1;
  g_testCommandList.items = reinterpret_cast<std::uintptr_t>(g_testCommandPointers);
  g_testCommandList.itemCount = 2;
  g_testCommandList.allocatedItems = 2;
  g_testCommandList.itemCapacity = 2;
  g_testCommandList.declaredCount = 2;
  g_testCommandList.presentMask = 1;
  auto* parsed = reinterpret_cast<TestNewSyncResponse*>(response);
  std::memset(parsed, 0, sizeof(*parsed));
  parsed->vtable = 1;
  parsed->commandList = reinterpret_cast<std::uintptr_t>(&g_testCommandList);
  parsed->continueFlag = 1;
  parsed->status = 2;
  parsed->onlineVersion = 3;
  parsed->serverTime = static_cast<std::uint32_t>(value);
  parsed->presentMask = 0x7f;
  g_testSink ^= value;
}

extern "C" __declspec(dllexport) __attribute__((noinline, noclone)) void CfpayTestNewSyncResponseParseReturn(
    NetworkServiceSession* response) {
  g_testSink ^= reinterpret_cast<std::uintptr_t>(response);
}

extern "C" __declspec(dllexport) __attribute__((noinline, noclone)) void CfpayTestAddMsgParse(
    TestAddMsg* message,
    void*) {
  std::memset(message, 0, sizeof(*message));
  message->vtable = 1;
  message->messageType = 49;
  message->presentMask = 5;
}

__attribute__((noinline, noclone)) void testNetworkDecodedDelivery(NetworkServiceSession* session, std::uintptr_t value) {
  CfpayTestNewSyncResponseParse(session, value);
  CfpayTestNewSyncResponseParseReturn(session);
  TestAddMsg message{};
  CfpayTestAddMsgParse(&message, nullptr);
  g_testSink ^= value + 1;
}

__attribute__((noinline, noclone)) void testNetworkResponseDispatch(NetworkServiceSession* session, std::uintptr_t value) {
  testNetworkDecodedDelivery(session, value);
  g_testSink ^= value + 2;
}

using StopObserver = BOOL(WINAPI*)();
using StartObserver = BOOL(WINAPI*)();

// 模拟微信的解析线程：周期性调用假解析函数，让宿主的断点有东西可命中。
DWORD WINAPI parserThread(void*) {
  Sleep(500);
  ReceiptOutput output{};
  CfpayTestParser(&output, "CNY 0.03", "transaction-thread-000003", "thread");
  AccountOutput account{};
  CfpayTestAccount(nullptr, &account, "wxid_thread_account");
  NicknameProfile nickname{};
  CfpayTestNickname(nullptr, &nickname, "Thread Nickname");
  g_testSink ^= static_cast<std::uint64_t>(output.paySubtype) ^ account.uin;
  return 0;
}

// 自有测试目标入口：按参数决定跑登录窗口还是解析线程，用于在不接触微信的前提下验证 Hook 与管道协议。
int wmain(int argc, wchar_t** argv) {
  wchar_t executablePath[32768]{};
  const DWORD executableLength = GetModuleFileNameW(nullptr, executablePath, static_cast<DWORD>(std::size(executablePath)));
  const wchar_t* executableName = executableLength == 0 ? L"" : std::wcsrchr(executablePath, L'\\');
  executableName = executableName == nullptr ? executablePath : executableName + 1;
  if (argc == 1 && _wcsicmp(executableName, L"Weixin.exe") == 0) return runLoginWindow(false);
  if (argc == 2 && std::wcscmp(argv[1], L"--login-button") == 0) return runLoginWindow(false);
  if (argc == 2 && std::wcscmp(argv[1], L"--login-qr") == 0) return runLoginWindow(true);
  if (argc == 2 && std::wcscmp(argv[1], L"--login-direct") == 0) return runLoginWindow(false, true);
  if (argc == 2 && std::wcscmp(argv[1], L"--login-switch") == 0) return runLoginWindow(false, true);
  const bool waitForInjection = argc == 3 && std::wcscmp(argv[1], L"--wait-injection") == 0;
  const bool waitForPipeInjection = argc == 2 && std::wcscmp(argv[1], L"--wait-pipe-injection") == 0;
  const bool waitForPipeReactivation = argc == 2 && std::wcscmp(argv[1], L"--wait-pipe-reactivation") == 0;
  const bool selfLoad = argc == 3 && !waitForInjection;
  if (!selfLoad && !waitForInjection && !waitForPipeInjection && !waitForPipeReactivation) {
    ::fwprintf(
        stderr,
        L"usage: cfpay-hook-test-target.exe <observer.dll> <output.jsonl> | --wait-injection <output.jsonl> | --wait-pipe-injection | --wait-pipe-reactivation\n");
    return 2;
  }

  if (waitForPipeInjection || waitForPipeReactivation) {
    wchar_t pipeName[128] = {};
    ::swprintf(pipeName, L"\\\\.\\pipe\\cfpay-hook-test-%lu", GetCurrentProcessId());
    if (!SetEnvironmentVariableW(L"CFPAY_HOOK_TEST_PIPE", pipeName)) return 3;
  } else {
    if (!SetEnvironmentVariableW(L"CFPAY_HOOK_TEST_OUTPUT", argv[2])) return 3;
  }

  wchar_t readyName[96] = {};
  ::swprintf(readyName, L"Local\\CfpayHookTestReady-%lu", GetCurrentProcessId());
  HANDLE readyEvent = CreateEventW(nullptr, TRUE, FALSE, readyName);
  if (!readyEvent) return 4;

  HMODULE observer = nullptr;
  if (selfLoad) {
    observer = LoadLibraryW(argv[1]);
    if (!observer) {
      CloseHandle(readyEvent);
      return 5;
    }
  }

  if (WaitForSingleObject(readyEvent, 10'000) != WAIT_OBJECT_0) {
    CloseHandle(readyEvent);
    return 6;
  }
  if (waitForInjection || waitForPipeInjection || waitForPipeReactivation) {
    observer = GetModuleHandleW(L"cfpay-hook-observer.dll");
    if (!observer) return 9;
  }

  FARPROC stopProcedure = GetProcAddress(observer, "CfpayObserverStop");
  StopObserver stopObserver = nullptr;
  static_assert(sizeof(stopObserver) == sizeof(stopProcedure));
  std::memcpy(&stopObserver, &stopProcedure, sizeof(stopObserver));
  FARPROC startProcedure = GetProcAddress(observer, "CfpayObserverStart");
  StartObserver startObserver = nullptr;
  static_assert(sizeof(startObserver) == sizeof(startProcedure));
  std::memcpy(&startObserver, &startProcedure, sizeof(startObserver));
  if (!stopObserver || !startObserver) return 8;

  const int cycles = waitForPipeReactivation ? 2 : 1;
  for (int cycle = 0; cycle < cycles; cycle += 1) {
    NetworkServiceSession networkSession{};
    testNetworkResponseDispatch(&networkSession, 0x300);
    testNetworkResponseDispatch(&networkSession, 0x600);
    ReceiptOutput first{};
    ReceiptOutput second{};
    ReceiptOutput afterStop{};
    AccountOutput firstAccount{};
    AccountOutput duplicateAccount{};
    AccountOutput loggedOutAccount{};
    AccountOutput afterStopAccount{};
    NicknameProfile firstNickname{};
    NicknameProfile secondNickname{};
    CfpayTestParser(&first, "CNY 0.01", "transaction-test-000001", "first");
    CfpayTestParser(&second, "CNY 0.02", "transaction-test-000002", "second");
    CfpayTestAccount(nullptr, &loggedOutAccount, "");
    CfpayTestNickname(nullptr, &firstNickname, "Receiver Nickname");
    Sleep(500);
    CfpayTestAccount(nullptr, &firstAccount, "wxid_receiver_long_name");
    CfpayTestNickname(nullptr, &secondNickname, "Receiver Nickname");
    CfpayTestAccount(nullptr, &duplicateAccount, "wxid_receiver_long_name");
    g_testSink ^= static_cast<std::uint64_t>(first.paySubtype) ^ static_cast<std::uint64_t>(second.paySubtype) ^
                  firstAccount.uin ^ duplicateAccount.uin;
    HANDLE thread = CreateThread(nullptr, 0, parserThread, nullptr, 0, nullptr);
    if (!thread || WaitForSingleObject(thread, 5000) != WAIT_OBJECT_0) return 7;
    CloseHandle(thread);
    AccountOutput firstLoggedOut{};
    AccountOutput secondLoggedOut{};
    AccountOutput thirdLoggedOut{};
    CfpayTestAccount(nullptr, &firstLoggedOut, "");
    CfpayTestAccount(nullptr, &secondLoggedOut, "");
    CfpayTestAccount(nullptr, &thirdLoggedOut, "");
    Sleep(300);

    if (cycle + 1 < cycles) ResetEvent(readyEvent);
    if (!stopObserver()) return 8;
    CfpayTestParser(&afterStop, "CNY 9.99", "transaction-after-stop", "after-stop");
    CfpayTestAccount(nullptr, &afterStopAccount, "wxid_after_stop");
    NicknameProfile afterStopNickname{};
    CfpayTestNickname(nullptr, &afterStopNickname, "After Stop");
    g_testSink ^= static_cast<std::uint64_t>(afterStop.paySubtype) ^ afterStopAccount.uin;
    Sleep(200);
    if (cycle + 1 < cycles && WaitForSingleObject(readyEvent, 15'000) != WAIT_OBJECT_0) return 10;
  }
  CloseHandle(readyEvent);
  return 0;
}
