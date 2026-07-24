// test_main.cpp — 纯逻辑单元测试（控制台 exe，可在 Wine headless 下运行）
// 覆盖 FormatWindowsVersion / IsXpLook / ChooseLogo / ComputeLayout。
#include <cstdio>
#include <cstring>
#include <string>

#include "os_info.h"
#include "app_window.h"
#include "dpi.h"

using namespace winver;

static int g_failures = 0;
static int g_checks = 0;

static void CheckStr(const char* what, const std::string& got, const std::string& want) {
    ++g_checks;
    if (got != want) {
        ++g_failures;
        std::printf("FAIL [%s]\n  got : \"%s\"\n  want: \"%s\"\n", what, got.c_str(), want.c_str());
    }
}

static void CheckInt(const char* what, long got, long want) {
    ++g_checks;
    if (got != want) {
        ++g_failures;
        std::printf("FAIL [%s]\n  got : %ld\n  want: %ld\n", what, got, want);
    }
}

static void CheckBool(const char* what, bool cond) {
    ++g_checks;
    if (!cond) {
        ++g_failures;
        std::printf("FAIL [%s] (condition false)\n", what);
    }
}

// —— OsFacts 构造助手 ——
static OsFacts MakeNt(DWORD major, DWORD minor, DWORD build,
                      bool isServer = false, const char* csd = "",
                      const char* productName = "", const char* displayVersion = "",
                      DWORD ubr = 0) {
    OsFacts f;
    f.platformId = VER_PLATFORM_WIN32_NT;
    f.major = major; f.minor = minor; f.build = build; f.ubr = ubr;
    std::strncpy(f.csdVersion, csd, sizeof(f.csdVersion) - 1);
    f.productName = productName;
    f.displayVersion = displayVersion;
    f.isServer = isServer;
    return f;
}

static OsFacts Make9x(DWORD minor, DWORD build, const char* csd = "") {
    OsFacts f;
    f.platformId = VER_PLATFORM_WIN32_WINDOWS;
    f.major = 4; f.minor = minor; f.build = build;
    std::strncpy(f.csdVersion, csd, sizeof(f.csdVersion) - 1);
    return f;
}

