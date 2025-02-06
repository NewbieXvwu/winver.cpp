#include <windows.h>
#include <commctrl.h>
#include <winreg.h>
#include <cstdlib>
#include <string>
#include <cctype>
#include <cstdio>
#include <cstring>

#if !defined(WM_DPICHANGED)
#define WM_DPICHANGED 0x02E0
#endif

#if !defined(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((HANDLE)-4)
#endif

typedef UINT (WINAPI *GetDpiForWindowProc)(HWND hwnd);
typedef UINT (WINAPI *GetDpiForSystemProc)(void);

typedef enum _PROCESS_DPI_AWARENESS {
    PROCESS_DPI_UNAWARE = 0,
    PROCESS_SYSTEM_DPI_AWARE = 1,
    PROCESS_PER_MONITOR_DPI_AWARE = 2
} PROCESS_DPI_AWARENESS;

typedef BOOL    (WINAPI *SetProcessDPIAwareProc)(void);
typedef HRESULT (WINAPI *SetProcessDPIAwarenessProc)(PROCESS_DPI_AWARENESS);
typedef BOOL    (WINAPI *SetProcessDPIAwarenessContextProc)(DPI_AWARENESS_CONTEXT);

constexpr int BASE_DPI = 96;
#define SCALE_X(x) MulDiv(x, g_dpiX, BASE_DPI)
#define SCALE_Y(x) MulDiv(x, g_dpiY, BASE_DPI)

static UINT g_dpiX = BASE_DPI;
static UINT g_dpiY = BASE_DPI;
static HFONT g_hFont = nullptr;

// 函数原型声明
bool IsModernUIAvailable();
void InitDPIScaling(HWND hWnd, bool forceSystemDPI = false);
void EnableModernFeatures();
void UpdateLayout(HWND hWnd);
HFONT CreateDPIFont();
void GetRealOSVersion(OSVERSIONINFOA* osvi);

bool IsServerEdition() {
    HKEY hKey;
    char installType[32] = {0};
    DWORD dwSize = sizeof(installType);
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                      "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
                      0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        if (RegQueryValueExA(hKey, "InstallationType", nullptr, nullptr,
                             reinterpret_cast<LPBYTE>(installType), &dwSize) == ERROR_SUCCESS)
        {
            RegCloseKey(hKey);
            return (strstr(installType, "Server") != nullptr);
        }
        RegCloseKey(hKey);
    }
    return false;
}

BOOL CALLBACK SetChildFont(HWND hWndChild, LPARAM lParam) {
    SendMessage(hWndChild, WM_SETFONT, static_cast<WPARAM>(lParam), TRUE);
    return TRUE;
}

const char* GetWin9xProductName(DWORD dwMinorVersion, DWORD dwBuildNumber) {
    switch (dwMinorVersion) {
        case 0: return "95";
        case 10: return (dwBuildNumber & 0xFFFF) >= 2222 ? "98 SE" : "98";
        case 90: return "Me";
        default: return nullptr;
    }
}

