// gdiplus_api.h — GDI+ 平面 API 的动态加载与启动/关闭
#pragma once

#include <windows.h>

namespace winver {
namespace gdip {

constexpr int kOk = 0;

// 惰性加载 gdiplus.dll 并 GdiplusStartup；幂等，成功返回 true。
bool Initialize();

// 关闭并卸载 gdiplus.dll（在释放所有 GDI+ 图像之后调用）。
void Shutdown();

bool Available();

// —— 我们用到的平面 API 包装 ——
int  CreateBitmapFromStream(IStream* stream, void** outImage);
int  GetImageWidth(void* image, UINT* width);
int  GetImageHeight(void* image, UINT* height);
void DisposeImage(void* image);
int  CreateFromHDC(HDC hdc, void** graphics);
void SetSmoothingMode(void* graphics, int mode);
void SetInterpolationMode(void* graphics, int mode);
int  DrawImageRectI(void* graphics, void* image, int x, int y, int w, int h);
void DeleteGraphics(void* graphics);

enum SmoothingMode {
    SmoothingModeHighQuality = 2,
    SmoothingModeAntiAlias = 4,
};
enum InterpolationMode {
    InterpolationModeHighQualityBicubic = 7,
};

} // namespace gdip
} // namespace winver
