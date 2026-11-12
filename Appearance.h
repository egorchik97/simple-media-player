#pragma once
#include <windows.h>
#include <cmath>

// Include after the definition of g_config (PopupConfig). UI-thread helpers.
// Glass is a GDI illustration, not desktop blur or undocumented acrylic.
namespace appearance_detail {
inline int Clamp(int value, int low, int high) {
    return value < low ? low : value > high ? high : value;
}
struct Palette {
    COLORREF background, accent;
    COLORREF text = RGB(239, 242, 247), muted = RGB(166, 175, 192);
};
inline Palette Colors() {
    if (g_config.theme == 5) {
        return {static_cast<COLORREF>(Clamp(g_config.customBackground, 0, 0xFFFFFF)),
                static_cast<COLORREF>(Clamp(g_config.customAccent, 0, 0xFFFFFF)),
                static_cast<COLORREF>(Clamp(g_config.customText, 0, 0xFFFFFF)),
                static_cast<COLORREF>(Clamp(g_config.customMuted, 0, 0xFFFFFF))};
    }
    static const Palette palettes[] = {
        {RGB(20, 23, 32), RGB(114, 164, 255)},
        {RGB(27, 20, 37), RGB(187, 139, 255)},
        {RGB(15, 29, 29), RGB(83, 217, 184)},
        {RGB(33, 25, 20), RGB(255, 181, 99)},
        {RGB(30, 20, 29), RGB(246, 135, 186)}
    };
    return palettes[Clamp(g_config.theme, 0, 4)];
}
inline COLORREF Mix(COLORREF a, COLORREF b, int amount) {
    amount = Clamp(amount, 0, 100);
    return RGB((GetRValue(a) * (100 - amount) + GetRValue(b) * amount) / 100,
               (GetGValue(a) * (100 - amount) + GetGValue(b) * amount) / 100,
               (GetBValue(a) * (100 - amount) + GetBValue(b) * amount) / 100);
}
inline int Radius(int width, int height) {
    int radius = Clamp(g_config.roundness, 0, 28);
    if (radius > width / 2) radius = width / 2;
    if (radius > height / 2) radius = height / 2;
    // Радиус меньше 8 пикселей даёт заметную лесенку на кривых краях GDI;
    // минимальный видимый скруглитель — 8 px (эллипс 16×16 px).
    if (radius > 0 && radius < 8) radius = 8;
    return radius;
}
}

inline COLORREF AccentColor() { return appearance_detail::Colors().accent; }
inline COLORREF BackgroundColor() { return appearance_detail::Colors().background; }
inline COLORREF TextColor() { return appearance_detail::Colors().text; }
inline COLORREF MutedColor() { return appearance_detail::Colors().muted; }

// Paint first, before content. Restores the caller's DC state and clip.
inline void PaintAppearance(HDC dc, int width, int height) {
    if (!dc || width <= 0 || height <= 0) return;
    using namespace appearance_detail;
    const int saved = SaveDC(dc);
    if (!saved) return;
    IntersectClipRect(dc, 0, 0, width, height);
    const Palette colors = Colors();
    const int glow = Clamp(g_config.glow, 0, 100);
    const int radius = Radius(width, height);
    const int strength = g_config.glass ? Clamp(g_config.glassStrength, 0, 100) : 0;
    const int tint = Clamp(g_config.glassTint, 0, 100);
    const int speed = Clamp(g_config.glassSpeed, 0, 100);
    double phase = 0.0;
    // Caller schedules repaints; no timers, allocations, or invalidation here.
    // Integer modulo keeps the trigonometric argument bounded over long uptime.
    if (strength && g_config.glassAnimated && speed) {
        const ULONGLONG period = 240000 / static_cast<ULONGLONG>(speed);
        phase = 6.283185307179586 * static_cast<double>(GetTickCount64() % period) / period;
    }
    const double drift = 0.18 * std::sin(phase);
    const double tilt = 0.12 * std::cos(phase);
    // Bounded sampling: at most 3072 stock-brush fills regardless of size.
    // Broad polynomial falloff gives a soft curved sheen, not a blur effect.
    const int columns = strength ? (width < 64 ? width : 64) : 1;
    const int rows = strength ? (height < 48 ? height : 48) : 1;
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
            sheen *= sheen; // Zero slope at the band's edges.
            COLORREF color = Mix(base, RGB(255, 255, 255),
                                 static_cast<int>(strength * 0.22 * sheen));
            RECT cell{static_cast<LONG>(1LL * column * width / columns),
                      static_cast<LONG>(1LL * row * height / rows),
                      static_cast<LONG>(1LL * (column + 1) * width / columns),
                      static_cast<LONG>(1LL * (row + 1) * height / rows)};
            SetDCBrushColor(dc, color);
            FillRect(dc, &cell, brush);
        }
    }
    SelectObject(dc, GetStockObject(NULL_BRUSH));
    SelectObject(dc, GetStockObject(DC_PEN));
    // Inset glow stays inside the window region; no extra windows required.
    for (int inset = 5; inset >= 0; --inset) {
        if (width <= 2 * inset + 1 || height <= 2 * inset + 1) continue;
        if (inset && !glow) continue;
        int borderStrength = inset ? glow * (6 - inset) / 24 : 18 + glow / 2;
        SetDCPenColor(dc, Mix(colors.background, colors.accent, borderStrength));
        int r = radius > inset ? radius - inset : 0;
        if (r) RoundRect(dc, inset, inset, width - inset, height - inset, r * 2, r * 2);
        else Rectangle(dc, inset, inset, width - inset, height - inset);
    }
    RestoreDC(dc, saved);
}

// Width/height are whole-window pixel dimensions (borderless popup).
// Returns false on API failure (not every GDI API supplies extended errors).
// Call after creation, resize or appearance changes, not inside WM_PAINT.
inline bool ApplyAppearance(HWND window, int width, int height) {
    if (!window || width <= 0 || height <= 0) {
        SetLastError(ERROR_INVALID_PARAMETER); return false;
    }
    SetLastError(ERROR_SUCCESS);
    LONG_PTR style = GetWindowLongPtrW(window, GWL_EXSTYLE);
    if (!style && GetLastError() != ERROR_SUCCESS) return false;
    if (!(style & WS_EX_LAYERED)) {
        SetLastError(ERROR_SUCCESS);
        if (!SetWindowLongPtrW(window, GWL_EXSTYLE, style | WS_EX_LAYERED) &&
            GetLastError() != ERROR_SUCCESS) return false;
    }
    const BYTE alpha = static_cast<BYTE>((appearance_detail::Clamp(g_config.opacity, 45, 100) * 255 + 50) / 100);
    if (!SetLayeredWindowAttributes(window, 0, alpha, LWA_ALPHA)) return false;
    const int radius = appearance_detail::Radius(width, height);
    if (!radius) return SetWindowRgn(window, nullptr, TRUE) != 0;
    HRGN region = CreateRoundRectRgn(0, 0, width, height, radius * 2, radius * 2);
    if (!region) return false;
    if (!SetWindowRgn(window, region, TRUE)) {
        DWORD error = GetLastError();
        DeleteObject(region);
        SetLastError(error);
        return false;
    }
    // Ownership transfers to the system only after successful SetWindowRgn.
    return true;
}