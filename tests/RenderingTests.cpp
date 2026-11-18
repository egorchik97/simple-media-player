#include "PlayerControls.h"
#include "SettingsUI.h"
#include "Layout.h"
#include "Appearance.h"
#include "Types.h"
#include "Config.h"

#include <windows.h>
#include <gdiplus.h>
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

#pragma comment(lib, "gdiplus.lib")

using namespace Gdiplus;

static bool RunRenderingSmoke() {
    if (!media::appearance_detail::InitGdiplus()) {
        std::cerr << "GDI+ initialization failed\n";
        return false;
    }

    media::Config config;
    media::Layout layout;
    // Initialize layout for settings mode to match the test's settings=true
    layout.Update(0, 0, config, false, true, 0, {0, 0}, {0, 0, 1920, 1080});

    HDC screenDC = GetDC(nullptr);
    HDC memDC = CreateCompatibleDC(screenDC);
    HBITMAP bitmap = CreateCompatibleBitmap(screenDC, layout.width() * 3, layout.height() * 3);
    auto oldBmp = SelectObject(memDC, bitmap);

    for (int page = 0; page <= 6; ++page) {
        media::appearance_detail::RenderState state;
        state.config = &config;
        state.layout = &layout;
        // Initialize minimal mediaState for PaintFrame validation
        MediaState testMediaState;
        state.mediaState = &testMediaState;
        state.settings = true;
        state.settingsPage = page;
        state.expanded = false;
        state.volumeDrag = false;
        state.volumePreview = 0;
        state.volumeAvailable = false;
        state.volumeLevel = 0;
        state.volumeMuted = false;
        state.busy = false;
        state.seekDrag = false;
        state.seekPosition = 0;
        state.compactLyricsText = L"Test lyrics";
        state.lyricsText = L"Full lyrics text for testing";
        state.lyricsError = false;

        if (!media::appearance_detail::PaintFrame(memDC, state)) {
            std::cerr << "PaintFrame failed on settings page " << page << "\n";
            return false;
        }
    }

    std::cout << "All rendering smoke tests passed\n";
    return true;
}

static bool RunCornerCoverage() {
    using media::appearance_detail::RoundedRectCoverage;
    if (RoundedRectCoverage(50.f, 40.f, 100.f, 80.f, 16.f) < 0.99f) {
        std::cerr << "rounded-rect center is not covered\n";
        return false;
    }
    if (RoundedRectCoverage(-8.f, -8.f, 100.f, 80.f, 16.f) > 0.01f) {
        std::cerr << "outside corner is not transparent\n";
        return false;
    }
    if (RoundedRectCoverage(50.f, 40.f, 100.f, 80.f, 0.f) < 0.99f) {
        std::cerr << "square center is not covered\n";
        return false;
    }
    bool seen[256]{};
    int distinct = 0;
    int previous = -1;
    // The edge is a 1px ramp. Sample it densely, not across the whole corner.
    for (int i = 0; i < 64; ++i) {
        const float x = 3.2f + i * (3.f / 63.f);
        const int alpha = static_cast<int>(RoundedRectCoverage(x, x, 100.f, 80.f, 16.f) * 255.f + 0.5f);
        if (alpha < 0 || alpha > 255) return false;
        if (previous >= 0 && std::abs(alpha - previous) > 40) {
            std::cerr << "corner coverage jumped " << previous << " -> " << alpha << "\n";
            return false;
        }
        previous = alpha;
        if (!seen[alpha]) { seen[alpha] = true; ++distinct; }
    }
    if (distinct < 8) {
        std::cerr << "corner coverage is still quantized, levels=" << distinct << "\n";
        return false;
    }
    std::cout << "Corner coverage passed (" << distinct << " edge levels)\n";
    return true;
}

