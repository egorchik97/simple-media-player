#include "Appearance.h"
#include "PlayerControls.h"

#include <gdiplus.h>
#include <algorithm>
#include <cmath>
#include <memory>
#include <random>
#include <array>
#include <vector>
#include <string>

#pragma comment(lib, "gdiplus.lib")

using namespace Gdiplus;

namespace media::appearance_detail {

namespace {

constexpr int kSupersample = 2;

float UnitHash(int index, int salt) {
    uint32_t hash = 2166136261u ^ static_cast<uint32_t>(index * 16777619u + salt * 131u);
    hash = hash * 1664525u + 1013904223u;
    return (hash >> 8) * (1.f / 16777216.f);
}

Snowflake MakeSnowflake(int index, int width, int height) {
    Snowflake flake;
    flake.x = UnitHash(index, 1) * static_cast<float>(std::max(width, 1));
    flake.y = UnitHash(index, 2) * static_cast<float>(std::max(height, 1));
    flake.sizeScale = 0.55f + 0.6f * UnitHash(index, 3);
    flake.speedScale = 0.55f + 0.9f * UnitHash(index, 4);
    flake.drift = 8.f + 16.f * UnitHash(index, 5);
    flake.phase = UnitHash(index, 6) * 6.2831853f;
    flake.alphaScale = 0.55f + 0.45f * UnitHash(index, 7);
    return flake;
}

float WrapSpan(float value, float low, float high) {
    const float span = high - low;
    if (span <= 0.f) return low;
    float wrapped = std::fmod(value - low, span);
    if (wrapped < 0.f) wrapped += span;
    return low + wrapped;
}

struct SnowSim {
    Snowfield field;
    uint64_t lastTick = 0;
    bool active = false;
};

SnowSim g_snow;

} // namespace

void ConfigureSnowfield(Snowfield& field, const Config& config, int width, int height) {
    if (width <= 0 || height <= 0) return;
    const int count = Clamp(config.snowfallCount, 30, 150);
    if (field.width > 0 && field.height > 0 && !field.flakes.empty() &&
        (field.width != width || field.height != height)) {
        const float sx = static_cast<float>(width) / static_cast<float>(field.width);
        const float sy = static_cast<float>(height) / static_cast<float>(field.height);
        for (auto& flake : field.flakes) {
            flake.x *= sx;
            flake.y *= sy;
        }
    }
    field.width = width;
    field.height = height;
    field.count = count;
    field.speed = Clamp(config.snowfallSpeed, 1, 10);
    field.size = Clamp(config.snowfallSize, 1, 5);
    field.opacity = Clamp(config.snowfallOpacity, 10, 100);
    field.windDirection = Clamp(config.snowfallWindDirection, 0, 2);
    field.windStrength = Clamp(config.snowfallWindStrength, 0, 100);
    field.blur = Clamp(config.snowfallBlur, 0, 3);
    if (field.flakes.capacity() < 150) field.flakes.reserve(150);
    if (field.flakes.size() > static_cast<size_t>(count)) field.flakes.resize(static_cast<size_t>(count));
    while (field.flakes.size() < static_cast<size_t>(count))
        field.flakes.push_back(MakeSnowflake(static_cast<int>(field.flakes.size()), width, height));
}

void AdvanceSnowfield(Snowfield& field, float deltaSeconds, int width, int height) {
    if (field.flakes.empty() || width <= 0 || height <= 0) return;
    float dt = deltaSeconds;
    if (dt < 0.f) dt = 0.f;
    if (dt > 0.1f) dt = 0.1f;
    const int windDir = Clamp(field.windDirection, 0, 2);
    const float wind = windDir == 1 ? -field.windStrength * 0.45f
                      : windDir == 2 ? field.windStrength * 0.45f
                                     : 0.f;
    const float speed = static_cast<float>(std::max(field.speed, 1));
    for (auto& flake : field.flakes) {
        const float radius = std::max(0.7f, field.size * flake.sizeScale);
        const float fall = speed * flake.speedScale * 18.f;
        flake.phase += dt * (0.8f + flake.speedScale);
        flake.x += (wind + std::sin(flake.phase) * flake.drift) * dt;
        flake.y += fall * dt;
        const float left = -radius;
        const float right = static_cast<float>(width) + radius;
        const float top = -radius;
        const float bottom = static_cast<float>(height) + radius;
        flake.x = WrapSpan(flake.x, left, right);
        if (flake.y > bottom) flake.y = WrapSpan(flake.y, top, bottom);
    }
    field.width = width;
    field.height = height;
}

namespace {

void DrawSnow(HDC dc, const Snowfield& field) {
    if (!dc || field.flakes.empty()) return;
    Graphics g(dc);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(PixelOffsetModeHalf);
    g.SetCompositingMode(CompositingModeSourceOver);
    g.SetCompositingQuality(CompositingQualityHighQuality);
    g.ScaleTransform(static_cast<REAL>(kSupersample), static_cast<REAL>(kSupersample));
    const int rings = 1 + Clamp(field.blur, 0, 3);
    for (const auto& flake : field.flakes) {
        const float radius = std::max(0.7f, field.size * flake.sizeScale);
        const int baseAlpha = std::clamp(
            static_cast<int>(field.opacity * flake.alphaScale * 255.f / 100.f), 0, 255);
        for (int ring = rings; ring >= 1; --ring) {
            const float grow = 1.f + (ring - 1) * 0.8f;
            const int alpha = baseAlpha / (ring * ring);
            if (alpha <= 0) continue;
            SolidBrush brush(Color(static_cast<BYTE>(alpha), 255, 255, 255));
            const float extent = radius * grow;
            g.FillEllipse(&brush, flake.x - extent, flake.y - extent, extent * 2.f, extent * 2.f);
        }
    }
}

void PaintSnow(HDC dc, int width, int height, const Config& config) {
    if (!config.snowfallEnabled) {
        g_snow.field.flakes.clear();
        g_snow.field.count = 0;
        g_snow.field.width = 0;
        g_snow.field.height = 0;
        g_snow.lastTick = 0;
        g_snow.active = false;
        return;
    }
    ConfigureSnowfield(g_snow.field, config, width, height);
    const uint64_t now = GetTickCount64();
    float dt = 0.f;
    if (g_snow.active && now > g_snow.lastTick)
        dt = static_cast<float>(now - g_snow.lastTick) / 1000.f;
    g_snow.lastTick = now;
    g_snow.active = true;
    if (dt > 0.f) AdvanceSnowfield(g_snow.field, dt, width, height);
    DrawSnow(dc, g_snow.field);
}

} // namespace

// GDI+ token for startup/shutdown
static ULONG_PTR g_gdiplusToken = 0;

// Procedural noise/pattern generator with caching
class PatternCache {
public:
    struct Entry {
        std::unique_ptr<Bitmap> bitmap;
        GlassConfig config;
        int width = 0;
        int height = 0;
        uint64_t frame = 0;
    };

