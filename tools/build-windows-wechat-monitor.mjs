import { access, copyFile, mkdir, rm } from "node:fs/promises";
import path from "node:path";

const root = path.resolve(import.meta.dirname, "..");
const outputDirectory = path.join(root, "dist/windows-wechat-monitor");
const nativeDirectory = path.join(root, "dist/wechat-hook-harness");

// 本脚本只做打包，不负责编译：原生产物由 tools/build-wechat-hook-harness.sh 生成。
// dist/ 是可清理的构建输出目录，缺失时给出明确指引，而不是让 copyFile 抛一个看不懂的 ENOENT。
const sourceExecutable = path.join(nativeDirectory, "cfpay-wechat-monitor.exe");
try {
  await access(sourceExecutable);
} catch {
  console.error(
    `缺少 ${path.relative(root, sourceExecutable)}。\n` +
      "请先运行 bash tools/build-wechat-hook-harness.sh，或改用推荐的一键单 EXE 构建：npm run wechat:monitor:single",
  );
  process.exit(1);
}

await rm(outputDirectory, { recursive: true, force: true });
await mkdir(outputDirectory, { recursive: true });
// 观察 DLL 已内嵌进 EXE，运行时按需释放，因此发行包只有单个可执行文件。
for (const filename of ["cfpay-wechat-monitor.exe"]) {
  await copyFile(sourceExecutable, path.join(outputDirectory, filename));
}
console.log(outputDirectory);
