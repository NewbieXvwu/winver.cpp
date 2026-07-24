// raii.h — 轻量 RAII 句柄封装（纯头文件）
// 用最小的 unique_resource 风格替换手工 cleanup / goto / 疑似双重释放。
#pragma once

#include <windows.h>
#include <utility>

namespace winver {

// 通用 unique 句柄：Traits 提供 pointer 类型、无效值 invalid() 与 close()。
template <class Traits>
class UniqueHandle {
public:
    using pointer = typename Traits::pointer;

    UniqueHandle() noexcept : h_(Traits::invalid()) {}
    explicit UniqueHandle(pointer h) noexcept : h_(h) {}
    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;

    UniqueHandle(UniqueHandle&& o) noexcept : h_(o.release()) {}
    UniqueHandle& operator=(UniqueHandle&& o) noexcept {
        if (this != &o) { reset(o.release()); }
        return *this;
    }
    ~UniqueHandle() { reset(); }

    pointer get() const noexcept { return h_; }
    explicit operator bool() const noexcept { return h_ != Traits::invalid(); }

    pointer release() noexcept {
        pointer h = h_;
        h_ = Traits::invalid();
        return h;
    }
    void reset(pointer h = Traits::invalid()) noexcept {
        if (h_ != Traits::invalid()) { Traits::close(h_); }
        h_ = h;
    }

private:
    pointer h_;
};

struct HModuleTraits {
    using pointer = HMODULE;
    static pointer invalid() noexcept { return nullptr; }
    static void close(pointer h) noexcept { FreeLibrary(h); }
};
using UniqueHModule = UniqueHandle<HModuleTraits>;

struct HKeyTraits {
    using pointer = HKEY;
    static pointer invalid() noexcept { return nullptr; }
    static void close(pointer h) noexcept { RegCloseKey(h); }
};
using UniqueHKey = UniqueHandle<HKeyTraits>;

// GDI 对象（HBITMAP/HBRUSH/HFONT 等），用 DeleteObject 释放。
struct GdiObjTraits {
    using pointer = HGDIOBJ;
    static pointer invalid() noexcept { return nullptr; }
    static void close(pointer h) noexcept { DeleteObject(h); }
};
class UniqueGdiObj {
public:
    UniqueGdiObj() noexcept : h_(nullptr) {}
    explicit UniqueGdiObj(HGDIOBJ h) noexcept : h_(h) {}
    UniqueGdiObj(const UniqueGdiObj&) = delete;
    UniqueGdiObj& operator=(const UniqueGdiObj&) = delete;
    UniqueGdiObj(UniqueGdiObj&& o) noexcept : h_(o.release()) {}
    UniqueGdiObj& operator=(UniqueGdiObj&& o) noexcept {
        if (this != &o) { reset(o.release()); }
        return *this;
    }
    ~UniqueGdiObj() { reset(); }

    HGDIOBJ get() const noexcept { return h_; }
    HBITMAP bitmap() const noexcept { return static_cast<HBITMAP>(h_); }
    HBRUSH brush() const noexcept { return static_cast<HBRUSH>(h_); }
    HFONT font() const noexcept { return static_cast<HFONT>(h_); }
    explicit operator bool() const noexcept { return h_ != nullptr; }

    HGDIOBJ release() noexcept { HGDIOBJ h = h_; h_ = nullptr; return h; }
    void reset(HGDIOBJ h = nullptr) noexcept {
        if (h_) { DeleteObject(h_); }
        h_ = h;
    }
private:
    HGDIOBJ h_;
};

// CreateCompatibleDC 得到的内存 DC，用 DeleteDC 释放。
class UniqueMemDC {
public:
    UniqueMemDC() noexcept : dc_(nullptr) {}
    explicit UniqueMemDC(HDC dc) noexcept : dc_(dc) {}
    UniqueMemDC(const UniqueMemDC&) = delete;
    UniqueMemDC& operator=(const UniqueMemDC&) = delete;
    UniqueMemDC(UniqueMemDC&& o) noexcept : dc_(o.release()) {}
    UniqueMemDC& operator=(UniqueMemDC&& o) noexcept {
        if (this != &o) { reset(o.release()); }
        return *this;
    }
    ~UniqueMemDC() { reset(); }

    HDC get() const noexcept { return dc_; }
    explicit operator bool() const noexcept { return dc_ != nullptr; }
    HDC release() noexcept { HDC d = dc_; dc_ = nullptr; return d; }
    void reset(HDC d = nullptr) noexcept {
        if (dc_) { DeleteDC(dc_); }
        dc_ = d;
    }
private:
    HDC dc_;
};

// 构造时 SelectObject，析构时还原原对象。
class DcSelectGuard {
public:
    DcSelectGuard(HDC dc, HGDIOBJ obj) noexcept
        : dc_(dc), old_(obj ? SelectObject(dc, obj) : nullptr) {}
    DcSelectGuard(const DcSelectGuard&) = delete;
    DcSelectGuard& operator=(const DcSelectGuard&) = delete;
    ~DcSelectGuard() { if (dc_ && old_) { SelectObject(dc_, old_); } }
    bool ok() const noexcept { return old_ != nullptr; }
private:
    HDC dc_;
    HGDIOBJ old_;
};

// GlobalAlloc 内存，用 GlobalFree 释放。
class UniqueHGlobal {
public:
    UniqueHGlobal() noexcept : h_(nullptr) {}
    explicit UniqueHGlobal(HGLOBAL h) noexcept : h_(h) {}
    UniqueHGlobal(const UniqueHGlobal&) = delete;
    UniqueHGlobal& operator=(const UniqueHGlobal&) = delete;
    UniqueHGlobal(UniqueHGlobal&& o) noexcept : h_(o.release()) {}
    UniqueHGlobal& operator=(UniqueHGlobal&& o) noexcept {
        if (this != &o) { reset(o.release()); }
        return *this;
    }
    ~UniqueHGlobal() { reset(); }

    HGLOBAL get() const noexcept { return h_; }
    explicit operator bool() const noexcept { return h_ != nullptr; }
    HGLOBAL release() noexcept { HGLOBAL h = h_; h_ = nullptr; return h; }
    void reset(HGLOBAL h = nullptr) noexcept {
        if (h_) { GlobalFree(h_); }
        h_ = h;
    }
private:
    HGLOBAL h_;
};

// COM 接口智能指针（Release）。
template <class T>
class ComPtr {
public:
    ComPtr() noexcept : p_(nullptr) {}
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;
    ComPtr(ComPtr&& o) noexcept : p_(o.p_) { o.p_ = nullptr; }
    ComPtr& operator=(ComPtr&& o) noexcept {
        if (this != &o) { reset(); p_ = o.p_; o.p_ = nullptr; }
        return *this;
    }
    ~ComPtr() { reset(); }

    T** put() noexcept { reset(); return &p_; }
    T* get() const noexcept { return p_; }
    T* operator->() const noexcept { return p_; }
    explicit operator bool() const noexcept { return p_ != nullptr; }
    void reset() noexcept { if (p_) { p_->Release(); p_ = nullptr; } }
private:
    T* p_;
};

} // namespace winver
