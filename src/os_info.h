// os_info.h — OS 版本检测/命名、Logo 分派（纯逻辑可测）
#pragma once

#include <windows.h>
#include <string>

namespace winver {

// 系统事实的纯数据快照：所有版本/命名/分派逻辑仅依赖它，便于单测穷举。
struct OsFacts {
    DWORD platformId = VER_PLATFORM_WIN32_NT;
    DWORD major = 0;
    DWORD minor = 0;
    DWORD build = 0;
    DWORD ubr = 0;                 // Win10+ 注册表 UBR
    char  csdVersion[128] = {0};   // szCSDVersion（Service Pack 描述）
    std::string productName;       // 注册表 ProductName
    std::string displayVersion;    // 注册表 DisplayVersion，回退 ReleaseId
    std::string installationType;  // 注册表 InstallationType
    bool  isServer = false;        // 由 installationType 派生
};

// 收集真实系统信息（GetVersionExA + 注册表），进程内缓存一次。
const OsFacts& GetOsFacts();

// —— 纯函数（可单测）——

// 复刻旧 GetWindowsVersion 的版本字符串（含 \r\n 与 Build 段）。
std::string FormatWindowsVersion(const OsFacts& f);

// 是否 XP 外观（5.1 / 5.2），用于布局与绘制分支。
bool IsXpLook(const OsFacts& f);

// Logo 资源分派（修正了旧代码中永不可达的 5.2 分支）。
struct LogoChoice {
    enum Dll { Basebrd, Shell32, Moricons } dll = Basebrd;
    int resourceId = 2123;
    bool isXP = false;  // 走 XP 直接加载 + 备用 ID 回退逻辑
};
LogoChoice ChooseLogo(const OsFacts& f);

// 现代 UI 可用（major >= 6）。
inline bool IsModernUI(const OsFacts& f) { return f.major >= 6; }

} // namespace winver
