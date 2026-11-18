#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using Session = winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSession;

struct Cover {
    winrt::com_array<uint8_t> pixels;
    uint32_t width = 0;
    uint32_t height = 0;
};

struct MediaState {
    std::wstring title = L"\u041e\u0442\u043a\u0440\u043e\u0439\u0442\u0435 \u043c\u0443\u0437\u0443\u043a\u0443 \u0432 \u043f\u043b\u0435\u044f\u0440\u0435";
    std::wstring artist;
    std::wstring source;
    std::wstring error;
    bool playing = false;
    bool toggle = false;
    bool previous = false;
    bool next = false;
    bool seek = false;
    int64_t start = 0;
    int64_t end = 0;
    int64_t position = 0;
    int64_t minimum = 0;
    int64_t maximum = 0;
    Session session{nullptr};
    std::shared_ptr<Cover> cover;
};

struct MediaCommand {
    int action = 0;
    int64_t position = 0;
    Session session{nullptr};
};

struct VolumeState {
    bool available = false;
    float level = 0.0f;
    bool muted = false;
};

struct VolumeChoice {
    std::wstring id;
    std::wstring label;
    bool selected = false;
};

struct LyricsLine {
    double seconds = 0;
    std::wstring text;
};

struct LyricsSnapshot {
    std::wstring source = L"LRCLIB external catalog";
    std::wstring status = L"\u041e\u0436\u0438\u0434\u0430\u043d\u0438\u0435 \u043c\u0435\u0442\u0430\u0434\u0430\u043d\u043d\u044b\u0445 \u0442\u0440\u0435\u043a\u0430";
    std::wstring text;
    std::vector<LyricsLine> lines;

    std::wstring DisplayAt(double seconds) const;
};

struct AsrSnapshot {
    bool active = false;
    std::wstring status = L"\u0421\u0442\u043e\u043f";
    std::wstring text;
    std::wstring provisional;
    double modelLoadMs = 0;
    double inferenceMs = 0;
    double audioAgeMs = 0;
};

struct HoverState {
    ULONGLONG outsideSince = 0;
    bool armed = true;
    bool trackingOutside = false;

    bool Collapse(bool inside, bool blocked, ULONGLONG now, int delay) {
        if (inside || blocked) {
            outsideSince = 0; trackingOutside = false; return false;
        }
        if (!trackingOutside || now < outsideSince) {
            outsideSince = now; trackingOutside = true;
        }
        if (now - outsideSince < static_cast<ULONGLONG>(delay > 0 ? delay : 0)) return false;
        armed = false;
        return true;
    }
};