    Bitmap* GetOrCreate(int width, int height, const GlassConfig& config, uint64_t frame) {
        GlassConfig resolved = config;
        // Choose once per process, never once per frame (which caused flicker).
        static const uint32_t startupSeed = static_cast<uint32_t>(GetTickCount64()) ^ GetCurrentProcessId();
        if (resolved.randomSeed) resolved.seed = startupSeed;
        resolved.randomSeed = false;
        if (resolved.pattern == GlassPattern::RandomPattern) {
            std::mt19937 selection(resolved.seed);
            resolved.pattern = static_cast<GlassPattern>(selection() % 6);
        }
        if (resolved.pattern != GlassPattern::FlowingGlass && resolved.pattern != GlassPattern::LiquidWaves)
            frame = 0;
        if (entry_.bitmap && entry_.width == width && entry_.height == height &&
            entry_.frame == frame && ConfigsEqual(entry_.config, resolved)) {
            return entry_.bitmap.get();
        }
        entry_ = Generate(width, height, resolved, frame);
        return entry_.bitmap.get();
    }

    void Invalidate() { entry_.bitmap.reset(); }

private:
    Entry entry_;

    static bool ConfigsEqual(const GlassConfig& a, const GlassConfig& b) {
        return a.pattern == b.pattern && a.strength == b.strength && a.tint == b.tint &&
               a.speed == b.speed && a.animated == b.animated && a.opacity == b.opacity &&
               a.seed == b.seed && a.randomSeed == b.randomSeed;
    }

