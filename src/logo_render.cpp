// logo_render.cpp
#include "logo_render.h"
#include "gdiplus_api.h"
#include "raii.h"
#include "win_util.h"

namespace winver {

namespace {

using PfnAlphaBlend = BOOL (WINAPI*)(HDC, int, int, int, int, HDC, int, int, int, int, BLENDFUNCTION);
using PfnTransparentBlt = BOOL (WINAPI*)(HDC, int, int, int, int, HDC, int, int, int, int, UINT);

struct DrawRect { int x, y, w, h; };

DrawRect ComputeLogoRect(HDC hdc, const LogoAssets& a, bool isXP,
                         int x, int y, int width, int height) {
    DrawRect r{};
    const float aspect = static_cast<float>(a.width) / static_cast<float>(a.height);
    if (isXP) {
        RECT rc{};
        GetClientRect(WindowFromDC(hdc), &rc);
        r.w = rc.right - rc.left;   // 填满宽度
        r.h = static_cast<int>(r.w / aspect);
        r.x = 0;
        r.y = 0;
    } else {
        r.w = width;
        r.h = static_cast<int>(width / aspect);
        if (r.h > height) {
            r.h = height;
            r.w = static_cast<int>(height * aspect);
        }
        r.x = x + (width - r.w) / 2;
        r.y = y;
    }
    return r;
}

// 预乘 Alpha：按亮度推导透明度，Win10+ 与旧系统采用不同阈值。
bool TryPreMultipliedAlpha(HDC hdc, HDC hdcMem, const DrawRect& d, BITMAP& bm,
                           bool isWin10OrLater, PfnAlphaBlend pfnAlphaBlend) {
    UniqueMemDC temp(CreateCompatibleDC(hdc));
    if (!temp) return false;

    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = bm.bmWidth;
    bi.bmiHeader.biHeight = bm.bmHeight;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* pvBits = nullptr;
    UniqueGdiObj dib(CreateDIBSection(temp.get(), &bi, DIB_RGB_COLORS, &pvBits, nullptr, 0));
    if (!dib || !pvBits) return false;

    DcSelectGuard sel(temp.get(), dib.get());
    if (!sel.ok()) return false;
    if (!BitBlt(temp.get(), 0, 0, bm.bmWidth, bm.bmHeight, hdcMem, 0, 0, SRCCOPY)) return false;

    const int stride = bm.bmWidth * 4;
    const int imageSize = stride * bm.bmHeight;
    BYTE* pBits = static_cast<BYTE*>(pvBits);

    for (int i = 0; i < bm.bmHeight; ++i) {
        for (int j = 0; j < bm.bmWidth; ++j) {
            int offset = i * stride + j * 4;
            if (offset < 0 || offset >= imageSize - 3) continue;

            BYTE b = pBits[offset + 0];
            BYTE g = pBits[offset + 1];
            BYTE r = pBits[offset + 2];
            int luminance = static_cast<int>(0.299 * r + 0.587 * g + 0.114 * b);

            if (isWin10OrLater) {
                if (luminance >= 200)       pBits[offset + 3] = 0;
                else if (luminance >= 185)  pBits[offset + 3] = 10;
                else if (luminance >= 170)  pBits[offset + 3] = 20;
                else if (luminance >= 150)  pBits[offset + 3] = 64;
                else if (luminance >= 130)  pBits[offset + 3] = 128;
                else if (luminance >= 110)  pBits[offset + 3] = 192;
                else                        pBits[offset + 3] = 255;

                if (pBits[offset + 3] > 0 && pBits[offset + 3] < 255) {
                    bool hasHighLumNeighbor = false;
                    const int neighbors[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
                    for (auto& n : neighbors) {
                        int ni = i + n[0];
                        int nj = j + n[1];
                        if (ni < 0 || ni >= bm.bmHeight || nj < 0 || nj >= bm.bmWidth) continue;
                        int nOffset = ni * stride + nj * 4;
                        if (nOffset < 0 || nOffset >= imageSize - 3) continue;
                        int nLum = static_cast<int>(0.299 * pBits[nOffset + 2] +
                                                    0.587 * pBits[nOffset + 1] +
                                                    0.114 * pBits[nOffset + 0]);
                        if (nLum > 180) { hasHighLumNeighbor = true; break; }
                    }
                    if (hasHighLumNeighbor) {
                        pBits[offset + 3] = static_cast<BYTE>(pBits[offset + 3] * 0.7);
                    }
                }
            } else {
                if (luminance >= 240)       pBits[offset + 3] = 0;
                else if (luminance >= 230)  pBits[offset + 3] = 64;
                else if (luminance >= 210)  pBits[offset + 3] = 128;
                else                        pBits[offset + 3] = 255;
            }

            if (pBits[offset + 3] < 255) {
                float alpha = pBits[offset + 3] / 255.0f;
                pBits[offset + 0] = static_cast<BYTE>(b * alpha);
                pBits[offset + 1] = static_cast<BYTE>(g * alpha);
                pBits[offset + 2] = static_cast<BYTE>(r * alpha);
            }
        }
    }

    BLENDFUNCTION bf = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    SetStretchBltMode(hdc, HALFTONE);
    SetBrushOrgEx(hdc, 0, 0, nullptr);
    return pfnAlphaBlend(hdc, d.x, d.y, d.w, d.h, temp.get(), 0, 0,
                         bm.bmWidth, bm.bmHeight, bf) != FALSE;
}

// 标准 Alpha：接近白色 → 全透明，其余不透明。
bool TryStandardAlpha(HDC hdc, HDC hdcMem, const DrawRect& d, BITMAP& bm,
                      PfnAlphaBlend pfnAlphaBlend) {
    UniqueMemDC temp(CreateCompatibleDC(hdc));
    if (!temp) return false;

    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = bm.bmWidth;
    bi.bmiHeader.biHeight = bm.bmHeight;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* pvBits = nullptr;
    UniqueGdiObj dib(CreateDIBSection(temp.get(), &bi, DIB_RGB_COLORS, &pvBits, nullptr, 0));
    if (!dib || !pvBits) return false;

    DcSelectGuard sel(temp.get(), dib.get());
    BitBlt(temp.get(), 0, 0, bm.bmWidth, bm.bmHeight, hdcMem, 0, 0, SRCCOPY);

    BYTE* pBits = static_cast<BYTE*>(pvBits);
    for (int i = 0; i < bm.bmHeight; ++i) {
        for (int j = 0; j < bm.bmWidth; ++j) {
            BYTE b = pBits[0], g = pBits[1], r = pBits[2];
            pBits[3] = (r > 240 && g > 240 && b > 240) ? 0 : 255;
            pBits += 4;
        }
    }

    BLENDFUNCTION bf = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    SetStretchBltMode(hdc, HALFTONE);
    SetBrushOrgEx(hdc, 0, 0, nullptr);
    return pfnAlphaBlend(hdc, d.x, d.y, d.w, d.h, temp.get(), 0, 0,
                         bm.bmWidth, bm.bmHeight, bf) != FALSE;
}

// 黑白掩码方法：适用于任何 Windows 版本。
bool TryMaskMethod(HDC hdc, HDC hdcMem, const DrawRect& d, BITMAP& bm) {
    UniqueMemDC maskDc(CreateCompatibleDC(hdc));
    UniqueGdiObj mask(CreateBitmap(bm.bmWidth, bm.bmHeight, 1, 1, nullptr));
    if (!maskDc || !mask) return false;

    DcSelectGuard selMask(maskDc.get(), mask.get());
    SetBkColor(hdcMem, RGB(255, 255, 255));
    SetTextColor(hdcMem, RGB(0, 0, 0));
    BitBlt(maskDc.get(), 0, 0, bm.bmWidth, bm.bmHeight, hdcMem, 0, 0, SRCCOPY);

    SetStretchBltMode(hdc, HALFTONE);
    SetBrushOrgEx(hdc, 0, 0, nullptr);

    UniqueMemDC colorDc(CreateCompatibleDC(hdc));
    UniqueGdiObj colorBmp(CreateCompatibleBitmap(hdc, bm.bmWidth, bm.bmHeight));
    if (colorDc && colorBmp) {
        DcSelectGuard selColor(colorDc.get(), colorBmp.get());
        BitBlt(colorDc.get(), 0, 0, bm.bmWidth, bm.bmHeight, hdcMem, 0, 0, SRCCOPY);
        BitBlt(colorDc.get(), 0, 0, bm.bmWidth, bm.bmHeight, maskDc.get(), 0, 0, SRCAND);
        SetStretchBltMode(hdc, HALFTONE);
        SetBrushOrgEx(hdc, 0, 0, nullptr);
        StretchBlt(hdc, d.x, d.y, d.w, d.h, colorDc.get(), 0, 0, bm.bmWidth, bm.bmHeight, SRCCOPY);
    } else {
        StretchBlt(hdc, d.x, d.y, d.w, d.h, maskDc.get(), 0, 0, bm.bmWidth, bm.bmHeight, SRCCOPY);
    }
    return true;
}

// 备用透明方法：灰度 + 掩码合成。
bool TryAlternativeTransparency(HDC hdc, HDC hdcMem, const DrawRect& d, BITMAP& bm,
                                bool darkMode) {
    UniqueMemDC grayDc(CreateCompatibleDC(hdc));
    UniqueGdiObj grayBmp(CreateCompatibleBitmap(hdc, bm.bmWidth, bm.bmHeight));
    UniqueMemDC maskDc(CreateCompatibleDC(hdc));
    UniqueGdiObj maskBmp(CreateBitmap(bm.bmWidth, bm.bmHeight, 1, 1, nullptr));
    if (!grayDc || !grayBmp || !maskDc || !maskBmp) return false;

    DcSelectGuard selGray(grayDc.get(), grayBmp.get());
    DcSelectGuard selMask(maskDc.get(), maskBmp.get());

    BitBlt(grayDc.get(), 0, 0, bm.bmWidth, bm.bmHeight, hdcMem, 0, 0, SRCCOPY);
    SetBkColor(grayDc.get(), RGB(255, 255, 255));
    SetTextColor(grayDc.get(), RGB(0, 0, 0));
    BitBlt(maskDc.get(), 0, 0, bm.bmWidth, bm.bmHeight, grayDc.get(), 0, 0, SRCCOPY);

    if (darkMode) {
        SetBkColor(hdc, RGB(0, 0, 0));
        BitBlt(grayDc.get(), 0, 0, bm.bmWidth, bm.bmHeight, maskDc.get(), 0, 0, SRCAND);
        SetStretchBltMode(hdc, HALFTONE);
        SetBrushOrgEx(hdc, 0, 0, nullptr);
        StretchBlt(hdc, d.x, d.y, d.w, d.h, grayDc.get(), 0, 0, bm.bmWidth, bm.bmHeight, SRCPAINT);
    } else {
        BitBlt(grayDc.get(), 0, 0, bm.bmWidth, bm.bmHeight, maskDc.get(), 0, 0, SRCAND);
        SetStretchBltMode(hdc, HALFTONE);
        SetBrushOrgEx(hdc, 0, 0, nullptr);
        StretchBlt(hdc, d.x, d.y, d.w, d.h, grayDc.get(), 0, 0, bm.bmWidth, bm.bmHeight, SRCCOPY);
    }
    return true;
}

// 兜底：32 位 DIB + 简单白键，AlphaBlend 绘制。
bool TryWhiteKeyAlpha(HDC hdc, HDC hdcMem, const DrawRect& d, BITMAP& bm,
                      PfnAlphaBlend pfnAlphaBlend) {
    UniqueMemDC temp(CreateCompatibleDC(hdc));
    if (!temp) return false;

    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = bm.bmWidth;
    bi.bmiHeader.biHeight = bm.bmHeight;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* pvBits = nullptr;
    UniqueGdiObj dib(CreateDIBSection(temp.get(), &bi, DIB_RGB_COLORS, &pvBits, nullptr, 0));
    if (!dib || !pvBits) return false;

    DcSelectGuard sel(temp.get(), dib.get());
    BitBlt(temp.get(), 0, 0, bm.bmWidth, bm.bmHeight, hdcMem, 0, 0, SRCCOPY);

    BYTE* pBits = static_cast<BYTE*>(pvBits);
    for (int i = 0; i < bm.bmHeight; ++i) {
        for (int j = 0; j < bm.bmWidth; ++j) {
            BYTE b = pBits[0], g = pBits[1], r = pBits[2];
            pBits[3] = (r > 250 && g > 250 && b > 250) ? 0 : 255;
            pBits += 4;
        }
    }

    BLENDFUNCTION bf = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    SetStretchBltMode(hdc, HALFTONE);
    SetBrushOrgEx(hdc, 0, 0, nullptr);
    return pfnAlphaBlend(hdc, d.x, d.y, d.w, d.h, temp.get(), 0, 0,
                         bm.bmWidth, bm.bmHeight, bf) != FALSE;
}

void DrawWithGdi(HDC hdc, const LogoAssets& a, const OsFacts& f, bool darkMode,
                 const DrawRect& d) {
    BITMAP bm;
    if (!GetObjectA(a.bitmap.get(), sizeof(bm), &bm)) return;

    UniqueMemDC memdc(CreateCompatibleDC(hdc));
    if (!memdc) return;
    DcSelectGuard sel(memdc.get(), a.bitmap.get());

    const bool isWin10OrLater = f.major >= 10;

    UniqueHModule msimg(LoadLibraryA("msimg32.dll"));
    if (msimg) {
        auto pAlpha = GetProc<PfnAlphaBlend>(msimg.get(), "AlphaBlend");
        auto pTrans = GetProc<PfnTransparentBlt>(msimg.get(), "TransparentBlt");

        bool success = false;
        if (!success && pAlpha) success = TryPreMultipliedAlpha(hdc, memdc.get(), d, bm, isWin10OrLater, pAlpha);
        if (!success && pAlpha) success = TryStandardAlpha(hdc, memdc.get(), d, bm, pAlpha);
        if (!success && pTrans) {
            SetStretchBltMode(hdc, HALFTONE);
            SetBrushOrgEx(hdc, 0, 0, nullptr);
            success = pTrans(hdc, d.x, d.y, d.w, d.h, memdc.get(), 0, 0,
                             a.width, a.height, RGB(255, 255, 255)) != FALSE;
        }
        if (!success) success = TryAlternativeTransparency(hdc, memdc.get(), d, bm, darkMode);
        if (!success && pAlpha) success = TryWhiteKeyAlpha(hdc, memdc.get(), d, bm, pAlpha);
        if (success) return;
    }

    if (TryMaskMethod(hdc, memdc.get(), d, bm)) return;

    if (!darkMode) {
        UniqueHModule m2(LoadLibraryA("msimg32.dll"));
        if (m2) {
            auto pTrans = GetProc<PfnTransparentBlt>(m2.get(), "TransparentBlt");
            if (pTrans) {
                SetStretchBltMode(hdc, HALFTONE);
                SetBrushOrgEx(hdc, 0, 0, nullptr);
                if (pTrans(hdc, d.x, d.y, d.w, d.h, memdc.get(), 0, 0,
                           a.width, a.height, RGB(255, 255, 255))) {
                    return;
                }
            }
        }
    }

    SetStretchBltMode(hdc, HALFTONE);
    SetBrushOrgEx(hdc, 0, 0, nullptr);
    StretchBlt(hdc, d.x, d.y, d.w, d.h, memdc.get(), 0, 0, a.width, a.height, SRCCOPY);
}

} // namespace

void DrawWindowsLogo(HDC hdc, const LogoAssets& a, const OsFacts& f, bool darkMode,
                     int x, int y, int width, int height) {
    if (!a.loaded) return;

    const bool isXP = IsXpLook(f);
    const DrawRect d = ComputeLogoRect(hdc, a, isXP, x, y, width, height);

    // 优先 GDI+。
    if (a.gdipImage && gdip::Available()) {
        void* graphics = nullptr;
        if (gdip::CreateFromHDC(hdc, &graphics) == gdip::kOk && graphics) {
            gdip::SetSmoothingMode(graphics, gdip::SmoothingModeHighQuality);
            gdip::SetInterpolationMode(graphics, gdip::InterpolationModeHighQualityBicubic);
            gdip::DrawImageRectI(graphics, a.gdipImage, d.x, d.y, d.w, d.h);
            gdip::DeleteGraphics(graphics);
            return;
        }
    }

    if (a.bitmap) {
        DrawWithGdi(hdc, a, f, darkMode, d);
    }
}

void DrawXpSeparator(HDC hdc, const LogoAssets& a, int x, int y, int width, int height) {
    if (!a.sepLoaded || !a.separator) return;

    UniqueMemDC memdc(CreateCompatibleDC(hdc));
    if (!memdc) return;
    DcSelectGuard sel(memdc.get(), a.separator.get());

    BITMAP bm;
    if (!GetObjectA(a.separator.get(), sizeof(bm), &bm)) return;

    SetStretchBltMode(hdc, HALFTONE);
    SetBrushOrgEx(hdc, 0, 0, nullptr);

    UniqueHModule msimg(LoadLibraryA("msimg32.dll"));
    if (msimg) {
        auto pTrans = GetProc<PfnTransparentBlt>(msimg.get(), "TransparentBlt");
        if (pTrans) {
            if (pTrans(hdc, x, y, width, height, memdc.get(), 0, 0,
                       bm.bmWidth, bm.bmHeight, RGB(192, 192, 192))) {
                return;
            }
        }
    }

    StretchBlt(hdc, x, y, width, height, memdc.get(), 0, 0, bm.bmWidth, bm.bmHeight, SRCCOPY);
}

} // namespace winver
