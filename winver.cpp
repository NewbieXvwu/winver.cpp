// 编译命令:
// windres --input-format=rc --output-format=coff --target=pe-i386 winver.rc -o winver.res
// i686-w64-mingw32-g++ -o winver.exe winver.cpp winver.res -mwindows -march=i486 -std=gnu++17 -D_WIN32_WINDOWS=0x0400 -Wl,--subsystem=windows -static-libgcc -static-libstdc++ -static -lcomctl32 -lkernel32 -luser32 -ladvapi32 -lgdi32 -lole32 -lshlwapi

#include <windows.h>
#include <commctrl.h>
#include <winreg.h>
#include <cstdlib>
#include <string>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <shlwapi.h>

#pragma comment(lib, "shlwapi.lib")

// GDI+ 函数指针类型和常量
typedef int Status;
typedef Status GpStatus;

#define Ok 0
#define InvalidParameter 2

struct GdiplusStartupInput {
    UINT32 GdiplusVersion;
    void* DebugEventCallback;
    BOOL SuppressBackgroundThread;
    BOOL SuppressExternalCodecs;
    GdiplusStartupInput() : GdiplusVersion(1), DebugEventCallback(NULL), SuppressBackgroundThread(FALSE), SuppressExternalCodecs(FALSE) {}
};

typedef Status (WINAPI *FuncGdiplusStartup)(ULONG_PTR*, const GdiplusStartupInput*, void*);
typedef void (WINAPI *FuncGdiplusShutdown)(ULONG_PTR);
typedef Status (WINAPI *FuncGdipCreateBitmapFromStream)(void*, void**);
typedef Status (WINAPI *FuncGdipGetImageWidth)(void*, UINT*);
typedef Status (WINAPI *FuncGdipGetImageHeight)(void*, UINT*);
typedef Status (WINAPI *FuncGdipDisposeImage)(void*);
typedef Status (WINAPI *FuncGdipCreateFromHDC)(HDC, void**);
typedef Status (WINAPI *FuncGdipSetSmoothingMode)(void*, int);
typedef Status (WINAPI *FuncGdipSetInterpolationMode)(void*, int);
typedef Status (WINAPI *FuncGdipDrawImageRectI)(void*, void*, int, int, int, int);
typedef Status (WINAPI *FuncGdipDeleteGraphics)(void*);

// 全局函数指针类型定义
typedef HRESULT (WINAPI *FnDwmSetWindowAttribute)(HWND, DWORD, LPCVOID, DWORD);
typedef UINT (WINAPI *GetDpiForWindowProc)(HWND);
typedef UINT (WINAPI *GetDpiForSystemProc)(void);
typedef BOOL (WINAPI *SetProcessDPIAwareProc)(void);
typedef HRESULT (WINAPI *SetProcessDPIAwarenessProc)(int);
typedef BOOL (WINAPI *SetProcessDPIAwarenessContextProc)(HANDLE);
typedef BOOL (WINAPI *IsWow64ProcessProc)(HANDLE, PBOOL);
typedef HRESULT (WINAPI *PFN_SetWindowTheme)(HWND, LPCWSTR, LPCWSTR);

static FuncGdiplusStartup GdiplusStartup = NULL;
static FuncGdiplusShutdown GdiplusShutdown = NULL;
static FuncGdipCreateBitmapFromStream GdipCreateBitmapFromStream = NULL;
static FuncGdipGetImageWidth GdipGetImageWidth = NULL;
static FuncGdipGetImageHeight GdipGetImageHeight = NULL;
static FuncGdipDisposeImage GdipDisposeImage = NULL;
static FuncGdipCreateFromHDC GdipCreateFromHDC = NULL;
static FuncGdipSetSmoothingMode GdipSetSmoothingMode = NULL;
static FuncGdipSetInterpolationMode GdipSetInterpolationMode = NULL;
static FuncGdipDrawImageRectI GdipDrawImageRectI = NULL;
static FuncGdipDeleteGraphics GdipDeleteGraphics = NULL;

enum SmoothingMode {
    SmoothingModeHighQuality = 2,
    SmoothingModeAntiAlias = 4
};

enum InterpolationMode {
    InterpolationModeHighQualityBicubic = 7
};

// SetWindowTheme 动态加载
static PFN_SetWindowTheme pfnSetWindowTheme = nullptr;

HRESULT SetWindowTheme(HWND hwnd, LPCWSTR pszSubAppName, LPCWSTR pszSubIdList) {
    static bool bInitialized = false;
    if (!bInitialized) {
        HMODULE hUxTheme = LoadLibraryA("uxtheme.dll");
        if (hUxTheme) {
            pfnSetWindowTheme = (PFN_SetWindowTheme)GetProcAddress(hUxTheme, "SetWindowTheme");
        }
        bInitialized = true;
    }
    if (pfnSetWindowTheme) {
        return pfnSetWindowTheme(hwnd, pszSubAppName, pszSubIdList);
    }
    return S_FALSE;
}

#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0
#endif

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

constexpr int BASE_DPI = 96;
#define SCALE_X(x) MulDiv(x, g_dpiX, BASE_DPI)
#define SCALE_Y(x) MulDiv(x, g_dpiY, BASE_DPI)

