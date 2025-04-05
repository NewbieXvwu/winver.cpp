// 编译命令（32位版，兼容Win95~Win11）:
// windres --input-format=rc --output-format=coff --target=pe-i386 winver.rc -o winver.res
// i686-w64-mingw32-g++ -o winver-x86.exe winver.cpp winver.res -mwindows -march=pentium -std=gnu++17 -D_WIN32_WINDOWS=0x0400 -Wl,--subsystem=windows -static-libgcc -static-libstdc++ -static -lcomctl32 -lkernel32 -luser32 -lgdi32 -lole32 -lshlwapi -ladvapi32 -Wl,--gc-sections -O3
// 编译命令（64位版，兼容WinXP x64~Win11）:
// windres --input-format=rc --output-format=coff --target=pe-x86-64 winver.rc -o winver.res
// x86_64-w64-mingw32-g++ -o winver-x64.exe winver.cpp winver.res -mwindows -march=x86-64 -mtune=generic -std=gnu++17 -D_WIN32_WINDOWS=0x0410 -D_WIN32_WINNT=0x0502 -Wl,--subsystem=windows -static-libgcc -static-libstdc++ -lcomctl32 -lkernel32 -luser32 -ladvapi32 -lshcore -lgdi32 -lole32 -ldwmapi -luxtheme -lshlwapi -Wl,--gc-sections -Ofast -funroll-loops -fpeel-loops -fpredictive-commoning -floop-interchange -floop-unroll-and-jam -finline-functions -fipa-cp -fipa-ra -fdevirtualize -foptimize-sibling-calls -ffast-math -fomit-frame-pointer -freorder-blocks -freorder-functions -fstrength-reduce -ftree-vectorize

#include <windows.h>
#include <commctrl.h>
#include <winreg.h>
#include <cstdlib>
#include <string>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <shlwapi.h>
#include <cstdarg>  // 添加对可变参数的支持

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "msimg32.lib")

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
static HBITMAP g_hLogoBitmap = NULL;
// XP分隔条相关变量
static HBITMAP g_hSeparatorBitmap = NULL;
static int g_separatorWidth = 0;
static int g_separatorHeight = 0;
static bool g_separatorLoaded = false;

// 函数声明
bool IsModernUIAvailable();
void InitDPIScaling(HWND hWnd, bool forceSystemDPI = false);
void EnableModernFeatures();
void UpdateLayout(HWND hWnd);
HFONT CreateDPIFont();
void GetRealOSVersion(OSVERSIONINFOA* osvi);
bool IsServerEdition();
bool LoadWindowsLogo();
bool LoadXPSeparator();
void DrawWindowsLogo(HDC hdc, int x, int y, int width, int height);
void DrawXPSeparator(HDC hdc, int x, int y, int width, int height);
bool Is64BitOS();
bool CanLoad64BitModules();
bool InitializeGdiplus();
void ShutdownGdiplus();
void EnableDarkMode(HWND hwnd);
void ApplyDarkModeSettings(HWND hwnd);
bool TryPreMultipliedAlphaMethod(HDC hdc, HDC hdcMem, int drawX, int drawY, int drawWidth, int drawHeight, 
                               BITMAP &bm, BOOL (WINAPI *pfnAlphaBlend)(HDC, int, int, int, int, HDC, int, int, int, int, BLENDFUNCTION));
bool TryStandardAlphaMethod(HDC hdc, HDC hdcMem, int drawX, int drawY, int drawWidth, int drawHeight, 
                          BITMAP &bm, BOOL (WINAPI *pfnAlphaBlend)(HDC, int, int, int, int, HDC, int, int, int, int, BLENDFUNCTION));
