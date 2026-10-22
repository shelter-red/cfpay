import { existsSync, readFileSync } from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import tailwindcss from "@tailwindcss/vite";
import vue from "@vitejs/plugin-vue";
import { defineConfig } from "vite";

const root = path.dirname(fileURLToPath(import.meta.url));
const packageInfo = JSON.parse(readFileSync(path.resolve(root, "../package.json"), "utf8")) as { version?: string };
const versionHeader = readFileSync(path.resolve(root, "../native/wechat-observer/wechat_monitor_version.h"), "utf8");
const monitorVersion = versionHeader.match(/kVersion\[\][^\"]*\"([^\"]+)\"/)?.[1];
const appVersion = packageInfo.version;
if (!appVersion) throw new Error("无法从 package.json 读取 CFPay 版本号");
if (monitorVersion !== appVersion) throw new Error("package.json 与 Windows 监控端版本号不一致");

/** 读取前置原生构建暂存的下载信息；生产构建禁止绕过单 EXE 构建步骤。 */
function monitorDownloadInfo(command: string) {
  const metadataPath = path.resolve(root, "public/downloads/wechat-monitor.json");
  if (existsSync(metadataPath)) return JSON.parse(readFileSync(metadataPath, "utf8"));
  if (command === "build") throw new Error("缺少 Windows 监控端暂存文件，请使用 npm run admin:build 构建");
  return { filename: "", monitorVersion, weixinVersion: "", size: 0, sha256: "" };
}

// 产物直接落进 Worker 的静态资源目录 public/admin-app，由 ASSETS 绑定按 /admin-app/* 提供；
// base 必须与该 URL 前缀一致，否则打包出的资源地址会 404。
export default defineConfig(({ command }) => ({
  define: {
    __APP_VERSION__: JSON.stringify(appVersion),
    __WECHAT_MONITOR_DOWNLOAD__: JSON.stringify(monitorDownloadInfo(command)),
  },
  plugins: [vue(), tailwindcss()],
  base: "/admin-app/",
  resolve: {
    alias: { "@": path.resolve(root, "src") },
  },
  build: {
    outDir: path.resolve(root, "../public/admin-app"),
    emptyOutDir: true,
    assetsDir: "assets",
    sourcemap: false,
  },
}));
