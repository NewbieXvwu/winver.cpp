// dpi.cpp
#include "dpi.h"
#include "raii.h"
#include "win_util.h"

namespace winver {

namespace {
using GetDpiForWindowProc = UINT (WINAPI*)(HWND);
using GetDpiForSystemProc = UINT (WINAPI*)(void);
using SetProcessDpiAwareProc = BOOL (WINAPI*)(void);
using SetProcessDpiAwarenessContextProc = BOOL (WINAPI*)(HANDLE);
} // namespace

void EnableDpiAwareness() {
    UniqueHModule user32(LoadLibraryA("user32.dll"));
    if (!user32) return;

    auto pSetContext = GetProc<SetProcessDpiAwarenessContextProc>(
        user32.get(), "SetProcessDpiAwarenessContext");
    if (pSetContext) {
        // 优先 PER_MONITOR_AWARE_V2 (-4)，回退 PER_MONITOR_AWARE (-3)。
        if (!pSetContext(reinterpret_cast<HANDLE>(static_cast<INT_PTR>(-4)))) {
            pSetContext(reinterpret_cast<HANDLE>(static_cast<INT_PTR>(-3)));
        }
        return;
    }
    // 旧系统（Vista+）回退。
    auto pSetAware = GetProc<SetProcessDpiAwareProc>(user32.get(), "SetProcessDPIAware");
    if (pSetAware) pSetAware();
}

UINT QueryDpi(HWND hWnd) {
    UINT dpi = 0;

    UniqueHModule user32(LoadLibraryA("user32.dll"));
    if (user32) {
        auto pWin = GetProc<GetDpiForWindowProc>(user32.get(), "GetDpiForWindow");
        auto pSys = GetProc<GetDpiForSystemProc>(user32.get(), "GetDpiForSystem");
        if (hWnd && pWin) {
            dpi = pWin(hWnd);
        } else if (pSys) {
            dpi = pSys();
        }
    }

    if (dpi == 0) {
        HDC hdc = GetDC(nullptr);
        if (hdc) {
            dpi = static_cast<UINT>(GetDeviceCaps(hdc, LOGPIXELSX));
            ReleaseDC(nullptr, hdc);
        }
    }
    return dpi == 0 ? kBaseDpi : dpi;
}

} // namespace winver
