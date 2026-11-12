#pragma once
#include <windows.h>
#include <shlobj.h>
#include <string>
#include <utility>
#include <limits>
#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Ole32.lib")

struct PopupConfig {
    bool cover = true, title = true, artist = true, pause = true;
    bool compactPrevious = false, compactNext = false;
    bool hover = true, pinned = false, volume = true, asr = false;
    int delay = 700;
    int theme = 0, opacity = 100, glow = 35, roundness = 18;
    bool glass = true, sourceLyrics = false;
    int customBackground = RGB(20, 23, 32), customAccent = RGB(114, 164, 255);
    int customText = RGB(239, 242, 247), customMuted = RGB(166, 175, 192);
    bool compactLyrics = false;
    int compactWidth = 280;
    int compactHeight = 70;
    int compactLyricsHeight = 102;
    int glassStrength = 50, glassTint = 35, glassSpeed = 30;
    bool glassAnimated = true;
};

namespace popup_config_detail {
inline bool AbsolutePath(const std::wstring& path, std::wstring& result) {
    if (path.empty() || path.find(L'\0') != std::wstring::npos) {
        SetLastError(ERROR_INVALID_NAME); return false;
    }
    DWORD count = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
    if (!count) return false;
    std::wstring buffer(count, L'\0');
    DWORD written = GetFullPathNameW(path.c_str(), count, buffer.data(), nullptr);
    if (!written) return false;
    if (written >= count) { SetLastError(ERROR_INSUFFICIENT_BUFFER); return false; }
    buffer.resize(written);
    result = std::move(buffer);
    return true;
}
inline bool Read(const std::wstring& path, const wchar_t* key, int& value) {
    wchar_t text[64]{};
    DWORD n = GetPrivateProfileStringW(L"Settings", key, L"", text, 64, path.c_str());
    if (!n || n >= 63) return false;
    int parsed = 0;
    for (DWORD i = 0; i < n; ++i) {
        if (text[i] < L'0' || text[i] > L'9') return false;
        int digit = text[i] - L'0';
        if (parsed > ((std::numeric_limits<int>::max)() - digit) / 10) return false;
        parsed = parsed * 10 + digit;
    }
    value = parsed;
    return true;
}
inline void ReadBool(const std::wstring& path, const wchar_t* key, bool& value) {
    wchar_t text[4]{};
    DWORD n = GetPrivateProfileStringW(L"Settings", key, L"", text, 4, path.c_str());
    if (n == 1 && (text[0] == L'0' || text[0] == L'1')) value = text[0] == L'1';
}
inline void ReadBounded(const std::wstring& path, const wchar_t* key, int& value, int low, int high) {
    int parsed = 0;
    if (Read(path, key, parsed) && parsed >= low && parsed <= high) value = parsed;
}
struct TemporaryFile {
    std::wstring path;
    HANDLE handle = INVALID_HANDLE_VALUE;
    ~TemporaryFile() {
        DWORD error = GetLastError();
        if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
        if (!path.empty()) DeleteFileW(path.c_str());
        SetLastError(error);
    }
};
}

// Missing or malformed individual values retain their defaults.
inline PopupConfig LoadConfig(const std::wstring& path) {
    PopupConfig config;
    std::wstring full;
    if (!popup_config_detail::AbsolutePath(path, full)) return config;
    using popup_config_detail::ReadBool;
    ReadBool(full, L"cover", config.cover);
    ReadBool(full, L"title", config.title);
    ReadBool(full, L"artist", config.artist);
    ReadBool(full, L"pause", config.pause);
    ReadBool(full, L"compactPrevious", config.compactPrevious);
    ReadBool(full, L"compactNext", config.compactNext);
    ReadBool(full, L"hover", config.hover);
    ReadBool(full, L"pinned", config.pinned);
    ReadBool(full, L"volume", config.volume);
    ReadBool(full, L"asr", config.asr);
    ReadBool(full, L"glass", config.glass);
    ReadBool(full, L"sourceLyrics", config.sourceLyrics);
    ReadBool(full, L"compactLyrics", config.compactLyrics);
    popup_config_detail::ReadBounded(full, L"compactWidth", config.compactWidth, 200, 900);
    popup_config_detail::ReadBounded(full, L"compactHeight", config.compactHeight, 50, 600);
    popup_config_detail::ReadBounded(full, L"compactLyricsHeight", config.compactLyricsHeight, 80, 600);
    ReadBool(full, L"glassAnimated", config.glassAnimated);
    popup_config_detail::ReadBounded(full, L"theme", config.theme, 0, 5);
    popup_config_detail::ReadBounded(full, L"customBackground", config.customBackground, 0, 0xFFFFFF);
    popup_config_detail::ReadBounded(full, L"customAccent", config.customAccent, 0, 0xFFFFFF);
    popup_config_detail::ReadBounded(full, L"customText", config.customText, 0, 0xFFFFFF);
    popup_config_detail::ReadBounded(full, L"customMuted", config.customMuted, 0, 0xFFFFFF);
    popup_config_detail::ReadBounded(full, L"glassStrength", config.glassStrength, 0, 100);
    popup_config_detail::ReadBounded(full, L"glassTint", config.glassTint, 0, 100);
    popup_config_detail::ReadBounded(full, L"glassSpeed", config.glassSpeed, 0, 100);
    popup_config_detail::ReadBounded(full, L"opacity", config.opacity, 45, 100);
    popup_config_detail::ReadBounded(full, L"glow", config.glow, 0, 100);
    popup_config_detail::ReadBounded(full, L"roundness", config.roundness, 0, 28);
    popup_config_detail::Read(full, L"delay", config.delay);
    return config;
}

