// win_util.cpp
#include "win_util.h"
#include "raii.h"

namespace winver {

bool RegReadString(HKEY root, const char* subKey, const char* value,
                   std::string& out, REGSAM extraFlags) {
    out.clear();
    HKEY raw = nullptr;
    if (RegOpenKeyExA(root, subKey, 0, KEY_READ | extraFlags, &raw) != ERROR_SUCCESS) {
        return false;
    }
    UniqueHKey key(raw);

    char buf[512] = {0};
    DWORD size = sizeof(buf);
    DWORD type = 0;
    LONG rc = RegQueryValueExA(key.get(), value, nullptr, &type,
                               reinterpret_cast<LPBYTE>(buf), &size);
    if (rc != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ)) {
        return false;
    }
    // 保证以 NUL 结尾。
    buf[sizeof(buf) - 1] = '\0';
    out.assign(buf);
    return true;
}

bool RegReadDword(HKEY root, const char* subKey, const char* value, DWORD& out) {
    HKEY raw = nullptr;
    if (RegOpenKeyExA(root, subKey, 0, KEY_READ, &raw) != ERROR_SUCCESS) {
        return false;
    }
    UniqueHKey key(raw);

    DWORD data = 0;
    DWORD size = sizeof(data);
    DWORD type = 0;
    LONG rc = RegQueryValueExA(key.get(), value, nullptr, &type,
                              reinterpret_cast<LPBYTE>(&data), &size);
    if (rc != ERROR_SUCCESS || type != REG_DWORD) {
        return false;
    }
    out = data;
    return true;
}

} // namespace winver