static bool RunSnowfieldLogic() {
    media::Config config;
    config.snowfallCount = 70;
    config.snowfallSpeed = 8;
    config.snowfallSize = 3;
    config.snowfallOpacity = 80;
    config.snowfallWindDirection = 2;
    config.snowfallWindStrength = 100;
    config.snowfallBlur = 2;
    media::appearance_detail::Snowfield field;
    media::appearance_detail::ConfigureSnowfield(field, config, 200, 120);
    if (field.flakes.size() != 70 || field.blur != 2 || field.opacity != 80) {
        std::cerr << "snowfield did not adopt settings\n";
        return false;
    }
    float minSize = 10.f, maxSize = 0.f, minSpeed = 10.f, maxSpeed = 0.f;
    for (const auto& flake : field.flakes) {
        minSize = std::min(minSize, flake.sizeScale);
        maxSize = std::max(maxSize, flake.sizeScale);
        minSpeed = std::min(minSpeed, flake.speedScale);
        maxSpeed = std::max(maxSpeed, flake.speedScale);
    }
    if (maxSize - minSize < 0.2f || maxSpeed - minSpeed < 0.2f) {
        std::cerr << "snowflakes lack size/speed variation\n";
        return false;
    }

    for (auto& flake : field.flakes) { flake.drift = 0.f; flake.phase = 0.f; flake.x = 100.f; flake.y = 10.f; }
    const auto rightStart = field.flakes[0].x;
    media::appearance_detail::AdvanceSnowfield(field, 0.1f, 200, 120);
    if (field.flakes[0].x <= rightStart || field.flakes[0].y <= 10.f) {
        std::cerr << "right wind or fall did not move flakes\n";
        return false;
    }

    config.snowfallWindDirection = 1;
    media::appearance_detail::ConfigureSnowfield(field, config, 200, 120);
    for (auto& flake : field.flakes) { flake.drift = 0.f; flake.phase = 0.f; flake.x = 100.f; flake.y = 10.f; }
    const auto leftStart = field.flakes[0].x;
    media::appearance_detail::AdvanceSnowfield(field, 0.1f, 200, 120);
    if (field.flakes[0].x >= leftStart) {
        std::cerr << "left wind did not move flakes\n";
        return false;
    }

    config.snowfallWindDirection = 0;
    config.snowfallWindStrength = 0;
    config.snowfallSpeed = 10;
    config.snowfallSize = 2;
    media::appearance_detail::ConfigureSnowfield(field, config, 80, 40);
    for (auto& flake : field.flakes) {
        flake.drift = 0.f; flake.phase = 0.f; flake.speedScale = 1.f; flake.sizeScale = 1.f;
        flake.x = 20.f; flake.y = 38.f;
    }
    media::appearance_detail::AdvanceSnowfield(field, 0.1f, 80, 40);
    int wrapped = 0;
    for (const auto& flake : field.flakes) if (flake.y < 20.f) ++wrapped;
    if (wrapped != 70) {
        std::cerr << "snow did not wrap, wrapped=" << wrapped << " y=" << field.flakes[0].y << "\n";
        return false;
    }

    const float scaled = field.flakes[0].x;
    media::appearance_detail::ConfigureSnowfield(field, config, 40, 40);
    if (std::fabs(field.flakes[0].x - scaled * 0.5f) > 0.02f) {
        std::cerr << "snow did not adapt to width\n";
        return false;
    }
    config.snowfallCount = 30;
    media::appearance_detail::ConfigureSnowfield(field, config, 40, 40);
    if (field.flakes.size() != 30) {
        std::cerr << "snow count did not shrink\n";
        return false;
    }
    std::cout << "Snowfield logic passed\n";
    return true;
}

static int DifferingPixels(const std::vector<uint32_t>& a, const std::vector<uint32_t>& b) {
    int diff = 0;
    const size_t n = std::min(a.size(), b.size());
    for (size_t i = 0; i < n; ++i) if ((a[i] & 0x00FFFFFFu) != (b[i] & 0x00FFFFFFu)) ++diff;
    return diff;
}