    static Entry Generate(int width, int height, const GlassConfig& config, uint64_t frame) {
        Entry result;
        result.config = config;
        result.width = width;
        result.height = height;
        result.frame = frame;

        // Rasterize at the supersample resolution so GDI+ anti-aliasing is not
        // destroyed by later upscaling a 1x bitmap. Coordinates stay logical.
        result.bitmap = std::make_unique<Bitmap>(width * kSupersample, height * kSupersample, PixelFormat32bppPARGB);
        if (!result.bitmap || result.bitmap->GetLastStatus() != Ok) {
            return result;
        }

        Graphics g(result.bitmap.get());
        g.Clear(Color(0, 0, 0, 0));
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetPixelOffsetMode(PixelOffsetModeHalf);
        g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
        g.SetCompositingQuality(CompositingQualityHighQuality);
        g.ScaleTransform(static_cast<REAL>(kSupersample), static_cast<REAL>(kSupersample));

        // Generate pattern based on type
        uint32_t seed = config.seed;
        if (config.randomSeed && config.seed == 0) {
            // Use frame-based seed for "random" pattern selection
            std::mt19937 rng(static_cast<uint32_t>(frame + width * 10000 + height));
            seed = rng();
        }
        std::mt19937 rng(seed);

        switch (config.pattern) {
            case GlassPattern::SoftFrost:
                RenderSoftFrost(g, width, height, config, rng);
                break;
            case GlassPattern::FlowingGlass:
                RenderFlowingGlass(g, width, height, config, rng, frame);
                break;
            case GlassPattern::Crystal:
                RenderCrystal(g, width, height, config, rng);
                break;
            case GlassPattern::LiquidWaves:
                RenderLiquidWaves(g, width, height, config, rng, frame);
                break;
            case GlassPattern::FineNoise:
                RenderFineNoise(g, width, height, config, rng);
                break;
            case GlassPattern::MinimalGlass:
                RenderMinimalGlass(g, width, height, config);
                break;
            case GlassPattern::RandomPattern: {
                // Pick a pattern based on seed
                int patternIdx = static_cast<int>(rng() % 6);
                GlassConfig tempConfig = config;
                tempConfig.pattern = static_cast<GlassPattern>(patternIdx);
                // Recursively call with selected pattern (one level deep)
                return Generate(width, height, tempConfig, frame);
            }
        }

        return result;
    }

    // Soft Frost - subtle frosted texture with gentle variance
    static void RenderSoftFrost(Graphics& g, int w, int h, const GlassConfig& cfg, std::mt19937& rng) {
        std::uniform_real_distribution<float> dist(0.0f, 1.0f);
        const float strength = cfg.strength / 100.0f * 0.35f;
        // Smooth frost: a vertical falloff plus soft puffs. Hard 8px cells read as blocks.
        LinearGradientBrush grad(
            RectF(0, 0, static_cast<REAL>(w), static_cast<REAL>(h)),
            Color(static_cast<int>(strength * 70), 255, 255, 255),
            Color(static_cast<int>(strength * 10), 255, 255, 255),
            LinearGradientModeVertical);
        g.FillRectangle(&grad, 0, 0, w, h);

        for (int i = 0; i < 7; ++i) {
            const float cx = dist(rng) * w;
            const float cy = dist(rng) * h;
            const float rx = w * (0.18f + 0.22f * dist(rng));
            const float ry = h * (0.16f + 0.2f * dist(rng));
            if (rx < 1.f || ry < 1.f) continue;
            GraphicsPath puff;
            puff.AddEllipse(cx - rx, cy - ry, rx * 2.f, ry * 2.f);
            PathGradientBrush brush(&puff);
            if (brush.GetLastStatus() != Ok) continue;
            const int alpha = std::clamp(static_cast<int>(strength * (80.f + 90.f * dist(rng))), 0, 255);
            brush.SetCenterColor(Color(alpha, 255, 255, 255));
            Color edge(0, 255, 255, 255);
            INT count = 1;
            brush.SetSurroundColors(&edge, &count);
            g.FillEllipse(&brush, cx - rx, cy - ry, rx * 2.f, ry * 2.f);
        }
    }

