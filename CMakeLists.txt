cmake_minimum_required(VERSION 3.16)
project(kalo VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(MSVC)
    # /utf-8 不是可选项：源文件是 UTF-8 且含中文注释与字符串字面量，不指定的话
    # MSVC 会按系统代码页（中文 Windows 上是 GBK）去解释，中文字符串会乱码。
    # /wd4996 用来屏蔽 MSVC 对 POSIX 名字（read / write / open / close）的弃用提醒。
    add_compile_options(/W4 /utf-8 /wd4996)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

set(KALO_SOURCES
    src/Terminal.cpp
    src/Screen.cpp
    src/Editor.cpp
    src/Row.cpp
    src/Buffer.cpp
    src/Cursor.cpp
    src/PieceTable.cpp
    src/Utf8.cpp
    src/Syntax.cpp
)

# 编辑器本体。终端接口在 Linux / macOS 上直接用系统自带的 POSIX 头文件，
# 在 Windows 上则用 src/platform/win32 下的兼容层（说明见该目录）。
add_executable(kalo main.cpp ${KALO_SOURCES})

target_include_directories(kalo PRIVATE src)

if(WIN32)
    # 只把兼容层加进 include 路径，src/ 下的 POSIX 源码一个字都不用改。
    # win32 目录放 MinGW 与 MSVC 共用的替身（termios.h、sys/ioctl.h）；
    # MSVC 额外缺 <unistd.h>，所以再多加一个目录。
    target_include_directories(kalo PRIVATE src/platform/win32)
    if(MSVC)
        target_include_directories(kalo PRIVATE src/platform/msvc)
    endif()
endif()

# 纯逻辑层测试：PieceTable / Buffer / Row 都只用标准库，不碰终端接口，
# 所以在 Linux、macOS、Windows 上都能直接构建运行。
enable_testing()

add_executable(kalo_tests
    test/test_pt.cpp
    src/PieceTable.cpp
    src/Buffer.cpp
    src/Row.cpp
    src/Utf8.cpp
    src/Syntax.cpp
)

target_include_directories(kalo_tests PRIVATE src)

add_test(NAME kalo_core COMMAND kalo_tests)

if(WIN32)
    add_executable(kalo_console_tests test/test_console_win32.cpp src/Terminal.cpp src/Utf8.cpp)
    target_include_directories(kalo_console_tests PRIVATE src src/platform/win32)
    if(MSVC)
        target_include_directories(kalo_console_tests PRIVATE src/platform/msvc)
    endif()
    add_test(NAME kalo_console_unicode COMMAND kalo_console_tests)
    set_tests_properties(kalo_console_unicode PROPERTIES TIMEOUT 20)
endif()

# 端到端冒烟测试：把按键序列喂给编辑器本体，核对落盘内容。
# 脚本是 bash 写的，所以先看有没有 bash —— MSVC 那条构建路径上 UNIX/MINGW 都不成立，
# 但只要装了 Git Bash 或 MSYS2 就照样能跑，比按平台一刀切更实在。
# Windows 上 PATH 里的 bash.exe 常常是 WSL 的启动器（C:/Windows/System32/bash.exe）：
# 它会把脚本交给 Linux 子系统执行，路径语义完全不同（C:/... 在子系统里根本不存在），
# 冒烟测试必然失败。所以先在 Git Bash / MSYS2 这些「真 bash」的惯用位置里找。
find_program(KALO_BASH
    NAMES bash.exe bash
    PATHS "C:/Program Files/Git/bin" "C:/Program Files/Git/usr/bin"
          "C:/msys64/usr/bin" "C:/tools/msys64/usr/bin"
          "D:/msys64/usr/bin" "D:/tools/msys64/usr/bin"
    NO_DEFAULT_PATH)

if(NOT KALO_BASH)
    find_program(KALO_BASH NAMES bash.exe bash)
endif()

# 兜底：WindowsApps / System32 下的 bash.exe 其实都是指向 wsl.exe 的启动器
# （WindowsApps 里那个还是个符号链接），会把脚本丢进 Linux 子系统执行。
# 解出真实路径再判一次 —— 宁可不注册这条测试，也不要注册一条必然失败的测试。
if(KALO_BASH)
    get_filename_component(KALO_BASH_REAL "${KALO_BASH}" REALPATH)
    if(KALO_BASH_REAL MATCHES "wsl[.]exe$")
        message(STATUS "${KALO_BASH} 实际是 WSL 启动器，跳过 kalo_smoke")
        unset(KALO_BASH CACHE)
        set(KALO_BASH "")
    endif()
endif()

if(KALO_BASH)
    add_test(NAME kalo_smoke
             COMMAND ${KALO_BASH} ${CMAKE_CURRENT_SOURCE_DIR}/test/smoke_editor.sh $<TARGET_FILE:kalo>)
else()
    message(STATUS "未找到 bash，跳过 kalo_smoke（冒烟测试依赖 bash 脚本）")
endif()

# 性能基准：PieceTable 与 std::string 跑同一组操作的耗时对照。
# 刻意**不**注册进 ctest —— 基准耗时会随机器漂移，拿它当门禁只会制造噪声。
# 要跑就手动跑：cmake --build build && ./build/kalo_bench
add_executable(kalo_bench
    test/bench.cpp
    src/PieceTable.cpp
    src/Buffer.cpp
    src/Row.cpp
    src/Utf8.cpp
    src/Syntax.cpp
)

target_include_directories(kalo_bench PRIVATE src)