static bool CaptureFrame(const media::Config& config, bool expanded, std::vector<uint32_t>& pixels) {
    media::Layout layout;
    layout.Update(0, 0, config, expanded, false, 0, {0, 0}, {0, 0, 1920, 1080});
    MediaState media;
    HDC screen = GetDC(nullptr);
    HDC dc = CreateCompatibleDC(screen);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = layout.width();
    info.bmiHeader.biHeight = -layout.height();
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!dc || !bitmap || !bits) {
        if (bitmap) DeleteObject(bitmap);
        if (dc) DeleteDC(dc);
        if (screen) ReleaseDC(nullptr, screen);
        return false;
    }
    auto old = SelectObject(dc, bitmap);
    media::appearance_detail::RenderState state;
    state.config = &config;
    state.mediaState = &media;
    state.layout = &layout;
    state.expanded = expanded;
    const bool ok = media::appearance_detail::PaintFrame(dc, state);
    const int count = layout.width() * layout.height();
    pixels.assign(static_cast<uint32_t*>(bits), static_cast<uint32_t*>(bits) + count);
    SelectObject(dc, old);
    DeleteObject(bitmap);
    DeleteDC(dc);
    ReleaseDC(nullptr, screen);
    return ok;
}

static bool RunSnowPaintAndGlassPatterns() {
    if (!media::appearance_detail::InitGdiplus()) return false;
    media::Config config;
    config.glass = false;
    config.glow = 0;
    config.snowfallEnabled = false;
    std::vector<uint32_t> plain, snowy;
    if (!CaptureFrame(config, true, plain)) return false;
    config.snowfallEnabled = true;
    config.snowfallCount = 80;
    config.snowfallSize = 4;
    config.snowfallOpacity = 90;
    if (!CaptureFrame(config, true, snowy)) return false;
    const int diff = DifferingPixels(plain, snowy);
    if (diff < 30) {
        std::cerr << "enabled snowfall did not change the frame, diffs=" << diff << "\n";
        return false;
    }
    config.snowfallEnabled = false;
    std::vector<uint32_t> stopped;
    if (!CaptureFrame(config, true, stopped)) return false;
    if (DifferingPixels(plain, stopped) != 0) {
        std::cerr << "disabling snowfall still painted flakes\n";
        return false;
    }
    config.glass = true;
    config.glow = 40;
    config.glassRandomSeed = false;
    config.glassSeed = 12345;
    for (int pattern = 0; pattern < 8; ++pattern) {
        config.glassPattern = pattern;
        std::vector<uint32_t> frame;
        if (!CaptureFrame(config, true, frame)) {
            std::cerr << "glass pattern " << pattern << " failed to paint\n";
            return false;
        }
    }
    std::cout << "Snow paint and glass patterns passed, snow diffs=" << diff << "\n";
    return true;
}

