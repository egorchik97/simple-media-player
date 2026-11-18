#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>
#include <winrt/Windows.Data.Json.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cwctype>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "windowsapp.lib")

namespace media {

struct LyricsLine { double seconds = 0; std::wstring text; };
struct LyricsSnapshot {
    std::wstring source = L"LRCLIB external catalog";
    std::wstring status = L"\u041e\u0436\u0438\u0434\u0430\u043d\u0438\u0435 \u043c\u0435\u0442\u0430\u0434\u0430\u043d\u043d\u044b\u0445 \u0442\u0440\u0435\u043a\u0430";
    std::wstring text;
    std::vector<LyricsLine> lines;
    std::wstring DisplayAt(double seconds) const;
};

namespace lyrics_detail {
std::wstring Trim(std::wstring_view s);
bool Number(std::wstring_view s, double& v);
std::vector<LyricsLine> ParseLrc(std::wstring_view input);
std::wstring Encode(std::wstring_view s);
LyricsSnapshot ParseJson(std::string_view body);
}

class LyricsEngine {
public:
    LyricsEngine();
    ~LyricsEngine();

    LyricsEngine(const LyricsEngine&) = delete;
    LyricsEngine& operator=(const LyricsEngine&) = delete;

    void Request(std::wstring title, std::wstring artist, double duration);
    void Clear();
    LyricsSnapshot Snapshot() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace media
