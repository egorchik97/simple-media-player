#pragma once

#include "../config/Config.h"
#include "../core/Types.h"
#include "../ui/Layout.h"

#include <windows.h>
#include <algorithm>
#include <cmath>
#include <string_view>
#include <gdiplus.h>
#include <memory>
#include <vector>

namespace media::appearance_detail {

inline int Clamp(int value, int low, int high) {
    return value < low ? low : value > high ? high : value;
}

struct Palette {
    COLORREF background;
    COLORREF accent;
    COLORREF text = RGB(239, 242, 247);
    COLORREF muted = RGB(166, 175, 192);
};

inline Palette Colors(const Config& config) {
    if (config.theme == 5) {
        return {static_cast<COLORREF>(Clamp(config.customBackground, 0, 0xFFFFFF)),
                static_cast<COLORREF>(Clamp(config.customAccent, 0, 0xFFFFFF)),
                static_cast<COLORREF>(Clamp(config.customText, 0, 0xFFFFFF)),
                static_cast<COLORREF>(Clamp(config.customMuted, 0, 0xFFFFFF))};
    }
    static const Palette palettes[] = {
        {RGB(20, 23, 32), RGB(114, 164, 255)},
        {RGB(27, 20, 37), RGB(187, 139, 255)},
        {RGB(15, 29, 29), RGB(83, 217, 184)},
        {RGB(33, 25, 20), RGB(255, 181, 99)},
        {RGB(30, 20, 29), RGB(246, 135, 186)},
    };
    return palettes[Clamp(config.theme, 0, 4)];
}

inline COLORREF Mix(COLORREF a, COLORREF b, int amount) {
    amount = Clamp(amount, 0, 100);
    return RGB(
        (GetRValue(a) * (100 - amount) + GetRValue(b) * amount) / 100,
        (GetGValue(a) * (100 - amount) + GetGValue(b) * amount) / 100,
        (GetBValue(a) * (100 - amount) + GetBValue(b) * amount) / 100);
}

inline int Radius(const Config& config, int width, int height) {
    int radius = Clamp(config.roundness, 0, 28);
    if (radius > width / 2) radius = width / 2;
    if (radius > height / 2) radius = height / 2;
    if (radius > 0 && radius < 8) radius = 8;
    return radius;
}

enum class GlassPattern : int {
    SoftFrost = 0,
    FlowingGlass = 1,
    Crystal = 2,
    LiquidWaves = 3,
    FineNoise = 4,
    MinimalGlass = 5,
    RandomPattern = 6,
    Legacy = 7,
    Count = 8
};

/// Glass pattern configuration
struct GlassConfig {
    GlassPattern pattern = GlassPattern::SoftFrost;
    int strength = 50;
    int tint = 35;
    int speed = 30;
    bool animated = true;
    int opacity = 100;
    uint32_t seed = 0;
    bool randomSeed = true;
};

/// Extract glass config from main Config
inline GlassConfig GetGlassConfig(const Config& config) {
    GlassConfig gc;
    gc.pattern = static_cast<GlassPattern>(Clamp(config.glassPattern, 0, static_cast<int>(GlassPattern::Count) - 1));
    gc.strength = Clamp(config.glassStrength, 0, 100);
    gc.tint = Clamp(config.glassTint, 0, 100);
    gc.speed = Clamp(config.glassSpeed, 0, 100);
    gc.animated = config.glassAnimated;
    gc.opacity = Clamp(config.opacity, 45, 100);
    gc.seed = static_cast<uint32_t>(config.glassSeed);
    gc.randomSeed = config.glassRandomSeed;
    return gc;
}

inline COLORREF AccentColor(const Config& config) { return Colors(config).accent; }
inline COLORREF BackgroundColor(const Config& config) { return Colors(config).background; }
inline COLORREF TextColor(const Config& config) { return Colors(config).text; }
inline COLORREF MutedColor(const Config& config) { return Colors(config).muted; }

// Analytic coverage of a rounded rectangle. 1 is fully inside, 0 is fully outside.
// The layered-window silhouette uses this instead of a binary 2x2 sample mask.
inline float RoundedRectCoverage(float x, float y, float width, float height, float radius) {
    if (width <= 0.f || height <= 0.f) return 0.f;
    radius = std::clamp(radius, 0.f, std::min(width, height) * 0.5f);
    const float cx = width * 0.5f;
    const float cy = height * 0.5f;
    const float qx = std::fabs(x - cx) - (cx - radius);
    const float qy = std::fabs(y - cy) - (cy - radius);
    const float ox = std::max(qx, 0.f);
    const float oy = std::max(qy, 0.f);
    const float sd = std::min(std::max(qx, qy), 0.f) + std::sqrt(ox * ox + oy * oy) - radius;
    return std::clamp(0.5f - sd, 0.f, 1.f);
}

struct Snowflake {
    float x = 0.f;
    float y = 0.f;
    float sizeScale = 1.f;
    float speedScale = 1.f;
    float drift = 0.f;
    float phase = 0.f;
    float alphaScale = 1.f;
};

struct Snowfield {
    std::vector<Snowflake> flakes;
    int width = 0;
    int height = 0;
    int count = 0;
    int speed = 4;
    int size = 2;
    int opacity = 65;
    int windDirection = 0;
    int windStrength = 50;
    int blur = 0;
};

void ConfigureSnowfield(Snowfield& field, const Config& config, int width, int height);
void AdvanceSnowfield(Snowfield& field, float deltaSeconds, int width, int height);

/// Cover art data for rendering
struct CoverData {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels;  // 32-bit BGRA
};

/// Complete UI state needed for rendering a frame
struct RenderState {
    const Config* config = nullptr;
    const MediaState* mediaState = nullptr;
    const Layout* layout = nullptr;
    bool settings = false;
    int settingsPage = 0;
    bool expanded = false;
    bool volumeDrag = false;
    float volumePreview = 0.0f;
    bool volumeAvailable = false;
    float volumeLevel = 0.0f;
    bool volumeMuted = false;
    bool busy = false;
    bool seekDrag = false;
    int64_t seekPosition = 0;
    std::wstring compactLyricsText;
    std::wstring lyricsText;
    bool lyricsError = false;
    CoverData cover;
    HoverState hover;
};

/// Render a complete frame (background + controls) into a 32-bit premultiplied BGRA bitmap
/// and push to layered window via UpdateLayeredWindow.
bool RenderFrame(HWND window, const RenderState& state);

// Paint a 2x supersampled presentation into an ordinary opaque DC.
// Window silhouette anti-aliasing is applied later by analytic coverage, not by this scale factor.
// Also used by offscreen regression tests; window opacity is applied only on presentation.
bool PaintFrame(HDC dc, const RenderState& state);

/// Initialize GDI+ (call once at startup).
bool InitGdiplus();

/// Shutdown GDI+ (call once at shutdown).
void ShutdownGdiplus();

} // namespace media::appearance_detail

using media::appearance_detail::RenderFrame;
using media::appearance_detail::AccentColor;
using media::appearance_detail::BackgroundColor;
using media::appearance_detail::TextColor;
using media::appearance_detail::MutedColor;
using media::appearance_detail::InitGdiplus;
using media::appearance_detail::ShutdownGdiplus;
using media::appearance_detail::GlassPattern;
using media::appearance_detail::GlassConfig;
using media::appearance_detail::GetGlassConfig;
using media::appearance_detail::RenderState;
using media::appearance_detail::CoverData;