// 全局变量
static UINT g_dpiX = BASE_DPI;
static UINT g_dpiY = BASE_DPI;
static HFONT g_hFont = nullptr;
static HBRUSH g_lightBrush = nullptr;
static bool g_darkModeEnabled = false;
static HBRUSH g_hDarkBrush = nullptr;
static COLORREF g_darkTextColor = RGB(255, 255, 255);
static COLORREF g_darkBkColor = RGB(32, 32, 32);
static COLORREF g_lightBkColor = RGB(255, 255, 255);
static GdiplusStartupInput gdiplusStartupInput;
static ULONG_PTR gdiplusToken = 0;
static void* g_pLogoImage = nullptr;
static int g_logoWidth = 0;
static int g_logoHeight = 0;
static bool g_logoLoaded = false;
static bool g_gdiplusInitialized = false;
static HMODULE g_hGdiPlus = NULL;

// 函数声明
bool IsModernUIAvailable();
void InitDPIScaling(HWND hWnd, bool forceSystemDPI = false);
void EnableModernFeatures();
void UpdateLayout(HWND hWnd);
HFONT CreateDPIFont();
void GetRealOSVersion(OSVERSIONINFOA* osvi);
bool LoadWindowsLogo();
void DrawWindowsLogo(HDC hdc, int x, int y, int width, int height);
bool Is64BitOS();
bool CanLoad64BitModules();
bool InitializeGdiplus();
void ShutdownGdiplus();
void EnableDarkMode(HWND hwnd);
void ApplyDarkModeSettings(HWND hwnd);

bool IsDarkModeSupported() {
    HMODULE hUxtheme = LoadLibraryA("uxtheme.dll");
    if (hUxtheme) {
        auto AllowDarkModeForWindow = (BOOL(WINAPI*)(HWND, BOOL))GetProcAddress(hUxtheme, MAKEINTRESOURCEA(133));
        auto SetPreferredAppMode = (BOOL(WINAPI*)(int))GetProcAddress(hUxtheme, MAKEINTRESOURCEA(135));
        FreeLibrary(hUxtheme);
        return (AllowDarkModeForWindow && SetPreferredAppMode);
    }
    return false;
}

void EnableDarkMode(HWND hwnd) {
    typedef BOOL (WINAPI *FnAllowDarkModeForWindow)(HWND, BOOL);
    typedef BOOL (WINAPI *FnSetPreferredAppMode)(int);

    HMODULE hUxtheme = LoadLibraryA("uxtheme.dll");
    if (hUxtheme) {
        FnAllowDarkModeForWindow pAllowDarkModeForWindow = (FnAllowDarkModeForWindow)GetProcAddress(hUxtheme, MAKEINTRESOURCEA(133));
        FnSetPreferredAppMode pSetPreferredAppMode = (FnSetPreferredAppMode)GetProcAddress(hUxtheme, MAKEINTRESOURCEA(135));

        if (pAllowDarkModeForWindow && pSetPreferredAppMode) {
            pSetPreferredAppMode(1); // AllowDark
            pAllowDarkModeForWindow(hwnd, TRUE);

            BOOL useDarkMode = TRUE;
            HMODULE hDwm = LoadLibraryA("dwmapi.dll");
            if (hDwm) {
                FnDwmSetWindowAttribute pDwmSetWindowAttribute = (FnDwmSetWindowAttribute)GetProcAddress(hDwm, "DwmSetWindowAttribute");
                if (pDwmSetWindowAttribute) {
                    pDwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &useDarkMode, sizeof(useDarkMode));
                }
                FreeLibrary(hDwm);
            }
        }
        FreeLibrary(hUxtheme);
    }
}

void ApplyDarkModeSettings(HWND hwnd) {
    if (!IsModernUIAvailable()) return;

    bool oldDarkModeEnabled = g_darkModeEnabled;
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD dwType = REG_DWORD, dwData = 0, dwSize = sizeof(DWORD);
        if (RegQueryValueExA(hKey, "AppsUseLightTheme", nullptr, &dwType, (LPBYTE)&dwData, &dwSize) == ERROR_SUCCESS) {
            g_darkModeEnabled = (dwData == 0);
        }
        RegCloseKey(hKey);
    }

    if (g_darkModeEnabled != oldDarkModeEnabled) {
        if (g_lightBrush) { DeleteObject(g_lightBrush); g_lightBrush = nullptr; }
        if (g_hDarkBrush) { DeleteObject(g_hDarkBrush); g_hDarkBrush = nullptr; }

        if (g_darkModeEnabled) {
            EnableDarkMode(hwnd);
            g_hDarkBrush = CreateSolidBrush(g_darkBkColor);
            SetClassLongPtrA(hwnd, GCLP_HBRBACKGROUND, (LONG_PTR)g_hDarkBrush);

            HWND hStatic = GetDlgItem(hwnd, 0);
            if (hStatic) SetWindowTheme(hStatic, L"DarkMode_Explorer", nullptr);
            HWND hButton = GetDlgItem(hwnd, 1);
            if (hButton) SetWindowTheme(hButton, L"DarkMode_Explorer", nullptr);
        } else {
            BOOL useDarkMode = FALSE;
            HMODULE hDwm = LoadLibraryA("dwmapi.dll");
            if (hDwm) {
                FnDwmSetWindowAttribute pDwmSetWindowAttribute = (FnDwmSetWindowAttribute)GetProcAddress(hDwm, "DwmSetWindowAttribute");
                if (pDwmSetWindowAttribute) {
                    pDwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &useDarkMode, sizeof(useDarkMode));
                }
                FreeLibrary(hDwm);
            }
            SetClassLongPtrA(hwnd, GCLP_HBRBACKGROUND, (LONG_PTR)(COLOR_BTNFACE + 1));
            g_lightBrush = CreateSolidBrush(g_lightBkColor);

            HWND hStatic = GetDlgItem(hwnd, 0);
            if (hStatic) SetWindowTheme(hStatic, L"", nullptr);
            HWND hButton = GetDlgItem(hwnd, 1);
            if (hButton) SetWindowTheme(hButton, L"", nullptr);
        }
        InvalidateRect(hwnd, nullptr, TRUE);
    }
}

