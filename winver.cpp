#include <windows.h>
#include <commctrl.h>
#include <winreg.h>
#include <cstdlib>
#include <string>
#include <cctype>
#include <cstdio>
#include <cstring>

// 删除原有的静态声明
// #ifndef SetWindowTheme
// extern "C" HRESULT WINAPI SetWindowTheme(HWND hwnd, LPCWSTR pszSubAppName, LPCWSTR pszSubIdList);
// #endif

// 添加 SetWindowTheme 动态加载包装函数
typedef HRESULT (WINAPI *PFN_SetWindowTheme)(HWND, LPCWSTR, LPCWSTR);

HRESULT SetWindowTheme(HWND hwnd, LPCWSTR pszSubAppName, LPCWSTR pszSubIdList) {
    // 静态变量保存加载结果，保证只加载一次
    static PFN_SetWindowTheme pfnSetWindowTheme = nullptr;
    static bool bInitialized = false;

    if (!bInitialized) {
        // 不使用 LOAD_LIBRARY_SEARCH_SYSTEM32，这样在 Win98 下也不会报错
        HMODULE hUxTheme = LoadLibraryW(L"uxtheme.dll");
        if (hUxTheme) {
            pfnSetWindowTheme = reinterpret_cast<PFN_SetWindowTheme>(GetProcAddress(hUxTheme, "SetWindowTheme"));
        }
        bInitialized = true;
    }
    if (pfnSetWindowTheme) {
        return pfnSetWindowTheme(hwnd, pszSubAppName, pszSubIdList);
    }
    // 加载失败或不存在时返回一个失败码，但不做任何改变
    return S_FALSE;
}

#if !defined(WM_DPICHANGED)
#define WM_DPICHANGED 0x02E0
#endif

#if !defined(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((HANDLE)-4)
#endif

// 定义沉浸式深色模式的系统属性（新版 Windows 使用）
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
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

// 全局 DPI 与字体变量
static UINT g_dpiX = BASE_DPI;
static UINT g_dpiY = BASE_DPI;
static HFONT g_hFont = nullptr;
static HBRUSH g_lightBrush = nullptr; 

// 以下全局变量用于支持深色模式
static bool g_darkModeEnabled = false;                   // 是否启用深色模式
static HBRUSH g_hDarkBrush = nullptr;                      // 深色背景画刷
static COLORREF g_darkTextColor = RGB(255, 255, 255);        // 深色模式文本颜色（白色）
static COLORREF g_darkBkColor = RGB(32, 32, 32);             // 深色模式背景颜色（深色）
static COLORREF g_lightBkColor = RGB(255, 255, 255);         // 浅色模式背景颜色（备用）

// 函数原型声明
bool IsModernUIAvailable();
void InitDPIScaling(HWND hWnd, bool forceSystemDPI = false);
void EnableModernFeatures();
void UpdateLayout(HWND hWnd);
HFONT CreateDPIFont();
void GetRealOSVersion(OSVERSIONINFOA* osvi);

// ----- 支持系统深色模式的代码开始 -----
//
// 检查系统是否支持深色模式，通过动态加载 uxtheme.dll 并获取相关 API 地址
bool IsDarkModeSupported()
{
    HMODULE hUxtheme = LoadLibraryExW(L"uxtheme.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (hUxtheme)
    {
        // 尝试获取启用深色模式相关函数
        auto AllowDarkModeForWindow = (BOOL(WINAPI*)(HWND, BOOL))GetProcAddress(hUxtheme, MAKEINTRESOURCEA(133));
        auto AllowDarkModeForApp = (BOOL(WINAPI*)(BOOL))GetProcAddress(hUxtheme, MAKEINTRESOURCEA(135));
        auto SetPreferredAppMode = (BOOL(WINAPI*)(int))GetProcAddress(hUxtheme, MAKEINTRESOURCEA(135));
        FreeLibrary(hUxtheme);
        return (AllowDarkModeForWindow && AllowDarkModeForApp && SetPreferredAppMode);
    }
    return false;
}

// 启用深色模式：通过动态加载函数以及 DwmSetWindowAttribute API 应用沉浸式深色模式
// 修改后的 EnableDarkMode 函数
void EnableDarkMode(HWND hwnd) {
    // 定义函数指针类型
    typedef BOOL (WINAPI *FnAllowDarkModeForWindow)(HWND, BOOL);
    typedef BOOL (WINAPI *FnAllowDarkModeForApp)(BOOL);
    typedef BOOL (WINAPI *FnSetPreferredAppMode)(int);
    typedef HRESULT (WINAPI *FnDwmSetWindowAttribute)(HWND, DWORD, LPCVOID, DWORD);
    
    HMODULE hUxtheme = LoadLibraryExW(L"uxtheme.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (hUxtheme) {
        FnAllowDarkModeForWindow pAllowDarkModeForWindow = 
            (FnAllowDarkModeForWindow)GetProcAddress(hUxtheme, MAKEINTRESOURCEA(133));
        FnAllowDarkModeForApp pAllowDarkModeForApp = 
            (FnAllowDarkModeForApp)GetProcAddress(hUxtheme, MAKEINTRESOURCEA(135));
        FnSetPreferredAppMode pSetPreferredAppMode = 
            (FnSetPreferredAppMode)GetProcAddress(hUxtheme, MAKEINTRESOURCEA(135));
        
        if (pAllowDarkModeForWindow && pAllowDarkModeForApp && pSetPreferredAppMode) {
            // 启用全局深色模式
            pAllowDarkModeForApp(TRUE);
            pSetPreferredAppMode(1); // 允许深色模式（1 = AllowDark）
            pAllowDarkModeForWindow(hwnd, TRUE);
            
            BOOL useDarkMode = TRUE;
            // 修正：动态加载 dwmapi.dll 中的 DwmSetWindowAttribute 函数以避免编译错误
            HMODULE hDwm = LoadLibraryA("dwmapi.dll");
            if (hDwm) {
                FnDwmSetWindowAttribute pDwmSetWindowAttribute = 
                    (FnDwmSetWindowAttribute)GetProcAddress(hDwm, "DwmSetWindowAttribute");
                if (pDwmSetWindowAttribute) {
                    pDwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &useDarkMode, sizeof(useDarkMode));
                }
                FreeLibrary(hDwm);
            }
        }
        FreeLibrary(hUxtheme);
    }
}