static void TestVersionStrings() {
    CheckStr("Win95", FormatWindowsVersion(Make9x(0, 950)),
             "Windows 95\r\n(Build 4.00.0950)");
    CheckStr("Win98", FormatWindowsVersion(Make9x(10, 1998)),
             "Windows 98\r\n(Build 4.10.1998)");
    CheckStr("Win98SE", FormatWindowsVersion(Make9x(10, 2222)),
             "Windows 98 SE\r\n(Build 4.10.2222)");
    CheckStr("WinMe", FormatWindowsVersion(Make9x(90, 3000)),
             "Windows Me\r\n(Build 4.90.3000)");

    CheckStr("Win2000", FormatWindowsVersion(MakeNt(5, 0, 2195)),
             "Windows 2000\r\n(Build 2195)");
    CheckStr("WinXP", FormatWindowsVersion(MakeNt(5, 1, 2600)),
             "Windows XP\r\n(Build 2600)");
    CheckStr("WinXP-SP3", FormatWindowsVersion(MakeNt(5, 1, 2600, false, "Service Pack 3")),
             "Windows XP SP3\r\n(Build 2600)");
    CheckStr("WinXPx64", FormatWindowsVersion(MakeNt(5, 2, 3790, false)),
             "Windows XP x64\r\n(Build 3790)");
    CheckStr("Server2003", FormatWindowsVersion(MakeNt(5, 2, 3790, true)),
             "Windows Server 2003\r\n(Build 3790)");
    CheckStr("Vista", FormatWindowsVersion(MakeNt(6, 0, 6002, false)),
             "Windows Vista\r\n(Build 6002)");
    CheckStr("Server2008", FormatWindowsVersion(MakeNt(6, 0, 6002, true)),
             "Windows Server 2008\r\n(Build 6002)");
    CheckStr("Win7", FormatWindowsVersion(MakeNt(6, 1, 7601, false)),
             "Windows 7\r\n(Build 7601)");
    CheckStr("Server2008R2", FormatWindowsVersion(MakeNt(6, 1, 7601, true)),
             "Windows Server 2008 R2\r\n(Build 7601)");
    CheckStr("Win8", FormatWindowsVersion(MakeNt(6, 2, 9200, false)),
             "Windows 8\r\n(Build 9200)");
    CheckStr("Server2012", FormatWindowsVersion(MakeNt(6, 2, 9200, true)),
             "Windows Server 2012\r\n(Build 9200)");
    CheckStr("Win81", FormatWindowsVersion(MakeNt(6, 3, 9600, false)),
             "Windows 8.1\r\n(Build 9600)");
    CheckStr("Server2012R2", FormatWindowsVersion(MakeNt(6, 3, 9600, true)),
             "Windows Server 2012 R2\r\n(Build 9600)");

    CheckStr("Win10-22H2", FormatWindowsVersion(MakeNt(10, 0, 19045, false, "", "", "22H2", 3570)),
             "Windows 10 22H2\r\n(Build 19045.3570)");
    CheckStr("Win10-noDisplayVer", FormatWindowsVersion(MakeNt(10, 0, 19045, false, "", "", "", 3570)),
             "Windows 10\r\n(Build 19045.3570)");
    CheckStr("Win11-23H2", FormatWindowsVersion(MakeNt(10, 0, 22631, false, "", "", "23H2", 2861)),
             "Windows 11 23H2\r\n(Build 22631.2861)");
    // 现代 Server：保留旧代码 "Windows " + ProductName 的行为。
    CheckStr("Server2022", FormatWindowsVersion(MakeNt(10, 0, 20348, true, "", "Windows Server 2022", "", 1000)),
             "Windows Windows Server 2022\r\n(Build 20348.1000)");
}

static void TestXpLook() {
    CheckBool("IsXpLook 5.1", IsXpLook(MakeNt(5, 1, 2600)));
    CheckBool("IsXpLook 5.2", IsXpLook(MakeNt(5, 2, 3790)));
    CheckBool("IsXpLook !5.0", !IsXpLook(MakeNt(5, 0, 2195)));
    CheckBool("IsXpLook !6.0", !IsXpLook(MakeNt(6, 0, 6000)));
    CheckBool("IsXpLook !10", !IsXpLook(MakeNt(10, 0, 19045)));
}

static void TestChooseLogo() {
    // XP 32 位按编辑版分派（覆盖死分支修复）。
    LogoChoice c;
    c = ChooseLogo(MakeNt(5, 1, 2600, false, "", "Microsoft Windows XP Professional"));
    CheckInt("XP-Pro dll", c.dll, LogoChoice::Shell32);
    CheckInt("XP-Pro id", c.resourceId, 131);
    CheckBool("XP-Pro isXP", c.isXP);

    c = ChooseLogo(MakeNt(5, 1, 2600, false, "", "Microsoft Windows XP Home Edition"));
    CheckInt("XP-Home id", c.resourceId, 147);
    c = ChooseLogo(MakeNt(5, 1, 2600, false, "", "Windows XP Embedded"));
    CheckInt("XP-Embedded id", c.resourceId, 149);
    c = ChooseLogo(MakeNt(5, 1, 2600, false, "", "Windows XP"));
    CheckInt("XP-default id", c.resourceId, 131);

    // 5.2 非服务器 → moricons/131（此前永不可达）。
    c = ChooseLogo(MakeNt(5, 2, 3790, false));
    CheckInt("XPx64 dll", c.dll, LogoChoice::Moricons);
    CheckInt("XPx64 id", c.resourceId, 131);
    CheckBool("XPx64 isXP", c.isXP);

    // 5.2 服务器 → basebrd/2123。
    c = ChooseLogo(MakeNt(5, 2, 3790, true));
    CheckInt("Server2003 dll", c.dll, LogoChoice::Basebrd);
    CheckInt("Server2003 id", c.resourceId, 2123);
    CheckBool("Server2003 !isXP", !c.isXP);

    // Windows 2000（5.0）→ shell32/131。
    c = ChooseLogo(MakeNt(5, 0, 2195));
    CheckInt("Win2000 dll", c.dll, LogoChoice::Shell32);
    CheckInt("Win2000 id", c.resourceId, 131);

    // 8 / 8.1 → basebrd/2121。
    c = ChooseLogo(MakeNt(6, 2, 9200));
    CheckInt("Win8 id", c.resourceId, 2121);
    c = ChooseLogo(MakeNt(6, 3, 9600));
    CheckInt("Win81 id", c.resourceId, 2121);

    // Vista（6.0）与 10 → basebrd/2123。
    c = ChooseLogo(MakeNt(6, 0, 6000));
    CheckInt("Vista id", c.resourceId, 2123);
    c = ChooseLogo(MakeNt(10, 0, 19045));
    CheckInt("Win10 id", c.resourceId, 2123);
}

