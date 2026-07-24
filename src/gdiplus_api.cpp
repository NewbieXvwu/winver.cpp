// gdiplus_api.cpp
#include "gdiplus_api.h"
#include "app_state.h"
#include "win_util.h"

namespace winver {
namespace gdip {

namespace {

struct GdiplusStartupInput {
    UINT32 GdiplusVersion = 1;
    void* DebugEventCallback = nullptr;
    BOOL SuppressBackgroundThread = FALSE;
    BOOL SuppressExternalCodecs = FALSE;
};

using FnStartup            = int (WINAPI*)(ULONG_PTR*, const GdiplusStartupInput*, void*);
using FnShutdown           = void (WINAPI*)(ULONG_PTR);
using FnCreateFromStream   = int (WINAPI*)(void*, void**);
using FnGetImageWidth      = int (WINAPI*)(void*, UINT*);
using FnGetImageHeight     = int (WINAPI*)(void*, UINT*);
using FnDisposeImage       = int (WINAPI*)(void*);
using FnCreateFromHDC      = int (WINAPI*)(HDC, void**);
using FnSetSmoothingMode   = int (WINAPI*)(void*, int);
using FnSetInterpMode      = int (WINAPI*)(void*, int);
using FnDrawImageRectI     = int (WINAPI*)(void*, void*, int, int, int, int);
using FnDeleteGraphics     = int (WINAPI*)(void*);

HMODULE g_module = nullptr;
ULONG_PTR g_token = 0;
bool g_initialized = false;

FnStartup            pStartup = nullptr;
FnShutdown           pShutdown = nullptr;
FnCreateFromStream   pCreateFromStream = nullptr;
FnGetImageWidth      pGetImageWidth = nullptr;
FnGetImageHeight     pGetImageHeight = nullptr;
FnDisposeImage       pDisposeImage = nullptr;
FnCreateFromHDC      pCreateFromHDC = nullptr;
FnSetSmoothingMode   pSetSmoothingMode = nullptr;
FnSetInterpMode      pSetInterpMode = nullptr;
FnDrawImageRectI     pDrawImageRectI = nullptr;
FnDeleteGraphics     pDeleteGraphics = nullptr;

} // namespace

bool Available() { return g_initialized; }

bool Initialize() {
    if (g_initialized) return true;

    g_module = LoadLibraryA("gdiplus.dll");
    if (!g_module) return false;

    pStartup          = GetProc<FnStartup>(g_module, "GdiplusStartup");
    pShutdown         = GetProc<FnShutdown>(g_module, "GdiplusShutdown");
    pCreateFromStream = GetProc<FnCreateFromStream>(g_module, "GdipCreateBitmapFromStream");
    pGetImageWidth    = GetProc<FnGetImageWidth>(g_module, "GdipGetImageWidth");
    pGetImageHeight   = GetProc<FnGetImageHeight>(g_module, "GdipGetImageHeight");
    pDisposeImage     = GetProc<FnDisposeImage>(g_module, "GdipDisposeImage");
    pCreateFromHDC    = GetProc<FnCreateFromHDC>(g_module, "GdipCreateFromHDC");
    pSetSmoothingMode = GetProc<FnSetSmoothingMode>(g_module, "GdipSetSmoothingMode");
    pSetInterpMode    = GetProc<FnSetInterpMode>(g_module, "GdipSetInterpolationMode");
    pDrawImageRectI   = GetProc<FnDrawImageRectI>(g_module, "GdipDrawImageRectI");
    pDeleteGraphics   = GetProc<FnDeleteGraphics>(g_module, "GdipDeleteGraphics");

    if (!pStartup || !pShutdown || !pCreateFromStream || !pGetImageWidth ||
        !pGetImageHeight || !pDisposeImage || !pCreateFromHDC || !pSetSmoothingMode ||
        !pSetInterpMode || !pDrawImageRectI || !pDeleteGraphics) {
        FreeLibrary(g_module);
        g_module = nullptr;
        return false;
    }

    GdiplusStartupInput input;
    if (pStartup(&g_token, &input, nullptr) != kOk) {
        FreeLibrary(g_module);
        g_module = nullptr;
        return false;
    }

    g_initialized = true;
    return true;
}

void Shutdown() {
    if (g_initialized && g_token && pShutdown) {
        pShutdown(g_token);
        g_token = 0;
        g_initialized = false;
    }
    if (g_module) {
        FreeLibrary(g_module);
        g_module = nullptr;
        pStartup = nullptr; pShutdown = nullptr; pCreateFromStream = nullptr;
        pGetImageWidth = nullptr; pGetImageHeight = nullptr; pDisposeImage = nullptr;
        pCreateFromHDC = nullptr; pSetSmoothingMode = nullptr; pSetInterpMode = nullptr;
        pDrawImageRectI = nullptr; pDeleteGraphics = nullptr;
    }
}

int  CreateBitmapFromStream(IStream* s, void** out) { return pCreateFromStream ? pCreateFromStream(s, out) : -1; }
int  GetImageWidth(void* img, UINT* w) { return pGetImageWidth ? pGetImageWidth(img, w) : -1; }
int  GetImageHeight(void* img, UINT* h) { return pGetImageHeight ? pGetImageHeight(img, h) : -1; }
void DisposeImage(void* img) { if (pDisposeImage && img) pDisposeImage(img); }
int  CreateFromHDC(HDC hdc, void** g) { return pCreateFromHDC ? pCreateFromHDC(hdc, g) : -1; }
void SetSmoothingMode(void* g, int m) { if (pSetSmoothingMode) pSetSmoothingMode(g, m); }
void SetInterpolationMode(void* g, int m) { if (pSetInterpMode) pSetInterpMode(g, m); }
int  DrawImageRectI(void* g, void* img, int x, int y, int w, int h) {
    return pDrawImageRectI ? pDrawImageRectI(g, img, x, y, w, h) : -1;
}
void DeleteGraphics(void* g) { if (pDeleteGraphics && g) pDeleteGraphics(g); }

} // namespace gdip

// LogoAssets 析构：释放 GDI+ 图像（GDI 位图由 UniqueGdiObj 自动释放）。
LogoAssets::~LogoAssets() {
    if (gdipImage) {
        gdip::DisposeImage(gdipImage);
        gdipImage = nullptr;
    }
}

} // namespace winver