    // Flowing Glass - smooth flowing highlights with animated motion
    static void RenderFlowingGlass(Graphics& g, int w, int h, const GlassConfig& cfg, std::mt19937& rng, uint64_t frame) {
        float phase = cfg.animated ? (static_cast<float>(frame % 240) / 240.0f * 6.2831853f) : 0.0f;
        float strength = cfg.strength / 100.0f * 0.45f;

        for (int band = 0; band < 3; ++band) {
            float bandPhase = phase + band * 2.094f;
            float yCenter = 0.3f + 0.2f * std::sin(bandPhase * 0.7f) + 0.15f * std::sin(bandPhase * 1.3f);
            yCenter = std::clamp(yCenter, 0.1f, 0.9f);

            float bandHeight = 0.15f + 0.1f * std::cos(bandPhase * 0.5f);
            float intensity = strength * (0.7f + 0.3f * std::sin(bandPhase));

            REAL y1 = (yCenter - bandHeight * 0.5f) * h;
            REAL y2 = (yCenter + bandHeight * 0.5f) * h;
            y1 = (std::max)(0.0f, y1);
            y2 = (std::min)(static_cast<REAL>(h), y2);

            LinearGradientBrush brush(
                RectF(0, y1, static_cast<REAL>(w), y2 - y1),
                Color(0, 255, 255, 255),
                Color(static_cast<int>(intensity * 180), 255, 255, 255),
                LinearGradientModeVertical);
            g.FillRectangle(&brush, 0.0f, y1, static_cast<REAL>(w), y2 - y1);

            LinearGradientBrush shimmer(
                RectF(0, y1, static_cast<REAL>(w), y2 - y1),
                Color(0, 255, 255, 255),
                Color(static_cast<int>(intensity * 120), 255, 255, 255),
                LinearGradientModeHorizontal);
            g.FillRectangle(&shimmer, 0.0f, y1, static_cast<REAL>(w), y2 - y1);
        }

        Pen edgePen(Color(static_cast<int>(strength * 80), 255, 255, 255), 1.5f);
        g.DrawLine(&edgePen, 0.0f, 0.0f, static_cast<REAL>(w), 0.0f);
        g.DrawLine(&edgePen, 0.0f, static_cast<REAL>(h - 1), static_cast<REAL>(w), static_cast<REAL>(h - 1));
    }

    // Crystal - subtle crystalline reflections with geometric facets
    static void RenderCrystal(Graphics& g, int w, int h, const GlassConfig& cfg, std::mt19937& rng) {
        std::uniform_real_distribution<float> dist(0.0f, 1.0f);
        float strength = cfg.strength / 100.0f * 0.4f;

        LinearGradientBrush base(
            RectF(0, 0, static_cast<REAL>(w), static_cast<REAL>(h)),
            Color(static_cast<int>(strength * 40), 240, 245, 255),
            Color(static_cast<int>(strength * 20), 220, 230, 250),
            LinearGradientModeVertical);
        g.FillRectangle(&base, 0, 0, w, h);

        const int facets = 5 + (rng() % 4);
        for (int i = 0; i < facets; ++i) {
            float cx = dist(rng);
            float cy = dist(rng);
            float size = 0.08f + 0.12f * dist(rng);
            int points = 4 + (rng() % 4);

            PointF pts[8];
            for (int p = 0; p < points; ++p) {
                float angle = (p * 6.2831853f / points) + dist(rng) * 0.3f;
                float r = size * (0.7f + 0.3f * dist(rng));
                pts[p].X = (cx + r * std::cos(angle)) * w;
                pts[p].Y = (cy + r * std::sin(angle)) * h;
            }
            pts[points] = pts[0];

            int alpha = static_cast<int>(strength * (80 + 40 * dist(rng)));
            SolidBrush facetBrush(Color(alpha, 255, 255, 255));
            g.FillPolygon(&facetBrush, pts, points + 1, FillModeWinding);
        }
    }