static void TestLayout() {
    const UINT dpis[] = {96, 120, 144, 192};
    for (UINT dpi : dpis) {
        for (int logo = 0; logo < 2; ++logo) {
            for (int ml = 0; ml < 2; ++ml) {
                LayoutInput in;
                in.dpi = dpi;
                in.clientWidth = ScaleForDpi(320, dpi);
                in.isXP = false;
                in.logoLoaded = (logo != 0);
                in.logoWidth = 400; in.logoHeight = 100;
                in.isMultiline = (ml != 0);
                Layout out = ComputeLayout(in);

                char tag[64];
                // 按钮高度遮蔽 bug 修复：有 logo→35，无 logo→30（随 DPI 缩放）。
                std::snprintf(tag, sizeof(tag), "btnH dpi=%u logo=%d", dpi, logo);
                CheckInt(tag, out.button.h, in.logoLoaded ? ScaleForDpi(35, dpi) : ScaleForDpi(30, dpi));

                // 文本水平居中。
                std::snprintf(tag, sizeof(tag), "textX dpi=%u", dpi);
                CheckInt(tag, out.text.x, (in.clientWidth - out.text.w) / 2);

                // 按钮位于文本下方，不重叠。
                std::snprintf(tag, sizeof(tag), "no-overlap dpi=%u logo=%d ml=%d", dpi, logo, ml);
                CheckBool(tag, out.button.y >= out.text.y + out.text.h);

                // 按钮水平居中。
                std::snprintf(tag, sizeof(tag), "btnX dpi=%u", dpi);
                CheckInt(tag, out.button.x, (in.clientWidth - out.button.w) / 2);
            }
        }
    }

    // XP 有 logo：文本偏移应大于 logo 计算高度（贴顶 logo + 分隔条 + 间距）。
    LayoutInput xp;
    xp.dpi = 96;
    xp.clientWidth = 360;
    xp.isXP = true;
    xp.logoLoaded = true;
    xp.logoWidth = 360; xp.logoHeight = 120;   // aspect=3 → logoHeight=clientWidth/3=120
    xp.separatorLoaded = true; xp.separatorHeight = 10;
    xp.isMultiline = false;
    Layout xpOut = ComputeLayout(xp);
    CheckBool("XP text below logo+sep", xpOut.text.y >= 120 + 10);
    CheckBool("XP button below text", xpOut.button.y >= xpOut.text.y + xpOut.text.h);
    CheckInt("XP btnH has-logo", xpOut.button.h, ScaleForDpi(35, 96));
}

int main() {
    TestVersionStrings();
    TestXpLook();
    TestChooseLogo();
    TestLayout();

    if (g_failures == 0) {
        std::printf("OK: all %d checks passed\n", g_checks);
        return 0;
    }
    std::printf("FAILED: %d/%d checks failed\n", g_failures, g_checks);
    return 1;
}