bool TryAlternativeTransparencyMethod(HDC hdc, HDC hdcMem, int drawX, int drawY, int drawWidth, int drawHeight, BITMAP &bm);

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
        if (g_lightBrush) DeleteObject(g_lightBrush); g_lightBrush = nullptr;
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

    // 移除对Windows 10的版本限制检查，Win8.1也支持GDI+
    // OSVERSIONINFOA osvi = { sizeof(OSVERSIONINFOA) };
    // GetRealOSVersion(&osvi);
    // if (osvi.dwMajorVersion < 10) return false;

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
    if (g_hLogoBitmap) {
        DeleteObject(g_hLogoBitmap);
        g_hLogoBitmap = NULL;
    }

    if (g_hSeparatorBitmap) {
        DeleteObject(g_hSeparatorBitmap);
        g_hSeparatorBitmap = NULL;
        g_separatorLoaded = false;
    }
    
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
    if (g_logoLoaded) {
        if (g_pLogoImage) return true;
        if (g_hLogoBitmap) return true;
    }

    bool gdiplusAvailable = InitializeGdiplus();
    
    // 获取系统版本以确定正确的资源ID
    OSVERSIONINFOA osvi = { sizeof(OSVERSIONINFOA) };
    GetRealOSVersion(&osvi);
    
    // 首先，确定我们需要加载的资源ID和DLL路径
    int logoResourceId = 2123; // 默认Windows 10/11的资源ID
    char szResourceDllPath[MAX_PATH] = "C:\\Windows\\Branding\\Basebrd\\basebrd.dll";
    bool isXP = false;

    if (osvi.dwMajorVersion == 5) {
        isXP = true;
        
        // 32位XP需要确定是哪个版本(Professional/Home/Embedded)
        bool isProfessional = false;
        bool isHomeEdition = false;
        bool isEmbedded = false;
        
        // 检查系统版本以确定正确的资源ID
        HKEY hKey;
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            char productName[256] = {0};
            DWORD dwSize = sizeof(productName);
            if (RegQueryValueExA(hKey, "ProductName", NULL, NULL, (LPBYTE)productName, &dwSize) == ERROR_SUCCESS) {
                // 根据产品名称确定版本
                if (strstr(productName, "Professional")) {
                    isProfessional = true;
                    logoResourceId = 131; // Professional版本
                } else if (strstr(productName, "Home")) {
                    isHomeEdition = true;
                    logoResourceId = 147; // Home Edition版本
                } else if (strstr(productName, "Embedded")) {
                    isEmbedded = true;
                    logoResourceId = 149; // Embedded版本
                } else {
                    // 默认使用Professional版本的Logo
                    logoResourceId = 131;
                }
            }
            RegCloseKey(hKey);
        }
        
        // 设置为shell32.dll路径
        GetSystemDirectoryA(szResourceDllPath, MAX_PATH);
        PathCombineA(szResourceDllPath, szResourceDllPath, "shell32.dll");
    }
    // Windows XP 64位版本检测 (5.2) - 需要区分Windows Server 2003
    else if (osvi.dwMajorVersion == 5 && osvi.dwMinorVersion == 2) {
        // 检查是否为服务器版本
        bool isServer = IsServerEdition();
        
        // 如果不是服务器版本，那么是XP 64位
        if (!isServer) {
            isXP = true;
            // XP 64位版使用moricons.dll，ID是131
            logoResourceId = 131;
            GetSystemDirectoryA(szResourceDllPath, MAX_PATH);
            PathCombineA(szResourceDllPath, szResourceDllPath, "moricons.dll");
        }
        // 否则是Windows Server 2003/R2，继续使用默认的basebrd.dll
    }
    // Windows 8/8.1 使用不同的资源ID
    else if (osvi.dwMajorVersion == 6 && (osvi.dwMinorVersion == 2 || osvi.dwMinorVersion == 3)) {
        logoResourceId = 2121; // Windows 8/8.1的资源ID
    }
    // 其他版本(Win 10/11等)使用默认值2123
    
    // 加载包含logo的DLL文件
    HMODULE hResourceDll = NULL;
    
    // 对于XP以外的系统，尝试标准的basebrd.dll路径
    if (!isXP) {
        // 标准basebrd.dll路径查找
        hResourceDll = LoadLibraryA(szResourceDllPath);
        if (!hResourceDll) {
            char szSystemDir[MAX_PATH] = {0};
            GetSystemDirectoryA(szSystemDir, MAX_PATH);
            PathCombineA(szResourceDllPath, szSystemDir, "..\\Branding\\Basebrd\\basebrd.dll");
            hResourceDll = LoadLibraryA(szResourceDllPath);
            if (!hResourceDll) {
                char szWindowsDir[MAX_PATH] = {0};
                GetWindowsDirectoryA(szWindowsDir, MAX_PATH);
                PathCombineA(szResourceDllPath, szWindowsDir, "Branding\\Basebrd\\basebrd.dll");
                hResourceDll = LoadLibraryA(szResourceDllPath);
                if (!hResourceDll) {
                    return false;
                }
            }
        }
    } else {
        // 对于XP系统，直接加载shell32.dll或moricons.dll
        hResourceDll = LoadLibraryA(szResourceDllPath);
        if (!hResourceDll) {
            return false;
        }
    }
    
    // 尝试加载位图资源
    g_hLogoBitmap = LoadBitmapA(hResourceDll, MAKEINTRESOURCEA(logoResourceId));
    if (g_hLogoBitmap) {
        // 获取位图信息
        BITMAP bm;
        if (GetObject(g_hLogoBitmap, sizeof(BITMAP), &bm)) {
            g_logoWidth = bm.bmWidth;
            g_logoHeight = bm.bmHeight;
            g_logoLoaded = true;
            return true;
        } else {
            DeleteObject(g_hLogoBitmap);
            g_hLogoBitmap = NULL;
        }
    }

    // 只有在GDI+可用时才尝试第二种方法
    if (gdiplusAvailable) {
        // 尝试多种类型
        HRSRC hResInfo = NULL;
        const char* foundType = NULL;
        
        // 尝试IMAGE类型
        hResInfo = FindResourceA(hResourceDll, MAKEINTRESOURCEA(logoResourceId), "IMAGE");
        if (hResInfo) foundType = "IMAGE";
        
        // 尝试PNG类型
        if (!hResInfo) {
            hResInfo = FindResourceA(hResourceDll, MAKEINTRESOURCEA(logoResourceId), "PNG");
            if (hResInfo) foundType = "PNG";
        }
        
        if (hResInfo) {
            HGLOBAL hResData = LoadResource(hResourceDll, hResInfo);
            if (!hResData) {
                FreeLibrary(hResourceDll);
                return false;
            }

            DWORD dwSize = SizeofResource(hResourceDll, hResInfo);
            if (dwSize == 0) { 
                FreeLibrary(hResourceDll);
                return false;
            }

            void* pResourceData = LockResource(hResData);
            if (!pResourceData) {
                FreeLibrary(hResourceDll);
                return false;
            }

            // 直接从资源数据创建流
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
                            FreeLibrary(hResourceDll);
                            return true;
                        }
                        if (g_pLogoImage) {
                            GdipDisposeImage(g_pLogoImage);
                            g_pLogoImage = nullptr;
                        }
                    }
                }
                GlobalFree(hBuffer);
            }
        }
    }
    
    // 尝试普通位图资源加载方法 - 只在前面的方法失败时进行
    if (!g_logoLoaded) {
        HRSRC hResInfo = FindResourceA(hResourceDll, MAKEINTRESOURCEA(logoResourceId), RT_BITMAP);
        if (hResInfo) {
            HGLOBAL hResData = LoadResource(hResourceDll, hResInfo);
            if (hResData) {
                LPBITMAPINFOHEADER lpBitmap = (LPBITMAPINFOHEADER)LockResource(hResData);
                if (lpBitmap) {
                    HDC hDC = GetDC(NULL);
                    
                    // 计算位图信息
                    int colorTableSize = 0;
                    if (lpBitmap->biBitCount <= 8) {
                        colorTableSize = (1 << lpBitmap->biBitCount) * sizeof(RGBQUAD);
                    }
                    
                    // 计算像素数据位置
                    BYTE* lpBits = (BYTE*)lpBitmap + lpBitmap->biSize + colorTableSize;
                    
                    // 使用CreateDIBitmap创建DDB位图
                    g_hLogoBitmap = CreateDIBitmap(
                        hDC,
                        lpBitmap,
                        CBM_INIT,
                        lpBits,
                        (BITMAPINFO*)lpBitmap,
                        DIB_RGB_COLORS
                    );
                    
                    ReleaseDC(NULL, hDC);
                    
                    if (g_hLogoBitmap) {
                        // 获取位图信息
                        BITMAP bm;
                        if (GetObject(g_hLogoBitmap, sizeof(BITMAP), &bm)) {
                            g_logoWidth = bm.bmWidth;
                            g_logoHeight = bm.bmHeight;
                            g_logoLoaded = true;
                            
                            return true;
                        }
                        DeleteObject(g_hLogoBitmap);
                        g_hLogoBitmap = NULL;
                    }
                }
            }
        }
    }
    
    // 尝试备用资源ID (如果之前的方法都失败了)
    if (!g_logoLoaded) {
        // XP系统尝试不同的ID
        if (isXP) {
            // 如果当前是XP 64位(使用的是moricons.dll)，那么没有备用ID可尝试
            if (osvi.dwMajorVersion == 5 && osvi.dwMinorVersion == 2) {
                // 64位XP只有一个ID可用，无备用选项
            } 
            // 32位XP系统 - 尝试不同版本的Logo ID
            else {
                int altIds[] = {131, 147, 149};  // Professional, Home Edition, Embedded
                for (int i = 0; i < 3; i++) {
                    if (altIds[i] == logoResourceId) continue;  // 跳过已尝试过的ID
                    
                    g_hLogoBitmap = LoadBitmapA(hResourceDll, MAKEINTRESOURCEA(altIds[i]));
                    if (g_hLogoBitmap) {
                        BITMAP bm;
                        if (GetObject(g_hLogoBitmap, sizeof(BITMAP), &bm)) {
                            g_logoWidth = bm.bmWidth;
                            g_logoHeight = bm.bmHeight;
                            g_logoLoaded = true;
                            
                            return true;
                        }
                        DeleteObject(g_hLogoBitmap);
                        g_hLogoBitmap = NULL;
                    }
                }
            }
        } else {
            // 非XP系统 - 尝试备用ID
            int altResourceId = (logoResourceId == 2123) ? 2121 : 2123;
            
            g_hLogoBitmap = LoadBitmapA(hResourceDll, MAKEINTRESOURCEA(altResourceId));
            if (g_hLogoBitmap) {
                // 获取位图信息
                BITMAP bm;
                if (GetObject(g_hLogoBitmap, sizeof(BITMAP), &bm)) {
                    g_logoWidth = bm.bmWidth;
                    g_logoHeight = bm.bmHeight;
                    g_logoLoaded = true;
                    
                    return true;
                }
                DeleteObject(g_hLogoBitmap);
                g_hLogoBitmap = NULL;
            }
        }
    }
    
    FreeLibrary(hResourceDll);
    return false;
}