    // Liquid Waves - organic wave-like distortion
    static void RenderLiquidWaves(Graphics& g, int w, int h, const GlassConfig& cfg, std::mt19937& rng, uint64_t frame) {
        float phase = cfg.animated ? (static_cast<float>(frame % 360) / 360.0f * 6.2831853f) : 0.0f;
        float strength = cfg.strength / 100.0f * 0.5f;

        for (int layer = 0; layer < 4; ++layer) {
            float layerPhase = phase * (0.5f + layer * 0.3f) + layer * 1.57f;
            float amplitude = 0.03f * (1.0f + layer * 0.5f) * strength;
            float frequency = 1.5f + layer * 0.8f;
            float yBase = 0.2f + layer * 0.2f;

            const int segments = 48;
            std::vector<PointF> crest(static_cast<size_t>(segments) + 1);
            for (int i = 0; i <= segments; ++i) {
                const float x = static_cast<float>(i) / segments;
                float y = yBase + amplitude * std::sin(x * frequency * 6.2831853f + layerPhase);
                y = std::clamp(y, 0.0f, 1.0f);
                crest[static_cast<size_t>(i)] = PointF(x * w, y * h);
            }
            GraphicsPath path;
            path.AddLine(0.0f, static_cast<REAL>(h), crest.front().X, crest.front().Y);
            path.AddCurve(crest.data(), static_cast<INT>(crest.size()), 0.5f);
            path.AddLine(crest.back().X, crest.back().Y, static_cast<REAL>(w), static_cast<REAL>(h));
            path.CloseFigure();

            int alpha = static_cast<int>(strength * (60 - layer * 10));
            SolidBrush waveBrush(Color(alpha, 255, 255, 255));
            g.FillPath(&waveBrush, &path);
        }

        Pen highlightPen(Color(static_cast<int>(strength * 100), 255, 255, 255), 1.0f);
        for (int i = 0; i < 3; ++i) {
            float y = 0.15f + i * 0.3f + 0.05f * std::sin(phase + i * 2.0f);
            y = std::clamp(y, 0.0f, 1.0f);
            g.DrawLine(&highlightPen, 0.0f, y * h, static_cast<REAL>(w), y * h);
        }
    }

    // Fine Noise - high-quality fine-grained texture
    static void RenderFineNoise(Graphics& g, int w, int h, const GlassConfig& cfg, std::mt19937& rng) {
        std::uniform_int_distribution<int> distAlpha(0, 255);
        float strength = cfg.strength / 100.0f * 0.25f;

        const int noiseSize = 128;
        Bitmap noiseBmp(noiseSize, noiseSize, PixelFormat32bppPARGB);
        {
            std::uniform_int_distribution<int> dist(0, 255);
            for (int y = 0; y < noiseSize; ++y) {
                for (int x = 0; x < noiseSize; ++x) {
                    int alpha = static_cast<int>(strength * dist(rng) * 0.15f);
                    noiseBmp.SetPixel(x, y, Color(alpha, 255, 255, 255));
                }
            }
        }

        TextureBrush textureBrush(&noiseBmp, WrapModeTile);
        Matrix texTransform(1.0f, 0, 0, 1.0f, 0, 0);
        textureBrush.SetTransform(&texTransform);
        g.FillRectangle(&textureBrush, 0.0f, 0.0f, static_cast<REAL>(w), static_cast<REAL>(h));

        GraphicsPath vignettePath;
        vignettePath.AddEllipse(-w * 0.3f, -h * 0.3f, w * 1.6f, h * 1.6f);
        PathGradientBrush vignette(&vignettePath);
        Color centerColor(0, 0, 0, 0);
        Color edgeColor(static_cast<int>(strength * 40), 0, 0, 0);
        vignette.SetCenterColor(centerColor);
        INT surroundCount = 1;
        vignette.SetSurroundColors(&edgeColor, &surroundCount);
        g.FillRectangle(&vignette, 0.0f, 0.0f, static_cast<REAL>(w), static_cast<REAL>(h));
    }