// Failure preserves the destination; GetLastError describes the failure.
inline bool SaveConfig(const std::wstring& path, const PopupConfig& config) {
    try {
        if (config.delay < 0 || config.theme < 0 || config.theme > 5 ||
            config.customBackground < 0 || config.customBackground > 0xFFFFFF ||
            config.customAccent < 0 || config.customAccent > 0xFFFFFF ||
            config.customText < 0 || config.customText > 0xFFFFFF ||
            config.customMuted < 0 || config.customMuted > 0xFFFFFF ||
            config.glassStrength < 0 || config.glassStrength > 100 ||
            config.glassTint < 0 || config.glassTint > 100 ||
            config.glassSpeed < 0 || config.glassSpeed > 100 ||
            config.opacity < 45 || config.opacity > 100 || config.glow < 0 || config.glow > 100 ||
            config.roundness < 0 || config.roundness > 28 ||
            config.compactWidth < 200 || config.compactWidth > 900 ||
            config.compactHeight < 50 || config.compactHeight > 600 ||
            config.compactLyricsHeight < 80 || config.compactLyricsHeight > 600) {
            SetLastError(ERROR_INVALID_PARAMETER); return false;
        }
        std::wstring full;
        if (!popup_config_detail::AbsolutePath(path, full)) return false;
        std::wstring directory = full.substr(0, full.find_last_of(L"\\/") + 1);
        popup_config_detail::TemporaryFile temp;
        wchar_t name[MAX_PATH]{};
        if (!GetTempFileNameW(directory.c_str(), L"mpc", 0, name)) return false;
        try { temp.path = name; }
        catch (...) { DeleteFileW(name); throw; }
        temp.handle = CreateFileW(name, GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
        if (temp.handle == INVALID_HANDLE_VALUE) return false;
        const WORD bom = 0xFEFF;
        DWORD written = 0;
        if (!WriteFile(temp.handle, &bom, sizeof(bom), &written, nullptr)) return false;
        if (written != sizeof(bom)) { SetLastError(ERROR_WRITE_FAULT); return false; }
        if (!FlushFileBuffers(temp.handle)) return false;
        HANDLE handle = temp.handle;
        temp.handle = INVALID_HANDLE_VALUE;
        if (!CloseHandle(handle)) return false;
        std::wstring text=L"\xFEFF[Settings]\r\n";
        auto put = [&](const wchar_t* key, bool value) {
            text += key; text += value ? L"=1\r\n" : L"=0\r\n";
        };
        put(L"cover",config.cover); put(L"title",config.title);
        put(L"artist",config.artist); put(L"pause",config.pause);
        put(L"compactPrevious",config.compactPrevious); put(L"compactNext",config.compactNext);
        put(L"hover",config.hover); put(L"pinned",config.pinned); put(L"volume",config.volume); put(L"asr",config.asr);
        text += L"delay=" + std::to_wstring(config.delay) + L"\r\n";
        put(L"glass", config.glass); put(L"sourceLyrics", config.sourceLyrics);
        put(L"compactLyrics", config.compactLyrics); put(L"glassAnimated", config.glassAnimated);
        text += L"compactWidth=" + std::to_wstring(config.compactWidth) + L"\r\n";
        text += L"compactHeight=" + std::to_wstring(config.compactHeight) + L"\r\n";
        text += L"compactLyricsHeight=" + std::to_wstring(config.compactLyricsHeight) + L"\r\n";
        text += L"customBackground=" + std::to_wstring(config.customBackground) + L"\r\n";
        text += L"customAccent=" + std::to_wstring(config.customAccent) + L"\r\n";
        text += L"customText=" + std::to_wstring(config.customText) + L"\r\n";
        text += L"customMuted=" + std::to_wstring(config.customMuted) + L"\r\n";
        text += L"glassStrength=" + std::to_wstring(config.glassStrength) + L"\r\n";
        text += L"glassTint=" + std::to_wstring(config.glassTint) + L"\r\n";
        text += L"glassSpeed=" + std::to_wstring(config.glassSpeed) + L"\r\n";
        text += L"theme=" + std::to_wstring(config.theme) + L"\r\n";
        text += L"opacity=" + std::to_wstring(config.opacity) + L"\r\n";
        text += L"glow=" + std::to_wstring(config.glow) + L"\r\n";
        text += L"roundness=" + std::to_wstring(config.roundness) + L"\r\n";
        temp.handle = CreateFileW(name, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
        if (temp.handle == INVALID_HANDLE_VALUE) return false;
        DWORD bytes=static_cast<DWORD>(text.size()*sizeof(wchar_t));
        if (!WriteFile(temp.handle,text.data(),bytes,&written,nullptr) || written!=bytes) return false;
        if (!FlushFileBuffers(temp.handle)) return false;
        handle = temp.handle;
        temp.handle = INVALID_HANDLE_VALUE;
        if (!CloseHandle(handle)) return false;
        if (!MoveFileExW(name, full.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return false;
        temp.path.clear();
        return true;
    } catch (...) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return false;
    }
}


inline std::wstring ConfigPath() {
    PWSTR local = nullptr;
    HRESULT hr = SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &local);
    if (FAILED(hr)) {
        CoTaskMemFree(local);
        SetLastError(HRESULT_FACILITY(hr) == FACILITY_WIN32 ? HRESULT_CODE(hr) : ERROR_PATH_NOT_FOUND);
        return {};
    }
    try {
        std::wstring folder(local);
        CoTaskMemFree(local);
        local = nullptr;
        folder += L"\\MediaPopup";
        if (!CreateDirectoryW(folder.c_str(), nullptr)) {
            if (GetLastError() != ERROR_ALREADY_EXISTS) return {};
            DWORD attributes = GetFileAttributesW(folder.c_str());
            if (attributes == INVALID_FILE_ATTRIBUTES) return {};
            if (!(attributes & FILE_ATTRIBUTE_DIRECTORY)) { SetLastError(ERROR_DIRECTORY); return {}; }
        }
        return folder + L"\\settings.ini";
    } catch (...) {
        CoTaskMemFree(local);
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return {};
    }
}

// Oversized popups align to the work area's top-left on the affected axis.
inline RECT PopupRect(POINT anchor, int w, int h, RECT work) {
    auto axis = [](LONG position, int size, LONG low, LONG high, LONG& first, LONG& last) {
        const long long extent = size > 0 ? size : 0;
        long long limit = static_cast<long long>(high) - extent;
        if (limit < low) limit = low;
        long long start = position;
        if (start < low) start = low;
        if (start > limit) start = limit;
        first = static_cast<LONG>(start);
        long long end = start + extent;
        if (end > (std::numeric_limits<LONG>::max)()) end = (std::numeric_limits<LONG>::max)();
        last = static_cast<LONG>(end);
    };
    RECT result{};
    axis(anchor.x, w, work.left, work.right, result.left, result.right);
    axis(anchor.y, h, work.top, work.bottom, result.top, result.bottom);
    return result;
}

struct HoverState {
    ULONGLONG outsideSince = 0;
    bool armed = true;
    // Separate timer validity makes a first sample at tick zero unambiguous.
    bool trackingOutside = false;

    bool Collapse(bool inside, bool blocked, ULONGLONG now, int delay) {
        if (inside || blocked) {
            outsideSince = 0;
            trackingOutside = false;
            return false;
        }
        if (!trackingOutside || now < outsideSince) {
            outsideSince = now;
            trackingOutside = true;
        }
        if (now - outsideSince < static_cast<ULONGLONG>(delay > 0 ? delay : 0)) return false;
        armed = false;
        return true;
    }
};
