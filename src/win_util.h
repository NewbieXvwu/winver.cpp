// win_util.h — 注册表读取助手 + 动态 GetProcAddress 助手
#pragma once

#include <windows.h>
#include <string>

namespace winver {

// 读取 HKLM/HKCU 下某键的字符串值；失败返回 false，out 置空。
bool RegReadString(HKEY root, const char* subKey, const char* value,
                   std::string& out, REGSAM extraFlags = 0);

// 读取 DWORD 值；失败返回 false。
bool RegReadDword(HKEY root, const char* subKey, const char* value, DWORD& out);

// 从已加载模块按名解析函数指针（模板包装，省去到处强转）。
template <class Fn>
Fn GetProc(HMODULE mod, const char* name) {
    if (!mod) return nullptr;
    return reinterpret_cast<Fn>(reinterpret_cast<void*>(GetProcAddress(mod, name)));
}

// 从序号解析（uxtheme 的未文档化导出用得到）。
template <class Fn>
Fn GetProcByOrdinal(HMODULE mod, WORD ordinal) {
    if (!mod) return nullptr;
    return reinterpret_cast<Fn>(
        reinterpret_cast<void*>(GetProcAddress(mod, MAKEINTRESOURCEA(ordinal))));
}

} // namespace winver