    // Minimal Glass - nearly transparent, clean glass
    static void RenderMinimalGlass(Graphics& g, int w, int h, const GlassConfig& cfg) {
        float strength = cfg.strength / 100.0f * 0.15f;

        LinearGradientBrush grad(
            RectF(0, 0, static_cast<REAL>(w), static_cast<REAL>(h)),
            Color(static_cast<int>(strength * 30), 245, 250, 255),
            Color(static_cast<int>(strength * 10), 230, 240, 255),
            LinearGradientModeVertical);
        g.FillRectangle(&grad, 0, 0, w, h);

        Pen linePen(Color(static_cast<int>(strength * 60), 255, 255, 255), 0.5f);
        g.DrawLine(&linePen, w * 0.1f, h * 0.15f, w * 0.9f, h * 0.15f);
        g.DrawLine(&linePen, w * 0.1f, h * 0.85f, w * 0.9f, h * 0.85f);
    }
};

// Global pattern cache
static PatternCache g_patternCache;

// Helper to convert COLORREF to GDI+ Color with alpha
inline Color ColorRefToColor(COLORREF cr, BYTE alpha = 255) {
    return Color(alpha, GetRValue(cr), GetGValue(cr), GetBValue(cr));
}

bool InitGdiplus() {
    if (g_gdiplusToken) return true;
    GdiplusStartupInput input;
    return GdiplusStartup(&g_gdiplusToken, &input, nullptr) == Ok;
}

void ShutdownGdiplus() {
    // GDI+ objects must be destroyed before the library is shut down.
    g_patternCache.Invalidate();
    if (g_gdiplusToken) GdiplusShutdown(g_gdiplusToken);
    g_gdiplusToken = 0;
}

// Original GitHub sheen: a curved highlight band, not a bitmap pattern.
static void PaintLegacySheen(HDC dc, int width, int height, const Config& config, const GlassConfig& glass) {
    const auto colors = Colors(config);
    const int strength = glass.strength;
    const int tint = glass.tint;
    const int speed = glass.speed;
    double phase = 0.0;
    if (strength && glass.animated && speed) {
        const ULONGLONG period = 240000ull / static_cast<ULONGLONG>(speed);
        phase = 6.283185307179586 * static_cast<double>(GetTickCount64() % period) / static_cast<double>(period);
    }
    const double drift = 0.18 * std::sin(phase);
    const double tilt = 0.12 * std::cos(phase);
    const int columns = width < 64 ? width : 64;
    const int rows = height < 48 ? height : 48;
    HBRUSH brush = static_cast<HBRUSH>(GetStockObject(DC_BRUSH));
    for (int row = 0; row < rows; ++row) {
        const double y = (row + 0.5) / rows;
        COLORREF base = Mix(colors.background, colors.accent,
                            static_cast<int>(strength * tint * (1.0 - y) / 250.0));
        base = Mix(base, RGB(255, 255, 255),
                   static_cast<int>(strength * 0.14 * (1.0 - y) * (1.0 - y)));
        for (int column = 0; column < columns; ++column) {
            const double x = (column + 0.5) / columns - 0.5;
            const double center = 0.28 + drift + tilt * x + 0.65 * x * x;
            const double distance = (y - center) / 0.30;
            double sheen = 1.0 - distance * distance;
            if (sheen < 0.0) sheen = 0.0;
            sheen *= sheen;
            const COLORREF color = Mix(base, RGB(255, 255, 255),
                                       static_cast<int>(strength * 0.22 * sheen));
            RECT cell{
                static_cast<LONG>(1LL * column * width / columns),
                static_cast<LONG>(1LL * row * height / rows),
                static_cast<LONG>(1LL * (column + 1) * width / columns),
                static_cast<LONG>(1LL * (row + 1) * height / rows)};
            SetDCBrushColor(dc, color);
            FillRect(dc, &cell, brush);
        }
    }
}

// Same inset rings as the original PaintAppearance. Logical coordinates, so the
// anisotropic DC places them on the rounded rim. The GDI+ path was offset.
static void PaintInsetGlow(HDC dc, int width, int height, const Config& config) {
    const auto colors = Colors(config);
    SelectObject(dc, GetStockObject(NULL_BRUSH));
    SelectObject(dc, GetStockObject(DC_PEN));
    const int glow = Clamp(config.glow, 0, 100);
    const int radius = Radius(config, width, height);
    for (int inset = 5; inset >= 0; --inset) {
        if (width <= 2 * inset + 1 || height <= 2 * inset + 1 || (inset && !glow)) continue;
        const int strength = inset ? glow * (6 - inset) / 24 : 18 + glow / 2;
        SetDCPenColor(dc, Mix(colors.background, colors.accent, strength));
        const int r = (std::max)(0, radius - inset);
        if (r) RoundRect(dc, inset, inset, width - inset, height - inset, r * 2, r * 2);
        else Rectangle(dc, inset, inset, width - inset, height - inset);
    }
}

static void PaintBackground(HDC dc, int width, int height, const Config& config) {
    const int saved = SaveDC(dc);
    const auto colors = Colors(config);
    RECT bounds{0, 0, width, height};
    SetDCBrushColor(dc, colors.background);
    FillRect(dc, &bounds, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    if (config.glass && config.glassStrength > 0) {
        auto glass = GetGlassConfig(config);
        if (glass.pattern == GlassPattern::Legacy) {
            PaintLegacySheen(dc, width, height, config, glass);
        } else {
            uint64_t frame = 0;
            if (glass.animated && glass.speed > 0)
                frame = GetTickCount64() / ((240000 / glass.speed) / 240);
            // Pattern bitmaps are already supersampled. Blit 1:1 in device pixels.
            // A second ScaleTransform plus bicubic upscale was re-aliasing the glass.
            auto pattern = g_patternCache.GetOrCreate(width, height, glass, frame);
            if (pattern) {
                Graphics graphics(dc);
                graphics.SetInterpolationMode(InterpolationModeNearestNeighbor);
                graphics.SetPixelOffsetMode(PixelOffsetModeNone);
                graphics.SetSmoothingMode(SmoothingModeAntiAlias);
                graphics.SetCompositingQuality(CompositingQualityHighQuality);
                const int deviceW = width * kSupersample;
                const int deviceH = height * kSupersample;
                SolidBrush tint(ColorRefToColor(colors.accent,
                    static_cast<BYTE>(Clamp(config.glassTint, 0, 100) * Clamp(config.glassStrength, 0, 100) * 255 / 25000)));
                graphics.FillRectangle(&tint, 0, 0, deviceW, deviceH);
                graphics.DrawImage(pattern, 0, 0, deviceW, deviceH);
            }
        }
    }
    PaintInsetGlow(dc, width, height, config);
    RestoreDC(dc, saved);
}

bool PaintFrame(HDC dc, const RenderState& state) {
    if (!dc || !state.config || !state.mediaState || !state.layout) return false;
    const int width = state.layout->width(), height = state.layout->height();
    if (width <= 0 || height <= 0) return false;
    HDC buffer = CreateCompatibleDC(dc);
    HBITMAP bitmap = CreateCompatibleBitmap(dc, width * kSupersample, height * kSupersample);
    if (!buffer || !bitmap) {
        if (bitmap) DeleteObject(bitmap);
        if (buffer) DeleteDC(buffer);
        return false;
    }
    auto old = SelectObject(buffer, bitmap);
    SetMapMode(buffer, MM_ANISOTROPIC);
    SetWindowExtEx(buffer, width, height, nullptr);
    SetViewportExtEx(buffer, width * kSupersample, height * kSupersample, nullptr);
    PaintBackground(buffer, width, height, *state.config);
    // Snow sits on the background/glass and under controls. GDI+ state ends with PaintSnow.
    PaintSnow(buffer, width, height, *state.config);
    PaintControls(buffer, state);
    SetMapMode(buffer, MM_TEXT);
    const int saved = SaveDC(dc);
    SetStretchBltMode(dc, HALFTONE);
    SetBrushOrgEx(dc, 0, 0, nullptr);
    const bool result = StretchBlt(dc, 0, 0, width, height, buffer, 0, 0,
                                  width * kSupersample, height * kSupersample, SRCCOPY) != FALSE;
    RestoreDC(dc, saved);
    SelectObject(buffer, old);
    DeleteObject(bitmap);
    DeleteDC(buffer);
    return result;
}

bool RenderFrame(HWND window, const RenderState& state) {
    if (!window || !state.config || !state.layout || !state.mediaState) return false;
    const int width = state.layout->width(), height = state.layout->height();
    if (width <= 0 || height <= 0) return false;
    auto style = GetWindowLongPtrW(window, GWL_EXSTYLE);
    if (!(style & WS_EX_LAYERED)) {
        SetLastError(ERROR_SUCCESS);
        if (!SetWindowLongPtrW(window, GWL_EXSTYLE, style | WS_EX_LAYERED) && GetLastError()) return false;
    }
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    void* pixels = nullptr;
    HBITMAP bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    HDC dc = CreateCompatibleDC(nullptr);
    if (!bitmap || !dc) {
        if (bitmap) DeleteObject(bitmap);
        if (dc) DeleteDC(dc);
        return false;
    }
    auto old = SelectObject(dc, bitmap);
    bool result = PaintFrame(dc, state);
    if (result) {
        GdiFlush();
        // GDI foreground writes RGB, not alpha. The old mask used four binary samples,
        // so a corner pixel could only be 0/25/50/75/100% covered. Analytic coverage
        // gives a continuous edge without raising the whole-frame supersample factor.
        const float radius = static_cast<float>(Radius(*state.config, width, height));
        const float fw = static_cast<float>(width);
        const float fh = static_cast<float>(height);
        auto data = static_cast<uint32_t*>(pixels);
        for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
            const float coverage = RoundedRectCoverage(x + 0.5f, y + 0.5f, fw, fh, radius);
            const uint32_t rgb = data[y * width + x];
            uint32_t a = 255;
            if (coverage <= 0.f) a = 0;
            else if (coverage < 1.f) a = static_cast<uint32_t>(coverage * 255.f + 0.5f);
            if (a == 255) {
                data[y * width + x] = (255u << 24) | (rgb & 0x00FFFFFFu);
            } else if (a == 0) {
                data[y * width + x] = 0;
            } else {
                data[y * width + x] = (a << 24) |
                    ((((rgb >> 16) & 255) * a / 255) << 16) |
                    ((((rgb >> 8) & 255) * a / 255) << 8) | ((rgb & 255) * a / 255);
            }
        }
        RECT rect{};
        GetWindowRect(window, &rect);
        POINT destination{rect.left, rect.top}, source{};
        SIZE size{width, height};
        const BYTE opacity = static_cast<BYTE>((Clamp(state.config->opacity, 45, 100) * 255 + 50) / 100);
        BLENDFUNCTION blend{AC_SRC_OVER, 0, opacity, AC_SRC_ALPHA};
        result = UpdateLayeredWindow(window, nullptr, &destination, &size, dc, &source, 0, &blend, ULW_ALPHA) != FALSE;
        // Alpha coverage supplies smooth corners and hit testing; no stale region on radius=0.
    }
    SelectObject(dc, old);
    DeleteObject(bitmap);
    DeleteDC(dc);
    return result;
}

} // namespace media::appearance_detail
