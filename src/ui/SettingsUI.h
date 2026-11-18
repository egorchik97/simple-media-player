#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "../config/Config.h"
#include "../ui/Appearance.h"

#include <windows.h>

namespace media {

class SettingsUI {
public:
    explicit SettingsUI(Config& config) : config_(config) {}

    // Paints settings content at the original 340x392 coordinates, including
    // headings and footers. The caller owns background and window chrome.
    void Render(HWND hWnd, HDC dc, int page);
    // Mutates configuration/page only; caller owns persistence and window layout.
    bool HandleClick(HWND hWnd, POINT p, int& page);

private:
    void ChooseThemeColor(HWND h, int& color);
    void SettingsHeader(HDC dc, const std::wstring& title);
    void SettingsCard(HDC dc, int index, const std::wstring& label, bool accent = false, int textRight = 312);
    void SettingsToggle(HDC dc, int index, const std::wstring& label, bool value);
    Config& config_;
};

} // namespace media