const char* GetModernWindowsName(DWORD dwMajor, DWORD dwMinor, DWORD dwBuild) {
    // 增加对 Windows NT 4.0 的检测（NT 4.0 的版本号为 4.0，其 dwMajor 小于 5）
    if (dwMajor < 5) {
        // 如果为服务器版返回 "Windows NT 4.0 Server"，否则返回 "Windows NT 4.0"
        return IsServerEdition() ? "Windows NT 4.0 Server" : "Windows NT 4.0";
    }

    bool isServerEdition = IsServerEdition();

    if (dwMajor == 10) {
        if (isServerEdition) {
            static char sServerName[128] = {0};
            char productName[128] = {0};
            DWORD dwSize = sizeof(productName);
            HKEY hKey;
            if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                              "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
                              0, KEY_READ, &hKey) == ERROR_SUCCESS)
            {
                RegQueryValueExA(hKey, "ProductName", nullptr, nullptr,
                                 reinterpret_cast<LPBYTE>(productName), &dwSize);
                RegCloseKey(hKey);
            }
            strncpy(sServerName, productName, sizeof(sServerName) - 1);
            sServerName[sizeof(sServerName) - 1] = '\0';
            return sServerName;
        } else {
            if (dwBuild >= 22000) return "11";
            if (dwBuild >= 10240) return "10";
        }
    }
    if (dwMajor == 6) {
        if (isServerEdition) {
            static char sServerName[128] = {0};
            char productName[128] = {0};
            DWORD dwSize = sizeof(productName);
            HKEY hKey;
            if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                              "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
                              0, KEY_READ, &hKey) == ERROR_SUCCESS)
            {
                RegQueryValueExA(hKey, "ProductName", nullptr, nullptr,
                                 reinterpret_cast<LPBYTE>(productName), &dwSize);
                RegCloseKey(hKey);
            }
            strncpy(sServerName, productName, sizeof(sServerName) - 1);
            sServerName[sizeof(sServerName) - 1] = '\0';
            return sServerName;
        } else {
            switch (dwMinor) {
                case 3: return "8.1";
                case 2: return "8";
                case 1: return "7";
                case 0: return "Vista";
            }
        }
    }
    if (dwMajor == 5) {
        switch (dwMinor) {
            case 2: {
                HKEY hKey;
                char productName[64] = {0};
                DWORD dwSize = sizeof(productName);
                if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                                  "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
                                  0, KEY_READ, &hKey) == ERROR_SUCCESS)
                {
                    RegQueryValueExA(hKey, "ProductName", nullptr, nullptr,
                                     reinterpret_cast<LPBYTE>(productName), &dwSize);
                    RegCloseKey(hKey);
                    if (strstr(productName, "XP") || strstr(productName, "Client")) {
                        return "XP x64";
                    }
                }
                return "Server 2003";
            }
            case 1: return "XP";
            case 0: return "2000";
        }
    }
    static char sProductName[128] = {0};
    char productName[128] = {0};
    DWORD dwSize = sizeof(productName);
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                      "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
                      0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        RegQueryValueExA(hKey, "ProductName", nullptr, nullptr,
                         reinterpret_cast<LPBYTE>(productName), &dwSize);
        RegCloseKey(hKey);
    }
    strncpy(sProductName, productName, sizeof(sProductName) - 1);
    sProductName[sizeof(sProductName) - 1] = '\0';
    return sProductName;
}

