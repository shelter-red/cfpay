#!/usr/bin/env bash
# 一键把微信 Hook 监控端编译成单个 Windows EXE。
# 观察 DLL 通过 .incbin 内嵌进 EXE 的只读数据段；quirc、nlohmann/json、宿主与配置界面
# 都静态编译，运行库使用 -static -static-libgcc -static-libstdc++，产物不依赖任何外部 DLL。
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
build_dir="$root/dist/single-exe-build"
output_dir="$root/dist/single-exe"
run_self_test=0
clean_first=0
jobs=""
monitor_version=""
weixin_version=""

usage() {
  cat <<USAGE
用法: tools/build-single-exe.sh [选项]

选项:
  --build-dir <路径>   CMake 构建目录（默认 dist/single-exe-build）
  --output <路径>      单 EXE 输出目录（默认 dist/single-exe）
  --version <版本号>   覆盖监控端版本号（默认从 wechat_monitor_version.h 的 kVersion 读取）
  --weixin-version <版本号> 覆盖支持的微信版本号（默认从 wechat_host.cpp 的 kExpectedVersion 读取）
  --self-test          构建后在 Windows 上运行 EXE 自检（需要 WSL 互操作）
  --clean              构建前删除构建目录，做一次全新编译
  --jobs <N>           并行编译任务数
  -h, --help           显示本帮助

产物:
  <output>/cfpay-wechat-monitor.exe                                固定名副本，供本机部署与脚本使用
  <output>/cfpay-wechat-monitor-<版本>-weixin-<微信版本>-win64.exe   发布工件（上传到 GitHub Release）
  <output>/BUILD_INFO                                              版本、哈希、大小与构建环境信息
  <output>/SHA256SUMS                                              上述两个 EXE 的校验和清单
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --build-dir) build_dir=$2; shift 2 ;;
    --output) output_dir=$2; shift 2 ;;
    --version) monitor_version=$2; shift 2 ;;
    --weixin-version) weixin_version=$2; shift 2 ;;
    --self-test) run_self_test=1; shift ;;
    --clean) clean_first=1; shift ;;
    --jobs) jobs=$2; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "未知参数: $1" >&2; usage; exit 2 ;;
  esac
done

require() {
  command -v "$1" >/dev/null 2>&1 || { echo "缺少依赖: $1" >&2; exit 3; }
}

require cmake
require sha256sum

# 版本号以 C++ 头文件为唯一来源，避免发布工件名与二进制内部版本漂移：
# 监控端版本读 wechat_monitor_version.h 的 kVersion，微信版本读 wechat_target.h 的 kWeixinVersion
# （宿主校验、观察器断点、上报文本与 WinHTTP User-Agent 都从这两个头文件派生）。
version_header="$root/native/wechat-observer/wechat_monitor_version.h"
weixin_source="$root/native/wechat-observer/wechat_host.cpp"
target_header="$root/native/wechat-observer/wechat_target.h"
if [[ -z "$monitor_version" ]]; then
  monitor_version=$(sed -n 's/.*kVersion\[\][^"]*"\([^"]*\)".*/\1/p' "$version_header" | head -n1)
fi
if [[ -z "$weixin_version" ]]; then
  weixin_version=$(sed -n 's/.*kWeixinVersion\[\][^"]*"\([^"]*\)".*/\1/p' "$target_header" | head -n1)
fi
[[ -n "$monitor_version" ]] || { echo "无法从 $version_header 解析监控端版本号（kVersion）" >&2; exit 3; }
[[ -n "$weixin_version" ]] || { echo "无法从 $target_header 解析微信版本号（kWeixinVersion）" >&2; exit 3; }
if [[ ! "$monitor_version" =~ ^[0-9]+\.[0-9]+\.[0-9]+([-.+][0-9A-Za-z.-]+)?$ ]]; then
  echo "监控端版本号格式不合法: $monitor_version" >&2
  exit 3
fi
if [[ ! "$weixin_version" =~ ^[0-9]+(\.[0-9]+)*$ ]]; then
  echo "微信版本号格式不合法: $weixin_version" >&2
  exit 3
fi
release_name="cfpay-wechat-monitor-${monitor_version}-weixin-${weixin_version}-win64"
release_exe_name="${release_name}.exe"

cxx=""
for candidate in x86_64-w64-mingw32-g++-posix x86_64-w64-mingw32-g++; do
  if command -v "$candidate" >/dev/null 2>&1; then cxx=$candidate; break; fi
