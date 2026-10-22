import { existsSync, readFileSync, readdirSync, statSync } from "node:fs";
import path from "node:path";
import { describe, expect, it } from "vitest";

const root = path.resolve(import.meta.dirname, "..");
const uiRoot = path.join(root, "admin-ui", "src");
const publicRoot = path.join(root, "public");

function read(relative) {
  return readFileSync(path.join(root, relative), "utf8");
}

function walk(directory) {
  const found = [];
  for (const entry of readdirSync(directory, { withFileTypes: true })) {
    const full = path.join(directory, entry.name);
    if (entry.isDirectory()) found.push(...walk(full));
    else found.push(full);
  }
  return found;
}

function sourceFiles() {
  return walk(uiRoot).filter((file) => /\.(ts|vue)$/.test(file) && !file.endsWith(".d.ts"));
}

function resolveAlias(specifier) {
  const relative = specifier.replace(/^@\//, "");
  const base = path.join(uiRoot, relative);
  const candidates = [base, `${base}.ts`, `${base}.vue`, `${base}.css`, path.join(base, "index.ts")];
  return candidates.find((candidate) => existsSync(candidate) && statSync(candidate).isFile()) ?? null;
}

function normalizeFrontendPath(value) {
  // 去掉查询串与模板插值，再与后端注册的路由模式对齐。
  return (
    value
      .split("?")[0]
      .replace(/\$\{[^}]*\}/g, "*")
      .replace(/\/+$/, "") || "/"
  );
}

function normalizeRoutePath(value) {
  return value.replace(/\/:[A-Za-z0-9_]+/g, "/*").replace(/\/+$/, "") || "/";
}

describe("admin console is a Vue SPA, not hand-written html/css/js", () => {
  it("has removed the legacy static admin files", () => {
    for (const legacy of ["public/admin.html", "public/admin.js", "public/vendor"]) {
      expect(existsSync(path.join(root, legacy)), `${legacy} 应已删除`).toBe(false);
    }
  });

  it("keeps the built bundle out of git", () => {
    expect(read(".gitignore")).toContain("public/admin-app/");
    expect(read(".gitignore")).toContain("admin-ui/node_modules/");
  });

  it("builds the worker deploy through the admin UI build", () => {
    const manifest = JSON.parse(read("package.json"));
    const adminManifest = JSON.parse(read("admin-ui/package.json"));
    const builder = read("tools/build-admin-ui.mjs");
    expect(manifest.scripts["admin:build"]).toContain("admin-ui");
    expect(manifest.scripts.deploy).toBe("npm run admin:build && node tools/deploy.mjs");
    const deployer = read("tools/deploy.mjs");
    expect(deployer).toContain('"deploy"');
    expect(deployer).toContain("CLOUDFLARE_ACCOUNT_ID");
    expect(deployer).toContain("CFPAY_D1_DATABASE_ID");
    expect(adminManifest.scripts.build).toContain("build-admin-ui.mjs");
    expect(builder.indexOf('run("bash", ["tools/build-single-exe.sh"])')).toBeLessThan(builder.indexOf('run("npm", ["--prefix", "admin-ui", "run", "build:vite"])'));
    expect(builder).toContain("管理端产物中的 Windows EXE 校验失败");
    expect(read(".gitignore")).toContain("admin-ui/public/downloads/");
  });

  it("resolves every @/ alias import inside the admin UI", () => {
    const unresolved = [];
    for (const file of sourceFiles()) {
      const source = readFileSync(file, "utf8");
      for (const match of source.matchAll(/from\s+"(@\/[^"]+)"/g)) {
        if (!resolveAlias(match[1])) unresolved.push(`${path.relative(root, file)} -> ${match[1]}`);
      }
      for (const match of source.matchAll(/import\("(@\/[^"]+)"\)/g)) {
        if (!resolveAlias(match[1])) unresolved.push(`${path.relative(root, file)} -> ${match[1]}`);
      }
    }
    expect(unresolved).toEqual([]);
  });
});