char* GetWindowsVersion() {
    static char szVersion[256] = "";
    OSVERSIONINFOA osvi = {0};
    osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFOA);
    GetRealOSVersion(&osvi);

    if (osvi.dwMajorVersion == 5 && osvi.dwMinorVersion == 0 && osvi.szCSDVersion[0] == '\0') {
        char regCSD[64] = "";
        DWORD size = sizeof(regCSD);
        HKEY hKey;
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, 
                          "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
                          0, KEY_READ, &hKey) == ERROR_SUCCESS)
        {
            RegQueryValueExA(hKey, "CSDVersion", nullptr, nullptr,
                             reinterpret_cast<LPBYTE>(regCSD), &size);
            RegCloseKey(hKey);
        }
        if (regCSD[0] != '\0') {
            strncpy(osvi.szCSDVersion, regCSD, sizeof(osvi.szCSDVersion) - 1);
            osvi.szCSDVersion[sizeof(osvi.szCSDVersion) - 1] = '\0';
        }
    }

    char spBuffer[64] = "";
    if (osvi.szCSDVersion[0] != '\0') {
        const char* p = osvi.szCSDVersion;
        while (*p && !isdigit(*p)) { ++p; }
        if (*p && *p != '0') {
            char number[16] = {0};
            int i = 0;
            while (isdigit(*p) && i < 15) {
                number[i++] = *p;
                p++;
            }
            number[i] = '\0';
            // 将 sprintf_s 替换为 sprintf
            sprintf(spBuffer, " sp%s", number);
        }
    }

    if (osvi.dwPlatformId == VER_PLATFORM_WIN32_WINDOWS) {
        if (const char* product = GetWin9xProductName(osvi.dwMinorVersion, osvi.dwBuildNumber)) {
            if (spBuffer[0] != '\0')
                sprintf(szVersion, "Windows %s%s\r\n(Build %d.%02d.%04d)",
                        product, spBuffer, osvi.dwMajorVersion, osvi.dwMinorVersion, osvi.dwBuildNumber & 0xFFFF);
            else
                sprintf(szVersion, "Windows %s\r\n(Build %d.%02d.%04d)",
                        product, osvi.dwMajorVersion, osvi.dwMinorVersion, osvi.dwBuildNumber & 0xFFFF);
        } else {
            if (spBuffer[0] != '\0')
                sprintf(szVersion, "Windows %d.%d%s\r\n(Build %d)",
                        osvi.dwMajorVersion, osvi.dwMinorVersion, spBuffer, osvi.dwBuildNumber & 0xFFFF);
            else
                sprintf(szVersion, "Windows %d.%d\r\n(Build %d)",
                        osvi.dwMajorVersion, osvi.dwMinorVersion, osvi.dwBuildNumber & 0xFFFF);
        }
    } else {
        DWORD ubr = 0;
        HKEY hKey;
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                          "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", 0, KEY_READ, &hKey) == ERROR_SUCCESS)
        {
            DWORD dwSize = sizeof(DWORD);
            RegQueryValueExA(hKey, "UBR", nullptr, nullptr, reinterpret_cast<LPBYTE>(&ubr), &dwSize);
            RegCloseKey(hKey);
        }

        if (const char* product = GetModernWindowsName(osvi.dwMajorVersion, osvi.dwMinorVersion, osvi.dwBuildNumber)) {
            if (osvi.dwMajorVersion >= 10 && !IsServerEdition()) {
                char displayVersion[64] = "";
                HKEY hKey = nullptr;
                if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                                  "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
                    DWORD dwSize = sizeof(displayVersion);
                    if (RegQueryValueExA(hKey, "DisplayVersion", nullptr, nullptr,
                                         reinterpret_cast<LPBYTE>(displayVersion), &dwSize) != ERROR_SUCCESS) {
                        dwSize = sizeof(displayVersion);
                        if (RegQueryValueExA(hKey, "ReleaseId", nullptr, nullptr,
                                             reinterpret_cast<LPBYTE>(displayVersion), &dwSize) != ERROR_SUCCESS) {
                            displayVersion[0] = '\0';
                        }
                    }
                    RegCloseKey(hKey);
                }
                if (displayVersion[0] != '\0') {
                    bool hasWindowsPrefix = (strncmp(product, "Windows", 7) == 0);
                    if (ubr) {
                        if (hasWindowsPrefix)
                            sprintf(szVersion, "%s %s\r\n(Build %d.%d)", product, displayVersion, osvi.dwBuildNumber, ubr);
                        else
                            sprintf(szVersion, "Windows %s %s\r\n(Build %d.%d)", product, displayVersion, osvi.dwBuildNumber, ubr);
                    } else {
                        if (hasWindowsPrefix)
                            sprintf(szVersion, "%s %s\r\n(Build %d)", product, displayVersion, osvi.dwBuildNumber);
                        else
                            sprintf(szVersion, "Windows %s %s\r\n(Build %d)", product, displayVersion, osvi.dwBuildNumber);
                    }
                    return szVersion;
                }
            }
            bool hasWindowsPrefix = (strncmp(product, "Windows", 7) == 0);
            if (spBuffer[0] != '\0') {
                if (ubr) {
                    if (hasWindowsPrefix)
                        sprintf(szVersion, "%s%s\r\n(Build %d.%d)", product, spBuffer, osvi.dwBuildNumber, ubr);
                    else
                        sprintf(szVersion, "Windows %s%s\r\n(Build %d.%d)", product, spBuffer, osvi.dwBuildNumber, ubr);
                } else {
                    if (hasWindowsPrefix)
                        sprintf(szVersion, "%s%s\r\n(Build %d)", product, spBuffer, osvi.dwBuildNumber);
                    else
                        sprintf(szVersion, "Windows %s%s\r\n(Build %d)", product, spBuffer, osvi.dwBuildNumber);
                }
            } else {
                if (ubr) {
                    if (hasWindowsPrefix)
                        sprintf(szVersion, "%s\r\n(Build %d.%d)", product, osvi.dwBuildNumber, ubr);
                    else
                        sprintf(szVersion, "Windows %s\r\n(Build %d.%d)", product, osvi.dwBuildNumber, ubr);
                } else {
                    if (hasWindowsPrefix)
                        sprintf(szVersion, "%s\r\n(Build %d)", product, osvi.dwBuildNumber);
                    else
                        sprintf(szVersion, "Windows %s\r\n(Build %d)", product, osvi.dwBuildNumber);
                }
            }
        } else {
            if (spBuffer[0] != '\0') {
                sprintf(szVersion, "Windows NT %d.%d%s\r\n(Build %d)",
                        osvi.dwMajorVersion, osvi.dwMinorVersion, spBuffer, osvi.dwBuildNumber);
            } else {
                sprintf(szVersion, "Windows NT %d.%d\r\n(Build %d)",
                        osvi.dwMajorVersion, osvi.dwMinorVersion, osvi.dwBuildNumber);
            }
        }
    }
    return szVersion;
}

