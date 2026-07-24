// logo_loader.cpp
#include "logo_loader.h"
#include "gdiplus_api.h"
#include "raii.h"

#include <shlwapi.h>

namespace winver {

namespace {

// 打开包含 logo 的资源 DLL；basebrd 依次尝试多个候选路径。
UniqueHModule OpenResourceDll(const LogoChoice& c) {
    char path[MAX_PATH] = {0};

    if (c.dll == LogoChoice::Shell32 || c.dll == LogoChoice::Moricons) {
        GetSystemDirectoryA(path, MAX_PATH);
        PathCombineA(path, path, c.dll == LogoChoice::Shell32 ? "shell32.dll" : "moricons.dll");
        return UniqueHModule(LoadLibraryA(path));
    }

    // Basebrd：三个候选路径。
    if (HMODULE m = LoadLibraryA("C:\\Windows\\Branding\\Basebrd\\basebrd.dll")) {
        return UniqueHModule(m);
    }
    char sysDir[MAX_PATH] = {0};
    GetSystemDirectoryA(sysDir, MAX_PATH);
    PathCombineA(path, sysDir, "..\\Branding\\Basebrd\\basebrd.dll");
    if (HMODULE m = LoadLibraryA(path)) {
        return UniqueHModule(m);
    }
    char winDir[MAX_PATH] = {0};
    GetWindowsDirectoryA(winDir, MAX_PATH);
    PathCombineA(path, winDir, "Branding\\Basebrd\\basebrd.dll");
    return UniqueHModule(LoadLibraryA(path));
}

bool StoreBitmap(HBITMAP hbmp, LogoAssets& a) {
    if (!hbmp) return false;
    BITMAP bm;
    if (GetObjectA(hbmp, sizeof(bm), &bm)) {
        a.bitmap.reset(hbmp);
        a.width = bm.bmWidth;
        a.height = bm.bmHeight;
        a.loaded = true;
        return true;
    }
    DeleteObject(hbmp);
    return false;
}

bool TryLoadBitmapId(HMODULE dll, int id, LogoAssets& a) {
    return StoreBitmap(LoadBitmapA(dll, MAKEINTRESOURCEA(id)), a);
}

// GDI+ 路径：从 IMAGE/PNG 资源解码。
bool TryLoadGdipImage(HMODULE dll, int id, LogoAssets& a) {
    HRSRC info = FindResourceA(dll, MAKEINTRESOURCEA(id), "IMAGE");
    if (!info) info = FindResourceA(dll, MAKEINTRESOURCEA(id), "PNG");
    if (!info) return false;

    HGLOBAL resData = LoadResource(dll, info);
    if (!resData) return false;
    DWORD size = SizeofResource(dll, info);
    if (size == 0) return false;
    void* src = LockResource(resData);
    if (!src) return false;

    UniqueHGlobal buffer(GlobalAlloc(GMEM_MOVEABLE, size));
    if (!buffer) return false;
    void* dst = GlobalLock(buffer.get());
    if (!dst) return false;
    CopyMemory(dst, src, size);
    GlobalUnlock(buffer.get());

    ComPtr<IStream> stream;
    // fDeleteOnRelease=TRUE：流接管 hGlobal，Release 时释放，故此处转移所有权。
    if (FAILED(CreateStreamOnHGlobal(buffer.get(), TRUE, stream.put()))) {
        return false;
    }
    buffer.release();

    void* image = nullptr;
    if (gdip::CreateBitmapFromStream(stream.get(), &image) == gdip::kOk && image) {
        UINT w = 0, h = 0;
        gdip::GetImageWidth(image, &w);
        gdip::GetImageHeight(image, &h);
        a.gdipImage = image;
        a.width = static_cast<int>(w);
        a.height = static_cast<int>(h);
        a.loaded = true;
        return true;
    }
    if (image) gdip::DisposeImage(image);
    return false;
}

// RT_BITMAP 原始位图路径。
bool TryLoadRawBitmap(HMODULE dll, int id, LogoAssets& a) {
    HRSRC info = FindResourceA(dll, MAKEINTRESOURCEA(id), RT_BITMAP);
    if (!info) return false;
    HGLOBAL resData = LoadResource(dll, info);
    if (!resData) return false;
    auto* header = static_cast<LPBITMAPINFOHEADER>(LockResource(resData));
    if (!header) return false;

    int colorTableSize = 0;
    if (header->biBitCount <= 8) {
        colorTableSize = (1 << header->biBitCount) * sizeof(RGBQUAD);
    }
    BYTE* bits = reinterpret_cast<BYTE*>(header) + header->biSize + colorTableSize;

    HDC hdc = GetDC(nullptr);
    HBITMAP hbmp = CreateDIBitmap(hdc, header, CBM_INIT, bits,
                                  reinterpret_cast<BITMAPINFO*>(header), DIB_RGB_COLORS);
    ReleaseDC(nullptr, hdc);
    return StoreBitmap(hbmp, a);
}

} // namespace

bool LoadWindowsLogo(const OsFacts& f, LogoAssets& a) {
    if (a.loaded) return true;

    const bool gdiplusAvailable = gdip::Initialize();
    const LogoChoice choice = ChooseLogo(f);

    UniqueHModule dll = OpenResourceDll(choice);
    if (!dll) return false;

    // 1) 直接位图资源。
    if (TryLoadBitmapId(dll.get(), choice.resourceId, a)) return true;

    // 2) GDI+ 图像（IMAGE/PNG）。
    if (gdiplusAvailable && TryLoadGdipImage(dll.get(), choice.resourceId, a)) return true;

    // 3) RT_BITMAP 原始位图。
    if (TryLoadRawBitmap(dll.get(), choice.resourceId, a)) return true;

    // 4) 备用资源 ID。
    if (choice.isXP) {
        if (!(f.major == 5 && f.minor == 2)) {
            // 32 位 XP：尝试其它编辑版 ID。
            const int altIds[] = {131, 147, 149};
            for (int id : altIds) {
                if (id == choice.resourceId) continue;
                if (TryLoadBitmapId(dll.get(), id, a)) return true;
            }
        }
    } else {
        const int altId = (choice.resourceId == 2123) ? 2121 : 2123;
        if (TryLoadBitmapId(dll.get(), altId, a)) return true;
    }

    return false;
}

bool LoadXpSeparator(const OsFacts& f, LogoAssets& a) {
    if (a.sepLoaded) return true;
    if (f.major != 5) return false;

    char path[MAX_PATH] = {0};
    GetSystemDirectoryA(path, MAX_PATH);
    PathCombineA(path, path, "shell32.dll");
    UniqueHModule dll(LoadLibraryA(path));
    if (!dll) return false;

    HBITMAP hbmp = LoadBitmapA(dll.get(), MAKEINTRESOURCEA(138));
    if (!hbmp) return false;
    BITMAP bm;
    if (GetObjectA(hbmp, sizeof(bm), &bm)) {
        a.separator.reset(hbmp);
        a.sepWidth = bm.bmWidth;
        a.sepHeight = bm.bmHeight;
        a.sepLoaded = true;
        return true;
    }
    DeleteObject(hbmp);
    return false;
}

} // namespace winver
