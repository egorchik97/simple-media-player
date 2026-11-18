#include "../src/ui/SettingsUI.h"
#include <cassert>
#include <iostream>

// Standalone Windows test: compile with SettingsUI.cpp, user32, gdi32,
// comdlg32 and windowsapp; C++20, /utf-8, UNICODE, _UNICODE, NOMINMAX.
int main() {
    media::Config config;
    media::SettingsUI ui(config);
    HWND window = CreateWindowExW(0, L"STATIC", L"Settings tests", 0,
        0, 0, 340, 392, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    assert(window);
    int page = 0;
    auto click = [&](int row, int x = 200) {
        assert(ui.HandleClick(window, {x, 48 + row * 30 + 15}, page));
    };
    click(8); assert(config.delay == 800 && page == 0);
    click(8, 169); assert(config.delay == 700);
    click(9); assert(page == 1);
    click(1); assert(page == 2);
    assert(!ui.HandleClick(window, {200, 300}, page));
    click(7); assert(page == 1);
    click(2); assert(page == 3);
    config.glassStrength = 0;
    click(6); assert(config.glassStrength == 50 && page == 3);
    click(8); assert(page == 5);
    for (int i = 1; i <= 8; ++i) {
        click(0); assert(config.glassPattern == i % 8);
    }
    click(0, 169); assert(config.glassPattern == 7);
    config.glassRandomSeed = false;
    config.glassSeed = 0x7FFFFFFF;
    click(3); assert(config.glassSeed == 0x7FFFFFFF);
    click(7); assert(page == 3);
    click(7); assert(page == 1);
    click(7); assert(page == 0);
    click(3); assert(page == 4);
    config.compactWidth = 880;
    click(3); assert(config.compactWidth == 900);
    click(3); assert(config.compactWidth == 900);
    click(6); assert(page == 0);
    assert(!ui.HandleClick(window, {15, 63}, page));
    assert(!ui.HandleClick(window, {324, 63}, page));
    assert(!ui.HandleClick(window, {200, 348}, page));
    click(9); assert(page == 1);
    click(2); assert(page == 3);
    click(9); assert(page == 6);
    click(0); assert(config.snowfallEnabled);
    config.snowfallCount = 150; click(1); assert(config.snowfallCount == 150);
    click(1, 169); assert(config.snowfallCount == 140);
    config.snowfallSpeed = 1; click(2, 169); assert(config.snowfallSpeed == 1);
    config.snowfallSize = 5; click(3); assert(config.snowfallSize == 5);
    config.snowfallOpacity = 10; click(4); assert(config.snowfallOpacity == 15);
    config.snowfallWindDirection = 0; click(5); assert(config.snowfallWindDirection == 1);
    click(5, 169); assert(config.snowfallWindDirection == 0);
    config.snowfallWindStrength = 0; click(6, 169); assert(config.snowfallWindStrength == 0);
    config.snowfallBlur = 3; click(7); assert(config.snowfallBlur == 3);
    click(8);
    assert(!config.snowfallEnabled && config.snowfallCount == 70 && config.snowfallSpeed == 4
        && config.snowfallSize == 2 && config.snowfallOpacity == 65 && config.snowfallWindDirection == 0
        && config.snowfallWindStrength == 50 && config.snowfallBlur == 0 && page == 6);
    click(9); assert(page == 3);

    HDC dc = CreateCompatibleDC(nullptr);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = 340;
    info.bmiHeader.biHeight = -392;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    void* pixels = nullptr;
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    assert(dc && bitmap);
    auto old = SelectObject(dc, bitmap);
    const COLORREF sentinel = RGB(255, 0, 255);
    HBRUSH brush = CreateSolidBrush(sentinel);
    RECT bounds{0, 0, 340, 392};
    auto changed = [&](RECT area) {
        for (int y = area.top; y < area.bottom; ++y)
            for (int x = area.left; x < area.right; ++x)
                if (GetPixel(dc, x, y) != sentinel) return true;
        return false;
    };
    for (int p = 0; p <= 6; ++p) {
        FillRect(dc, &bounds, brush);
        ui.Render(window, dc, p);
        assert(changed({16, 8, 276, 40})); // Every page has its own heading.
        assert(!changed({284, 4, 340, 28})); // No window chrome.
        assert(GetPixel(dc, 0, 200) == sentinel); // No background.
        if (p != 4) assert(changed({16, 348, 324, 386}));
        if (p == 2) assert(GetPixel(dc, 300, 63) == COLORREF(config.customBackground));
    }
    config.cover = false;
    ui.Render(window, dc, 0);
    auto off = GetPixel(dc, 296, 57);
    config.cover = true;
    ui.Render(window, dc, 0);
    assert(GetPixel(dc, 296, 57) != off); // Boolean value, not pointer truthiness.
    SelectObject(dc, old);
    DeleteObject(bitmap);
    DeleteObject(brush);
    DeleteDC(dc);
    DestroyWindow(window);
    std::cout << "Settings UI tests passed\n";
}