#!/usr/bin/env bash
set -euo pipefail

compiler=${CXX:-x86_64-w64-mingw32-g++}
objdump=${OBJDUMP:-x86_64-w64-mingw32-objdump}
output=$(mktemp --suffix=.exe)
trap 'rm -f "$output"' EXIT

"$compiler" --version | sed -n '1,2p'
printf '%s\n' \
  '#include <windows.h>' \
  'int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {' \
  '  return sizeof(void*) == 8 ? 0 : 1;' \
  '}' |
  "$compiler" -x c++ -std=c++17 -O2 -s -mwindows - -o "$output"

file "$output"
"$objdump" -f "$output"
"$objdump" -p "$output" | sed -n '/Subsystem/Ip;/DLL Name/p'
