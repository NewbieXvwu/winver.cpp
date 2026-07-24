cmake_minimum_required(VERSION 3.20)
project(winver CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)  # gnu++17（保留旧构建的 -std=gnu++17 语义）

enable_language(RC)

# 目标架构：x86 | x64 | arm64（默认 x64）。
if(NOT DEFINED WINVER_ARCH)
    set(WINVER_ARCH "x64")
endif()

option(WINVER_BUILD_TESTS "Build pure-logic unit tests (console exe)" OFF)

# —— 核心源码（供 GUI 与测试复用；不含 main.cpp）——
set(WINVER_CORE_SOURCES
    src/win_util.cpp
    src/os_info.cpp
    src/dpi.cpp
    src/gdiplus_api.cpp
    src/logo_loader.cpp
    src/logo_render.cpp
    src/theme.cpp
    src/app_window.cpp
)

add_library(winver_core STATIC ${WINVER_CORE_SOURCES})
target_include_directories(winver_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

# 旧构建保留的导入库集合（Win95 上不存在的库全部运行时 LoadLibrary）。
if(WINVER_ARCH STREQUAL "x86")
    set(WINVER_LIBS comctl32 kernel32 user32 gdi32 ole32 shlwapi advapi32)
else()
    set(WINVER_LIBS comctl32 kernel32 user32 advapi32 shcore gdi32 ole32 dwmapi uxtheme shlwapi)
endif()

# —— 架构相关的编译/链接标志（对齐旧 GCC 构建，clang 兼容集）——
set(WINVER_ARCH_FLAGS "")
if(WINVER_ARCH STREQUAL "x86")
    list(APPEND WINVER_ARCH_FLAGS -march=pentium -D_WIN32_WINDOWS=0x0400)
    set(WINVER_OPT_FLAGS -O3)
    set(WINVER_STATIC_LINK -static-libgcc -static-libstdc++ -static)
    set(WINVER_OUTPUT_NAME winver-x86)
elseif(WINVER_ARCH STREQUAL "x64")
    list(APPEND WINVER_ARCH_FLAGS -march=x86-64 -mtune=generic
         -D_WIN32_WINDOWS=0x0410 -D_WIN32_WINNT=0x0502)
    set(WINVER_OPT_FLAGS -O3 -ffast-math -funroll-loops -finline-functions
        -foptimize-sibling-calls -fomit-frame-pointer -ftree-vectorize)
    set(WINVER_STATIC_LINK -static-libgcc -static-libstdc++)
    set(WINVER_OUTPUT_NAME winver-x64)
elseif(WINVER_ARCH STREQUAL "arm64")
    list(APPEND WINVER_ARCH_FLAGS -D_WIN32_WINDOWS=0x0410 -D_WIN32_WINNT=0x0502)
    set(WINVER_OPT_FLAGS -O3 -ffast-math -funroll-loops -finline-functions
        -foptimize-sibling-calls -fomit-frame-pointer -ftree-vectorize)
    set(WINVER_STATIC_LINK -static-libgcc -static-libstdc++)
    set(WINVER_OUTPUT_NAME winver-arm64)
else()
    message(FATAL_ERROR "Unknown WINVER_ARCH: ${WINVER_ARCH} (expected x86|x64|arm64)")
endif()

target_compile_options(winver_core PUBLIC ${WINVER_ARCH_FLAGS} ${WINVER_OPT_FLAGS})

# RC 需要能找到 res/winver.manifest。
set(CMAKE_RC_FLAGS "${CMAKE_RC_FLAGS} --include-dir ${CMAKE_CURRENT_SOURCE_DIR}/res")

# —— GUI 可执行文件 ——
add_executable(winver WIN32 src/main.cpp res/winver.rc)
target_link_libraries(winver PRIVATE winver_core ${WINVER_LIBS})
target_link_options(winver PRIVATE
    -mwindows -Wl,--subsystem=windows -Wl,--gc-sections ${WINVER_STATIC_LINK})
set_target_properties(winver PROPERTIES OUTPUT_NAME ${WINVER_OUTPUT_NAME})

# —— 纯逻辑单元测试（控制台 exe，可在 Wine headless 下运行）——
if(WINVER_BUILD_TESTS)
    add_executable(winver_tests tests/test_main.cpp)
    target_link_libraries(winver_tests PRIVATE winver_core ${WINVER_LIBS})
    target_link_options(winver_tests PRIVATE -Wl,--gc-sections ${WINVER_STATIC_LINK})
    set_target_properties(winver_tests PROPERTIES OUTPUT_NAME winver-tests)
    enable_testing()
    add_test(NAME winver_tests COMMAND winver_tests)
endif()
