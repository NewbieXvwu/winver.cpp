// app_window.h — 窗口类、消息处理、布局计算（ComputeLayout 为纯函数）
#pragma once

#include <windows.h>
#include "app_state.h"
#include "os_info.h"

namespace winver {

// 布局计算的纯输入。
struct LayoutInput {
    UINT dpi = 96;
    int clientWidth = 0;
    bool isXP = false;
    bool logoLoaded = false;
    int logoWidth = 0;
    int logoHeight = 0;
    bool separatorLoaded = false;
    int separatorHeight = 0;
    bool isMultiline = false;
};

struct ControlRect { int x, y, w, h; };
struct Layout { ControlRect text; ControlRect button; };

// 纯函数：由输入算出文本框与按钮的位置/尺寸（可单测）。
Layout ComputeLayout(const LayoutInput& in);

const char* WindowClassName();
ATOM RegisterAppClass(HINSTANCE hInstance);
HFONT CreateDpiFont(UINT dpi, const OsFacts& f);
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

} // namespace winver
