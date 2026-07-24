// logo_render.h — 绘制 Windows logo（含透明回退）与 XP 分隔条
#pragma once

#include "app_state.h"
#include "os_info.h"

namespace winver {

void DrawWindowsLogo(HDC hdc, const LogoAssets& a, const OsFacts& f, bool darkMode,
                     int x, int y, int width, int height);

void DrawXpSeparator(HDC hdc, const LogoAssets& a, int x, int y, int width, int height);

} // namespace winver
