// app_state.h — AppContext：经 GWLP_USERDATA 持有的 UI 状态与 logo 资源
#pragma once

#include <windows.h>
#include "raii.h"

namespace winver {

// Logo / XP 分隔条资源。GDI 位图用 RAII 管理；GDI+ 图像需在 gdip::Shutdown 之前释放。
struct LogoAssets {
    UniqueGdiObj bitmap;       // HBITMAP（GDI 路径）
    void* gdipImage = nullptr; // GDI+ 图像（经 gdip::DisposeImage 释放）
    int width = 0;
    int height = 0;
    bool loaded = false;

    UniqueGdiObj separator;    // HBITMAP（XP 分隔条）
    int sepWidth = 0;
    int sepHeight = 0;
    bool sepLoaded = false;

    LogoAssets() = default;
    LogoAssets(const LogoAssets&) = delete;
    LogoAssets& operator=(const LogoAssets&) = delete;
    ~LogoAssets();  // 释放 gdipImage（在 gdiplus_api.cpp 中实现，确保顺序正确）
};

struct AppContext {
    UINT dpi = 96;
    bool darkMode = false;
    UniqueGdiObj font;       // HFONT
    UniqueGdiObj darkBrush;  // HBRUSH
    UniqueGdiObj lightBrush; // HBRUSH
    LogoAssets logo;
};

// 控件 ID（旧代码用裸 0/1）。
enum ControlId : int {
    kVersionTextId = 0,
    kOkButtonId = 1,
};

} // namespace winver
