// app_window.cpp — 窗口类注册、消息处理、布局计算
#include "app_window.h"

#include <cstring>

#include "dpi.h"
#include "theme.h"
#include "logo_loader.h"
#include "logo_render.h"
#include "gdiplus_api.h"

#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0
#endif

namespace winver {

namespace {

constexpr COLORREF kDarkText = RGB(255, 255, 255);
constexpr COLORREF kDarkBk = RGB(32, 32, 32);

AppContext* Ctx(HWND hWnd) {
    return reinterpret_cast<AppContext*>(GetWindowLongPtrA(hWnd, GWLP_USERDATA));
}

BOOL CALLBACK SetChildFont(HWND hChild, LPARAM lParam) {
    SendMessageA(hChild, WM_SETFONT, static_cast<WPARAM>(lParam), TRUE);
    return TRUE;
}

// 依据 ComputeLayout 的结果摆放子控件并刷新文本。
void ApplyLayout(HWND hWnd, AppContext& ctx, const OsFacts& facts) {
    const std::string versionStr = FormatWindowsVersion(facts);
    const bool isMultiline = versionStr.find("\r\n") != std::string::npos;

    RECT rc;
    GetClientRect(hWnd, &rc);

    LayoutInput in;
    in.dpi = ctx.dpi;
    in.clientWidth = rc.right - rc.left;
    in.isXP = IsXpLook(facts);
    in.logoLoaded = ctx.logo.loaded;
    in.logoWidth = ctx.logo.width;
    in.logoHeight = ctx.logo.height;
    in.separatorLoaded = ctx.logo.sepLoaded;
    in.separatorHeight = ctx.logo.sepHeight;
    in.isMultiline = isMultiline;

    const Layout layout = ComputeLayout(in);

    if (HWND hText = GetDlgItem(hWnd, kVersionTextId)) {
        SetWindowPos(hText, nullptr, layout.text.x, layout.text.y,
                     layout.text.w, layout.text.h, SWP_NOZORDER);
        SetWindowTextA(hText, versionStr.c_str());
    }
    if (HWND hButton = GetDlgItem(hWnd, kOkButtonId)) {
        SetWindowPos(hButton, nullptr, layout.button.x, layout.button.y,
                     layout.button.w, layout.button.h, SWP_NOZORDER);
    }
}

} // namespace

Layout ComputeLayout(const LayoutInput& in) {
    auto SX = [&](int v) { return ScaleForDpi(v, in.dpi); };
    auto SY = [&](int v) { return ScaleForDpi(v, in.dpi); };

    int textYOffset;
    if (in.isXP && in.logoLoaded && in.logoHeight != 0) {
        const float aspect = static_cast<float>(in.logoWidth) / static_cast<float>(in.logoHeight);
        const int logoHeight = (aspect != 0.0f) ? static_cast<int>(in.clientWidth / aspect) : 0;
        textYOffset = logoHeight;
        if (in.separatorLoaded) textYOffset += SY(in.separatorHeight);
        textYOffset += SY(30);
    } else if (in.logoLoaded) {
        textYOffset = SY(100);
    } else {
        textYOffset = SY(30);
    }

    Layout out{};

    const int textWidth = SX(300);
    out.text.w = textWidth;
    out.text.h = in.isMultiline ? SY(50) : SY(30);
    out.text.x = (in.clientWidth - textWidth) / 2;
    out.text.y = textYOffset;

    const int buttonWidth = SX(60);
    out.button.w = buttonWidth;
    // 修复：旧代码内层 `int buttonHeight = SCALE_Y(35);` 遮蔽外层，按钮永远 30 高。
    out.button.h = in.logoLoaded ? SY(35) : SY(30);
    out.button.x = (in.clientWidth - buttonWidth) / 2;
    out.button.y = in.logoLoaded
                       ? textYOffset + (in.isMultiline ? SY(75) : SY(55))
                       : textYOffset + (in.isMultiline ? SY(70) : SY(50));

    return out;
}

const char* WindowClassName() { return "WinVerClass"; }

HFONT CreateDpiFont(UINT dpi, const OsFacts& f) {
    const int fontSize = -MulDiv(17, static_cast<int>(dpi), 96);
    const char* face = IsModernUI(f) ? "Segoe UI" : "MS Sans Serif";
    return CreateFontA(fontSize, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, face);
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE: {
        auto* cs = reinterpret_cast<CREATESTRUCTA*>(lParam);
        auto* ctx = static_cast<AppContext*>(cs->lpCreateParams);
        SetWindowLongPtrA(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(ctx));

        const OsFacts& facts = GetOsFacts();
        ctx->dpi = QueryDpi(hWnd);
        ctx->font.reset(CreateDpiFont(ctx->dpi, facts));

        RECT rc;
        GetClientRect(hWnd, &rc);
        const int clientWidth = rc.right - rc.left;
        const int textWidth = ScaleForDpi(300, ctx->dpi);
        const int buttonWidth = ScaleForDpi(60, ctx->dpi);

        CreateWindowA("Static", "", WS_CHILD | WS_VISIBLE | SS_CENTER,
                      (clientWidth - textWidth) / 2, ScaleForDpi(120, ctx->dpi),
                      textWidth, ScaleForDpi(60, ctx->dpi), hWnd,
                      reinterpret_cast<HMENU>(kVersionTextId), cs->hInstance, nullptr);

        CreateWindowA("Button", "OK", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                      (clientWidth - buttonWidth) / 2, ScaleForDpi(180, ctx->dpi),
                      buttonWidth, ScaleForDpi(35, ctx->dpi), hWnd,
                      reinterpret_cast<HMENU>(kOkButtonId), cs->hInstance, nullptr);

        ApplyLayout(hWnd, *ctx, facts);
        if (ctx->font) EnumChildWindows(hWnd, SetChildFont, reinterpret_cast<LPARAM>(ctx->font.font()));
        ApplyDarkMode(hWnd, *ctx, facts);
        return 0;
    }
    case WM_PAINT: {
        AppContext* ctx = Ctx(hWnd);
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT rc;
        GetClientRect(hWnd, &rc);
        const bool dark = ctx && ctx->darkMode;
        FillRect(hdc, &rc, dark ? ctx->darkBrush.brush() : reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1));

        if (ctx && ctx->logo.loaded) {
            const OsFacts& facts = GetOsFacts();
            const bool isXP = IsXpLook(facts);
            if (isXP && ctx->logo.height != 0) {
                const float aspect = static_cast<float>(ctx->logo.width) / static_cast<float>(ctx->logo.height);
                const int logoHeight = (aspect != 0.0f) ? static_cast<int>(rc.right / aspect) : 0;
                DrawWindowsLogo(hdc, ctx->logo, facts, dark, 0, 0, rc.right, logoHeight);
                if (ctx->logo.sepLoaded) {
                    const int sepHeight = ScaleForDpi(ctx->logo.sepHeight, ctx->dpi);
                    DrawXpSeparator(hdc, ctx->logo, 0, logoHeight, rc.right, sepHeight);
                }
            } else {
                DrawWindowsLogo(hdc, ctx->logo, facts, dark,
                                ScaleForDpi(10, ctx->dpi), ScaleForDpi(20, ctx->dpi),
                                rc.right - ScaleForDpi(20, ctx->dpi), ScaleForDpi(85, ctx->dpi));
            }
        }
        EndPaint(hWnd, &ps);
        return 0;
    }
    case WM_DPICHANGED: {
        AppContext* ctx = Ctx(hWnd);
        RECT* prc = reinterpret_cast<RECT*>(lParam);
        SetWindowPos(hWnd, nullptr, prc->left, prc->top,
                     prc->right - prc->left, prc->bottom - prc->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        if (ctx) {
            ctx->dpi = LOWORD(wParam);
            ctx->font.reset(CreateDpiFont(ctx->dpi, GetOsFacts()));
            ApplyLayout(hWnd, *ctx, GetOsFacts());
            if (ctx->font) EnumChildWindows(hWnd, SetChildFont, reinterpret_cast<LPARAM>(ctx->font.font()));
        }
        RedrawWindow(hWnd, nullptr, nullptr, RDW_ERASE | RDW_INVALIDATE | RDW_ALLCHILDREN);
        return 0;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT: {
        AppContext* ctx = Ctx(hWnd);
        const bool dark = ctx && ctx->darkMode;
        HDC hdcStatic = reinterpret_cast<HDC>(wParam);
        SetTextColor(hdcStatic, dark ? kDarkText : GetSysColor(COLOR_WINDOWTEXT));
        SetBkColor(hdcStatic, dark ? kDarkBk : GetSysColor(COLOR_BTNFACE));
        return reinterpret_cast<LRESULT>(dark ? ctx->darkBrush.brush()
                                              : reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1));
    }
    case WM_CTLCOLORBTN: {
        AppContext* ctx = Ctx(hWnd);
        if (ctx && ctx->darkMode) {
            HDC hdcBtn = reinterpret_cast<HDC>(wParam);
            SetTextColor(hdcBtn, kDarkText);
            SetBkColor(hdcBtn, kDarkBk);
            return reinterpret_cast<LRESULT>(ctx->darkBrush.brush());
        }
        return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_BTNFACE));
    }
    case WM_COMMAND:
        // 修复：OK 走 DestroyWindow，统一由 WM_DESTROY 清理资源，不再绕过。
        if (LOWORD(wParam) == kOkButtonId) DestroyWindow(hWnd);
        return 0;
    case WM_DESTROY: {
        AppContext* ctx = Ctx(hWnd);
        SetWindowLongPtrA(hWnd, GWLP_USERDATA, 0);
        // 先销毁持有 GDI+ 图像的 AppContext，再关闭 GDI+，保证释放顺序。
        delete ctx;
        gdip::Shutdown();
        PostQuitMessage(0);
        return 0;
    }
    case WM_SETTINGCHANGE: {
        AppContext* ctx = Ctx(hWnd);
        if (ctx && lParam &&
            (_stricmp(reinterpret_cast<LPCSTR>(lParam), "ImmersiveColorSet") == 0 ||
             _stricmp(reinterpret_cast<LPCSTR>(lParam), "ThemeChanged") == 0)) {
            const bool wasDark = ctx->darkMode;
            ApplyDarkMode(hWnd, *ctx, GetOsFacts());
            if (wasDark != ctx->darkMode || ctx->darkMode) {
                InvalidateRect(hWnd, nullptr, TRUE);
                UpdateWindow(hWnd);
            }
        }
        return 0;
    }
    default:
        return DefWindowProcA(hWnd, message, wParam, lParam);
    }
    return 0;
}

ATOM RegisterAppClass(HINSTANCE hInstance) {
    WNDCLASSA wc = {};
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = WindowClassName();
    return RegisterClassA(&wc);
}

} // namespace winver
