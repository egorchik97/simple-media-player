#pragma once

#include "../core/Types.h"
#include "../config/Config.h"

#include <windows.h>

namespace media {

/// Calculates popup geometry, button/hit-test regions, and slider positions.
/// All layout math lives here so painting and input handling share one source of truth.
class Layout {
public:
    Layout() = default;

    void Update(int width, int height, const Config& config, bool expanded, bool settings,
                int settingsPage, POINT anchor, const RECT& workArea);

    int width() const { return width_; }
    int height() const { return height_; }
    POINT anchor() const { return anchor_; }

    RECT closeButton() const { return closeButton_; }
    RECT expandButton() const { return expandButton_; }
    RECT settingsButton() const { return settingsButton_; }
    RECT compactButton(int index) const;
    RECT seekRegion() const { return seekRegion_; }
    RECT volumeRegion() const { return volumeRegion_; }
    RECT button(int index) const { int x = 88 + index * 60; return {x, 78, x + 44, 122}; }
    int compactTextRight() const { return compactTextRight_; }
    RECT settingRow(int index) const { return {16, 48 + index * 30, 324, 78 + index * 30}; }

    bool compactButtonVisible(int index, const Config& config) const;
    int settingsParentPage(int page) const;

    bool Hit(RECT r, POINT p) const { return PtInRect(&r, p) != FALSE; }
    float VolumeLevel(int x) const;

    RECT windowRect() const { return windowRect_; }

    static RECT PopupRect(POINT anchor, int w, int h, RECT work);

private:
    int width_ = 280;
    int height_ = 70;
    POINT anchor_{};
    RECT windowRect_{};

    RECT closeButton_{};
    RECT expandButton_{};
    RECT settingsButton_{};
    RECT seekRegion_{};
    RECT volumeRegion_{};
    RECT buttons_[3] = {};
    int compactTextRight_ = 0;
};

} // namespace media
