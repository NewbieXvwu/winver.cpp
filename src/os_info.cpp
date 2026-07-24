// os_info.cpp
#include "os_info.h"
#include "win_util.h"

#include <cctype>
#include <cstdio>
#include <cstring>

namespace winver {

namespace {

constexpr char kNtCurrentVersion[] = "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";

OsFacts Gather() {
    OsFacts f;

    OSVERSIONINFOA osvi = { sizeof(OSVERSIONINFOA) };
    GetVersionExA(&osvi);
    f.platformId = osvi.dwPlatformId;
    f.major = osvi.dwMajorVersion;
    f.minor = osvi.dwMinorVersion;
    f.build = osvi.dwBuildNumber;
    std::memcpy(f.csdVersion, osvi.szCSDVersion, sizeof(f.csdVersion));
    f.csdVersion[sizeof(f.csdVersion) - 1] = '\0';

    RegReadString(HKEY_LOCAL_MACHINE, kNtCurrentVersion, "ProductName", f.productName);
    RegReadString(HKEY_LOCAL_MACHINE, kNtCurrentVersion, "InstallationType", f.installationType);
    f.isServer = f.installationType.find("Server") != std::string::npos;

    RegReadDword(HKEY_LOCAL_MACHINE, kNtCurrentVersion, "UBR", f.ubr);

    if (!RegReadString(HKEY_LOCAL_MACHINE, kNtCurrentVersion, "DisplayVersion", f.displayVersion)
        || f.displayVersion.empty()) {
        RegReadString(HKEY_LOCAL_MACHINE, kNtCurrentVersion, "ReleaseId", f.displayVersion);
    }
    return f;
}

// Service Pack 后缀（复刻旧逻辑）。
std::string FormatServicePack(const char* csd) {
    if (csd[0] == '\0') return {};

    const char* p = csd;
    while (*p && !std::isdigit(static_cast<unsigned char>(*p))) ++p;
    if (*p && *p != '0') {
        return std::string(" SP") + p;
    }
    if (std::strncmp(csd, "Service Pack ", 13) == 0 &&
        std::isdigit(static_cast<unsigned char>(csd[13]))) {
        std::string sp(" SP");
        sp += csd[13];
        return sp;
    }
    return {};
}

const char* Win9xProductName(DWORD minor, DWORD build) {
    switch (minor) {
        case 0:  return "95";
        case 10: return (build & 0xFFFF) >= 2222 ? "98 SE" : "98";
        case 90: return "Me";
        default: return "Unknown";
    }
}

std::string ModernWindowsName(const OsFacts& f) {
    if (f.major == 10) {
        if (f.isServer) return f.productName;
        return (f.build >= 22000) ? "11" : "10";
    }
    return f.productName;
}

} // namespace

const OsFacts& GetOsFacts() {
    static const OsFacts facts = Gather();
    return facts;
}

bool IsXpLook(const OsFacts& f) {
    return f.major == 5 && (f.minor == 1 || f.minor == 2);
}

std::string FormatWindowsVersion(const OsFacts& f) {
    const std::string sp = FormatServicePack(f.csdVersion);
    char buf[256] = {0};

    if (f.platformId == VER_PLATFORM_WIN32_WINDOWS) {
        const char* product = Win9xProductName(f.minor, f.build);
        std::snprintf(buf, sizeof(buf), "Windows %s%s\r\n(Build %lu.%02lu.%04lu)",
                      product, sp.c_str(),
                      static_cast<unsigned long>(f.major),
                      static_cast<unsigned long>(f.minor),
                      static_cast<unsigned long>(f.build & 0xFFFF));
        return buf;
    }

    if (f.platformId != VER_PLATFORM_WIN32_NT) {
        std::snprintf(buf, sizeof(buf),
                      "Unknown Windows Platform %lu\r\n(Version %lu.%lu Build %lu%s)",
                      static_cast<unsigned long>(f.platformId),
                      static_cast<unsigned long>(f.major),
                      static_cast<unsigned long>(f.minor),
                      static_cast<unsigned long>(f.build), sp.c_str());
        return buf;
    }

    // NT 分支。
    const char* base = nullptr;
    if (f.major == 4 && f.minor == 0)       base = "NT 4.0";
    else if (f.major == 5 && f.minor == 0)  base = "2000";
    else if (f.major == 5 && f.minor == 1)  base = "XP";
    else if (f.major == 5 && f.minor == 2)  base = f.isServer ? "Server 2003" : "XP x64";
    else if (f.major == 6 && f.minor == 0)  base = f.isServer ? "Server 2008" : "Vista";
    else if (f.major == 6 && f.minor == 1)  base = f.isServer ? "Server 2008 R2" : "7";
    else if (f.major == 6 && f.minor == 2)  base = f.isServer ? "Server 2012" : "8";
    else if (f.major == 6 && f.minor == 3)  base = f.isServer ? "Server 2012 R2" : "8.1";

    char ubrBuffer[24] = "";
    if (f.major >= 10 && f.ubr > 0) {
        std::snprintf(ubrBuffer, sizeof(ubrBuffer), ".%lu",
                      static_cast<unsigned long>(f.ubr));
    }

    if (base != nullptr) {
        std::snprintf(buf, sizeof(buf), "Windows %s%s\r\n(Build %lu%s)",
                      base, sp.c_str(), static_cast<unsigned long>(f.build),
                      (f.major >= 10) ? ubrBuffer : "");
        return buf;
    }

    const std::string product = ModernWindowsName(f);
    if (f.major >= 10 && !f.isServer) {
        if (!f.displayVersion.empty()) {
            std::snprintf(buf, sizeof(buf), "Windows %s %s%s\r\n(Build %lu%s)",
                          product.c_str(), f.displayVersion.c_str(), sp.c_str(),
                          static_cast<unsigned long>(f.build), ubrBuffer);
        } else {
            std::snprintf(buf, sizeof(buf), "Windows %s%s\r\n(Build %lu%s)",
                          product.c_str(), sp.c_str(),
                          static_cast<unsigned long>(f.build), ubrBuffer);
        }
    } else {
        std::snprintf(buf, sizeof(buf), "Windows %s%s\r\n(Build %lu%s)",
                      product.c_str(), sp.c_str(),
                      static_cast<unsigned long>(f.build),
                      (f.major >= 10) ? ubrBuffer : "");
    }
    return buf;
}

LogoChoice ChooseLogo(const OsFacts& f) {
    LogoChoice c;

    if (f.major == 5) {
        if (f.minor == 2 && f.isServer) {
            // Windows Server 2003 / R2 → 标准 basebrd 路径（多半不存在，回退无 logo）。
            c.dll = LogoChoice::Basebrd;
            c.resourceId = 2123;
            c.isXP = false;
        } else if (f.minor == 2 && !f.isServer) {
            // Windows XP Professional x64 → moricons.dll, ID 131。
            c.dll = LogoChoice::Moricons;
            c.resourceId = 131;
            c.isXP = true;
        } else {
            // Windows 2000 / XP 32 位 → shell32.dll，按编辑版选 ID。
            c.dll = LogoChoice::Shell32;
            c.isXP = true;
            if (f.productName.find("Professional") != std::string::npos)      c.resourceId = 131;
            else if (f.productName.find("Home") != std::string::npos)          c.resourceId = 147;
            else if (f.productName.find("Embedded") != std::string::npos)      c.resourceId = 149;
            else                                                               c.resourceId = 131;
        }
    } else if (f.major == 6 && (f.minor == 2 || f.minor == 3)) {
        c.dll = LogoChoice::Basebrd;
        c.resourceId = 2121;  // Windows 8 / 8.1
    } else {
        c.dll = LogoChoice::Basebrd;
        c.resourceId = 2123;  // Windows 10 / 11 及其它
    }
    return c;
}

} // namespace winver
