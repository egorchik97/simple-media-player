#include "Config.h"

#include <limits>
#include <shlobj.h>
#include <string>
#include <filesystem>
#include <charconv>

#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Ole32.lib")

namespace media {

namespace {

bool AbsolutePath(const std::wstring& path, std::wstring& result) {
    if (path.empty() || path.find(L'\0') != std::wstring::npos) {
        SetLastError(ERROR_INVALID_NAME);
        return false;
    }
    DWORD count = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
    if (!count) return false;
    std::wstring buffer(count, L'\0');
    DWORD written = GetFullPathNameW(path.c_str(), count, buffer.data(), nullptr);
    if (!written) return false;
    if (written >= count) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return false;
    }
    buffer.resize(written);
    result = std::move(buffer);
    return true;
}

bool ReadInt(const std::wstring& path, const wchar_t* key, int& value) {
    wchar_t text[64]{};
    DWORD n = GetPrivateProfileStringW(L"Settings", key, L"", text, 64, path.c_str());
    if (!n || n >= 63) return false;
    std::string ascii;
    for (DWORD i = 0; i < n; ++i) { if (text[i] > 127) return false; ascii += static_cast<char>(text[i]); }
    int parsed = 0;
    const auto result = std::from_chars(ascii.data(), ascii.data()+ascii.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != ascii.data()+ascii.size()) return false;
    value = parsed;
    return true;
}

void ReadBool(const std::wstring& path, const wchar_t* key, bool& value) {
    wchar_t text[4]{};
    DWORD n = GetPrivateProfileStringW(L"Settings", key, L"", text, 4, path.c_str());
    if (n == 1 && (text[0] == L'0' || text[0] == L'1')) value = text[0] == L'1';
}

void ReadBounded(const std::wstring& path, const wchar_t* key, int& value, int low, int high) {
    int parsed = 0;
    if (ReadInt(path, key, parsed) && parsed >= low && parsed <= high) value = parsed;
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

bool CreateTemporary(const std::wstring& destination, TemporaryFile& temp) {
    GUID guid{};
    const HRESULT hr = CoCreateGuid(&guid);
    if (FAILED(hr)) { SetLastError(HRESULT_CODE(hr)); return false; }
    wchar_t id[40]{};
    StringFromGUID2(guid, id, 40);
    temp.path = destination + L"." + id + L".tmp";
    temp.handle = CreateFileW(temp.path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    return temp.handle != INVALID_HANDLE_VALUE;
}

} // namespace

std::wstring ConfigPath() {
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
        return folder + L"\\SimpleMediaPlayer\\settings.ini";
    } catch (...) {
        CoTaskMemFree(local);
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return {};
    }
}

ConfigManager::ConfigManager() {
    configPath_ = media::ConfigPath();
    pathError_ = configPath_.empty() ? GetLastError() : ERROR_SUCCESS;
    if (!configPath_.empty()) legacyPath_ = (std::filesystem::path(configPath_).parent_path().parent_path() / L"MediaPopup/settings.ini").wstring();
    pathValid_ = !configPath_.empty();
}

ConfigManager::ConfigManager(std::wstring root) {
    if (!std::filesystem::path(root).is_absolute() || root.find(L'\0') != std::wstring::npos) {
        pathError_ = ERROR_INVALID_NAME; return;
    }
    configPath_ = (std::filesystem::path(root) / L"SimpleMediaPlayer" / L"settings.ini").wstring();
    legacyPath_ = (std::filesystem::path(root) / L"MediaPopup" / L"settings.ini").wstring();
    pathValid_ = true;
}

bool ConfigManager::Prepare() {
    if (!pathValid_) { SetLastError(pathError_ ? pathError_ : ERROR_INVALID_NAME); return false; }
    const auto directory = std::filesystem::path(configPath_).parent_path().wstring();
    // Ensure directory exists
    const int result = SHCreateDirectoryExW(nullptr, directory.c_str(), nullptr);
    if (result != ERROR_SUCCESS && result != ERROR_ALREADY_EXISTS && result != ERROR_FILE_EXISTS) {
        OutputDebugStringW((L"SHCreateDirectoryExW failed: " + std::to_wstring(result) + L" for " + directory + L"\n").c_str());
        SetLastError(static_cast<DWORD>(result)); return false;
    }
    const DWORD attributes = GetFileAttributesW(directory.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        DWORD gle = GetLastError();
        OutputDebugStringW((L"GetFileAttributesW failed for directory: " + directory + L" error=" + std::to_wstring(gle) + L"\n").c_str());
        SetLastError(gle);
        return false;
    }
    if (!(attributes & FILE_ATTRIBUTE_DIRECTORY)) { SetLastError(ERROR_DIRECTORY); return false; }
    const DWORD target = GetFileAttributesW(configPath_.c_str());
    if (target != INVALID_FILE_ATTRIBUTES) return true;
    DWORD error = GetLastError();
    if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND) return false;
    if (GetFileAttributesW(legacyPath_.c_str()) == INVALID_FILE_ATTRIBUTES) {
        error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) { SetLastError(ERROR_SUCCESS); return true; }
        return false;
    }
    // Copy to a unique sibling then publish without REPLACE_EXISTING. Neither
    // the legacy original nor a concurrently created target may be overwritten.
    TemporaryFile temp;
    if (!CreateTemporary(configPath_, temp)) {
        OutputDebugStringW(L"CreateTemporary failed\n");
        return false;
    }
    CloseHandle(temp.handle); temp.handle = INVALID_HANDLE_VALUE;
    if (!CopyFileW(legacyPath_.c_str(), temp.path.c_str(), FALSE)) {
        OutputDebugStringW(L"CopyFileW failed\n");
        return false;
    }
    if (!MoveFileExW(temp.path.c_str(), configPath_.c_str(), MOVEFILE_WRITE_THROUGH)) {
        error = GetLastError();
        OutputDebugStringW((L"MoveFileExW failed: " + std::to_wstring(error) + L"\n").c_str());
        if (error == ERROR_ALREADY_EXISTS || error == ERROR_FILE_EXISTS) return true;
        return false;
    }
    temp.path.clear();
    return true;
}