done
[[ -n "$cxx" ]] || { echo "缺少 MinGW-w64 C++ 交叉编译器（x86_64-w64-mingw32-g++）" >&2; exit 3; }
cc="${cxx//g++/gcc}"
[[ -x "$(command -v "$cc")" ]] || cc="${cc%%-posix}-posix"

generator="Unix Makefiles"
if command -v ninja >/dev/null 2>&1; then generator="Ninja"; fi

if [[ "$clean_first" == "1" && -d "$build_dir" ]]; then
  echo "==> 清理构建目录 $build_dir"
  find "$build_dir" -mindepth 1 -delete
fi

echo "==> 编译器: $cxx"
echo "==> 生成器: $generator"
echo "==> 构建目录: $build_dir"
echo "==> 监控端版本: $monitor_version"
echo "==> 支持微信版本: $weixin_version"

cmake -S "$root/native/wechat-observer" -B "$build_dir" -G "$generator" \
  -DCMAKE_SYSTEM_NAME=Windows \
  -DCMAKE_C_COMPILER="$cc" \
  -DCMAKE_CXX_COMPILER="$cxx" \
  -DCMAKE_BUILD_TYPE=Release
if [[ -n "$jobs" ]]; then
  cmake --build "$build_dir" --target cfpay-wechat-monitor --parallel "$jobs"
else
  cmake --build "$build_dir" --target cfpay-wechat-monitor
fi

dll="$build_dir/cfpay-hook-observer.dll"
exe="$build_dir/cfpay-wechat-monitor.exe"
[[ -f "$dll" ]] || { echo "观察 DLL 未生成: $dll" >&2; exit 4; }
[[ -f "$exe" ]] || { echo "监控端 EXE 未生成: $exe" >&2; exit 4; }

mkdir -p "$output_dir"
cp -f "$exe" "$output_dir/cfpay-wechat-monitor.exe"
final="$output_dir/cfpay-wechat-monitor.exe"
cp -f "$final" "$output_dir/$release_exe_name"
release_final="$output_dir/$release_exe_name"

# 清掉历史版本化产物，避免每升一次版本就在输出目录里多留一个 5 MB 的旧 EXE。
stale_count=0
while IFS= read -r -d '' stale; do
  [[ "$(basename "$stale")" == "$release_exe_name" ]] && continue
  rm -f "$stale" && stale_count=$((stale_count + 1))
done < <(find "$output_dir" -maxdepth 1 -type f -name 'cfpay-wechat-monitor-*-weixin-*.exe' -print0)
[[ "$stale_count" -gt 0 ]] && echo "==> 已清理 $stale_count 个历史版本化工件"

dll_size=$(stat -c%s "$dll")
exe_size=$(stat -c%s "$final")
dll_sha=$(sha256sum "$dll" | awk '{print $1}')
exe_sha=$(sha256sum "$final" | awk '{print $1}')
release_size=$(stat -c%s "$release_final")
release_sha=$(sha256sum "$release_final" | awk '{print $1}')

# 内嵌校验：在 EXE 中查找 DLL 中段的一段唯一字节，确认映像确实被嵌入。
probe_offset=$(( dll_size / 2 ))
if python3 - "$dll" "$final" "$probe_offset" <<'PY'
import sys
dll_path, exe_path, offset = sys.argv[1], sys.argv[2], int(sys.argv[3])
with open(dll_path, 'rb') as handle:
    handle.seek(offset)
    probe = handle.read(4096)
with open(exe_path, 'rb') as handle:
    image = handle.read()
sys.exit(0 if probe and probe in image else 1)
PY
then
  embedded="yes"
else
  embedded="NO"
fi

echo
echo "==> 单 EXE 构建完成"
echo "    路径:        $final"
echo "    发布工件:    $release_final"
echo "    EXE 大小:    $exe_size 字节"
echo "    EXE SHA-256: $exe_sha"
echo "    监控端版本:  $monitor_version"
echo "    微信版本:    $weixin_version"
echo "    内嵌 DLL:    $dll_size 字节"
echo "    DLL SHA-256: $dll_sha"
echo "    内嵌校验:    $embedded"
if command -v file >/dev/null 2>&1; then
  echo "    文件类型:    $(file -b "$final")"