void DrawWindowsLogo(HDC hdc, int x, int y, int width, int height) {
    if (!g_logoLoaded) {
        return;
    }

    // 获取系统版本以确定是否是Windows XP
    OSVERSIONINFOA osvi = { sizeof(OSVERSIONINFOA) };
    GetRealOSVersion(&osvi);
    bool isXP = (osvi.dwMajorVersion == 5 && (osvi.dwMinorVersion == 1 || osvi.dwMinorVersion == 2));

    // 如果有GDI+图像，优先使用GDI+绘制
    if (g_pLogoImage && g_gdiplusInitialized) {
        void* graphics = nullptr;
        if (GdipCreateFromHDC(hdc, &graphics) != Ok || !graphics) {
            goto UseGDIMethod;
        }

        GdipSetSmoothingMode(graphics, SmoothingModeHighQuality);
        GdipSetInterpolationMode(graphics, InterpolationModeHighQualityBicubic);

        float aspectRatio = (float)g_logoWidth / (float)g_logoHeight;
        int drawWidth, drawHeight, drawX, drawY;
        
        if (isXP) {
            // XP特殊处理：宽度适应窗口宽度，不缩放
            RECT rcClient;
            GetClientRect(WindowFromDC(hdc), &rcClient);
            int clientWidth = rcClient.right - rcClient.left;
            
            drawWidth = clientWidth; // 填满整个宽度
            drawHeight = (int)(drawWidth / aspectRatio);
            drawX = 0; // 完全贴左对齐
            drawY = 0; // 贴顶部
        } else {
            // 非XP系统使用原来的缩放逻辑
            drawWidth = width;
            drawHeight = (int)(width / aspectRatio);
            if (drawHeight > height) {
                drawHeight = height;
                drawWidth = (int)(height * aspectRatio);
            }
            drawX = x + (width - drawWidth) / 2;
            drawY = y;
        }

        GdipDrawImageRectI(graphics, g_pLogoImage, drawX, drawY, drawWidth, drawHeight);
        GdipDeleteGraphics(graphics);
        return;
    }
    
UseGDIMethod:
    // 如果有GDI位图，使用GDI绘制，支持透明色
    if (g_hLogoBitmap) {
        // 获取位图信息
        BITMAP bm;
        if (!GetObject(g_hLogoBitmap, sizeof(BITMAP), &bm)) {
            return;
        }
        
        // 创建兼容的内存DC
        HDC hdcMem = CreateCompatibleDC(hdc);
        if (!hdcMem) {
            return;
        }
        
        HBITMAP hOldBitmap = (HBITMAP)SelectObject(hdcMem, g_hLogoBitmap);
            
        // 计算绘制尺寸
        float aspectRatio = (float)g_logoWidth / (float)g_logoHeight;
        int drawWidth, drawHeight, drawX, drawY;
        
        if (isXP) {
            // XP特殊处理：宽度适应窗口宽度，不缩放
            RECT rcClient;
            GetClientRect(WindowFromDC(hdc), &rcClient);
            int clientWidth = rcClient.right - rcClient.left;
            
            drawWidth = clientWidth; // 填满整个宽度
            drawHeight = (int)(drawWidth / aspectRatio);
            drawX = 0; // 完全贴左对齐
            drawY = 0; // 贴顶部
        } else {
            // 非XP系统使用原来的绘制位置和大小
            drawWidth = width;
            drawHeight = (int)(width / aspectRatio);
            if (drawHeight > height) {
                drawHeight = height;
                drawWidth = (int)(height * aspectRatio);
            }
            drawX = x + (width - drawWidth) / 2;
            drawY = y;
        }
        
        // 无论是深色模式还是浅色模式，都应用透明处理
        // 浅色模式下的窗口背景可能不是纯白，所以需要透明处理
        
        // 动态加载msimg32.dll库
        HMODULE hMsImg32 = LoadLibraryA("msimg32.dll");
        if (hMsImg32) {
            // 获取AlphaBlend函数指针
            typedef BOOL (WINAPI *PFN_AlphaBlend)(HDC, int, int, int, int, HDC, int, int, int, int, BLENDFUNCTION);
            typedef BOOL (WINAPI *PFN_TransparentBlt)(HDC, int, int, int, int, HDC, int, int, int, int, UINT);
            
            PFN_AlphaBlend pfnAlphaBlend = (PFN_AlphaBlend)GetProcAddress(hMsImg32, "AlphaBlend");
            PFN_TransparentBlt pfnTransparentBlt = (PFN_TransparentBlt)GetProcAddress(hMsImg32, "TransparentBlt");
            
            // 尝试不同的透明处理方法
            int method = 0;
            bool success = false;
            
            while (!success && method < 4) { // 增加到4个方法
                switch (method) {
                case 0: // 预乘Alpha方法
                    if (pfnAlphaBlend) {
                        success = TryPreMultipliedAlphaMethod(hdc, hdcMem, drawX, drawY, drawWidth, drawHeight, bm, pfnAlphaBlend);
                    }
                    break;
                case 1: // 标准AlphaBlend方法 
                    if (pfnAlphaBlend) {
                        success = TryStandardAlphaMethod(hdc, hdcMem, drawX, drawY, drawWidth, drawHeight, bm, pfnAlphaBlend);
                    }
                    break;
                case 2: // TransparentBlt方法
                    if (pfnTransparentBlt) {
                        SetStretchBltMode(hdc, HALFTONE);
                        SetBrushOrgEx(hdc, 0, 0, NULL);
                        
                        // 更保守的颜色值 - 使用纯白而不是接近白色的值作为透明度
                        COLORREF transparentColor = RGB(255, 255, 255);
                        
                        BOOL result = pfnTransparentBlt(
                            hdc, drawX, drawY, drawWidth, drawHeight,
                            hdcMem, 0, 0, g_logoWidth, g_logoHeight,
                            transparentColor  // 指定白色为透明色
                        );
                        
                        success = (result != FALSE);
                    }
                    break;
                case 3: // 备用透明处理方法
                    // 最基本的黑白掩码方法，应该在任何Windows版本上都能工作
                    success = TryAlternativeTransparencyMethod(hdc, hdcMem, drawX, drawY, drawWidth, drawHeight, bm);
                    break;
                }
                
                if (!success) {
                    method++;
                }
            }
            
            // 如果以上方法都失败，使用最后的备用方案
            if (!success && pfnAlphaBlend) {
                HDC hdcTemp = CreateCompatibleDC(hdc);
                if (hdcTemp) {
                    BITMAPINFO bi = {0};
                    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                    bi.bmiHeader.biWidth = bm.bmWidth;
                    bi.bmiHeader.biHeight = bm.bmHeight;
                    bi.bmiHeader.biPlanes = 1;
                    bi.bmiHeader.biBitCount = 32;
                    bi.bmiHeader.biCompression = BI_RGB;
                    
                    void* pvBits = NULL;
                    HBITMAP hBitmapTemp = CreateDIBSection(hdcTemp, &bi, DIB_RGB_COLORS, &pvBits, NULL, 0);
                    
                    if (hBitmapTemp && pvBits) {
                        HBITMAP hOldBitmapTemp = (HBITMAP)SelectObject(hdcTemp, hBitmapTemp);
                        
                        // 复制原始位图到临时位图
                        BitBlt(hdcTemp, 0, 0, bm.bmWidth, bm.bmHeight, hdcMem, 0, 0, SRCCOPY);
                        
                        // 简单的白色识别，使用固定RGB判断
                        BYTE* pBits = (BYTE*)pvBits;
                        for (int i = 0; i < bm.bmHeight; i++) {
                            for (int j = 0; j < bm.bmWidth; j++) {
                                BYTE b = pBits[0];
                                BYTE g = pBits[1];
                                BYTE r = pBits[2];
                                
                                // 简单白色检测
                                if (r > 250 && g > 250 && b > 250) {
                                    pBits[3] = 0; // 完全透明
                                } else {
                                    pBits[3] = 255; // 完全不透明
                                }
                                
                                pBits += 4;
                            }
                        }
                        
                        // 使用AlphaBlend绘制
                        BLENDFUNCTION bf = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
                        SetStretchBltMode(hdc, HALFTONE);
                        SetBrushOrgEx(hdc, 0, 0, NULL);
                        
                        BOOL result = pfnAlphaBlend(
                            hdc, drawX, drawY, drawWidth, drawHeight,
                            hdcTemp, 0, 0, bm.bmWidth, bm.bmHeight,
                            bf
                        );
                        
                        // 清理资源
                        SelectObject(hdcTemp, hOldBitmapTemp);
                        DeleteObject(hBitmapTemp);
                        DeleteDC(hdcTemp);
                        
                        if (result) {
                            SelectObject(hdcMem, hOldBitmap);
                            DeleteDC(hdcMem);
                            FreeLibrary(hMsImg32);
                            return;
                        }
                    }
                    
                    if (hdcTemp) DeleteDC(hdcTemp);
                    if (hBitmapTemp && hBitmapTemp != INVALID_HANDLE_VALUE) 
                        DeleteObject(hBitmapTemp);
                }
            }
            
            // 如果成功完成，清理资源并返回
            if (success) {
                SelectObject(hdcMem, hOldBitmap);
                DeleteDC(hdcMem);
                FreeLibrary(hMsImg32);
                return;
            }
            
            FreeLibrary(hMsImg32);
        }
        
        // 所有透明方法都失败，尝试蒙版方法，不管是深色还是浅色模式
        // 创建蒙版位图：用于二值化处理
        HDC hdcMask = CreateCompatibleDC(hdc);
        HBITMAP hMaskBitmap = CreateBitmap(bm.bmWidth, bm.bmHeight, 1, 1, NULL);
        if (hdcMask && hMaskBitmap) {
            HBITMAP hOldMask = (HBITMAP)SelectObject(hdcMask, hMaskBitmap);
            
            // 设置文本背景色为黑色，文本色为白色（用于创建蒙版）
            SetBkColor(hdcMem, RGB(255, 255, 255)); // 白色区域视为背景
            SetTextColor(hdcMem, RGB(0, 0, 0));      // 非白色区域视为前景
            
            // 创建掩码：白色区域变成0，其他区域变成1
            BitBlt(hdcMask, 0, 0, bm.bmWidth, bm.bmHeight, hdcMem, 0, 0, SRCCOPY);
            
            // 绘制Logo到最终DC上，只绘制非白色部分
            SetStretchBltMode(hdc, HALFTONE);
            SetBrushOrgEx(hdc, 0, 0, NULL);
            
            // 需要创建一个兼容位图，复制Logo的彩色部分
            HDC hdcColor = CreateCompatibleDC(hdc);
            HBITMAP hColorBitmap = CreateCompatibleBitmap(hdc, bm.bmWidth, bm.bmHeight);
            if (hdcColor && hColorBitmap) {
                HBITMAP hOldColorBitmap = (HBITMAP)SelectObject(hdcColor, hColorBitmap);
                
                // 将原始Logo位图复制到彩色DC上
                BitBlt(hdcColor, 0, 0, bm.bmWidth, bm.bmHeight, hdcMem, 0, 0, SRCCOPY);
                
                // 在深色模式和浅色模式下，都适用相同的处理
                // 让蒙版的黑色部分去掉颜色位图上的像素（实现透明）
                BitBlt(hdcColor, 0, 0, bm.bmWidth, bm.bmHeight, hdcMask, 0, 0, SRCAND);
                
                // 绘制最终结果到目标DC上，使用缩放
                SetStretchBltMode(hdc, HALFTONE);
                SetBrushOrgEx(hdc, 0, 0, NULL);
                
                StretchBlt(
                    hdc, drawX, drawY, drawWidth, drawHeight,
                    hdcColor, 0, 0, bm.bmWidth, bm.bmHeight,
                    SRCCOPY
                );
                
                // 清理资源
                SelectObject(hdcColor, hOldColorBitmap);
                DeleteObject(hColorBitmap);
                DeleteDC(hdcColor);
            } else {
                // 回退方案：只绘制黑白版
                StretchBlt(
                    hdc, drawX, drawY, drawWidth, drawHeight,
                    hdcMask, 0, 0, bm.bmWidth, bm.bmHeight,
                    SRCCOPY
                );
            }
            
            // 清理掩码资源
            SelectObject(hdcMask, hOldMask);
            DeleteObject(hMaskBitmap);
            DeleteDC(hdcMask);
            
            SelectObject(hdcMem, hOldBitmap);
            DeleteDC(hdcMem);
            return;
        } else {
            if (hdcMask) DeleteDC(hdcMask);
            if (hMaskBitmap) DeleteObject(hMaskBitmap);
        }
        
        // 浅色模式下如果前面的方法都失败，仍然使用TransparentBlt
        if (!g_darkModeEnabled) {
            HMODULE hMsImg32 = LoadLibraryA("msimg32.dll");
            if (hMsImg32) {
                typedef BOOL (WINAPI *PFN_TransparentBlt)(HDC, int, int, int, int, HDC, int, int, int, int, UINT);
                PFN_TransparentBlt pfnTransparentBlt = (PFN_TransparentBlt)GetProcAddress(hMsImg32, "TransparentBlt");
                
                if (pfnTransparentBlt) {
                    // 使用高质量缩放模式
                    SetStretchBltMode(hdc, HALFTONE);
                    SetBrushOrgEx(hdc, 0, 0, NULL);
                    
                    BOOL result = pfnTransparentBlt(
                        hdc, drawX, drawY, drawWidth, drawHeight,
                        hdcMem, 0, 0, g_logoWidth, g_logoHeight,
                        RGB(255, 255, 255)  // 指定白色为透明色
                    );
                    
                    if (result) {
                        SelectObject(hdcMem, hOldBitmap);
                        DeleteDC(hdcMem);
                        FreeLibrary(hMsImg32);
                        return;
                    }
                }
                FreeLibrary(hMsImg32);
            }
        }
        
        // 所有特殊方法都失败，最后才使用标准StretchBlt
        // 这是最后的兜底方案，会绘制带白色背景的Logo
        SetStretchBltMode(hdc, HALFTONE);  // 使用高质量缩放模式
        SetBrushOrgEx(hdc, 0, 0, NULL);    // 设置刷子原点，避免刷子失真
        
        StretchBlt(
            hdc, drawX, drawY, drawWidth, drawHeight,
            hdcMem, 0, 0, g_logoWidth, g_logoHeight,
            SRCCOPY
        );
        
        // 清理资源
        SelectObject(hdcMem, hOldBitmap);
        DeleteDC(hdcMem);
    }
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
    // 使用大括号创建作用域
    {
        const char* versionStr = GetWindowsVersion();
        bool isMultiline = (strstr(versionStr, "\r\n") != nullptr);
        
        // 获取窗口客户区大小，用于计算居中位置
        RECT rcClient;
        GetClientRect(hWnd, &rcClient);
        int clientWidth = rcClient.right - rcClient.left;
        
        // 获取系统版本以确定是否是Windows XP
        OSVERSIONINFOA osvi = { sizeof(OSVERSIONINFOA) };
        GetRealOSVersion(&osvi);
        bool isXP = (osvi.dwMajorVersion == 5 && (osvi.dwMinorVersion == 1 || osvi.dwMinorVersion == 2));
        
        // 计算文本的Y位置偏移
        int textYOffset;
        if (isXP && g_logoLoaded) {
            // 如果是XP且有Logo，计算Logo实际高度和分隔条
            float aspectRatio = (float)g_logoWidth / (float)g_logoHeight;
            int logoHeight = (int)(clientWidth / aspectRatio); // 使用客户区宽度计算Logo高度
            
            textYOffset = logoHeight; // 从Logo实际高度开始
            if (g_separatorLoaded) {
                textYOffset += SCALE_Y(g_separatorHeight); // 加上分隔条的高度
            }
            textYOffset += SCALE_Y(30); // XP界面中，分隔条下方空间增加到20像素
        } else if (g_logoLoaded) {
            // 非XP但有Logo - 增加Y偏移以适应Logo下移的效果
            textYOffset = SCALE_Y(100);
        } else {
            // 无Logo
            textYOffset = SCALE_Y(30);
        }

        HWND hVersionText = GetDlgItem(hWnd, 0); // Assumes Static control has ID 0
        if (hVersionText) {
            // 调整文本高度和Y位置
            int textWidth = SCALE_X(300);
            int textHeight = isMultiline ? SCALE_Y(50) : SCALE_Y(30);
            int textX = (clientWidth - textWidth) / 2; // 居中计算
            int textY = textYOffset; // 直接使用计算好的偏移
            SetWindowPos(hVersionText, nullptr, textX, textY, textWidth, textHeight, SWP_NOZORDER);
            SetWindowTextA(hVersionText, versionStr);
        }

        HWND hExitButton = GetDlgItem(hWnd, 1); // Assumes Button has ID 1
        if (hExitButton) {
            // 调整按钮Y位置
            int buttonWidth = SCALE_X(60);
            int buttonHeight = SCALE_Y(30); // 无Logo
            if (g_logoLoaded) {
                // 非XP但有Logo
                int buttonHeight = SCALE_Y(35);
            }
            // XP界面中，按钮位置也需要往下调整
            int buttonY;
            if (g_logoLoaded) {
                // 非XP但有Logo
                buttonY = textYOffset + (isMultiline ? SCALE_Y(75) : SCALE_Y(55));
            } else {
                // 无Logo
                buttonY = textYOffset + (isMultiline ? SCALE_Y(70) : SCALE_Y(50));
            }
            int buttonX = (clientWidth - buttonWidth) / 2; // 居中计算
            SetWindowPos(hExitButton, nullptr, buttonX, buttonY, buttonWidth, buttonHeight, SWP_NOZORDER);
        }
    }
}


LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        {
            InitDPIScaling(hWnd);
            g_hFont = CreateDPIFont();
            
            // 获取客户区尺寸，用于居中计算
            RECT rcClient;
            GetClientRect(hWnd, &rcClient);
            int clientWidth = rcClient.right - rcClient.left;
            
            // 计算文本和按钮的初始位置，使其居中
            int textWidth = SCALE_X(300);
            int textX = (clientWidth - textWidth) / 2;
            int buttonWidth = SCALE_X(60);
            int buttonX = (clientWidth - buttonWidth) / 2;
            
            // 初始位置设为简单的值，UpdateLayout会处理准确位置
            int initialTextY = SCALE_Y(120); // 初始放在Logo下面一点的位置
            int initialButtonY = SCALE_Y(180); // 初始位置，后续会调整
            
            // Create Static text (ID 0)
            CreateWindowA("Static", "", WS_CHILD | WS_VISIBLE | SS_CENTER,
                        textX, initialTextY, 
                        textWidth, SCALE_Y(60), hWnd, (HMENU)0, nullptr, nullptr);
            
            // Create OK Button (ID 1)
            CreateWindowA("Button", "OK", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                        buttonX, initialButtonY, 
                        buttonWidth, SCALE_Y(35), hWnd, (HMENU)1, nullptr, nullptr);
            
            // Call UpdateLayout immediately after creating controls
            UpdateLayout(hWnd); // Set text and final positions
    
            if (g_hFont) EnumChildWindows(hWnd, SetChildFont, (LPARAM)g_hFont);
            ApplyDarkModeSettings(hWnd);
        }
        break;
    case WM_PAINT: 
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);
            RECT rcClient;
            GetClientRect(hWnd, &rcClient);
            FillRect(hdc, &rcClient, g_darkModeEnabled ? g_hDarkBrush : (HBRUSH)(COLOR_BTNFACE + 1));
            
            // 获取系统版本以确定是否是Windows XP
            OSVERSIONINFOA osvi = { sizeof(OSVERSIONINFOA) };
            GetRealOSVersion(&osvi);
            bool isXP = (osvi.dwMajorVersion == 5 && (osvi.dwMinorVersion == 1 || osvi.dwMinorVersion == 2));
            
            // 绘制Logo
            if (g_logoLoaded) {
                if (isXP) {
                    // 获取Logo实际高度以便精确计算
                    float aspectRatio = (float)g_logoWidth / (float)g_logoHeight;
                    int logoHeight = (int)(rcClient.right / aspectRatio);
                    
                    // XP特殊处理：Logo贴着窗口顶部和左侧，占据整个宽度
                    DrawWindowsLogo(hdc, 0, 0, rcClient.right, logoHeight);
                    
                    // 如果加载了分隔条，则绘制在Logo下方，无缝衔接
                    if (g_separatorLoaded) {
                        // 分隔条高度固定为分隔条原始高度的缩放版本
                        int separatorHeight = SCALE_Y(g_separatorHeight);
                        // 分隔条位置是Logo的实际高度，而不是固定值
                        DrawXPSeparator(hdc, 0, logoHeight, rcClient.right, separatorHeight);
                    }
                } else {
                    // 非XP系统使用原来的绘制位置和大小
                    DrawWindowsLogo(hdc, SCALE_X(10), SCALE_Y(20), rcClient.right - SCALE_X(20), SCALE_Y(85));
                }
            }
            
            EndPaint(hWnd, &ps);
            return 0;
        }
        break;
    case WM_DPICHANGED: 
        {
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
        break;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT: 
        {
            HDC hdcStatic = (HDC)wParam;
            SetTextColor(hdcStatic, g_darkModeEnabled ? g_darkTextColor : GetSysColor(COLOR_WINDOWTEXT));
            SetBkColor(hdcStatic, g_darkModeEnabled ? g_darkBkColor : GetSysColor(COLOR_BTNFACE));
            return (LRESULT)(g_darkModeEnabled ? g_hDarkBrush : (HBRUSH)(COLOR_BTNFACE + 1)); // Use standard brush for light mode
        }
    case WM_CTLCOLORBTN: 
        {
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
        {
            if (g_hFont) DeleteObject(g_hFont);
            if (g_hDarkBrush) DeleteObject(g_hDarkBrush);
            if (g_lightBrush) DeleteObject(g_lightBrush); // Clean up light brush if created
            ShutdownGdiplus();
            PostQuitMessage(0);
        }
        break;
    case WM_SETTINGCHANGE:
        {
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
        }
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
    LoadXPSeparator(); // 加载XP分隔条

    // 获取系统版本以确定是否是Windows XP
    OSVERSIONINFOA osvi = { sizeof(OSVERSIONINFOA) };
    GetRealOSVersion(&osvi);
    bool isXP = (osvi.dwMajorVersion == 5 && (osvi.dwMinorVersion == 1 || osvi.dwMinorVersion == 2));
    
    // 为XP系统调整窗口宽度，高度保持不变
    RECT rc;
    if (isXP) {
        rc = {0, 0, SCALE_X(360), SCALE_Y(240)}; // XP窗口宽度调整为原来的四分之三
    } else {
        rc = {0, 0, SCALE_X(320), g_logoLoaded ? SCALE_Y(240) : SCALE_Y(160)}; // 非XP系统使用原来的尺寸
    }
    
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

// 预乘Alpha处理方法
bool TryPreMultipliedAlphaMethod(HDC hdc, HDC hdcMem, int drawX, int drawY, int drawWidth, int drawHeight, 
                                BITMAP &bm, BOOL (WINAPI *pfnAlphaBlend)(HDC, int, int, int, int, HDC, int, int, int, int, BLENDFUNCTION)) {
    // 检测当前系统版本
    OSVERSIONINFOA osvi = { sizeof(OSVERSIONINFOA) };
    GetRealOSVersion(&osvi);
    bool isWin10OrLater = (osvi.dwMajorVersion >= 10);
    
    // 创建32位临时DIB位图
    HDC hdcTemp = CreateCompatibleDC(hdc);
    if (!hdcTemp) {
        return false;
    }
    
    BITMAPINFO bi = {0};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = bm.bmWidth;
    bi.bmiHeader.biHeight = bm.bmHeight;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    
    void* pvBits = NULL;
    HBITMAP hBitmapTemp = CreateDIBSection(hdcTemp, &bi, DIB_RGB_COLORS, &pvBits, NULL, 0);
    
    if (!hBitmapTemp || !pvBits) {
        DeleteDC(hdcTemp);
        if (hBitmapTemp) DeleteObject(hBitmapTemp);
        return false;
    }
    
    HBITMAP hOldBitmapTemp = (HBITMAP)SelectObject(hdcTemp, hBitmapTemp);
    if (!hOldBitmapTemp) {
        DeleteObject(hBitmapTemp);
        DeleteDC(hdcTemp);
        return false;
    }
    
    // 将原位图复制到临时位图
    if (!BitBlt(hdcTemp, 0, 0, bm.bmWidth, bm.bmHeight, hdcMem, 0, 0, SRCCOPY)) {
        SelectObject(hdcTemp, hOldBitmapTemp);
        DeleteObject(hBitmapTemp);
        DeleteDC(hdcTemp);
        return false;
    }
    
    // 计算图像大小和步长
    int imageSize = bm.bmWidth * bm.bmHeight * 4;
    int stride = bm.bmWidth * 4;
    int whiteCount = 0;
    
    BYTE* pBits = (BYTE*)pvBits;
    if (!pBits) {
        SelectObject(hdcTemp, hOldBitmapTemp);
        DeleteObject(hBitmapTemp);
        DeleteDC(hdcTemp);
        return false;
    }
    
    // 处理透明度 - 使用安全的索引计算方法
    for (int i = 0; i < bm.bmHeight; i++) {
        for (int j = 0; j < bm.bmWidth; j++) {
            // 计算安全的偏移量
            int offset = i * stride + j * 4;
            if (offset < 0 || offset >= imageSize - 3) {
                continue;
            }
            
            BYTE b = pBits[offset + 0];
            BYTE g = pBits[offset + 1];
            BYTE r = pBits[offset + 2];
            
            // 计算亮度
            int luminance = (int)(0.299 * r + 0.587 * g + 0.114 * b);
            
            // 根据系统版本选择透明度处理策略
            if (isWin10OrLater) {
                // Win10+ 高容忍度模式
                if (luminance >= 200) {
                    pBits[offset + 3] = 0;  // 完全透明
                    whiteCount++;
                }
                else if (luminance >= 185) 
                    pBits[offset + 3] = 10;  // 高度透明
                else if (luminance >= 170) 
                    pBits[offset + 3] = 20;  // 较透明
                else if (luminance >= 150) 
                    pBits[offset + 3] = 64;  // 中度透明
                else if (luminance >= 130) 
                    pBits[offset + 3] = 128; // 半透明
                else if (luminance >= 110) 
                    pBits[offset + 3] = 192; // 低透明
                else 
                    pBits[offset + 3] = 255; // 不透明
                
                // 边缘检测 - 简化实现
                if (pBits[offset + 3] > 0 && pBits[offset + 3] < 255) {
                    // 检查周围像素，使用更紧凑的检查方式
                    bool hasHighLumNeighbor = false;
                    
                    // 相邻像素检查的相对偏移量数组
                    int neighbors[4][2] = {{-1,0}, {1,0}, {0,-1}, {0,1}}; // 上下左右
                    
                    for (int n = 0; n < 4; n++) {
                        int ni = i + neighbors[n][0];
                        int nj = j + neighbors[n][1];
                        
                        // 边界检查
                        if (ni < 0 || ni >= bm.bmHeight || nj < 0 || nj >= bm.bmWidth)
                            continue;
                        
                        int nOffset = ni * stride + nj * 4;
                        if (nOffset < 0 || nOffset >= imageSize - 3)
                            continue;
                        
                        // 计算邻居亮度
                        int nLum = (int)(0.299 * pBits[nOffset + 2] + 
                                         0.587 * pBits[nOffset + 1] + 
                                         0.114 * pBits[nOffset + 0]);
                        
                        if (nLum > 180) {
                            hasHighLumNeighbor = true;
                            break;
                        }
                    }
                    
                    // 如果有亮邻居，增加透明度
                    if (hasHighLumNeighbor) {
                        pBits[offset + 3] = (BYTE)(pBits[offset + 3] * 0.7);
                    }
                }
            } else {
                // Win7/8/8.1 标准容忍度模式
                if (luminance >= 240) {
                    pBits[offset + 3] = 0;   // 完全透明
                    whiteCount++;
                }
                else if (luminance >= 230)
                    pBits[offset + 3] = 64;  // 较透明
                else if (luminance >= 210)
                    pBits[offset + 3] = 128; // 半透明
                else
                    pBits[offset + 3] = 255; // 不透明
            }
            
            // 应用预乘Alpha
            if (pBits[offset + 3] < 255) {
                float alpha = pBits[offset + 3] / 255.0f;
                pBits[offset + 0] = (BYTE)(b * alpha);
                pBits[offset + 1] = (BYTE)(g * alpha);
                pBits[offset + 2] = (BYTE)(r * alpha);
            }
        }
    }
    
    // 使用AlphaBlend绘制
    BLENDFUNCTION bf = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    
    SetStretchBltMode(hdc, HALFTONE);
    SetBrushOrgEx(hdc, 0, 0, NULL);
    
    // 绘制位图
    BOOL result = pfnAlphaBlend(
        hdc, drawX, drawY, drawWidth, drawHeight,
        hdcTemp, 0, 0, bm.bmWidth, bm.bmHeight,
        bf
    );
    
    // 清理资源
    SelectObject(hdcTemp, hOldBitmapTemp);
    DeleteObject(hBitmapTemp);
    DeleteDC(hdcTemp);
    
    return (result != FALSE);
}

// 标准Alpha处理方法
bool TryStandardAlphaMethod(HDC hdc, HDC hdcMem, int drawX, int drawY, int drawWidth, int drawHeight, 
                           BITMAP &bm, BOOL (WINAPI *pfnAlphaBlend)(HDC, int, int, int, int, HDC, int, int, int, int, BLENDFUNCTION)) {
    // 创建32位临时DIB位图
    HDC hdcTemp = CreateCompatibleDC(hdc);
    if (!hdcTemp) {
        return false;
    }
    
    BITMAPINFO bi = {0};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = bm.bmWidth;
    bi.bmiHeader.biHeight = bm.bmHeight;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    
    void* pvBits = NULL;
    HBITMAP hBitmapTemp = CreateDIBSection(hdcTemp, &bi, DIB_RGB_COLORS, &pvBits, NULL, 0);
    
    if (!hBitmapTemp || !pvBits) {
        DeleteDC(hdcTemp);
        if (hBitmapTemp) DeleteObject(hBitmapTemp);
        return false;
    }
    
    HBITMAP hOldBitmapTemp = (HBITMAP)SelectObject(hdcTemp, hBitmapTemp);
    
    // 将原位图复制到临时位图
    BitBlt(hdcTemp, 0, 0, bm.bmWidth, bm.bmHeight, hdcMem, 0, 0, SRCCOPY);
    
    // 处理Alpha通道
    BYTE* pBits = (BYTE*)pvBits;
    int totalPixels = bm.bmWidth * bm.bmHeight;
    
    for (int i = 0; i < bm.bmHeight; i++) {
        for (int j = 0; j < bm.bmWidth; j++) {
            // 获取像素颜色
            BYTE b = pBits[0];
            BYTE g = pBits[1];
            BYTE r = pBits[2];
            
            // 判断是否为白色或接近白色
            if (r > 240 && g > 240 && b > 240) {
                pBits[3] = 0;  // 完全透明
            } else {
                pBits[3] = 255; // 完全不透明
            }
            
            // 移动到下一个像素
            pBits += 4;
        }
    }
    
    // 使用AlphaBlend绘制
    BLENDFUNCTION bf = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    
    SetStretchBltMode(hdc, HALFTONE);
    SetBrushOrgEx(hdc, 0, 0, NULL);
    
    BOOL result = pfnAlphaBlend(
        hdc, drawX, drawY, drawWidth, drawHeight,
        hdcTemp, 0, 0, bm.bmWidth, bm.bmHeight,
        bf
    );
    
    // 清理资源
    SelectObject(hdcTemp, hOldBitmapTemp);
    DeleteObject(hBitmapTemp);
    DeleteDC(hdcTemp);
    
    return (result != FALSE);
}

// 添加新的尝试方法 - 如果前面的方法都不起作用
bool TryAlternativeTransparencyMethod(HDC hdc, HDC hdcMem, int drawX, int drawY, int drawWidth, int drawHeight, BITMAP &bm) {
    // 创建灰度位图用于创建掩码
    HDC hdcGray = CreateCompatibleDC(hdc);
    if (!hdcGray) {
        return false;
    }
    
    HBITMAP hBitmapGray = CreateCompatibleBitmap(hdc, bm.bmWidth, bm.bmHeight);
    if (!hBitmapGray) {
        DeleteDC(hdcGray);
        return false;
    }
    
    // 创建掩码位图
    HDC hdcMask = CreateCompatibleDC(hdc);
    if (!hdcMask) {
        DeleteDC(hdcGray);
        DeleteObject(hBitmapGray);
        return false;
    }
    
    HBITMAP hBitmapMask = CreateBitmap(bm.bmWidth, bm.bmHeight, 1, 1, NULL);
    if (!hBitmapMask) {
        DeleteDC(hdcGray);
        DeleteDC(hdcMask);
        DeleteObject(hBitmapGray);
        return false;
    }
    
    // 选择灰度位图
    HBITMAP hOldGray = (HBITMAP)SelectObject(hdcGray, hBitmapGray);
    // 选择掩码位图
    HBITMAP hOldMask = (HBITMAP)SelectObject(hdcMask, hBitmapMask);
    
    // 复制原始位图到灰度位图
    BitBlt(hdcGray, 0, 0, bm.bmWidth, bm.bmHeight, hdcMem, 0, 0, SRCCOPY);
    
    // 创建掩码位图
    
    // 设置白色为透明色
    COLORREF crTransparent = RGB(255, 255, 255);
    SetBkColor(hdcGray, crTransparent);
    // 黑色为前景色
    SetTextColor(hdcGray, RGB(0, 0, 0));
    
    // 生成掩码
    BitBlt(hdcMask, 0, 0, bm.bmWidth, bm.bmHeight, hdcGray, 0, 0, SRCCOPY);
    
    // 在深色模式下，使用特殊的混合方法
    if (g_darkModeEnabled) {
        // 设置背景混合模式
        SetBkColor(hdc, RGB(0, 0, 0));
        // 将掩码位图和原始位图混合，只绘制非透明的部分
        BitBlt(hdcGray, 0, 0, bm.bmWidth, bm.bmHeight, hdcMask, 0, 0, SRCAND);
        // 绘制结果
        SetStretchBltMode(hdc, HALFTONE);
        SetBrushOrgEx(hdc, 0, 0, NULL);
        StretchBlt(hdc, drawX, drawY, drawWidth, drawHeight, hdcGray, 0, 0, bm.bmWidth, bm.bmHeight, SRCPAINT);
    } else {
        // 在浅色模式下，使用更简单的方法
        // 将掩码位图和原始位图混合，只绘制非透明的部分
        BitBlt(hdcGray, 0, 0, bm.bmWidth, bm.bmHeight, hdcMask, 0, 0, SRCAND);
        // 绘制结果
        SetStretchBltMode(hdc, HALFTONE);
        SetBrushOrgEx(hdc, 0, 0, NULL);
        StretchBlt(hdc, drawX, drawY, drawWidth, drawHeight, hdcGray, 0, 0, bm.bmWidth, bm.bmHeight, SRCCOPY);
    }
    
    // 清理资源
    SelectObject(hdcMask, hOldMask);
    SelectObject(hdcGray, hOldGray);
    DeleteObject(hBitmapMask);
    DeleteObject(hBitmapGray);
    DeleteDC(hdcMask);
    DeleteDC(hdcGray);
    
    return true;
}

// 加载XP分隔条
bool LoadXPSeparator() {
    // 如果已加载，直接返回
    if (g_separatorLoaded) {
        return true;
    }
    
    // 获取系统版本以确定是否是Windows XP
    OSVERSIONINFOA osvi = { sizeof(OSVERSIONINFOA) };
    GetRealOSVersion(&osvi);
    
    // 只有XP才需要加载分隔条
    bool isXP = (osvi.dwMajorVersion == 5);
    if (!isXP) {
        return false;
    }
    
    // 加载shell32.dll
    char szResourceDllPath[MAX_PATH];
    GetSystemDirectoryA(szResourceDllPath, MAX_PATH);
    PathCombineA(szResourceDllPath, szResourceDllPath, "shell32.dll");
    HMODULE hResourceDll = LoadLibraryA(szResourceDllPath);
    
    if (!hResourceDll) {
        return false;
    }
    
    // 加载ID 138的分隔条位图
    g_hSeparatorBitmap = LoadBitmapA(hResourceDll, MAKEINTRESOURCEA(138));
    if (g_hSeparatorBitmap) {
        // 获取位图信息
        BITMAP bm;
        if (GetObject(g_hSeparatorBitmap, sizeof(BITMAP), &bm)) {
            g_separatorWidth = bm.bmWidth;
            g_separatorHeight = bm.bmHeight;
            g_separatorLoaded = true;
            FreeLibrary(hResourceDll);
            return true;
        } else {
            DeleteObject(g_hSeparatorBitmap);
            g_hSeparatorBitmap = NULL;
        }
    }
    
    FreeLibrary(hResourceDll);
    return false;
}

// 绘制XP分隔条
void DrawXPSeparator(HDC hdc, int x, int y, int width, int height) {
    if (!g_separatorLoaded || !g_hSeparatorBitmap) {
        return;
    }
    
    // 创建兼容的内存DC
    HDC hdcMem = CreateCompatibleDC(hdc);
    if (!hdcMem) {
        return;
    }
    
    HBITMAP hOldBitmap = (HBITMAP)SelectObject(hdcMem, g_hSeparatorBitmap);
    
    // 获取位图信息
    BITMAP bm;
    if (!GetObject(g_hSeparatorBitmap, sizeof(BITMAP), &bm)) {
        SelectObject(hdcMem, hOldBitmap);
        DeleteDC(hdcMem);
        return;
    }
    
    // 使用高质量缩放模式
    SetStretchBltMode(hdc, HALFTONE);
    SetBrushOrgEx(hdc, 0, 0, NULL);
    
    // 尝试使用透明绘制（对XP分隔线非常重要）
    HMODULE hMsImg32 = LoadLibraryA("msimg32.dll");
    if (hMsImg32) {
        typedef BOOL (WINAPI *PFN_TransparentBlt)(HDC, int, int, int, int, HDC, int, int, int, int, UINT);
        PFN_TransparentBlt pfnTransparentBlt = (PFN_TransparentBlt)GetProcAddress(hMsImg32, "TransparentBlt");
        
        if (pfnTransparentBlt) {
            // 使用浅灰色作为透明色
            COLORREF transparentColor = RGB(192, 192, 192);
            
            BOOL result = pfnTransparentBlt(
                hdc, x, y, width, height,
                hdcMem, 0, 0, bm.bmWidth, bm.bmHeight,
                transparentColor
            );
            
            if (result) {
                // 清理资源
                SelectObject(hdcMem, hOldBitmap);
                DeleteDC(hdcMem);
                FreeLibrary(hMsImg32);
                return;
            }
        }
        FreeLibrary(hMsImg32);
    }
    
    // 如果透明绘制失败，回退到标准绘制
    StretchBlt(
        hdc, x, y, width, height,
        hdcMem, 0, 0, bm.bmWidth, bm.bmHeight,
        SRCCOPY
    );
    
    // 清理资源
    SelectObject(hdcMem, hOldBitmap);
    DeleteDC(hdcMem);
}
