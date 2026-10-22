#pragma once

#include <windows.h>

#include <string>

int RunWechatHostCommand(int argc, wchar_t** argv);

// 程序创建的所有文件（配置、释放出的观察 DLL、收款队列状态、日志）统一存放的目录，
// 即 %LOCALAPPDATA%\CFPay\WeChatMonitor；取不到 LOCALAPPDATA 时退回 EXE 同目录。
std::wstring CfpayDataDirectory();

// 首次运行时按代码里的默认值创建 config.json；已存在时只补缺失字段，不覆盖用户填过的值。
// detail 返回机器可读的结果码（CONFIG_CREATED / CONFIG_UPDATED / CONFIG_UNCHANGED 等）。
bool EnsureWechatConfigFile(const std::wstring& configPath, std::string* detail);
int RunWechatConfigWindow(HINSTANCE instance, const std::wstring& configPath, int showCommand);
bool InitializeWechatConfigPanel(HWND parent, const std::wstring& configPath, int verticalOffset);
bool HandleWechatConfigPanelCommand(WPARAM wParam, LPARAM lParam);
void RunWechatConfigPanelAutoValidate();
void DestroyWechatConfigPanel();