bool InitializeGdiplus() {
    if (g_gdiplusInitialized) return true;

    OSVERSIONINFOA osvi = { sizeof(OSVERSIONINFOA) };
    GetRealOSVersion(&osvi);
    if (osvi.dwMajorVersion < 10) return false;

    g_hGdiPlus = LoadLibraryA("gdiplus.dll");
    if (!g_hGdiPlus) return false;

    GdiplusStartup = (FuncGdiplusStartup)GetProcAddress(g_hGdiPlus, "GdiplusStartup");
    GdiplusShutdown = (FuncGdiplusShutdown)GetProcAddress(g_hGdiPlus, "GdiplusShutdown");
    GdipCreateBitmapFromStream = (FuncGdipCreateBitmapFromStream)GetProcAddress(g_hGdiPlus, "GdipCreateBitmapFromStream");
    GdipGetImageWidth = (FuncGdipGetImageWidth)GetProcAddress(g_hGdiPlus, "GdipGetImageWidth");
    GdipGetImageHeight = (FuncGdipGetImageHeight)GetProcAddress(g_hGdiPlus, "GdipGetImageHeight");
    GdipDisposeImage = (FuncGdipDisposeImage)GetProcAddress(g_hGdiPlus, "GdipDisposeImage");
    GdipCreateFromHDC = (FuncGdipCreateFromHDC)GetProcAddress(g_hGdiPlus, "GdipCreateFromHDC");
    GdipSetSmoothingMode = (FuncGdipSetSmoothingMode)GetProcAddress(g_hGdiPlus, "GdipSetSmoothingMode");
    GdipSetInterpolationMode = (FuncGdipSetInterpolationMode)GetProcAddress(g_hGdiPlus, "GdipSetInterpolationMode");
    GdipDrawImageRectI = (FuncGdipDrawImageRectI)GetProcAddress(g_hGdiPlus, "GdipDrawImageRectI");
    GdipDeleteGraphics = (FuncGdipDeleteGraphics)GetProcAddress(g_hGdiPlus, "GdipDeleteGraphics");

    if (!GdiplusStartup || !GdiplusShutdown || !GdipCreateBitmapFromStream || !GdipGetImageWidth ||
        !GdipGetImageHeight || !GdipDisposeImage || !GdipCreateFromHDC || !GdipSetSmoothingMode ||
        !GdipSetInterpolationMode || !GdipDrawImageRectI || !GdipDeleteGraphics) {
        FreeLibrary(g_hGdiPlus);
        g_hGdiPlus = NULL;
        return false;
    }

    Status status = GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);
    if (status != Ok) {
        FreeLibrary(g_hGdiPlus);
        g_hGdiPlus = NULL;
        return false;
    }

    g_gdiplusInitialized = true;
    return true;
}

void ShutdownGdiplus() {
    if (g_logoLoaded && g_pLogoImage && GdipDisposeImage) {
        GdipDisposeImage(g_pLogoImage);
        g_pLogoImage = nullptr;
        g_logoLoaded = false;
    }
    if (g_gdiplusInitialized && gdiplusToken && GdiplusShutdown) {
        GdiplusShutdown(gdiplusToken);
        gdiplusToken = 0;
        g_gdiplusInitialized = false;
    }
    if (g_hGdiPlus) {
        FreeLibrary(g_hGdiPlus);
        g_hGdiPlus = NULL;
        GdiplusStartup = NULL;
        GdiplusShutdown = NULL;
        GdipCreateBitmapFromStream = NULL;
        GdipGetImageWidth = NULL;
        GdipGetImageHeight = NULL;
        GdipDisposeImage = NULL;
        GdipCreateFromHDC = NULL;
        GdipSetSmoothingMode = NULL;
        GdipSetInterpolationMode = NULL;
        GdipDrawImageRectI = NULL;
        GdipDeleteGraphics = NULL;
    }
}

