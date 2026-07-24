// dpi.h — DPI 感知与查询
#pragma once

#include <windows.h>

namespace winver {

constexpr int kBaseDpi = 96;

inline int ScaleForDpi(int value, UINT dpi) { return MulDiv(value, static_cast<int>(dpi), kBaseDpi); }

// 进程级 DPI 感知（尽早调用）。
void EnableDpiAwareness();

// 查询窗口 DPI；无 Per-Monitor API 时回退系统 DPI，最终回退 96。
UINT QueryDpi(HWND hWnd);

} // namespace winver
