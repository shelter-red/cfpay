#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
build_dir=${1:-"$root/dist/wechat-hook-harness"}

cmake -S "$root/native/wechat-observer" -B "$build_dir" -G Ninja \
  -DCMAKE_SYSTEM_NAME=Windows \
  -DCMAKE_C_COMPILER=x86_64-w64-mingw32-gcc-posix \
  -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++-posix \
  -DCMAKE_BUILD_TYPE=Release
cmake --build "$build_dir"

file "$build_dir/cfpay-hook-observer.dll" "$build_dir/cfpay-hook-network-diagnostic.dll" "$build_dir/cfpay-hook-test-target.exe" "$build_dir/cfpay-wechat-host.exe" "$build_dir/cfpay-wechat-config.exe" "$build_dir/cfpay-wechat-monitor.exe"
x86_64-w64-mingw32-objdump -p "$build_dir/cfpay-hook-observer.dll" | sed -n '/DLL Name/p'
x86_64-w64-mingw32-objdump -p "$build_dir/cfpay-hook-network-diagnostic.dll" | sed -n '/DLL Name/p'
x86_64-w64-mingw32-objdump -p "$build_dir/cfpay-wechat-host.exe" | sed -n '/DLL Name/p'
x86_64-w64-mingw32-objdump -p "$build_dir/cfpay-wechat-config.exe" | sed -n '/DLL Name/p'
x86_64-w64-mingw32-objdump -p "$build_dir/cfpay-wechat-monitor.exe" | sed -n '/DLL Name/p'