bool LoadWindowsLogo() {
    if (g_logoLoaded && g_pLogoImage) return true;
    if (!InitializeGdiplus()) return false;

    char szBasebrdPath[MAX_PATH] = "C:\\Windows\\Branding\\Basebrd\\basebrd.dll";
    HMODULE hBasebrd = LoadLibraryA(szBasebrdPath);
    if (!hBasebrd) {
        char szSystemDir[MAX_PATH] = {0};
        GetSystemDirectoryA(szSystemDir, MAX_PATH);
        PathCombineA(szBasebrdPath, szSystemDir, "..\\Branding\\Basebrd\\basebrd.dll");
        hBasebrd = LoadLibraryA(szBasebrdPath);
        if (!hBasebrd) {
            char szWindowsDir[MAX_PATH] = {0};
            GetWindowsDirectoryA(szWindowsDir, MAX_PATH);
            PathCombineA(szBasebrdPath, szWindowsDir, "Branding\\Basebrd\\basebrd.dll");
            hBasebrd = LoadLibraryA(szBasebrdPath);
            if (!hBasebrd) return false;
        }
    }

    HRSRC hResInfo = FindResourceA(hBasebrd, MAKEINTRESOURCEA(2123), "IMAGE");
    if (!hResInfo) hResInfo = FindResourceA(hBasebrd, MAKEINTRESOURCEA(2123), "PNG");
    if (!hResInfo) hResInfo = FindResourceA(hBasebrd, MAKEINTRESOURCEA(2123), MAKEINTRESOURCEA(10));
    if (!hResInfo) { FreeLibrary(hBasebrd); return false; }

    HGLOBAL hResData = LoadResource(hBasebrd, hResInfo);
    if (!hResData) { FreeLibrary(hBasebrd); return false; }

    DWORD dwSize = SizeofResource(hBasebrd, hResInfo);
    if (dwSize == 0) { FreeLibrary(hBasebrd); return false; }

    void* pResourceData = LockResource(hResData);
    if (!pResourceData) { FreeLibrary(hBasebrd); return false; }

    HGLOBAL hBuffer = GlobalAlloc(GMEM_MOVEABLE, dwSize);
    if (hBuffer) {
        void* pBuffer = GlobalLock(hBuffer);
        if (pBuffer) {
            CopyMemory(pBuffer, pResourceData, dwSize);
            GlobalUnlock(hBuffer);
            IStream* pStream = nullptr;
            if (SUCCEEDED(CreateStreamOnHGlobal(hBuffer, TRUE, &pStream))) {
                Status status = GdipCreateBitmapFromStream(pStream, &g_pLogoImage);
                pStream->Release();
                if (g_pLogoImage && status == Ok) {
                    GdipGetImageWidth(g_pLogoImage, (UINT*)&g_logoWidth);
                    GdipGetImageHeight(g_pLogoImage, (UINT*)&g_logoHeight);
                    g_logoLoaded = true;
                    FreeLibrary(hBasebrd);
                    return true;
                }
                if (g_pLogoImage) { GdipDisposeImage(g_pLogoImage); g_pLogoImage = nullptr; }
            }
        }
        GlobalFree(hBuffer);
    }
    FreeLibrary(hBasebrd);
    return false;
}

void DrawWindowsLogo(HDC hdc, int x, int y, int width, int height) {
    if (!g_logoLoaded || !g_pLogoImage || !g_gdiplusInitialized) return;

    void* graphics = nullptr;
    if (GdipCreateFromHDC(hdc, &graphics) != Ok || !graphics) return;

    GdipSetSmoothingMode(graphics, SmoothingModeHighQuality);
    GdipSetInterpolationMode(graphics, InterpolationModeHighQualityBicubic);

    float aspectRatio = (float)g_logoWidth / (float)g_logoHeight;
    int drawWidth = width;
    int drawHeight = (int)(width / aspectRatio);
    if (drawHeight > height) {
        drawHeight = height;
        drawWidth = (int)(height * aspectRatio);
    }
    int drawX = x + (width - drawWidth) / 2;
    int drawY = y + (height - drawHeight) / 2;

    GdipDrawImageRectI(graphics, g_pLogoImage, drawX, drawY, drawWidth, drawHeight);
    GdipDeleteGraphics(graphics);
}

bool IsServerEdition() {
    HKEY hKey;
    char installType[32] = {0};
    DWORD dwSize = sizeof(installType);
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        if (RegQueryValueExA(hKey, "InstallationType", nullptr, nullptr, (LPBYTE)installType, &dwSize) == ERROR_SUCCESS) {
            RegCloseKey(hKey);
            return (strstr(installType, "Server") != nullptr);
        }
        RegCloseKey(hKey);
    }
    return false;
}

BOOL CALLBACK SetChildFont(HWND hWndChild, LPARAM lParam) {
    SendMessageA(hWndChild, WM_SETFONT, (WPARAM)lParam, TRUE);
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
    bool isServer = IsServerEdition();
    if (dwMajor == 10) {
        if (isServer) {
            static char sServerName[128] = {0};
            char productName[128] = {0};
            DWORD dwSize = sizeof(productName);
            HKEY hKey;
            if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
                RegQueryValueExA(hKey, "ProductName", nullptr, nullptr, (LPBYTE)productName, &dwSize);
                RegCloseKey(hKey);
            }
            strncpy(sServerName, productName, sizeof(sServerName) - 1);
            return sServerName;
        }
        return (dwBuild >= 22000) ? "11" : "10";
    }
    static char sProductName[128] = {0};
    char productName[128] = {0};
    DWORD dwSize = sizeof(productName);
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        RegQueryValueExA(hKey, "ProductName", nullptr, nullptr, (LPBYTE)productName, &dwSize);
        RegCloseKey(hKey);
    }
    strncpy(sProductName, productName, sizeof(sProductName) - 1);
    return sProductName;
}

