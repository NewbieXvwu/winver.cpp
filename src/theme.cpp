// theme.cpp
#include "theme.h"
#include "raii.h"
#include "win_util.h"

namespace winver {

namespace {

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

constexpr COLORREF kDarkBk = RGB(32, 32, 32);
constexpr COLORREF kLightBk = RGB(255, 255, 255);

using FnSetWindowTheme = HRESULT (WINAPI*)(HWND, LPCWSTR, LPCWSTR);
using FnDwmSetWindowAttribute = HRESULT (WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
using FnAllowDarkModeForWindow = BOOL (WINAPI*)(HWND, BOOL);
using FnSetPreferredAppMode = BOOL (WINAPI*)(int);

void SetImmersiveDarkMode(HWND hwnd, BOOL enable) {
    UniqueHModule dwm(LoadLibraryA("dwmapi.dll"));
    if (!dwm) return;
    auto pSet = GetProc<FnDwmSetWindowAttribute>(dwm.get(), "DwmSetWindowAttribute");
    if (pSet) {
        pSet(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &enable, sizeof(enable));
    }
}

void EnableDarkTitleBar(HWND hwnd) {
    UniqueHModule uxtheme(LoadLibraryA("uxtheme.dll"));
    if (!uxtheme) return;
    auto pAllow = GetProcByOrdinal<FnAllowDarkModeForWindow>(uxtheme.get(), 133);
    auto pPrefer = GetProcByOrdinal<FnSetPreferredAppMode>(uxtheme.get(), 135);
    if (pAllow && pPrefer) {
        pPrefer(1);  // AllowDark
        pAllow(hwnd, TRUE);
        SetImmersiveDarkMode(hwnd, TRUE);
    }
}

} // namespace

HRESULT SetWindowThemeDyn(HWND hwnd, LPCWSTR subApp, LPCWSTR subIdList) {
    static FnSetWindowTheme pfn = [] {
        HMODULE m = LoadLibraryA("uxtheme.dll");  // 故意常驻，进程生命周期内可用
        return m ? GetProc<FnSetWindowTheme>(m, "SetWindowTheme") : nullptr;
    }();
    return pfn ? pfn(hwnd, subApp, subIdList) : S_FALSE;
}

bool QueryDarkModeEnabled() {
    DWORD data = 1;
    if (RegReadDword(HKEY_CURRENT_USER,
                     "Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                     "AppsUseLightTheme", data)) {
        return data == 0;
    }
    return false;
}

void ApplyDarkMode(HWND hwnd, AppContext& ctx, const OsFacts& f) {
    if (!IsModernUI(f)) return;

    const bool wasDark = ctx.darkMode;
    DWORD data = 1;
    if (RegReadDword(HKEY_CURRENT_USER,
                     "Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                     "AppsUseLightTheme", data)) {
        ctx.darkMode = (data == 0);
    }
    if (ctx.darkMode == wasDark) return;

    ctx.lightBrush.reset();
    ctx.darkBrush.reset();

    HWND hStatic = GetDlgItem(hwnd, kVersionTextId);
    HWND hButton = GetDlgItem(hwnd, kOkButtonId);

    if (ctx.darkMode) {
        EnableDarkTitleBar(hwnd);
        ctx.darkBrush.reset(CreateSolidBrush(kDarkBk));
        SetClassLongPtrA(hwnd, GCLP_HBRBACKGROUND,
                         reinterpret_cast<LONG_PTR>(ctx.darkBrush.brush()));
        if (hStatic) SetWindowThemeDyn(hStatic, L"DarkMode_Explorer", nullptr);
        if (hButton) SetWindowThemeDyn(hButton, L"DarkMode_Explorer", nullptr);
    } else {
        SetImmersiveDarkMode(hwnd, FALSE);
        SetClassLongPtrA(hwnd, GCLP_HBRBACKGROUND,
                         static_cast<LONG_PTR>(COLOR_BTNFACE + 1));
        ctx.lightBrush.reset(CreateSolidBrush(kLightBk));
        if (hStatic) SetWindowThemeDyn(hStatic, L"", nullptr);
        if (hButton) SetWindowThemeDyn(hButton, L"", nullptr);
    }
    InvalidateRect(hwnd, nullptr, TRUE);
}

} // namespace winver