void GetRealOSVersion(OSVERSIONINFOA* osvi) {
    HMODULE hNtdll = GetModuleHandleA("ntdll.dll");
    if (hNtdll) {
        using RtlGetVersionProc = LONG(WINAPI*)(LPOSVERSIONINFOEXW);
        if (auto pRtlGetVersion = reinterpret_cast<RtlGetVersionProc>(
            GetProcAddress(hNtdll, "RtlGetVersion")))
        {
            OSVERSIONINFOEXW osviW = { sizeof(osviW) };
            if (pRtlGetVersion(&osviW) == 0) {
                osvi->dwOSVersionInfoSize = sizeof(OSVERSIONINFOA);
                osvi->dwMajorVersion = osviW.dwMajorVersion;
                osvi->dwMinorVersion = osviW.dwMinorVersion;
                osvi->dwBuildNumber = osviW.dwBuildNumber;
                osvi->dwPlatformId = osviW.dwPlatformId;
                WideCharToMultiByte(CP_ACP, 0, osviW.szCSDVersion, -1,
                                    osvi->szCSDVersion, sizeof(osvi->szCSDVersion),
                                    NULL, NULL);
                return;
            }
        }
    }
    osvi->dwOSVersionInfoSize = sizeof(OSVERSIONINFOA);
    GetVersionExA(osvi);
}

void InitDPIScaling(HWND hWnd, bool forceSystemDPI) {
    HMODULE hUser32 = LoadLibraryA("user32.dll");
    if (hUser32 && !forceSystemDPI) {
        auto pGetDpiForWindow = reinterpret_cast<GetDpiForWindowProc>(
            GetProcAddress(hUser32, "GetDpiForWindow"));
        auto pGetDpiForSystem = reinterpret_cast<GetDpiForSystemProc>(
            GetProcAddress(hUser32, "GetDpiForSystem"));

        if (pGetDpiForWindow) {
            g_dpiX = pGetDpiForWindow(hWnd);
            g_dpiY = g_dpiX;
        }
        else if (pGetDpiForSystem) {
            g_dpiX = pGetDpiForSystem();
            g_dpiY = g_dpiX;
        }
        FreeLibrary(hUser32);
    }

    if (g_dpiX == BASE_DPI || forceSystemDPI) {
        HDC hdc = GetDC(nullptr);
        g_dpiX = GetDeviceCaps(hdc, LOGPIXELSX);
        g_dpiY = GetDeviceCaps(hdc, LOGPIXELSY);
        ReleaseDC(nullptr, hdc);
    }
}

