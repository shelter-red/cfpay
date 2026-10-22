#!/usr/bin/env node
import { copyFileSync, existsSync, mkdirSync, readFileSync, rmSync, statSync, writeFileSync } from "node:fs";
import { createHash } from "node:crypto";
import { spawnSync } from "node:child_process";
import path from "node:path";
import { fileURLToPath } from "node:url";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const releaseDirectory = path.join(root, "dist", "single-exe");
const stagingDirectory = path.join(root, "admin-ui", "public", "downloads");
const outputDirectory = path.join(root, "public", "admin-app", "downloads");

/** 执行构建子命令并原样转发输出，失败时立即终止整个前端构建。 */
function run(command, args) {
  const result = spawnSync(command, args, { cwd: root, stdio: "inherit" });
  if (result.error) throw result.error;
  if (result.status !== 0) process.exit(result.status ?? 1);
}

/** 解析单 EXE 构建脚本生成的 KEY=VALUE 元数据。 */
function readBuildInfo() {
  const source = readFileSync(path.join(releaseDirectory, "BUILD_INFO"), "utf8");
  return Object.fromEntries(source.split(/\r?\n/).filter(Boolean).map((line) => {
    const separator = line.indexOf("=");
    return separator < 0 ? [line, ""] : [line.slice(0, separator), line.slice(separator + 1)];
  }));
}

/** 计算构建产物 SHA-256，保证 Vite 打包进去的文件与原生构建结果一致。 */
function sha256(file) {
  return createHash("sha256").update(readFileSync(file)).digest("hex");
}

rmSync(stagingDirectory, { recursive: true, force: true });
try {
  console.log("==> 第一步：构建 Windows 微信监控端单 EXE");
  run("bash", ["tools/build-single-exe.sh"]);

  const info = readBuildInfo();
  const filename = info.RELEASE_ARTIFACT;
  if (!filename) throw new Error("BUILD_INFO 缺少 RELEASE_ARTIFACT");
  const source = path.join(releaseDirectory, filename);
  if (!existsSync(source)) throw new Error(`Windows 监控端构建产物不存在：${source}`);

  mkdirSync(stagingDirectory, { recursive: true });
  const staged = path.join(stagingDirectory, filename);
  copyFileSync(source, staged);
  const metadata = {
    filename,
    monitorVersion: info.MONITOR_VERSION,
    weixinVersion: info.WEIXIN_VERSION,
    size: statSync(staged).size,
    sha256: sha256(staged),
  };
  if (metadata.sha256 !== info.RELEASE_ARTIFACT_SHA256) throw new Error("暂存 EXE 与原生构建哈希不一致");
  writeFileSync(path.join(stagingDirectory, "wechat-monitor.json"), `${JSON.stringify(metadata, null, 2)}\n`);

  console.log("==> 第二步：将 Windows 单 EXE 打入管理端静态资源");
  run("npm", ["--prefix", "admin-ui", "run", "build:vite"]);

  const bundled = path.join(outputDirectory, filename);
  if (!existsSync(bundled) || sha256(bundled) !== metadata.sha256) throw new Error("管理端产物中的 Windows EXE 校验失败");
  console.log(`==> 管理端已内置 ${filename}（${metadata.size} 字节）`);
} finally {
  rmSync(stagingDirectory, { recursive: true, force: true });
}
