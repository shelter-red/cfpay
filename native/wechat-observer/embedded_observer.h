// 把生产观察 DLL 以只读数据形式嵌入单 EXE，运行时按需释放到程序目录后注入。
// 使用 .incbin 直接引用构建产物，避免生成数 MB 的 C 数组源文件。
#pragma once

#include <cstddef>

#if defined(CFPAY_EMBED_OBSERVER_DLL)

extern "C" {
extern const unsigned char cfpay_observer_dll_start[];
extern const unsigned char cfpay_observer_dll_end[];
}

__asm__(
    ".section .rodata\n"
    ".balign 16\n"
    ".global cfpay_observer_dll_start\n"
    "cfpay_observer_dll_start:\n"
    ".incbin \"" CFPAY_OBSERVER_DLL_FILE "\"\n"
    ".global cfpay_observer_dll_end\n"
    "cfpay_observer_dll_end:\n"
    ".balign 16\n");

inline const unsigned char* embeddedObserverDllImage() { return cfpay_observer_dll_start; }

// 返回内嵌观察 DLL 映像的字节数；映像由 .incbin 直接放进只读数据段，不生成大体积 C 数组。
inline std::size_t embeddedObserverDllSize() {
  return static_cast<std::size_t>(cfpay_observer_dll_end - cfpay_observer_dll_start);
}

#else

inline const unsigned char* embeddedObserverDllImage() { return nullptr; }

// 返回内嵌观察 DLL 映像的字节数；映像由 .incbin 直接放进只读数据段，不生成大体积 C 数组。
inline std::size_t embeddedObserverDllSize() { return 0; }

#endif