void EnableModernFeatures() {
    HMODULE hUser32 = LoadLibraryA("user32.dll");
    if (hUser32) {
        auto pSetContext = reinterpret_cast<SetProcessDPIAwarenessContextProc>(
            GetProcAddress(hUser32, "SetProcessDpiAwarenessContext"));
        if (pSetContext && pSetContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
            FreeLibrary(hUser32);
            return;
        }
        FreeLibrary(hUser32);
    }

    HMODULE hShcore = LoadLibraryA("shcore.dll");
    if (hShcore) {
        auto pSetDPIAwareness = reinterpret_cast<SetProcessDPIAwarenessProc>(
            GetProcAddress(hShcore, "SetProcessDpiAwareness"));
        if (pSetDPIAwareness) {
            pSetDPIAwareness(PROCESS_PER_MONITOR_DPI_AWARE);
            FreeLibrary(hShcore);
            return;
        }
        FreeLibrary(hShcore);
    }

    HMODULE hUser32Again = LoadLibraryA("user32.dll");
    if (hUser32Again) {
        auto pSetDPIAware = reinterpret_cast<SetProcessDPIAwareProc>(
            GetProcAddress(hUser32Again, "SetProcessDPIAware"));
        if (pSetDPIAware) pSetDPIAware();
        FreeLibrary(hUser32Again);
    }
}

bool IsModernUIAvailable() {
    static int sModern = -1;
    if (sModern == -1) {
        OSVERSIONINFOA osvi = { 0 };
        osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFOA);
        GetRealOSVersion(&osvi);
        sModern = (osvi.dwMajorVersion >= 6) ? 1 : 0;
    }
    return sModern == 1;
}

HFONT CreateDPIFont() {
    int fontSize = -MulDiv(18, g_dpiY, 96);
    const char* fontFace = IsModernUIAvailable() ? "Segoe UI" : "MS Sans Serif";
    DWORD quality = IsModernUIAvailable() ? CLEARTYPE_QUALITY : DEFAULT_QUALITY;

    return CreateFontA(
        fontSize, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        quality, DEFAULT_PITCH | FF_DONTCARE, fontFace);
}