describe("admin console routing", () => {
  it("gives every router route a real page component", () => {
    const router = read("admin-ui/src/router.ts");
    const pages = [...router.matchAll(/import\("@\/(pages\/[^"]+\.vue)"\)/g)].map((match) => match[1]);

    expect(pages.length).toBeGreaterThanOrEqual(7);
    for (const page of pages) expect(existsSync(path.join(uiRoot, page)), `${page} 应存在`).toBe(true);
  });

  it("keeps the sidebar navigation in sync with the router", () => {
    const router = read("admin-ui/src/router.ts");
    const routePaths = [...router.matchAll(/path: "([a-z-]+)"/g)].map((match) => `/${match[1]}`);
    const navigation = read("admin-ui/src/lib/admin-navigation.ts");
    const navPaths = [...navigation.matchAll(/path: "(\/[a-z-]+)"/g)].map((match) => match[1]);

    expect(navPaths.length).toBeGreaterThan(0);
    expect(navPaths.filter((entry) => !routePaths.includes(entry))).toEqual([]);
  });

  it("derives the router base from the worker-injected entry path", () => {
    const router = read("admin-ui/src/router.ts");
    expect(router).toContain("__CFPAY_ADMIN_BASE__");
    expect(read("admin-ui/index.html")).toContain("__CFPAY_ADMIN_BASE__");
    expect(read("src/index.ts")).toContain("__CFPAY_ADMIN_BASE__");
  });
});

describe("admin console API surface", () => {
  it("only calls endpoints the Worker actually registers", () => {
    const worker = read("src/index.ts");
    const registered = new Set(
      [...worker.matchAll(/app\.(?:get|post|put|delete|patch|all)\(\s*"([^"]+)"/g)].map((match) =>
        normalizeRoutePath(match[1]),
      ),
    );

    const called = new Set();
    for (const file of sourceFiles()) {
      const source = readFileSync(file, "utf8");
      for (const match of source.matchAll(/\bapi[<(][^`"']*?[`"'](\/[^`"']*)[`"']/g)) {
        called.add(normalizeFrontendPath(match[1]));
      }
      for (const match of source.matchAll(/fetch\(\s*[`"'](\/[^`"']*)[`"']/g)) {
        called.add(normalizeFrontendPath(match[1]));
      }
    }

    expect(called.size).toBeGreaterThan(0);
    expect([...called].filter((endpoint) => !registered.has(endpoint))).toEqual([]);
  });

  it("has no leftover object storage surface", () => {
    const worker = read("src/index.ts");
    for (const file of [...sourceFiles(), path.join(root, "src", "index.ts")]) {
      const source = readFileSync(file, "utf8");
      expect(source, path.relative(root, file)).not.toMatch(/admin-api\/storage/);
      expect(source, path.relative(root, file)).not.toMatch(/object_storage|objectStorage|aws4fetch/i);
    }
    expect(worker).not.toContain("migrateAlipayCredentialsToObjectStorage");
  });

  it("keeps manual settlements separate from monitor raw reports", () => {
    const worker = read("src/index.ts");
    const detail = read("admin-ui/src/lib/detail.ts");
    expect(worker).not.toContain("manual:${crypto.randomUUID()}");
    expect(worker).toContain("paid_source = 'MANUAL'");
    expect(detail).toContain('title: "支付确认"');
    expect(detail).toContain('order.paid_source === "MANUAL"');
  });

  it("does not duplicate persistent success states with success toasts", () => {
    const copyButton = read("admin-ui/src/components/admin/CopyButton.vue");
    const quickPay = read("admin-ui/src/pages/QuickPayPage.vue");
    const alipay = read("admin-ui/src/pages/AlipayChannelPage.vue");
    expect(copyButton).not.toContain('toast.success("已复制到剪贴板")');
    expect(quickPay).not.toContain('toast.success("支付、收款匹配和商户回调均已完成")');
    expect(alipay).not.toContain("查询成功，共");
  });

  it("keeps transient query and refresh feedback out of persistent page content", () => {
    const alipay = read("admin-ui/src/pages/AlipayChannelPage.vue");
    const worker = read("src/index.ts");
    expect(alipay).not.toContain("queryNotice");
    expect(alipay).toContain("账务查询完成，共");
    expect(alipay).toContain("复制编号");
    expect(worker).toContain('traceSource: result.traceSource');
    for (const page of ["DashboardPage.vue", "OrdersPage.vue", "QuickPayPage.vue", "UnmatchedPage.vue", "WechatChannelPage.vue"]) {
      expect(read(`admin-ui/src/pages/${page}`), page).not.toMatch(/toast\.success\([^\n]*已刷新/);
    }
  });

  it("keeps administrator copy focused on tasks instead of implementation details", () => {
    const visibleSources = [
      "admin-ui/src/lib/admin-navigation.ts",
      ...[
        "AlipayChannelPage.vue",
        "DashboardPage.vue",
        "GatewayPage.vue",
        "QuickPayPage.vue",
        "SecurityPage.vue",
        "SiteSettingsPage.vue",
        "WechatChannelPage.vue",
      ].map((page) => `admin-ui/src/pages/${page}`),
    ].map(read).join("\n");
    for (const phrase of [
      "管理端构建前",
      "文件不写入 D1",
      "SESSION_SECRET",
      "AES-GCM",
      "HMAC 签名",
      "缓存未命中时才读取",
      "每 2 秒读取普通订单",
      "实时连接已断开，正在轮询",
      "不会单独保存到 D1",
      "内部诊断信息",
      "返回 success",
    ]) expect(visibleSources, phrase).not.toContain(phrase);
  });

  it("uses compact dashboard and conditional security guidance", () => {
    const dashboard = read("admin-ui/src/pages/DashboardPage.vue");
    const security = read("admin-ui/src/pages/SecurityPage.vue");
    const settingSection = read("admin-ui/src/components/admin/SettingSection.vue");
    expect(dashboard).toContain("grid gap-px bg-border");
    expect(dashboard).not.toContain("缓存数据量（估算）");
    expect(security).toContain("查看配置命令");
    expect(security).toContain("turnstileState === 'disabled' || turnstileState === 'incomplete'");
    expect(settingSection).toContain('Card class="gap-4 py-5"');
    expect(settingSection).toContain('class="px-5"');
  });

  it("uses the successful login response without a second session probe", () => {
    const session = read("admin-ui/src/composables/useSession.ts");
    const worker = read("src/index.ts");
    expect(worker).toContain("{ ok: true, username: adminConfig.username }");
    expect(session).toContain("applySessionState(true, data.username ?? username)");
    expect(session).not.toMatch(/export async function login[\s\S]*await refreshSession\(\)/);
    expect(session).toContain("revision === sessionRevision");
  });

  it("does not poll order lists or trigger duplicate pagination loads", () => {
    const orders = read("admin-ui/src/pages/OrdersPage.vue");
    const unmatched = read("admin-ui/src/pages/UnmatchedPage.vue");
    expect(orders).not.toContain("POLL_INTERVAL_MS");
    expect(orders).not.toContain("schedulePoll");
    expect(orders).not.toContain("watch([page, pageSize]");
    expect(unmatched).not.toContain("watch([page, pageSize]");
    expect(unmatched).not.toContain("watch(candidatePage");
    expect(orders).toContain('params.set("refresh", "1")');
    expect(unmatched).toContain('params.set("refresh", "1")');
  });

  it("pauses quick-pay diagnostics in background tabs and reads one monitor snapshot", () => {
    const quickPayPage = read("admin-ui/src/pages/QuickPayPage.vue");
    const quickPayWorker = read("src/quick-pay.ts");
    const monitorState = read("src/monitor-state.ts");
    expect(quickPayPage).toContain('document.addEventListener("visibilitychange"');
    expect(quickPayPage).toContain('document.visibilityState !== "visible"');
    expect(quickPayWorker).not.toContain("getMonitorWechatChannelStatus");
    expect(quickPayWorker).toContain("monitor.wechatChannel");
    expect(monitorState).toContain("wechatChannel: this.wechatStatus(now)");
  });

  it("ships the Windows monitor through the frontend build and uses incremental live updates", () => {
    const page = read("admin-ui/src/pages/WechatChannelPage.vue");
    const qrCard = read("admin-ui/src/components/admin/QrCodeCard.vue");
    const monitorState = read("src/monitor-state.ts");
    const monitors = read("admin-ui/src/composables/useMonitors.ts");
    const deployWorkflow = read(".github/workflows/deploy.yml");
    expect(page).toContain("Windows 监控端");
    expect(page).toContain("downloadMonitor");
    expect(page).toContain("__WECHAT_MONITOR_DOWNLOAD__");
    expect(page).not.toContain('SettingSection title="Windows 监控端"');
    expect(page).toContain('SettingSection title="监控端接入"');
    expect(page).toContain("versionDialogOpen");
    expect(qrCard).toContain("sm:grid-cols-[11rem_minmax(0,1fr)]");
    expect(qrCard).not.toContain("size-44");
    expect(monitorState).toContain('JSON.stringify({ type: "update", data: update })');
    expect(monitors).toContain('message.type === "update"');
    expect(deployWorkflow).toContain("g++-mingw-w64-x86-64");
    expect(deployWorkflow).toContain("x86_64-w64-mingw32-g++ --version");
    expect(deployWorkflow.indexOf("安装 Windows 交叉编译工具链")).toBeLessThan(deployWorkflow.indexOf("npm run admin:build"));
  });
});

describe("worker serves the SPA only through the configured entry", () => {
  it("routes the admin entry to the SPA index and blocks direct asset access", () => {
    const worker = read("src/index.ts");

    // 必须请求目录形式：资源层 html_handling 会把 /admin-app/index.html 以 307 跳到目录，
    // 直接返回会让后台入口变成跳转而不是页面。
    expect(worker).toContain('const ADMIN_SPA_ENTRY = "/admin-app/"');
    expect(worker).toContain('response.headers.get("location")');
    expect(worker).toContain("matchesAdminEntry(rawPathname, config.adminPath)");
    expect(worker).toContain("BLOCKED_ADMIN_PATHNAMES.has(pathname)");
    // 旧的静态 admin.html / admin.js 下发路径必须已经消失。
    expect(worker).not.toContain('new URL("/admin", c.req.url)');
    expect(worker).not.toContain('pathname === "/admin.js"');
  });

  it("blocks the SPA bundle entry as a reserved admin path", () => {
    const auth = read("src/auth.ts");
    expect(auth).toContain('"admin-app"');
    expect(auth).toContain('"/admin-app/index.html"');
    expect(auth).toContain("pathname.startsWith(`${entry}/`)");
  });
});
