// logo_loader.h — 从系统 DLL 加载 Windows logo 位图/PNG 与 XP 分隔条
#pragma once

#include "app_state.h"
#include "os_info.h"

namespace winver {

// 加载 Windows logo 到 assets（GDI 位图或 GDI+ 图像）。成功返回 true。
bool LoadWindowsLogo(const OsFacts& f, LogoAssets& assets);

// 加载 XP 分隔条到 assets.separator（仅 5.x）。成功返回 true。
bool LoadXpSeparator(const OsFacts& f, LogoAssets& assets);

} // namespace winver