void UpdateLayout(HWND hWnd) {
    OSVERSIONINFOA osvi = { 0 };
    osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFOA);
    GetRealOSVersion(&osvi);
    bool hasSP = false;
    if (osvi.szCSDVersion[0] != '\0') {
        const char* p = osvi.szCSDVersion;
        while (*p && !isdigit(*p)) { ++p; }
        if (*p && *p != '0') {
            hasSP = true;
        }
    }

    if (HWND hVersionText = GetDlgItem(hWnd, 0)) {
        int textY, textHeight;
        if (hasSP) {
            textY = SCALE_Y(30);
            textHeight = SCALE_Y(60);
        } else {
            textY = SCALE_Y(40);
            textHeight = SCALE_Y(30);
        }
        SetWindowPos(hVersionText, nullptr, SCALE_X(10), textY,
                     SCALE_X(300), textHeight, SWP_NOZORDER);
        SetWindowLongPtr(hVersionText, GWL_STYLE,
                           GetWindowLongPtr(hVersionText, GWL_STYLE) | SS_EDITCONTROL | 0x20L);
        SetWindowTextA(hVersionText, GetWindowsVersion());
    }

    if (HWND hExitButton = GetDlgItem(hWnd, 1)) {
        int buttonY, buttonWidth;
        if (hasSP) {
            buttonY = SCALE_Y(130);
            buttonWidth = SCALE_X(60);
        } else {
            buttonY = SCALE_Y(100);
            buttonWidth = SCALE_X(50);
        }
        SetWindowPos(hExitButton, nullptr, (SCALE_X(320) - buttonWidth) / 2, buttonY,
                     buttonWidth, SCALE_Y(35), SWP_NOZORDER);
    }
    
    SetWindowPos(hWnd, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE: {
        if (IsModernUIAvailable()) {
            BOOL fontSmoothing = TRUE;
            SystemParametersInfo(SPI_SETFONTSMOOTHING, TRUE, &fontSmoothing, 0);
            SystemParametersInfo(SPI_SETFONTSMOOTHINGTYPE, 0,
                                   (PVOID)FE_FONTSMOOTHINGCLEARTYPE, 0);
        }

        InitDPIScaling(hWnd);
        g_hFont = CreateDPIFont();

        CreateWindowA("Static", GetWindowsVersion(),
                      WS_CHILD | WS_VISIBLE | SS_CENTER | SS_EDITCONTROL | 0x20L,
                      SCALE_X(10), SCALE_Y(30), SCALE_X(300), SCALE_Y(60),
                      hWnd, nullptr, nullptr, nullptr);

        CreateWindowA("Button", "OK", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                      (SCALE_X(320) - SCALE_X(60)) / 2, SCALE_Y(100),
                      SCALE_X(60), SCALE_Y(35), hWnd, (HMENU)1, nullptr, nullptr);

        if (g_hFont)
            EnumChildWindows(hWnd, SetChildFont, reinterpret_cast<LPARAM>(g_hFont));
        break;
    }
    case WM_DPICHANGED: {
        auto prcNewWindow = reinterpret_cast<RECT*>(lParam);
        SetWindowPos(hWnd, nullptr, prcNewWindow->left, prcNewWindow->top,
                     prcNewWindow->right - prcNewWindow->left,
                     prcNewWindow->bottom - prcNewWindow->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);

        g_dpiX = HIWORD(wParam);
        g_dpiY = LOWORD(wParam);
        
        if (g_hFont)
            DeleteObject(g_hFont);
        g_hFont = CreateDPIFont();
        
        SendMessage(hWnd, WM_SETREDRAW, FALSE, 0);
        UpdateLayout(hWnd);
        if (g_hFont)
            EnumChildWindows(hWnd, SetChildFont, reinterpret_cast<LPARAM>(g_hFont));
        SendMessage(hWnd, WM_SETREDRAW, TRUE, 0);
        
        RedrawWindow(hWnd, nullptr, nullptr,
                     RDW_ERASE | RDW_FRAME | RDW_INVALIDATE | RDW_ALLCHILDREN);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == 1)
            PostQuitMessage(0);
        break;
    case WM_DESTROY:
        if (g_hFont)
            DeleteObject(g_hFont);
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProcA(hWnd, message, wParam, lParam);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    EnableModernFeatures();
    
    HWND hWndDummy = CreateWindowA("Static", nullptr, 0, 0, 0, 0, 0, nullptr, nullptr, hInstance, nullptr);
    InitDPIScaling(hWndDummy, true);
    DestroyWindow(hWndDummy);

    RECT rc = {0, 0, SCALE_X(320), SCALE_Y(180)};
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MAXIMIZEBOX), FALSE);

    if (IsModernUIAvailable()) {
        INITCOMMONCONTROLSEX icc = { sizeof(INITCOMMONCONTROLSEX), ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES };
        InitCommonControlsEx(&icc);
    }

    WNDCLASSA wc = {0};
    wc.style         = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = "WinVerClass";

    if (!RegisterClassA(&wc)) {
        MessageBoxA(nullptr, "Window registration failed!", "Error", MB_ICONERROR);
        return 0;
    }

    HWND hWnd = CreateWindowA(
        "WinVerClass", 
        "Windows Version",
        WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MAXIMIZEBOX),
        CW_USEDEFAULT, CW_USEDEFAULT, 
        rc.right - rc.left, rc.bottom - rc.top,
        nullptr, nullptr, hInstance, nullptr);

    if (!hWnd) {
        MessageBoxA(nullptr, "Window creation failed!", "Error", MB_ICONERROR);
        return 0;
    }

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    MSG msg{};
    while (GetMessageA(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    return static_cast<int>(msg.wParam);
}