char* GetWindowsVersion() {
    static char szVersion[256] = "";
    OSVERSIONINFOA osvi = { sizeof(OSVERSIONINFOA) };
    GetRealOSVersion(&osvi); // 使用 GetVersionExA 获取真实版本

    char spBuffer[64] = "";
    if (osvi.szCSDVersion[0] != '\0') {
        // 尝试提取数字 SP 版本
        const char* p = osvi.szCSDVersion;
        while (*p && !isdigit(*p)) ++p; // 移动到第一个数字
        if (*p && *p != '0') { // 确保找到非零数字
            sprintf(spBuffer, " SP%s", p);
        } else if (osvi.szCSDVersion[0] != '\0') {
             // 如果没有数字，但有 CSDVersion，则直接附加（可能是一些非标准描述）
             // 为避免显示 " Service Pack "，可以稍微清理一下
             if (strncmp(osvi.szCSDVersion, "Service Pack ", 13) == 0 && isdigit(osvi.szCSDVersion[13])) {
                 sprintf(spBuffer, " SP%c", osvi.szCSDVersion[13]); // 提取单个数字 SP
             } else {
                 // 对于非常规或非数字SP（虽然不常见），直接附加可能不是最佳选择
                 // 安全起见，只处理已知模式
                 // sprintf(spBuffer, " %s", osvi.szCSDVersion); // 如果需要显示完整 CSDVersion
             }
        }
    }

    if (osvi.dwPlatformId == VER_PLATFORM_WIN32_WINDOWS) {
        // Windows 9x/Me 分支 (保持不变)
        const char* product = GetWin9xProductName(osvi.dwMinorVersion, osvi.dwBuildNumber);
        sprintf(szVersion, "Windows %s%s\r\n(Build %d.%02d.%04d)", product, spBuffer, osvi.dwMajorVersion, osvi.dwMinorVersion, osvi.dwBuildNumber & 0xFFFF);
    } else if (osvi.dwPlatformId == VER_PLATFORM_WIN32_NT) {
        // Windows NT 分支
        const char* baseProductName = nullptr;
        bool isServer = IsServerEdition(); // 检查是否为服务器版本

        // --- 核心修改：添加对旧版本的显式检查 ---
        if (osvi.dwMajorVersion == 4 && osvi.dwMinorVersion == 0) {
            baseProductName = "NT 4.0"; // <--- 修复 NT 4.0 的关键
        } else if (osvi.dwMajorVersion == 5 && osvi.dwMinorVersion == 0) {
            baseProductName = "2000";
        } else if (osvi.dwMajorVersion == 5 && osvi.dwMinorVersion == 1) {
            baseProductName = "XP";
        } else if (osvi.dwMajorVersion == 5 && osvi.dwMinorVersion == 2) {
            // 5.2 可能是 XP Professional x64 Edition 或 Server 2003 / R2
            // GetSystemMetrics(SM_SERVERR2) != 0 表示 R2
            // 通过 IsServerEdition() 进一步区分
             baseProductName = isServer ? "Server 2003" : "XP x64"; // 简化处理，优先考虑服务器，否则视为XP (x64)
        } else if (osvi.dwMajorVersion == 6 && osvi.dwMinorVersion == 0) {
            baseProductName = isServer ? "Server 2008" : "Vista";
        } else if (osvi.dwMajorVersion == 6 && osvi.dwMinorVersion == 1) {
            baseProductName = isServer ? "Server 2008 R2" : "7";
        } else if (osvi.dwMajorVersion == 6 && osvi.dwMinorVersion == 2) {
            baseProductName = isServer ? "Server 2012" : "8";
        } else if (osvi.dwMajorVersion == 6 && osvi.dwMinorVersion == 3) {
            baseProductName = isServer ? "Server 2012 R2" : "8.1";
        }
        // --- 核心修改结束 ---

        // 获取 UBR (主要用于 Win 10+)
        DWORD ubr = 0;
        if (osvi.dwMajorVersion >= 10) {
            HKEY hKey;
            if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
                DWORD dwSize = sizeof(DWORD);
                RegQueryValueExA(hKey, "UBR", nullptr, nullptr, (LPBYTE)&ubr, &dwSize);
                RegCloseKey(hKey);
            }
        }

        char ubrBuffer[20] = "";
        if (ubr > 0) {
            sprintf(ubrBuffer, ".%d", ubr);
        }


        if (baseProductName != nullptr) {
            // 如果我们通过版本号确定了名称 (适用于 NT 4.0 到 8.1)
            sprintf(szVersion, "Windows %s%s\r\n(Build %d%s)",
                    baseProductName,
                    spBuffer, // Service Pack 信息
                    osvi.dwBuildNumber,
                    (osvi.dwMajorVersion >= 10) ? ubrBuffer : ""); // 仅为 Win 10+ 添加 UBR
        } else {
            // 回退到现代逻辑 (适用于 Win 10+ 或未明确处理的旧版本)
            // 使用 GetModernWindowsName 获取注册表中的 ProductName (如 "10", "11" 或 详细服务器名)
            const char* product = GetModernWindowsName(osvi.dwMajorVersion, osvi.dwMinorVersion, osvi.dwBuildNumber);

            if (osvi.dwMajorVersion >= 10 && !isServer) {
                // 对于 Windows 10/11 客户端，尝试获取 DisplayVersion 或 ReleaseId
                char displayVersion[64] = "";
                HKEY hKey;
                if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
                    DWORD dwSize = sizeof(displayVersion);
                    // 优先 DisplayVersion (如 "22H2")
                    if (RegQueryValueExA(hKey, "DisplayVersion", nullptr, nullptr, (LPBYTE)displayVersion, &dwSize) != ERROR_SUCCESS || displayVersion[0] == '\0') {
                        // 回退到 ReleaseId (如 "2009")
                         dwSize = sizeof(displayVersion); // 重置大小
                        RegQueryValueExA(hKey, "ReleaseId", nullptr, nullptr, (LPBYTE)displayVersion, &dwSize);
                    }
                    RegCloseKey(hKey);
                }

                if (displayVersion[0] != '\0') {
                    // 格式: Windows 10/11 <DisplayVersion> SP<x> Build <build>.<ubr>
                    sprintf(szVersion, "Windows %s %s%s\r\n(Build %d%s)",
                            product, // "10" or "11"
                            displayVersion,
                            spBuffer, // SP (理论上 Win10+ 没有传统 SP)
                            osvi.dwBuildNumber,
                            ubrBuffer);
                } else {
                    // 如果 DisplayVersion/ReleaseId 都获取失败 (不太可能)
                    sprintf(szVersion, "Windows %s%s\r\n(Build %d%s)",
                            product,
                            spBuffer,
                            osvi.dwBuildNumber,
                            ubrBuffer);
                }
            } else {
                // 对于服务器版本 (10+) 或其他未处理情况 (使用注册表 ProductName)
                 // 格式: Windows <ProductName from Registry> SP<x> Build <build>.<ubr>
                sprintf(szVersion, "Windows %s%s\r\n(Build %d%s)",
                        product, // 来自 GetModernWindowsName (注册表 ProductName)
                        spBuffer,
                        osvi.dwBuildNumber,
                        (osvi.dwMajorVersion >= 10) ? ubrBuffer : ""); // 仅为 Win 10+ 添加 UBR
            }
        }
    } else {
        // 未知平台
        sprintf(szVersion, "Unknown Windows Platform %d\r\n(Version %d.%d Build %d%s)",
                osvi.dwPlatformId, osvi.dwMajorVersion, osvi.dwMinorVersion, osvi.dwBuildNumber, spBuffer);
    }

    return szVersion;
}

