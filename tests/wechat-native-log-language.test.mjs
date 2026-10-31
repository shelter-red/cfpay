import { readFile } from "node:fs/promises";
import { describe, expect, it } from "vitest";

const monitorSourcePath = new URL("../native/wechat-observer/wechat_monitor.cpp", import.meta.url);
const observerSourcePath = new URL("../native/wechat-observer/hardware_observer.cpp", import.meta.url);
const loginAssistantSourcePath = new URL("../native/wechat-observer/wechat_login_assistant.cpp", import.meta.url);
const nativeBuildPath = new URL("../native/wechat-observer/CMakeLists.txt", import.meta.url);

async function nativeLogCalls() {
  const source = await readFile(monitorSourcePath, "utf8");
  return [...source.matchAll(/appendLog\((?!const std::string&)([\s\S]*?)\);/g)].map((match) => match[1]);
}

describe("native wechat monitor log language", () => {
  it("keeps every persisted log explanation in Chinese", async () => {
    const calls = await nativeLogCalls();

    expect(calls.length).toBeGreaterThan(0);
    for (const call of calls) expect(call).toMatch(/\\u[0-9a-fA-F]{4}/);
  });

  it("maps status and rejection codes to Chinese explanations", async () => {
    const calls = await nativeLogCalls();
    const source = await readFile(monitorSourcePath, "utf8");
    const componentCall = calls.find((call) => call.includes('u8"\\u7ec4\\u4ef6\\u72b6\\u6001\\u5df2\\u66f4\\u65b0'));
    const loginCall = calls.find((call) => call.includes("loginStatusDescription(status)"));
    const rejectionCall = calls.find((call) => call.includes("evaluation.reason"));

    // 日志与上报载荷都必须使用翻译后的中文说明，绝不拼接观察 DLL 传回的英文原文。
    expect(componentCall).toContain("translated");
    expect(componentCall).not.toMatch(/\+\s*message\s*\+/);
    // 翻译发生在写入快照之前，否则每 5 秒的定期重申会把后台说明刷回英文。
    expect(source).toContain("const std::string translated = observerMessageDescription(detailCode, message);");
    expect(source).toContain('payload["message"] = snapshot.message;');
    expect(loginCall).toContain("loginStatusDescription(status)");
    expect(rejectionCall).toContain("rejectionReasonDescription(evaluation.reason)");
  });

  it("does not expire a logged-in account while the observer remains alive", async () => {
    const source = await readFile(observerSourcePath, "utf8");

    expect(source).not.toContain("kLoginObservationTimeoutMs");
    expect(source).toContain("kLoggedOutConfirmationCount");
    expect(source).toContain("g_consecutiveLoggedOutObservations");
    expect(source).toContain("previousState == 1 && consecutive < kLoggedOutConfirmationCount");
    expect(source).toContain("loginState")
    expect(source).toContain("g_loginState");
    expect(source).toContain('writeStatus("DEGRADED", "WEIXIN_LOGIN_REQUIRED"');
  });

  it("limits automatic login clicks after a failed restart", async () => {
    const source = await readFile(new URL("../native/wechat-observer/wechat_monitor.cpp", import.meta.url), "utf8");

    expect(source).toContain("loginUi.kind == WechatLoginUiKind::LoginRequired && clickedProcessId == 0 && !restartAttempted");
    expect(source).toContain("if (!restartAttempted && clickedAt != 0 && now - clickedAt >= 20'000)");
    expect(source).toContain("restartAttempted = false;");
    expect(source).toContain("candidates = processes;");
    expect(source).not.toContain("if (!processHasModule(processId, L\"Weixin.dll\")) candidates.push_back(processId);");
    expect(source).toContain("qrObservedProcessId != loginUiProcessId");
    expect(source).toContain("qrObservedProcessId = loginUiProcessId;");
  });

  it("uses direct window messages instead of accessibility for login", async () => {
    const source = await readFile(loginAssistantSourcePath, "utf8");

    expect(source).toContain("mmui::LoginWindow");
    expect(source).toContain("WM_LBUTTONDOWN");
    expect(source).toContain("WM_LBUTTONUP");
    expect(source).toContain("SendMessageTimeoutW");
    expect(source).not.toContain("IAccessible");
    expect(source).not.toContain("accDoDefaultAction");
    expect(source).not.toContain("AccessibleObjectFromWindow");
  });

  it("deduplicates identical status reports without hiding state changes", async () => {
    const source = await readFile(monitorSourcePath, "utf8");

    expect(source).toContain("kReportIntervalMs = 5'000");
    // 去重靠内容指纹：与最近一次成功上报的指纹一致就不标记待上报，状态变化立刻置脏。
    expect(source).toContain("componentFingerprint(gReport) != gReport.postedFingerprint");
    expect(source).toContain("loginFingerprint(gReport) != gReport.postedLoginFingerprint");
    expect(source).toContain("gReportMutex");
    // 待上报标记只在上报成功后清除，失败时保留，网络线程按退避间隔重试。
    expect(source).toContain("gReport.dirty = false;");
    expect(source).toContain("gReport.dirty = true;");
  });

  it("uploads hook status and wechat login state through a single request", async () => {
    const source = await readFile(monitorSourcePath, "utf8");

    // 只允许一个上报出口、一个状态路由：Hook 状态与微信登录状态合并进同一个请求。
    expect(source).toContain('client.post(L"/api/monitor/status"');
    expect(source).not.toContain('L"/api/monitor/wechat-login"');
    expect(source).not.toContain('L"/api/monitor/heartbeat"');
    expect(source).not.toContain("reassertStatus");
    expect(source).not.toMatch(/bool reportStatus\(/);
    expect(source).not.toMatch(/bool reportWechatLoginState\(/);
    // 登录状态作为子对象上行，允许的键与它单独成一条路由时完全一致。
    expect(source).toContain('payload["login"] = std::move(login);');
    expect(source).toContain('json login{{"status", snapshot.loginStatus}, {"observedAt", now}};');
    // 二维码只在 QR_READY 时上行，否则后台会继续显示已经失效的登录码。
    expect(source).toContain('snapshot.loginStatus == "QR_READY"');
    // 改状态的线程只写快照，发请求的只有网络线程这一处。
    expect(source).toContain("bool postStatus(const MonitorApiClient& client");
    expect(source).toContain("setComponentState(");
    expect(source).toContain("setLoginState(");
  });

  it("logs the full HTTP exchange so a rejected upload can be diagnosed", async () => {
    const source = await readFile(monitorSourcePath, "utf8");

    // 响应体必须无条件读取：失败时服务端返回的 {"code":-1,"msg":"监控端签名错误"}
    // 是唯一的原因来源，只在调用方要 JSON 时才读会把 401/400 的理由整个丢掉。
    expect(source).toContain("bool statusKnown = false;");
    expect(source).toContain("if (statusKnown) {");
    expect(source).toContain("logHttpExchange(requestPath, body, success, responseCode, requestError, responseBody);");
    // 日志必须同时给出请求体、响应体、HTTP 状态码、WinHTTP 错误码与服务端业务码/中文说明。
    expect(source).toContain('u8" \\u8bf7\\u6c42\\u4f53=" ');
    expect(source).toContain('u8" \\u54cd\\u5e94\\u4f53=" ');
    expect(source).toContain('u8" httpStatus=" ');
    expect(source).toContain('u8" winError=" ');
    expect(source).toContain('u8" \\u670d\\u52a1\\u7aef\\u7801=" ');
    expect(source).toContain('u8" \\u670d\\u52a1\\u7aef\\u8bf4\\u660e=" ');
    // 调试日志开关是唯一的落盘条件：关掉之后连上报失败也不写文件，
    // 否则用户会以为设置没生效。失败原因改由界面状态行与托盘提示展示。
    expect(source).not.toContain("appendLogAlways");
    expect(source).toContain("if (!gState.config.debugLogging || gState.config.logPath.empty()) return;");
    expect(source).toContain('{"hardware breakpoint installation failed", u8"\\u786c\\u4ef6\\u65ad\\u70b9\\u5b89\\u88c5\\u5931\\u8d25"}');
    expect(source).toContain('{"observer worker thread creation failed", u8"\\u89c2\\u5bdf\\u5668\\u5de5\\u4f5c\\u7ebf\\u7a0b\\u521b\\u5efa\\u5931\\u8d25"}');
    expect(source).toContain("void setNetworkOk(bool ok, const std::wstring& detail = {})");
    expect(source).toContain("setNetworkOk(success, success ? std::wstring() : utf8ToWide(serverMessage));");
    // 报文按上限截断，避免二维码或长报文刷满日志。
    expect(source).toContain("kMaxLoggedBodyBytes");
    expect(source).toContain("kMaxResponseBytes");
    // 密钥与签名只在请求头里，日志函数的入参不允许出现任何请求头。
    const parameters = /void logHttpExchange\(([\s\S]*?)\) \{/.exec(source)[1];
    expect(parameters).not.toMatch(/header|signature|secret/i);
  });

  it("keeps the WeChat target identity in a single header", async () => {
    // 版本号、文件指纹与 RVA 只能来自 wechat_target.h：宿主用它校验磁盘文件，
    // 观察器用它下断点，监控端用它生成上报的 targetVersion 与 hookOffset。
    // 三处各写一份字面量时，升级微信版本漏改任意一处都会让校验目标与实际断点不一致。
    const target = await readFile(new URL("../native/wechat-observer/wechat_target.h", import.meta.url), "utf8");
    const host = await readFile(new URL("../native/wechat-observer/wechat_host.cpp", import.meta.url), "utf8");
    const observer = await readFile(observerSourcePath, "utf8");
    const monitor = await readFile(monitorSourcePath, "utf8");

    expect(target).toContain('kWeixinVersion[] = L"4.2.0.11"');
    expect(target).toContain("kNewSyncParseRva = 0x10b0230");
    expect(host).toContain("cfpay::target::kWeixinVersion");
    expect(host).not.toContain('L"4.2.0.11"');
    expect(observer).toContain("cfpay::target::kNewSyncParseRva");
    expect(observer).not.toContain("= 0x10b0230");
    expect(monitor).toContain('payload["targetVersion"] = weixinTargetLabel();');
    expect(monitor).toContain('payload["hookOffset"] = parserHookOffsetText();');
  });

  it("reads the 4.2 CmdItem string from the command object", async () => {
    const observer = await readFile(observerSourcePath, "utf8");

    expect(observer).toContain("kCmdItemStringObjectOffset = 0x08");
    expect(observer).toContain("kCmdItemIdOffset = 0x10");
    expect(observer).toContain("resolvePayloadString(commandItemAddress");
    expect(observer).toContain("capturePayMessage(commandAddress, capturedAt);");
    expect(observer).not.toContain("capturePayMessage(payloadObject");
  });

  it("does not reuse target-validation success text for observer lifecycle failures", async () => {
    const host = await readFile(new URL("../native/wechat-observer/wechat_host.cpp", import.meta.url), "utf8");
    const monitor = await readFile(monitorSourcePath, "utf8");

    expect(host).toContain('if (!invokeRemoteObserverExport(processId, moduleName, "CfpayObserverApiVersion", 5\'000, &version, message)) return false;');
    expect(host).toContain("message->clear();\n  const bool observerLoaded =");
    expect(monitor).toContain('{"loaded observer runtime API version does not match the current package",');
  });

  it("identifies the exact 4.2 runtime signature check that failed", async () => {
    const observer = await readFile(observerSourcePath, "utf8");

    expect(observer).toContain("kWeixinAccountCallsiteSignature = {0xe8, 0x2d, 0x23, 0x4f, 0xfe}");
    expect(observer).not.toContain("kWeixinAccountCallsiteSignature = {0xe8, 0x5c, 0xbb, 0x4a, 0xfe}");
    expect(observer).toContain('"NewSync 解析入口签名不匹配"');
    expect(observer).toContain('"微信账号函数签名不匹配"');
    expect(observer).toContain('"微信账号调用点签名不匹配"');
    expect(observer).toContain('"微信昵称函数签名不匹配"');
    expect(observer).toContain("writeStatus(\"UNSUPPORTED\", \"SIGNATURE_MISMATCH\", validationMessage.c_str());");
  });

  it("rebuilds the single EXE when the embedded observer DLL changes", async () => {
    const build = await readFile(nativeBuildPath, "utf8");

    expect(build).toContain("OBJECT_DEPENDS \"${CMAKE_CURRENT_BINARY_DIR}/cfpay-hook-observer.dll\"");
  });

  it("reports device and component state through a single 5-second cadence", async () => {
    const native = await readFile(new URL("../native/wechat-observer/wechat_monitor.cpp", import.meta.url), "utf8");
    const worker = await readFile(new URL("../src/monitor-state.ts", import.meta.url), "utf8");

    // 上报节奏写死为 5 秒，不再是可配置项：配置化会让监控端与服务端的失联判定口径悄悄错开。
    expect(native).toContain("kReportIntervalMs = 5'000");
    expect(native).toContain("nextReport = now + kReportIntervalMs;");
    expect(native).not.toContain("heartbeatIntervalMs");
    expect(native).not.toContain("heartbeatIntervalSeconds");
    // 登录状态重发与观察 DLL 的健康重发也统一到 5 秒，不允许再出现 30 秒这条独立的上报节奏。
    expect(native).toContain("nextReportAt = now + kReportIntervalMs;");
    // 30 秒只允许作为现场测试的等待上限与观察器启动宽限存在（见下面两个用例），
    // 绝不能是另一条上报节奏，否则失联判定就会出现两个互相矛盾的口径。
    expect(native).not.toMatch(/kReportIntervalMs = 30'000/);
    expect(native).not.toMatch(/nextReport\w* = now \+ 30'000/);
    expect(native).not.toMatch(/nextReportAt = now \+ 30'000/);
    expect(native).not.toContain("kStatusRepeatIntervalMs");
    const observer = await readFile(observerSourcePath, "utf8");
    expect(observer).toContain("nextHealthReport = now + 5'000;");
    expect(observer).not.toMatch(/nextHealthReport = now \+ 30'000/);
    // 设备、组件与登录状态合并成一次上报，监控端不再单独发心跳。
    expect(native).not.toContain('L"/api/monitor/heartbeat"');
    expect(worker).not.toContain("INSERT INTO monitor_device");
    expect(worker).not.toContain("INSERT INTO monitor_component_status");
  });

  it("uses a single hardcoded offline window of three missed reports", async () => {
    // 只有一个窗口：监控端 5 秒上报一次，连续 3 次没收到（15 秒）即判定失联。
    // 设备在线与组件新鲜度由同一个请求写入，不存在第二个节奏，也就不需要第二个窗口。
    const worker = await readFile(new URL("../src/monitor-state.ts", import.meta.url), "utf8");
    const index = await readFile(new URL("../src/index.ts", import.meta.url), "utf8");

    expect(worker).toContain("export const MONITOR_ONLINE_WINDOW_MS = 15_000;");
    expect(worker).not.toContain("MONITOR_COMPONENT_FRESH_WINDOW_MS");
    expect(index).not.toContain("componentWindowMs");
    // 窗口必须是上报间隔的整数倍且等于 3 次，改动任一边都要同时改另一边。
    const native = await readFile(new URL("../native/wechat-observer/wechat_monitor.cpp", import.meta.url), "utf8");
    const interval = Number(/kReportIntervalMs = (\d)'(\d{3});/.exec(native).slice(1).join(""));
    const window = Number(/MONITOR_ONLINE_WINDOW_MS = (\d[\d_]*)/.exec(worker)[1].replaceAll("_", ""));
    expect(interval).toBe(5_000);
    expect(window).toBe(interval * 3);
    expect(worker).toContain("device.last_heartbeat_at > now - MONITOR_ONLINE_WINDOW_MS");
    expect(worker).toContain("device.last_heartbeat_at <= now - MONITOR_ONLINE_WINDOW_MS");
  });

  it("creates its own config file instead of shipping an example JSON", async () => {
    const monitor = await readFile(monitorSourcePath, "utf8");
    const config = await readFile(new URL("../native/wechat-observer/wechat_config.cpp", import.meta.url), "utf8");
    const header = await readFile(new URL("../native/wechat-observer/wechat_embedded.h", import.meta.url), "utf8");

    // 默认值写在代码里，与内嵌观察 DLL 同一套思路：首次运行自动创建，仓库不维护模板文件。
    expect(header).toContain("bool EnsureWechatConfigFile(const std::wstring& configPath, std::string* detail);");
    expect(config).toContain("json defaultConfigJson()");
    expect(config).toContain("EnsureWechatConfigFile");
    expect(monitor).toContain("EnsureWechatConfigFile(gState.configPath, &configDetail)");
    // 已存在的配置只补缺失字段，绝不覆盖用户填过的值。
    expect(config).toContain("if (config.contains(it.key())) continue;");
    // 早已不再读取的键要顺手清掉，避免配置里留着一个看起来还能调的心跳间隔。
    expect(config).toContain('"heartbeatIntervalSeconds"');
    expect(config).toContain("CONFIG_UPDATED");
    // 仓库里不再有 example 配置模板。
    await expect(
      readFile(new URL("../config/wechat-monitor.example.json", import.meta.url), "utf8"),
    ).rejects.toThrow();
  });

  it("keeps every program-created file inside one data directory", async () => {
    const config = await readFile(new URL("../native/wechat-observer/wechat_config.cpp", import.meta.url), "utf8");
    const monitor = await readFile(monitorSourcePath, "utf8");

    // 配置、释放出的观察 DLL、队列状态与日志都在 %LOCALAPPDATA%\CFPay\WeChatMonitor，
    // EXE 从任意位置运行都不会把文件散落到自己旁边。
    expect(config).toContain('return joinPath(joinPath(localAppData, L"CFPay"), L"WeChatMonitor");');
    expect(config).toContain('keepOr("observerDllPath", joinPath(dataDirectory, L"cfpay-hook-observer.dll"));');
    expect(config).toContain('keepOr("statePath", joinPath(dataDirectory, L"wechat-monitor-state.json"));');
    expect(config).toContain('keepOr("logPath", joinPath(dataDirectory, L"wechat-monitor.log"));');
    // 旧版本散落在 CFPay 根目录下的状态与日志要迁移进来，且只在取值等于旧默认值时才动。
    expect(config).toContain("migrateLegacyDataPaths");
    expect(config).toContain("MoveFileExW(oldPath.c_str(), newPath.c_str(), 0)");
    // 配置文件位置也回退到数据目录，而不是 EXE 同目录。
    expect(monitor).toContain('return joinPath(CfpayDataDirectory(), L"config.json");');
    expect(monitor).not.toContain("  return portable;");
  });

  it("keeps field tests inside the native monitor process", async () => {
    const source = await readFile(monitorSourcePath, "utf8");
    const loginSource = await readFile(loginAssistantSourcePath, "utf8");

    expect(source).toContain("kTestButtonLogin");
    expect(source).toContain("kTestQrLogin");
    expect(source).toContain("kTestPayment");
    expect(source).toContain("InvokeWechatAccountSwitch(loginUiProcessId)");
    expect(source).toContain("qrObservedProcessId == 0 && now - gState.fieldTestStartedAt.load() >= 30'000");
    expect(source).toContain('client.post(L"/api/monitor/payment", payload, &statusCode, &errorCode, &response)');
    expect(source).toContain('response.value("matched", false)');
    expect(source).toContain("defaultConfigPath()");
    expect(source).toContain('packageDirectory.find(L"WeChatFieldTest-")');
    expect(source).toContain("gState.fieldTestRequest = static_cast<int>(next)");
    expect(source).toContain("gState.fieldTestRequest = static_cast<int>(FieldTestKind::ButtonLogin)");
    expect(loginSource).toContain("bool InvokeWechatAccountSwitch(DWORD processId)");
    expect(source).not.toContain("powershell.exe");
    expect(source).not.toContain("pwsh.exe");
  });
});