ConfigManager::~ConfigManager() = default;

bool ConfigManager::Load(Config& config) {
    if (!Prepare()) return false;

    std::wstring full;
    if (!AbsolutePath(configPath_, full)) return false;

    HANDLE input = CreateFileW(full.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (input == INVALID_HANDLE_VALUE) {
        if (GetLastError() == ERROR_FILE_NOT_FOUND) { SetLastError(ERROR_SUCCESS); return true; }
        return false;
    }
    if (!CloseHandle(input)) return false;

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
    ReadBounded(full, L"delay", config.delay, 0, 10000);
    ReadBounded(full, L"theme", config.theme, 0, 5);
    ReadBounded(full, L"opacity", config.opacity, 45, 100);
    ReadBounded(full, L"glow", config.glow, 0, 100);
    ReadBounded(full, L"roundness", config.roundness, 0, 28);
    ReadBool(full, L"glass", config.glass);
    ReadBool(full, L"sourceLyrics", config.sourceLyrics);
    ReadBool(full, L"compactLyrics", config.compactLyrics);
    ReadBool(full, L"glassAnimated", config.glassAnimated);
    ReadBounded(full, L"customBackground", config.customBackground, 0, 0xFFFFFF);
    ReadBounded(full, L"customAccent", config.customAccent, 0, 0xFFFFFF);
    ReadBounded(full, L"customText", config.customText, 0, 0xFFFFFF);
    ReadBounded(full, L"customMuted", config.customMuted, 0, 0xFFFFFF);
    ReadBounded(full, L"compactWidth", config.compactWidth, 200, 900);
    ReadBounded(full, L"compactHeight", config.compactHeight, 50, 600);
    ReadBounded(full, L"compactLyricsHeight", config.compactLyricsHeight, 80, 600);
    ReadBounded(full, L"glassStrength", config.glassStrength, 0, 100);
    ReadBounded(full, L"glassTint", config.glassTint, 0, 100);
    ReadBounded(full, L"glassSpeed", config.glassSpeed, 0, 100);
    ReadBounded(full, L"glassPattern", config.glassPattern, 0, 7);
    ReadBounded(full, L"glassSeed", config.glassSeed, 0, 0x7FFFFFFF);
    ReadBool(full, L"glassRandomSeed", config.glassRandomSeed);

    ReadInt(full, L"windowX", config.windowX);
    ReadInt(full, L"windowY", config.windowY);
    ReadBool(full, L"windowPositionSaved", config.windowPositionSaved);

    // Snowfall settings
    ReadBool(full, L"snowfallEnabled", config.snowfallEnabled);
    ReadBounded(full, L"snowfallCount", config.snowfallCount, 30, 150);
    ReadBounded(full, L"snowfallSpeed", config.snowfallSpeed, 1, 10);
    ReadBounded(full, L"snowfallSize", config.snowfallSize, 1, 5);
    ReadBounded(full, L"snowfallOpacity", config.snowfallOpacity, 10, 100);
    ReadBounded(full, L"snowfallWindDirection", config.snowfallWindDirection, 0, 2);
    ReadBounded(full, L"snowfallWindStrength", config.snowfallWindStrength, 0, 100);
    ReadBounded(full, L"snowfallBlur", config.snowfallBlur, 0, 3);

    return true;
}

bool ConfigManager::Save(const Config& config) {
    if (!Prepare()) return false;

    std::wstring full;
    if (!AbsolutePath(configPath_, full)) return false;

    std::wstring text = L"\xFEFF[Settings]\r\n";
    text += L"cover=" + std::to_wstring(config.cover ? 1 : 0) + L"\r\n";
    text += L"title=" + std::to_wstring(config.title ? 1 : 0) + L"\r\n";
    text += L"artist=" + std::to_wstring(config.artist ? 1 : 0) + L"\r\n";
    text += L"pause=" + std::to_wstring(config.pause ? 1 : 0) + L"\r\n";
    text += L"compactPrevious=" + std::to_wstring(config.compactPrevious ? 1 : 0) + L"\r\n";
    text += L"compactNext=" + std::to_wstring(config.compactNext ? 1 : 0) + L"\r\n";
    text += L"hover=" + std::to_wstring(config.hover ? 1 : 0) + L"\r\n";
    text += L"pinned=" + std::to_wstring(config.pinned ? 1 : 0) + L"\r\n";
    text += L"volume=" + std::to_wstring(config.volume ? 1 : 0) + L"\r\n";
    text += L"asr=" + std::to_wstring(config.asr ? 1 : 0) + L"\r\n";
    text += L"delay=" + std::to_wstring(config.delay) + L"\r\n";
    text += L"theme=" + std::to_wstring(config.theme) + L"\r\n";
    text += L"opacity=" + std::to_wstring(config.opacity) + L"\r\n";
    text += L"glow=" + std::to_wstring(config.glow) + L"\r\n";
    text += L"roundness=" + std::to_wstring(config.roundness) + L"\r\n";
    text += L"glass=" + std::to_wstring(config.glass ? 1 : 0) + L"\r\n";
    text += L"sourceLyrics=" + std::to_wstring(config.sourceLyrics ? 1 : 0) + L"\r\n";
    text += L"customBackground=" + std::to_wstring(config.customBackground) + L"\r\n";
    text += L"customAccent=" + std::to_wstring(config.customAccent) + L"\r\n";
    text += L"customText=" + std::to_wstring(config.customText) + L"\r\n";
    text += L"customMuted=" + std::to_wstring(config.customMuted) + L"\r\n";
    text += L"compactLyrics=" + std::to_wstring(config.compactLyrics ? 1 : 0) + L"\r\n";
    text += L"compactWidth=" + std::to_wstring(config.compactWidth) + L"\r\n";
    text += L"compactHeight=" + std::to_wstring(config.compactHeight) + L"\r\n";
    text += L"compactLyricsHeight=" + std::to_wstring(config.compactLyricsHeight) + L"\r\n";
    text += L"glassStrength=" + std::to_wstring(config.glassStrength) + L"\r\n";
    text += L"glassTint=" + std::to_wstring(config.glassTint) + L"\r\n";
    text += L"glassSpeed=" + std::to_wstring(config.glassSpeed) + L"\r\n";
    text += L"glassAnimated=" + std::to_wstring(config.glassAnimated ? 1 : 0) + L"\r\n";
    text += L"glassPattern=" + std::to_wstring(config.glassPattern) + L"\r\n";
    text += L"glassSeed=" + std::to_wstring(config.glassSeed) + L"\r\n";
    text += L"glassRandomSeed=" + std::to_wstring(config.glassRandomSeed ? 1 : 0) + L"\r\n";
    text += L"windowX=" + std::to_wstring(config.windowX) + L"\r\n";
    text += L"windowY=" + std::to_wstring(config.windowY) + L"\r\n";
    text += L"windowPositionSaved=" + std::to_wstring(config.windowPositionSaved ? 1 : 0) + L"\r\n";

    // Snowfall settings
    text += L"snowfallEnabled=" + std::to_wstring(config.snowfallEnabled ? 1 : 0) + L"\r\n";
    text += L"snowfallCount=" + std::to_wstring(config.snowfallCount) + L"\r\n";
    text += L"snowfallSpeed=" + std::to_wstring(config.snowfallSpeed) + L"\r\n";
    text += L"snowfallSize=" + std::to_wstring(config.snowfallSize) + L"\r\n";
    text += L"snowfallOpacity=" + std::to_wstring(config.snowfallOpacity) + L"\r\n";
    text += L"snowfallWindDirection=" + std::to_wstring(config.snowfallWindDirection) + L"\r\n";
    text += L"snowfallWindStrength=" + std::to_wstring(config.snowfallWindStrength) + L"\r\n";
    text += L"snowfallBlur=" + std::to_wstring(config.snowfallBlur) + L"\r\n";

    TemporaryFile temp;
    if (!CreateTemporary(full, temp)) return false;

    DWORD bytes = static_cast<DWORD>(text.size() * sizeof(wchar_t));
    DWORD written = 0;
    if (!WriteFile(temp.handle, text.data(), bytes, &written, nullptr)) return false;
    if (written != bytes) { SetLastError(ERROR_WRITE_FAULT); return false; }
    if (!FlushFileBuffers(temp.handle)) return false;

    const HANDLE output = temp.handle;
    temp.handle = INVALID_HANDLE_VALUE;
    if (!CloseHandle(output)) return false;

    if (!MoveFileExW(temp.path.c_str(), full.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        return false;
    }
    temp.path.clear();
    return true;
}

std::wstring ConfigManager::ConfigPath() const {
    return configPath_;
}

} // namespace media