void GetRealOSVersion(OSVERSIONINFOA* osvi) {
    osvi->dwOSVersionInfoSize = sizeof(OSVERSIONINFOA);
    GetVersionExA(osvi);
}

bool Is64BitOS() {
    OSVERSIONINFOA osvi = { sizeof(OSVERSIONINFOA) };
    GetRealOSVersion(&osvi);
    if (osvi.dwPlatformId == VER_PLATFORM_WIN32_WINDOWS) return false;

    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    return (sysInfo.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64 || sysInfo.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_IA64);
}

bool CanLoad64BitModules() {
    OSVERSIONINFOA osvi = { sizeof(OSVERSIONINFOA) };
    GetRealOSVersion(&osvi);
    if (osvi.dwPlatformId == VER_PLATFORM_WIN32_WINDOWS) return true;

    HMODULE hKernel32 = GetModuleHandleA("kernel32.dll");
    if (hKernel32) {
        IsWow64ProcessProc fnIsWow64Process = (IsWow64ProcessProc)GetProcAddress(hKernel32, "IsWow64Process");
        if (fnIsWow64Process) {
            BOOL bIsWow64 = FALSE;
            fnIsWow64Process(GetCurrentProcess(), &bIsWow64);
            return !bIsWow64;
        }
    }
    return true;
}

void InitDPIScaling(HWND hWnd, bool forceSystemDPI) {
    HMODULE hUser32 = LoadLibraryA("user32.dll");
    if (hUser32 && !forceSystemDPI) {
        GetDpiForWindowProc pGetDpiForWindow = (GetDpiForWindowProc)GetProcAddress(hUser32, "GetDpiForWindow");
        GetDpiForSystemProc pGetDpiForSystem = (GetDpiForSystemProc)GetProcAddress(hUser32, "GetDpiForSystem");
        if (pGetDpiForWindow) {
            g_dpiX = g_dpiY = pGetDpiForWindow(hWnd);
        } else if (pGetDpiForSystem) {
            g_dpiX = g_dpiY = pGetDpiForSystem();
        }
        FreeLibrary(hUser32);
    }
    // Fallback or forced system DPI
    if (g_dpiX == 0 || g_dpiY == 0 || g_dpiX == BASE_DPI || forceSystemDPI) {
        HDC hdc = GetDC(nullptr);
        g_dpiX = GetDeviceCaps(hdc, LOGPIXELSX);
        g_dpiY = GetDeviceCaps(hdc, LOGPIXELSY);
        ReleaseDC(nullptr, hdc);
        // Ensure DPI is at least BASE_DPI
        if (g_dpiX == 0) g_dpiX = BASE_DPI;
        if (g_dpiY == 0) g_dpiY = BASE_DPI;
    }
}


void EnableModernFeatures() {
    HMODULE hUser32 = LoadLibraryA("user32.dll");
    if (hUser32) {
        SetProcessDPIAwarenessContextProc pSetContext = (SetProcessDPIAwarenessContextProc)GetProcAddress(hUser32, "SetProcessDpiAwarenessContext");
        if (pSetContext) {
            // Try DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 first
            if (!pSetContext((HANDLE)-4)) {
                // Fallback to DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE
                 pSetContext((HANDLE)-3);
            }
            FreeLibrary(hUser32);
            return;
        }
         // Fallback for older systems (Vista+)
        SetProcessDPIAwareProc pSetDPIAware = (SetProcessDPIAwareProc)GetProcAddress(hUser32, "SetProcessDPIAware");
        if (pSetDPIAware) pSetDPIAware();
        FreeLibrary(hUser32);
    }
}

