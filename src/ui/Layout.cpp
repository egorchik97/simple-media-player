#include "Layout.h"

#include <algorithm>
#include <limits>

namespace media {

void Layout::Update(int, int, const Config& config, bool expanded, bool settings,
                    int, POINT anchor, const RECT& workArea) {
    width_ = settings || expanded ? 340 : config.compactWidth;
    height_ = settings ? 392 : expanded ? ((config.asr || config.sourceLyrics) ? 360 : 210)
                              : (config.compactLyrics ? config.compactLyricsHeight : config.compactHeight);
    anchor_ = anchor;

    MONITORINFO mi{sizeof(mi)};
    const bool found = GetMonitorInfoW(MonitorFromPoint(anchor, MONITOR_DEFAULTTONEAREST), &mi) != FALSE;
    windowRect_ = PopupRect(anchor, width_, height_, found ? mi.rcWork : workArea);

    closeButton_ = {width_ - 28, 4, width_ - 4, 28};
    expandButton_ = {width_ - 54, 38, width_ - 30, 66};
    settingsButton_ = {width_ - 56, 4, width_ - 30, 28};

    int right = width_ - 56;
    for (int i = 2; i >= 0; --i) {
        if (compactButtonVisible(i, config)) {
            buttons_[i] = {right - 26, 34, right, 66};
            right -= 28;
        } else {
            buttons_[i] = {};
        }
    }

    compactTextRight_ = expandButton_.left - 6;
    for (int i = 0; i < 3; ++i) {
        if (compactButtonVisible(i, config)) {
            compactTextRight_ = buttons_[i].left - 6;
            break;
        }
    }

    seekRegion_ = {16 - 6, 126, 324 + 7, 143};
    volumeRegion_ = {120, 162, 324 + 7, 187};
}

RECT Layout::compactButton(int index) const {
    return buttons_[index];
}

bool Layout::compactButtonVisible(int index, const Config& config) const {
    return index == 0 ? config.compactPrevious
         : index == 1 ? config.pause
         : index == 2 ? config.compactNext
         : false;
}

int Layout::settingsParentPage(int page) const {
    if (page == 5 || page == 6) return 3;
    return page == 2 || page == 3 ? 1 : 0;
}

float Layout::VolumeLevel(int x) const {
    return float(std::clamp(x, 126, 324) - 126) / float(324 - 126);
}

RECT Layout::PopupRect(POINT anchor, int w, int h, RECT work) {
    auto axis = [](LONG position, int size, LONG low, LONG high, LONG& first, LONG& last) {
        const long long extent = size > 0 ? size : 0;
        long long limit = static_cast<long long>(high) - extent;
        if (limit < low) limit = low;
        long long start = position;
        if (start < low) start = low;
        if (start > limit) start = limit;
        first = static_cast<LONG>(start);
        long long end = start + extent;
        if (end > (std::numeric_limits<LONG>::max)()) end = (std::numeric_limits<LONG>::max)();
        last = static_cast<LONG>(end);
    };
    RECT result{};
    axis(anchor.x, w, work.left, work.right, result.left, result.right);
    axis(anchor.y, h, work.top, work.bottom, result.top, result.bottom);
    return result;
}

} // namespace media
