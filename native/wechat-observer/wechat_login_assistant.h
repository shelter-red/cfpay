#pragma once

#include <windows.h>

#include <cstdint>
#include <string>

enum class WechatLoginUiKind {
  None,
  LoginRequired,
  LoginClicked,
  QrReady,
};

struct WechatLoginUiResult {
  WechatLoginUiKind kind = WechatLoginUiKind::None;
  HWND window = nullptr;
  std::string qrPayload;
  std::string diagnostic;
};

WechatLoginUiResult InspectWechatLoginUi(DWORD processId, bool invokeLoginButton);
bool InvokeWechatAccountSwitch(DWORD processId);
bool RestartExactWeixinExecutable(const std::wstring& executablePath, DWORD* processId, DWORD* errorCode);
int RunWechatLoginAssistantSelfTest(const wchar_t* mode, DWORD processId);
