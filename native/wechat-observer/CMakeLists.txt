cmake_minimum_required(VERSION 3.16)
project(cfpay_wechat_observer LANGUAGES C CXX)

add_compile_definitions(_WIN32_WINNT=0x0601)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(quirc STATIC
  third_party/quirc/quirc.c
  third_party/quirc/decode.c
  third_party/quirc/identify.c
  third_party/quirc/version_db.c)
target_include_directories(quirc PUBLIC third_party/quirc)

add_library(cfpay-hook-observer SHARED hardware_observer.cpp)
set_target_properties(cfpay-hook-observer PROPERTIES PREFIX "" OUTPUT_NAME "cfpay-hook-observer")
target_compile_definitions(cfpay-hook-observer PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(cfpay-hook-observer PRIVATE -Wall -Wextra -Wpedantic)
target_link_options(cfpay-hook-observer PRIVATE -static -static-libgcc -static-libstdc++)

add_library(cfpay-hook-diagnostic SHARED hardware_observer.cpp)
set_target_properties(cfpay-hook-diagnostic PROPERTIES PREFIX "" OUTPUT_NAME "cfpay-hook-diagnostic")
target_compile_definitions(cfpay-hook-diagnostic PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX CFPAY_DIAGNOSTIC_BUILD)
target_compile_options(cfpay-hook-diagnostic PRIVATE -Wall -Wextra -Wpedantic)
target_link_options(cfpay-hook-diagnostic PRIVATE -static -static-libgcc -static-libstdc++)

add_library(cfpay-hook-network-diagnostic SHARED network_write_observer.cpp)
set_target_properties(cfpay-hook-network-diagnostic PROPERTIES PREFIX "" OUTPUT_NAME "cfpay-hook-network-diagnostic")
target_compile_definitions(cfpay-hook-network-diagnostic PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(cfpay-hook-network-diagnostic PRIVATE -Wall -Wextra -Wpedantic)
target_link_options(cfpay-hook-network-diagnostic PRIVATE -static -static-libgcc -static-libstdc++)

add_library(cfpay-hook-record-diagnostic SHARED record_dump_observer.cpp)
set_target_properties(cfpay-hook-record-diagnostic PROPERTIES PREFIX "" OUTPUT_NAME "cfpay-hook-record-diagnostic")
target_compile_definitions(cfpay-hook-record-diagnostic PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(cfpay-hook-record-diagnostic PRIVATE -Wall -Wextra -Wpedantic)
target_link_options(cfpay-hook-record-diagnostic PRIVATE -static -static-libgcc -static-libstdc++)

add_library(cfpay-hook-sync-diagnostic SHARED sync_command_observer.cpp)
set_target_properties(cfpay-hook-sync-diagnostic PROPERTIES PREFIX "" OUTPUT_NAME "cfpay-hook-sync-diagnostic")
target_compile_definitions(cfpay-hook-sync-diagnostic PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(cfpay-hook-sync-diagnostic PRIVATE -Wall -Wextra -Wpedantic)
target_link_options(cfpay-hook-sync-diagnostic PRIVATE -static -static-libgcc -static-libstdc++)

add_library(cfpay-hook-wmpf-diagnostic SHARED hardware_observer.cpp)
set_target_properties(cfpay-hook-wmpf-diagnostic PROPERTIES PREFIX "" OUTPUT_NAME "cfpay-hook-wmpf-diagnostic")
target_compile_definitions(cfpay-hook-wmpf-diagnostic PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX CFPAY_DIAGNOSTIC_BUILD CFPAY_WMPF_DIAGNOSTIC_BUILD)
target_compile_options(cfpay-hook-wmpf-diagnostic PRIVATE -Wall -Wextra -Wpedantic)
target_link_options(cfpay-hook-wmpf-diagnostic PRIVATE -static -static-libgcc -static-libstdc++)

add_executable(cfpay-hook-test-target test_target.cpp)
target_compile_definitions(cfpay-hook-test-target PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
target_compile_options(cfpay-hook-test-target PRIVATE -Wall -Wextra -Wpedantic)
target_link_options(cfpay-hook-test-target PRIVATE -municode -static -static-libgcc -static-libstdc++)
target_link_libraries(cfpay-hook-test-target PRIVATE gdi32 user32)

add_executable(cfpay-wechat-host wechat_host.cpp)
target_compile_definitions(cfpay-wechat-host PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(cfpay-wechat-host PRIVATE -Wall -Wextra -Wpedantic)
target_link_options(cfpay-wechat-host PRIVATE -municode -static -static-libgcc -static-libstdc++)
target_link_libraries(cfpay-wechat-host PRIVATE bcrypt ole32 uuid version)

add_executable(cfpay-wechat-config WIN32 wechat_config.cpp)
target_include_directories(cfpay-wechat-config PRIVATE third_party)
target_compile_definitions(cfpay-wechat-config PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
target_compile_options(cfpay-wechat-config PRIVATE -Wall -Wextra -Wpedantic)
target_link_options(cfpay-wechat-config PRIVATE -municode -static -static-libgcc -static-libstdc++)
target_link_libraries(cfpay-wechat-config PRIVATE bcrypt comctl32 ole32 shell32 uuid winhttp)

add_executable(cfpay-wechat-monitor WIN32
  wechat_monitor.cpp
  wechat_login_assistant.cpp
  wechat_host.cpp
  wechat_config.cpp)
target_include_directories(cfpay-wechat-monitor PRIVATE third_party)
target_compile_definitions(cfpay-wechat-monitor PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE CFPAY_EMBED_HOST CFPAY_EMBED_CONFIG)
target_compile_options(cfpay-wechat-monitor PRIVATE -Wall -Wextra -Wpedantic)
target_link_options(cfpay-wechat-monitor PRIVATE -municode -static -static-libgcc -static-libstdc++)
target_link_libraries(cfpay-wechat-monitor PRIVATE bcrypt comctl32 gdi32 ole32 quirc shell32 user32 uuid version winhttp)

# 单 EXE 分发：把生产观察 DLL 嵌入监控端可执行文件
add_dependencies(cfpay-wechat-monitor cfpay-hook-observer)
target_compile_definitions(cfpay-wechat-monitor PRIVATE
  CFPAY_EMBED_OBSERVER_DLL
  "CFPAY_OBSERVER_DLL_FILE=\"$<TARGET_FILE:cfpay-hook-observer>\"")
