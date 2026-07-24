// theme.h — 深色模式检测/应用 + SetWindowTheme 动态封装
#pragma once

#include <windows.h>
#include "app_state.h"
#include "os_info.h"

namespace winver {

// 动态解析 uxtheme!SetWindowTheme（Win95 无此库时安全返回）。
HRESULT SetWindowThemeDyn(HWND hwnd, LPCWSTR subApp, LPCWSTR subIdList);

// 读注册表判断当前是否深色主题（AppsUseLightTheme == 0）。
bool QueryDarkModeEnabled();

// 依据注册表刷新深色模式并应用到窗口/子控件；仅在状态变化时重绘。
void ApplyDarkMode(HWND hwnd, AppContext& ctx, const OsFacts& f);

} // namespace winver