static bool RunSnowfallSettingsClicks() {
    media::Config config;
    const int glassStrength = config.glassStrength;
    const int opacity = config.opacity;
    const int theme = config.theme;
    media::SettingsUI ui(config);
    HWND window = CreateWindowExW(0, L"STATIC", L"Snowfall settings", 0,
        0, 0, 340, 392, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!window) {
        std::cerr << "Snowfall settings window failed\n";
        return false;
    }
    int page = 3;
    auto click = [&](int row, int x = 200) {
        return ui.HandleClick(window, POINT{x, 48 + row * 30 + 15}, page);
    };
    auto fail = [&](const char* message) {
        std::cerr << message << "\n";
        DestroyWindow(window);
        return false;
    };

    if (!click(9) || page != 6) return fail("page 3 row 9 should open snowfall");
    if (!click(0) || !config.snowfallEnabled) return fail("snowfall enable");
    if (!click(0) || config.snowfallEnabled) return fail("snowfall disable");

    config.snowfallCount = 150;
    if (!click(1) || config.snowfallCount != 150) return fail("count max");
    if (!click(1, 100) || config.snowfallCount != 140) return fail("count down");
    config.snowfallCount = 30;
    if (!click(1, 100) || config.snowfallCount != 30) return fail("count min");
    if (!click(1) || config.snowfallCount != 40) return fail("count up");

    config.snowfallSpeed = 10;
    if (!click(2) || config.snowfallSpeed != 10) return fail("speed max");
    config.snowfallSpeed = 1;
    if (!click(2, 100) || config.snowfallSpeed != 1) return fail("speed min");

    config.snowfallSize = 5;
    if (!click(3) || config.snowfallSize != 5) return fail("size max");
    config.snowfallSize = 1;
    if (!click(3, 100) || config.snowfallSize != 1) return fail("size min");

    config.snowfallOpacity = 100;
    if (!click(4) || config.snowfallOpacity != 100) return fail("opacity max");
    config.snowfallOpacity = 10;
    if (!click(4, 100) || config.snowfallOpacity != 10) return fail("opacity min");
    if (!click(4) || config.snowfallOpacity != 15) return fail("opacity step");

    config.snowfallWindDirection = 0;
    if (!click(5) || config.snowfallWindDirection != 1) return fail("wind to left");
    if (!click(5) || config.snowfallWindDirection != 2) return fail("wind to right");
    if (!click(5) || config.snowfallWindDirection != 0) return fail("wind wrap");
    if (!click(5, 100) || config.snowfallWindDirection != 2) return fail("wind reverse");
    config.snowfallWindDirection = 9;
    if (!click(5) || config.snowfallWindDirection != 0) return fail("wind out of range");

    config.snowfallWindStrength = 100;
    if (!click(6) || config.snowfallWindStrength != 100) return fail("wind strength max");
    config.snowfallWindStrength = 0;
    if (!click(6, 100) || config.snowfallWindStrength != 0) return fail("wind strength min");

    config.snowfallBlur = 3;
    if (!click(7) || config.snowfallBlur != 3) return fail("blur max");
    config.snowfallBlur = 0;
    if (!click(7, 100) || config.snowfallBlur != 0) return fail("blur min");

    config.snowfallEnabled = true;
    config.snowfallCount = 150;
    config.snowfallSpeed = 9;
    config.snowfallSize = 5;
    config.snowfallOpacity = 100;
    config.snowfallWindDirection = 2;
    config.snowfallWindStrength = 80;
    config.snowfallBlur = 3;
    if (!click(8) || config.snowfallEnabled || config.snowfallCount != 70 || config.snowfallSpeed != 4
        || config.snowfallSize != 2 || config.snowfallOpacity != 65 || config.snowfallWindDirection != 0
        || config.snowfallWindStrength != 50 || config.snowfallBlur != 0) return fail("snowfall reset");
    if (config.glassStrength != glassStrength || config.opacity != opacity || config.theme != theme)
        return fail("reset changed unrelated appearance");
    if (!click(9) || page != 3) return fail("back to glass page");
    if (!click(8) || page != 5) return fail("glass pattern navigation");

    DestroyWindow(window);
    std::cout << "Snowfall settings clicks passed\n";
    return true;
}

int main() {
    try {
        if (!RunRenderingSmoke()) {
            std::cerr << "Rendering smoke tests FAILED\n";
            return 1;
        }
        if (!RunSnowfallSettingsClicks()) {
            std::cerr << "Snowfall settings clicks FAILED\n";
            return 1;
        }
        if (!RunCornerCoverage() || !RunSnowfieldLogic() || !RunSnowPaintAndGlassPatterns()) {
            std::cerr << "Snowfall/corner tests FAILED\n";
            return 1;
        }
        std::cout << "Rendering smoke tests PASSED\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << "\n";
        return 1;
    } catch (...) {
        std::cerr << "Unknown exception\n";
        return 1;
    }
}