bool IsModernUIAvailable() {
    OSVERSIONINFOA osvi = { sizeof(OSVERSIONINFOA) };
    GetRealOSVersion(&osvi);
    return (osvi.dwMajorVersion >= 6);
}

HFONT CreateDPIFont() {
    // Slightly smaller font size for better fit, adjust 18 if needed
    int fontSize = -MulDiv(17, g_dpiY, 96);
    const char* fontFace = IsModernUIAvailable() ? "Segoe UI" : "MS Sans Serif";
    return CreateFontA(fontSize, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, fontFace);
}

void UpdateLayout(HWND hWnd) {
    const char* versionStr = GetWindowsVersion();
    bool isMultiline = (strstr(versionStr, "\r\n") != nullptr);
    // *** MODIFIED: Increased Y offset for logo version ***
    int textYOffset = g_logoLoaded ? SCALE_Y(105) : SCALE_Y(30); // Changed 90 to 105 for logo

    HWND hVersionText = GetDlgItem(hWnd, 0); // Assumes Static control has ID 0
    if (hVersionText) {
        // Adjust text height and Y based on multiline status and offset
        int textHeight = isMultiline ? SCALE_Y(50) : SCALE_Y(30); // Reduced height slightly for multi
        int textY = textYOffset + (isMultiline ? SCALE_Y(5) : SCALE_Y(15)); // Adjusted Y spacing
        SetWindowPos(hVersionText, nullptr, SCALE_X(10), textY, SCALE_X(300), textHeight, SWP_NOZORDER);
        SetWindowTextA(hVersionText, versionStr);
    }

    HWND hExitButton = GetDlgItem(hWnd, 1); // Assumes Button has ID 1
    if (hExitButton) {
         // Adjust button Y based on multiline status and offset
        int buttonY = textYOffset + (isMultiline ? SCALE_Y(70) : SCALE_Y(60)); // Adjusted Y spacing
        int buttonWidth = SCALE_X(60); // Standardized button width slightly
        int buttonHeight = SCALE_Y(30); // Standardized button height slightly
        SetWindowPos(hExitButton, nullptr, (SCALE_X(320) - buttonWidth) / 2, buttonY, buttonWidth, buttonHeight, SWP_NOZORDER);
    }
}


LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        InitDPIScaling(hWnd);
        g_hFont = CreateDPIFont();
        // *** MODIFIED: Increased Y offset for logo version and added HMENU IDs ***
        // Create Static text (ID 0)
        CreateWindowA("Static", "", WS_CHILD | WS_VISIBLE | SS_CENTER,
                      SCALE_X(10), g_logoLoaded ? SCALE_Y(105) : SCALE_Y(30), // Y: 105 for logo, 30 for no logo
                      SCALE_X(300), SCALE_Y(60), hWnd, (HMENU)0, nullptr, nullptr);
        // Create OK Button (ID 1)
        CreateWindowA("Button", "OK", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                      SCALE_X(135), g_logoLoaded ? SCALE_Y(185) : SCALE_Y(110), // Y: 185 for logo, 110 for no logo
                      SCALE_X(50), SCALE_Y(35), hWnd, (HMENU)1, nullptr, nullptr);

        // *** ADDED: Call UpdateLayout immediately after creating controls ***
        UpdateLayout(hWnd); // Set text and final positions

        if (g_hFont) EnumChildWindows(hWnd, SetChildFont, (LPARAM)g_hFont);
        ApplyDarkModeSettings(hWnd);
        break;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT rcClient;
        GetClientRect(hWnd, &rcClient);
        FillRect(hdc, &rcClient, g_darkModeEnabled ? g_hDarkBrush : (HBRUSH)(COLOR_BTNFACE + 1));
        // Draw logo slightly higher and give it consistent space
        if (g_logoLoaded) DrawWindowsLogo(hdc, SCALE_X(10), SCALE_Y(10), rcClient.right - SCALE_X(20), SCALE_Y(85)); // Adjusted position/size
        EndPaint(hWnd, &ps);
        return 0;
    }
    case WM_DPICHANGED: {
        RECT* prcNewWindow = (RECT*)lParam;
        SetWindowPos(hWnd, nullptr, prcNewWindow->left, prcNewWindow->top, prcNewWindow->right - prcNewWindow->left, prcNewWindow->bottom - prcNewWindow->top, SWP_NOZORDER | SWP_NOACTIVATE);
        // Re-initialize DPI based on the new window's DPI
        // InitDPIScaling(hWnd); // Use the provided DPI instead
        g_dpiX = LOWORD(wParam);
        g_dpiY = HIWORD(wParam);

        if (g_hFont) DeleteObject(g_hFont);
        g_hFont = CreateDPIFont();
        UpdateLayout(hWnd); // Update layout based on new DPI
        if (g_hFont) EnumChildWindows(hWnd, SetChildFont, (LPARAM)g_hFont);
        RedrawWindow(hWnd, nullptr, nullptr, RDW_ERASE | RDW_INVALIDATE | RDW_ALLCHILDREN);
        return 0;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT: {
        HDC hdcStatic = (HDC)wParam;
        SetTextColor(hdcStatic, g_darkModeEnabled ? g_darkTextColor : GetSysColor(COLOR_WINDOWTEXT));
        SetBkColor(hdcStatic, g_darkModeEnabled ? g_darkBkColor : GetSysColor(COLOR_BTNFACE));
        return (LRESULT)(g_darkModeEnabled ? g_hDarkBrush : (HBRUSH)(COLOR_BTNFACE + 1)); // Use standard brush for light mode
    }
    case WM_CTLCOLORBTN: {
        // Let buttons draw themselves for modern themes, especially in light mode
         if (g_darkModeEnabled) {
             HDC hdcBtn = (HDC)wParam;
             SetTextColor(hdcBtn, g_darkTextColor);
             SetBkColor(hdcBtn, g_darkBkColor);
             return (LRESULT)g_hDarkBrush;
         }
         // For light mode, return NULL to allow default button drawing
         return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
        // return DefWindowProc(hWnd, message, wParam, lParam); // Alternative: forward to default processing
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == 1) PostQuitMessage(0); // ID 1 is the OK button
        break;
    case WM_DESTROY:
        if (g_hFont) DeleteObject(g_hFont);
        if (g_hDarkBrush) DeleteObject(g_hDarkBrush);
        if (g_lightBrush) DeleteObject(g_lightBrush); // Clean up light brush if created
        ShutdownGdiplus();
        PostQuitMessage(0);
        break;
    case WM_SETTINGCHANGE:
        // Check specifically for theme/color changes
        if (lParam && (_stricmp((LPCSTR)lParam, "ImmersiveColorSet") == 0 || _stricmp((LPCSTR)lParam, "ThemeChanged") == 0)) {
             // Re-apply dark mode settings which includes checking the registry again
            bool wasDarkMode = g_darkModeEnabled;
            ApplyDarkModeSettings(hWnd);
             // Force redraw only if mode actually changed or if it's dark mode (to apply theme correctly)
            if (wasDarkMode != g_darkModeEnabled || g_darkModeEnabled) {
                 InvalidateRect(hWnd, nullptr, TRUE);
                 UpdateWindow(hWnd); // Ensure immediate redraw
            }
        }
        // Also handle system font changes if necessary
        // if (wParam == SPI_SETNONCLIENTMETRICS || wParam == SPI_SETFONTSMOOTHING) {
        //    if (g_hFont) DeleteObject(g_hFont);
        //    g_hFont = CreateDPIFont();
        //    EnumChildWindows(hWnd, SetChildFont, (LPARAM)g_hFont);
        //    InvalidateRect(hWnd, nullptr, TRUE);
        // }
        break;
    default:
        return DefWindowProcA(hWnd, message, wParam, lParam);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    EnableModernFeatures(); // Call this early

    // Create a temporary window to get initial DPI before creating main window
    HWND hWndDummy = CreateWindowA("Static", nullptr, 0, 0, 0, 0, 0, nullptr, nullptr, hInstance, nullptr);
    if (hWndDummy) {
        InitDPIScaling(hWndDummy, false); // Use per-monitor DPI if available for initial scaling
        DestroyWindow(hWndDummy);
    } else {
        InitDPIScaling(nullptr, true); // Fallback to system DPI if dummy window fails
    }

    LoadWindowsLogo(); // Attempt to load the logo

    // *** MODIFIED: Increased height for logo version ***
    RECT rc = {0, 0, SCALE_X(320), g_logoLoaded ? SCALE_Y(240) : SCALE_Y(160)}; // Height: 240 for logo, 160 for no logo
    // Use WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX for a standard non-resizable window
    DWORD dwStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    AdjustWindowRect(&rc, dwStyle, FALSE); // Adjust rect for the specified style

    // Initialize Common Controls if needed (though not strictly required for Static/Button)
    // if (IsModernUIAvailable()) {
    //     INITCOMMONCONTROLSEX icc = { sizeof(INITCOMMONCONTROLSEX), ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES };
    //     InitCommonControlsEx(&icc);
    // }

    WNDCLASSA wc = { 0 }; // Initialize all members to zero/NULL
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION); // Load standard icon
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); // Default background
    wc.lpszClassName = "WinVerClass";
    if (!RegisterClassA(&wc)) {
        MessageBoxA(nullptr, "Window Registration Failed!", "Error", MB_ICONEXCLAMATION | MB_OK);
        return 0;
    }

    char szTitle[128] = "About Windows"; // Default title
    // Try to get actual product name for title
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", 0, KEY_READ | KEY_WOW64_64KEY, &hKey) == ERROR_SUCCESS) {
        DWORD dwSize = sizeof(szTitle);
        RegQueryValueExA(hKey, "ProductName", nullptr, nullptr, (LPBYTE)szTitle, &dwSize);
        RegCloseKey(hKey);
    }

    // Calculate centered window position
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    int windowWidth = rc.right - rc.left;
    int windowHeight = rc.bottom - rc.top;
    int posX = (screenWidth - windowWidth) / 2;
    int posY = (screenHeight - windowHeight) / 2;


    HWND hWnd = CreateWindowA(wc.lpszClassName, szTitle, dwStyle, // Use the defined style
                              posX, posY, // Centered position
                              windowWidth, windowHeight, // Adjusted width and height
                              nullptr, nullptr, hInstance, nullptr);

    if (!hWnd) {
         MessageBoxA(nullptr, "Window Creation Failed!", "Error", MB_ICONEXCLAMATION | MB_OK);
        return 0;
    }

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    MSG msg;
    while (GetMessageA(&msg, nullptr, 0, 0) > 0) { // Use > 0 for safety
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    return (int)msg.wParam;
}