// 根据注册表设置和系统支持情况应用深色模式
// 修改 ApplyDarkModeSettings 函数
void ApplyDarkModeSettings(HWND hwnd)
{
    // 仅在现代系统中尝试（兼容 Win9x 至 Win11，旧系统不支持深色模式）
    if (!IsModernUIAvailable())
        return;
        
    // 保存旧的深色模式状态
    bool oldDarkModeEnabled = g_darkModeEnabled;
    
    HKEY hKey;
    // 从注册表读取 "Software\Microsoft\Windows\CurrentVersion\Themes\Personalize" 中的 AppsUseLightTheme 配置
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", 
                      0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        DWORD dwType = REG_DWORD;
        DWORD dwData = 0;
        DWORD dwSize = sizeof(DWORD);
        if (RegQueryValueExW(hKey, L"AppsUseLightTheme", nullptr, &dwType, (LPBYTE)&dwData, &dwSize) == ERROR_SUCCESS)
        {
            // 0 表示启用深色模式
            g_darkModeEnabled = (dwData == 0);
        }
        RegCloseKey(hKey);
    }
    
    // 如果深色模式状态发生变化，需要更新窗口
    if (g_darkModeEnabled != oldDarkModeEnabled) {
        // 删除旧的画刷
        if (g_lightBrush) {
            DeleteObject(g_lightBrush);
            g_lightBrush = nullptr;
        }
        if (g_hDarkBrush) {
            DeleteObject(g_hDarkBrush);
            g_hDarkBrush = nullptr;
        }

        if (g_darkModeEnabled)
        {
            // 启用深色模式
            EnableDarkMode(hwnd);
            
            // 创建深色背景画刷
            g_hDarkBrush = CreateSolidBrush(g_darkBkColor);
            
            // 修改窗口类背景，保证整个窗口背景为深色
            SetClassLongPtrA(hwnd, GCLP_HBRBACKGROUND, (LONG_PTR)g_hDarkBrush);
            
            // 对部分子控件应用深色模式主题，替换为 SetWindowTheme 调用
            HWND hStatic = GetDlgItem(hwnd, 0);
            if (hStatic)
                SetWindowTheme(hStatic, L"DarkMode_Explorer", nullptr);
            HWND hButton = GetDlgItem(hwnd, 1);
            if (hButton)
                SetWindowTheme(hButton, L"DarkMode_Explorer", nullptr);
        }
        else
        {
            // 恢复浅色模式
            BOOL useDarkMode = FALSE;
            HMODULE hDwm = LoadLibraryA("dwmapi.dll");
            if (hDwm) {
                typedef HRESULT (WINAPI *FnDwmSetWindowAttribute)(HWND, DWORD, LPCVOID, DWORD);
                FnDwmSetWindowAttribute pDwmSetWindowAttribute = 
                    (FnDwmSetWindowAttribute)GetProcAddress(hDwm, "DwmSetWindowAttribute");
                if (pDwmSetWindowAttribute) {
                    pDwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &useDarkMode, sizeof(useDarkMode));
                }
                FreeLibrary(hDwm);
            }
            
            // 恢复默认背景
            SetClassLongPtrA(hwnd, GCLP_HBRBACKGROUND, (LONG_PTR)(COLOR_BTNFACE + 1));
            
            // 创建浅色背景画刷
            g_lightBrush = CreateSolidBrush(GetSysColor(COLOR_BTNFACE));
            
            // 恢复控件默认主题，使用 SetWindowTheme 将主题置空
            HWND hStatic = GetDlgItem(hwnd, 0);
            if (hStatic)
                SetWindowTheme(hStatic, L"", nullptr);
            HWND hButton = GetDlgItem(hwnd, 1);
            if (hButton)
                SetWindowTheme(hButton, L"", nullptr);
        }
    }
}
//
// ----- 支持深色模式的代码结束 -----


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
        default: return "Unknown";
    }
}

