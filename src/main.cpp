// main.cpp — WinMain：初始化、注册窗口类、创建窗口、消息循环
#include <windows.h>
#include <string>

#include "app_state.h"
#include "app_window.h"
#include "dpi.h"
#include "os_info.h"
#include "logo_loader.h"

using namespace winver;

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    EnableDpiAwareness();

    // 用临时窗口取初始 DPI，用于主窗口尺寸计算。
    UINT dpi = static_cast<UINT>(kBaseDpi);
    if (HWND dummy = CreateWindowA("Static", nullptr, 0, 0, 0, 0, 0,
                                   nullptr, nullptr, hInstance, nullptr)) {
        dpi = QueryDpi(dummy);
        DestroyWindow(dummy);
    } else {
        dpi = QueryDpi(nullptr);
    }

    const OsFacts& facts = GetOsFacts();

    AppContext* ctx = new AppContext();
    ctx->dpi = dpi;
    LoadWindowsLogo(facts, ctx->logo);
    LoadXpSeparator(facts, ctx->logo);

    const bool isXP = IsXpLook(facts);
    RECT rc;
    if (isXP) {
        rc = {0, 0, ScaleForDpi(360, dpi), ScaleForDpi(240, dpi)};
    } else {
        rc = {0, 0, ScaleForDpi(320, dpi),
              ctx->logo.loaded ? ScaleForDpi(240, dpi) : ScaleForDpi(160, dpi)};
    }

    const DWORD dwStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    AdjustWindowRect(&rc, dwStyle, FALSE);

    if (!RegisterAppClass(hInstance)) {
        MessageBoxA(nullptr, "Window Registration Failed!", "Error",
                    MB_ICONEXCLAMATION | MB_OK);
        delete ctx;
        return 0;
    }

    const std::string title = facts.productName.empty() ? "About Windows" : facts.productName;

    const int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    const int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    const int windowWidth = rc.right - rc.left;
    const int windowHeight = rc.bottom - rc.top;
    const int posX = (screenWidth - windowWidth) / 2;
    const int posY = (screenHeight - windowHeight) / 2;

    HWND hWnd = CreateWindowA(WindowClassName(), title.c_str(), dwStyle,
                              posX, posY, windowWidth, windowHeight,
                              nullptr, nullptr, hInstance, ctx);
    if (!hWnd) {
        MessageBoxA(nullptr, "Window Creation Failed!", "Error",
                    MB_ICONEXCLAMATION | MB_OK);
        // WM_CREATE 若已运行则 ctx 归窗口所有并已在 WM_DESTROY 释放，此处不再 delete。
        return 0;
    }

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    MSG msg = {};
    while (GetMessageA(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return static_cast<int>(msg.wParam);
}