fi
if command -v x86_64-w64-mingw32-objdump >/dev/null 2>&1; then
  echo "    导入 DLL:"
  x86_64-w64-mingw32-objdump -p "$final" | sed -n '/DLL Name/p' | sed 's/^/      /'
fi
[[ "$embedded" == "yes" ]] || { echo "内嵌校验失败：EXE 中未找到观察 DLL 映像" >&2; exit 5; }

build_time=$(date -u +"%Y-%m-%dT%H:%M:%SZ")
git_commit=$(git -C "$root" rev-parse HEAD 2>/dev/null || echo "unknown")
git_dirty="no"
if git -C "$root" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
  [[ -z "$(git -C "$root" status --porcelain 2>/dev/null)" ]] || git_dirty="yes"
fi
observer_api_version=$(sed -n 's/.*kObserverApiVersion[^0-9a-fx]*0x\([0-9a-fA-F]*\).*/0x\1/p' "$weixin_source" | head -n1)

build_info="$output_dir/BUILD_INFO"
cat >"$build_info" <<INFO
MONITOR_VERSION=$monitor_version
WEIXIN_VERSION=$weixin_version
OBSERVER_API_VERSION=$observer_api_version
RELEASE_NAME=$release_name
RELEASE_ARTIFACT=$release_exe_name
EXE_SIZE=$exe_size
EXE_SHA256=$exe_sha
RELEASE_ARTIFACT_SIZE=$release_size
RELEASE_ARTIFACT_SHA256=$release_sha
EMBEDDED_DLL_SIZE=$dll_size
EMBEDDED_DLL_SHA256=$dll_sha
COMPILER=$cxx
BUILD_TIME=$build_time
GIT_COMMIT=$git_commit
GIT_DIRTY=$git_dirty
GITHUB_RUN_ID=${GITHUB_RUN_ID:-local}
INFO

sha_file="$output_dir/SHA256SUMS"
( cd "$output_dir" && sha256sum "cfpay-wechat-monitor.exe" "$release_exe_name" >SHA256SUMS )

if [[ -n "${GITHUB_OUTPUT:-}" ]]; then
  {
    echo "monitor_version=$monitor_version"
    echo "weixin_version=$weixin_version"
    echo "release_name=$release_name"
    echo "artifact_path=$release_final"
    echo "artifact_sha256=$release_sha"
    echo "artifact_size=$release_size"
  } >>"$GITHUB_OUTPUT"
fi
if [[ -n "${GITHUB_STEP_SUMMARY:-}" ]]; then
  {
    echo "### 微信监控端单 EXE 构建结果"
    echo
    echo "- 监控端版本：\`$monitor_version\`"
    echo "- 支持微信版本：\`$weixin_version\`"
    echo "- 发布工件：\`$release_exe_name\`（$release_size 字节）"
    echo "- SHA-256：\`$release_sha\`"
    echo "- 内嵌观察 DLL：$dll_size 字节，校验结果 $embedded"
  } >>"$GITHUB_STEP_SUMMARY"
fi

if [[ "$run_self_test" == "1" ]]; then
  echo
  echo "==> 运行 Windows 自检"
  # WSL 互操作可以直接执行 /mnt 形式的 Windows PE 路径；wslpath -w 给出的反斜杠路径
  # bash 无法执行，会让自检恒定报失败。优先用 WSL 路径，失败再回退 Windows 形式。
  windows_path=$(command -v wslpath >/dev/null 2>&1 && wslpath -w "$final" 2>/dev/null || echo "$final")
  if "$final" --self-test; then
    echo "    自检通过"
  elif [[ "$windows_path" != "$final" ]] && "$windows_path" --self-test; then
    echo "    自检通过"
  else
    echo "    自检失败" >&2
    exit 6
  fi
fi

cat <<NEXT

分发与部署:
  1. 复制 $final（或发布工件 $release_exe_name）到 Windows 目录（例如 %LOCALAPPDATA%\\CFPay\\WeChatMonitor）。
  2. 首次运行会自动在 %LOCALAPPDATA%\CFPay\WeChatMonitor 创建 config.json（默认值写在代码里，
     仓库不再维护 example JSON）；随后在窗口里填网关地址与监控端密钥即可。
  3. 启动后 EXE 会把内嵌观察 DLL 释放到同目录 cfpay-hook-observer.dll 再注入微信；
     内容一致时不重写，被运行中的微信占用且内容不同时会要求先完整退出微信。
NEXT
