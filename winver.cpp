#include <windows.h>
#include <commctrl.h>
#include <winreg.h>      // 新增注册表操作头文件
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

// 原有函数原型声明
bool IsModernUIAvailable();
void InitDPIScaling(HWND hWnd, bool forceSystemDPI = false);
void EnableModernFeatures();
void UpdateLayout(HWND hWnd);
HFONT CreateDPIFont();

// 新增前向声明，解决 GetRealOSVersion 未声明问题
void GetRealOSVersion(OSVERSIONINFOA* osvi);

// 为方便服务器版判断，新增辅助函数，通过注册表判断是否为 Server 版本
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
            // 如果返回的字符串中包含 "Server"，则认为是服务器版
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

/*
   修改 GetModernWindowsName：
   针对不同版本，先检测是否为服务器版，若是则从注册表中读取完整的产品名称（如 "Windows Server 2016"），
   否则返回客户端版本（如 "7"、"8"、"Vista"、"10"、"11" 或 "XP x64"、"Server 2003" 等）。
*/
const char* GetModernWindowsName(DWORD dwMajor, DWORD dwMinor, DWORD dwBuild) {
    // 利用辅助函数判断是否为服务器版
    bool isServerEdition = IsServerEdition();

    if (dwMajor == 10) {
        if (isServerEdition) {
            // 读取注册表中的 ProductName 获取服务器完整名称
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
            // 使用 strncpy 替换 strcpy_s，以兼容 Windows XP
            strncpy(sServerName, productName, sizeof(sServerName) - 1);
            sServerName[sizeof(sServerName) - 1] = '\0';
            return sServerName;
        } else {
            // 客户版：根据 Build 判断 Windows 10 或 Windows 11
            if (dwBuild >= 22000) return "11";
            if (dwBuild >= 10240) return "10";
        }
    }
    if (dwMajor == 6) {
        if (isServerEdition) {
            // Windows Vista / Windows 7 时代的服务器版，例如 "Windows Server 2008" 或 "Windows Server 2008 R2"
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
            // 客户版
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
                // Windows 5.2：区分 Windows XP x64 与 Server 2003
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
                    // 若产品名称中包含 "XP" 或 "Client"，则为 XP x64 版
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
    // 其他版本则直接返回注册表中的 ProductName
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

/*
   修改 GetWindowsVersion：
   对于 Windows 10 及更高版本，会读取注册表中的 DisplayVersion 键，
   实现显示功能更新版本号（如“24H2”）。例如，Win11 24H2 将显示为：
      Windows 11 24H2
      (Build xxxx)
   而对于旧版本或无法查询到 DisplayVersion 时，依然使用原有逻辑（包括 Service Pack 等）进行显示。
*/
char* GetWindowsVersion() {
    static char szVersion[256]{};
    OSVERSIONINFOA osvi = {0};
    osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFOA);
    GetRealOSVersion(&osvi);

    // 从 Service Pack 字段中提取数字后缀，如 " sp2"
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
            sprintf_s(spBuffer, sizeof(spBuffer), " sp%s", number);
        }
    }

    if (osvi.dwPlatformId == VER_PLATFORM_WIN32_WINDOWS) {
        // 针对 Win9x 系统
        if (const char* product = GetWin9xProductName(osvi.dwMinorVersion, osvi.dwBuildNumber)) {
            if (spBuffer[0] != '\0')
                sprintf_s(szVersion, sizeof(szVersion), "Windows %s%s\r\n(Build %d.%02d.%04d)",
                          product, spBuffer, osvi.dwMajorVersion, osvi.dwMinorVersion, osvi.dwBuildNumber & 0xFFFF);
            else
                sprintf_s(szVersion, sizeof(szVersion), "Windows %s\r\n(Build %d.%02d.%04d)",
                          product, osvi.dwMajorVersion, osvi.dwMinorVersion, osvi.dwBuildNumber & 0xFFFF);
        } else {
            if (spBuffer[0] != '\0')
                sprintf_s(szVersion, sizeof(szVersion), "Windows %d.%d%s\r\n(Build %d)",
                          osvi.dwMajorVersion, osvi.dwMinorVersion, spBuffer, osvi.dwBuildNumber & 0xFFFF);
            else
                sprintf_s(szVersion, sizeof(szVersion), "Windows %d.%d\r\n(Build %d)",
                          osvi.dwMajorVersion, osvi.dwMinorVersion, osvi.dwBuildNumber & 0xFFFF);
        }
    } else {
        // 读取 UBR 值（用于精确到小版本，如 26100.2894）
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
            // 针对 Windows 10 及更高版本，先尝试读取功能更新版本号
            if (osvi.dwMajorVersion >= 10) {
                char displayVersion[64] = "";
                HKEY hKey = nullptr;
                if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                                  "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
                    DWORD dwSize = sizeof(displayVersion);
                    // 先尝试读取 DisplayVersion
                    if (RegQueryValueExA(hKey, "DisplayVersion", nullptr, nullptr,
                                          reinterpret_cast<LPBYTE>(displayVersion), &dwSize) != ERROR_SUCCESS) {
                        // 如果读取失败，则再尝试读取 ReleaseId
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
                            sprintf_s(szVersion, sizeof(szVersion), "%s %s\r\n(Build %d.%d)", product, displayVersion, osvi.dwBuildNumber, ubr);
                        else
                            sprintf_s(szVersion, sizeof(szVersion), "Windows %s %s\r\n(Build %d.%d)", product, displayVersion, osvi.dwBuildNumber, ubr);
                    } else {
                        if (hasWindowsPrefix)
                            sprintf_s(szVersion, sizeof(szVersion), "%s %s\r\n(Build %d)", product, displayVersion, osvi.dwBuildNumber);
                        else
                            sprintf_s(szVersion, sizeof(szVersion), "Windows %s %s\r\n(Build %d)", product, displayVersion, osvi.dwBuildNumber);
                    }
                    return szVersion;
                }
            }
            // 如果未能读取到功能更新版本号，则继续使用原有逻辑
            bool hasWindowsPrefix = (strncmp(product, "Windows", 7) == 0);
            if (spBuffer[0] != '\0') {
                if (ubr) {
                    if (hasWindowsPrefix)
                        sprintf_s(szVersion, sizeof(szVersion), "%s%s\r\n(Build %d.%d)", product, spBuffer, osvi.dwBuildNumber, ubr);
                    else
                        sprintf_s(szVersion, sizeof(szVersion), "Windows %s%s\r\n(Build %d.%d)", product, spBuffer, osvi.dwBuildNumber, ubr);
                } else {
                    if (hasWindowsPrefix)
                        sprintf_s(szVersion, sizeof(szVersion), "%s%s\r\n(Build %d)", product, spBuffer, osvi.dwBuildNumber);
                    else
                        sprintf_s(szVersion, sizeof(szVersion), "Windows %s%s\r\n(Build %d)", product, spBuffer, osvi.dwBuildNumber);
                }
            } else {
                if (ubr) {
                    if (hasWindowsPrefix)
                        sprintf_s(szVersion, sizeof(szVersion), "%s\r\n(Build %d.%d)", product, osvi.dwBuildNumber, ubr);
                    else
                        sprintf_s(szVersion, sizeof(szVersion), "Windows %s\r\n(Build %d.%d)", product, osvi.dwBuildNumber, ubr);
                } else {
                    if (hasWindowsPrefix)
                        sprintf_s(szVersion, sizeof(szVersion), "%s\r\n(Build %d)", product, osvi.dwBuildNumber);
                    else
                        sprintf_s(szVersion, sizeof(szVersion), "Windows %s\r\n(Build %d)", product, osvi.dwBuildNumber);
                }
            }
        } else {
            if (spBuffer[0] != '\0') {
                sprintf_s(szVersion, sizeof(szVersion), "Windows NT %d.%d%s\r\n(Build %d)",
                          osvi.dwMajorVersion, osvi.dwMinorVersion, spBuffer, osvi.dwBuildNumber);
            } else {
                sprintf_s(szVersion, sizeof(szVersion), "Windows NT %d.%d\r\n(Build %d)",
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
    int fontSize = -MulDiv(18, g_dpiY, 96); // 稍微减小字号
    const char* fontFace = IsModernUIAvailable() ? "Segoe UI" : "MS Sans Serif";
    DWORD quality = IsModernUIAvailable() ? CLEARTYPE_QUALITY : DEFAULT_QUALITY;

    return CreateFontA(
        fontSize, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        quality, DEFAULT_PITCH | FF_DONTCARE, fontFace);
}

/*
   修改 UpdateLayout：
   根据系统是否包含 Service Pack，调整文本框和按钮的位置及大小，同时更新文本内容。
*/
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
            // 有服务包时：文本框从 SCALE_Y(30) 开始，高度为 SCALE_Y(60)
            textY = SCALE_Y(30);
            textHeight = SCALE_Y(60);
        } else {
            // 无服务包时：恢复原始尺寸，文本框从 SCALE_Y(40) 开始，高度为 SCALE_Y(30)
            textY = SCALE_Y(40);
            textHeight = SCALE_Y(30);
        }
        SetWindowPos(hVersionText, nullptr, SCALE_X(10), textY,
                     SCALE_X(300), textHeight, SWP_NOZORDER);
        // 重新设置样式：保留 SS_EDITCONTROL 和 SS_MULTILINE（0x20L）
        SetWindowLongPtr(hVersionText, GWL_STYLE,
                           GetWindowLongPtr(hVersionText, GWL_STYLE) | SS_EDITCONTROL | 0x20L);
        SetWindowTextA(hVersionText, GetWindowsVersion());
    }

    if (HWND hExitButton = GetDlgItem(hWnd, 1)) {
        int buttonY, buttonWidth;
        if (hasSP) {
            // 有 SP 时，按钮下移且加宽
            buttonY = SCALE_Y(130);
            buttonWidth = SCALE_X(60);
        } else {
            // 无 SP 时，恢复原始尺寸
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

        // 创建静态文本框时不使用 SS_CENTERIMAGE，启用多行（SS_MULTILINE，值为 0x20L）
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
        MessageBoxA(nullptr, "窗口注册失败！", "错误", MB_ICONERROR);
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
        MessageBoxA(nullptr, "窗口创建失败！", "错误", MB_ICONERROR);
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