const char* GetModernWindowsName(DWORD dwMajor, DWORD dwMinor, DWORD dwBuild) {
    
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
            else return "10";
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
    if (dwMajor = 4) {
        return isServerEdition ? "Windows NT 4.0 Server" : "Windows NT 4.0";
    }
    if (dwMajor = 3) {
        return isServerEdition ? "Windows NT 3 Server" : "Windows NT 3";
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
    const char* versionStr = GetWindowsVersion();
    bool isMultiline = (strstr(versionStr, "\r\n") != nullptr || strchr(versionStr, '\n') != nullptr);

    if (HWND hVersionText = GetDlgItem(hWnd, 0)) {
        int textHeight = isMultiline ? SCALE_Y(60) : SCALE_Y(30);
        int textY = isMultiline ? SCALE_Y(30) : SCALE_Y(40);
        
        SetWindowPos(hVersionText, nullptr, SCALE_X(10), textY,
                     SCALE_X(300), textHeight, SWP_NOZORDER);
        SetWindowLongPtr(hVersionText, GWL_STYLE,
                         WS_CHILD | WS_VISIBLE | SS_CENTER | SS_EDITCONTROL);
        SetWindowTextA(hVersionText, versionStr);
    }

    if (HWND hExitButton = GetDlgItem(hWnd, 1)) {
        int buttonY = isMultiline ? 
            (SCALE_Y(30) + SCALE_Y(60) + SCALE_Y(20)) : 
            SCALE_Y(100);
        int buttonWidth = isMultiline ? SCALE_X(60) : SCALE_X(50);

        SetWindowPos(hExitButton, nullptr, 
                    (SCALE_X(320) - buttonWidth) / 2,
                    buttonY,
                    buttonWidth, 
                    SCALE_Y(35), 
                    SWP_NOZORDER);
    }
    
    SetWindowPos(hWnd, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE: {
        // 创建控件及初始化等代码保持不变
        if (IsModernUIAvailable()) {
            BOOL fontSmoothing = TRUE;
            SystemParametersInfo(SPI_SETFONTSMOOTHING, TRUE, &fontSmoothing, 0);
            SystemParametersInfo(SPI_SETFONTSMOOTHINGTYPE, 0,
                                   (PVOID)FE_FONTSMOOTHINGCLEARTYPE, 0);
        }

        InitDPIScaling(hWnd);
        g_hFont = CreateDPIFont();

        CreateWindowA("Static", GetWindowsVersion(),
              WS_CHILD | WS_VISIBLE | SS_CENTER | SS_EDITCONTROL,
              SCALE_X(10), SCALE_Y(30), SCALE_X(300), SCALE_Y(60),
              hWnd, nullptr, nullptr, nullptr);

        // 统一使用 UpdateLayout 中的位置计算逻辑创建按钮
        bool isInitialMultiline = strstr(GetWindowsVersion(), "\r\n") != nullptr;
        int initialButtonY = isInitialMultiline ? 
            (SCALE_Y(30) + SCALE_Y(60) + SCALE_Y(20)) : 
            SCALE_Y(100);
        int initialButtonWidth = isInitialMultiline ? SCALE_X(60) : SCALE_X(50);

        CreateWindowA("Button", "OK", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                      (SCALE_X(320) - initialButtonWidth) / 2, initialButtonY,
                      initialButtonWidth, SCALE_Y(35), hWnd, (HMENU)1, nullptr, nullptr);

        if (g_hFont)
            EnumChildWindows(hWnd, SetChildFont, reinterpret_cast<LPARAM>(g_hFont));
            
        // 调用深色模式设置（若用户注册表启用了深色模式则自动应用）
        ApplyDarkModeSettings(hWnd);
        break;
    }
    case WM_DPICHANGED: {
        auto prcNewWindow = reinterpret_cast<RECT*>(lParam);
        SetWindowPos(hWnd, nullptr, prcNewWindow->left, prcNewWindow->top,
                     prcNewWindow->right - prcNewWindow->left,
                     prcNewWindow->bottom - prcNewWindow->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);

        // 修改处：正确设置水平与垂直 DPI 的顺序
        g_dpiX = LOWORD(wParam); // 低位为水平 DPI
        g_dpiY = HIWORD(wParam); // 高位为垂直 DPI
        
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
    case WM_CTLCOLORSTATIC: {
        HDC hdcStatic = reinterpret_cast<HDC>(wParam);
        if (g_darkModeEnabled) {
            SetTextColor(hdcStatic, g_darkTextColor);
            SetBkColor(hdcStatic, g_darkBkColor);
            SetBkMode(hdcStatic, TRANSPARENT);  // 新增：设置透明背景模式
            return (INT_PTR)g_hDarkBrush;
        } else {
            SetTextColor(hdcStatic, GetSysColor(COLOR_WINDOWTEXT));
            SetBkColor(hdcStatic, GetSysColor(COLOR_BTNFACE));
            SetBkMode(hdcStatic, OPAQUE);  // 新增：强制使用不透明背景
            return (INT_PTR)(g_lightBrush ? g_lightBrush : GetSysColorBrush(COLOR_BTNFACE));
        }
    }
    case WM_CTLCOLOREDIT: {
        HDC hdcEdit = reinterpret_cast<HDC>(wParam);
        if (g_darkModeEnabled) {
            SetTextColor(hdcEdit, g_darkTextColor);
            SetBkColor(hdcEdit, g_darkBkColor);
            SetBkMode(hdcEdit, TRANSPARENT);  // 新增：设置透明背景模式
            return (INT_PTR)g_hDarkBrush;
        } else {
            SetTextColor(hdcEdit, GetSysColor(COLOR_WINDOWTEXT));
            SetBkColor(hdcEdit, GetSysColor(COLOR_BTNFACE));
            SetBkMode(hdcEdit, OPAQUE);  // 新增：强制使用不透明背景
            return (INT_PTR)(g_lightBrush ? g_lightBrush : GetSysColorBrush(COLOR_BTNFACE));
        }
    }
    case WM_CTLCOLORBTN: {
        HDC hdcButton = reinterpret_cast<HDC>(wParam);
        if (g_darkModeEnabled)
        {
            SetTextColor(hdcButton, g_darkTextColor);
            SetBkColor(hdcButton, g_darkBkColor);
            return (INT_PTR)g_hDarkBrush;
        }
        else
        {
            SetTextColor(hdcButton, GetSysColor(COLOR_WINDOWTEXT));
            SetBkColor(hdcButton, GetSysColor(COLOR_BTNFACE));
            return (INT_PTR)g_lightBrush;
        }
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == 1)
            PostQuitMessage(0);
        break;
    case WM_DESTROY:
        if (g_hFont)
            DeleteObject(g_hFont);
        // 释放深色和浅色背景画刷，避免内存泄漏
        if (g_hDarkBrush) {
            DeleteObject(g_hDarkBrush);
            g_hDarkBrush = nullptr;
        }
        if (g_lightBrush) {
            DeleteObject(g_lightBrush);
            g_lightBrush = nullptr;
        }
        PostQuitMessage(0);
        break;
    case WM_SETTINGCHANGE: {
        // 检查是否是主题相关的设置更改
        if (lParam && lstrcmpiA((LPCSTR)lParam, "ImmersiveColorSet") == 0) {
            // 重新应用深色模式设置
            ApplyDarkModeSettings(hWnd);
            
            // 强制重绘整个窗口
            RedrawWindow(hWnd, nullptr, nullptr,
                RDW_ERASE | RDW_FRAME | RDW_INVALIDATE | RDW_ALLCHILDREN);
        }
        break;
    }
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

    char szTitle[128] = "Windows Version";
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                     "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
                     0, KEY_READ, &hKey) == ERROR_SUCCESS) {
       DWORD dwSize = sizeof(szTitle);
       RegQueryValueExA(hKey, "ProductName", nullptr, nullptr,
                        reinterpret_cast<LPBYTE>(szTitle), &dwSize);
       RegCloseKey(hKey);
    }
    if (strstr(szTitle, "Enterprise LTSC") != nullptr) {
        if (strstr(szTitle, "2024") != nullptr && strncmp(szTitle, "Windows 10", 10) == 0) {
            szTitle[8] = '1';
            szTitle[9] = '1';
        }
        char* pos = strstr(szTitle, "Enterprise LTSC");
        if (pos != nullptr) {
            memmove(pos, pos + 11, strlen(pos + 11) + 1);
        }
    }

    HWND hWnd = CreateWindowA(
        "WinVerClass", 
        szTitle,
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
