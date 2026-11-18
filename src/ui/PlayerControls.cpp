#include "PlayerControls.h"
#include "SettingsUI.h"

#include <algorithm>
#include <string>

namespace media::appearance_detail {
namespace {

void Box(HDC dc, RECT r, COLORREF color, int radius = 0) {
    HBRUSH brush = CreateSolidBrush(color);
    auto oldBrush = SelectObject(dc, brush); auto oldPen = SelectObject(dc, GetStockObject(NULL_PEN));
    RoundRect(dc, r.left, r.top, r.right, r.bottom, radius, radius);
    SelectObject(dc, oldPen); SelectObject(dc, oldBrush); DeleteObject(brush);
}

void Text(HDC dc, std::wstring text, RECT r, int size, COLORREF color, bool bold = false, UINT align = DT_LEFT) {
    auto font = CreateFontW(-size, 0, 0, 0, bold ? FW_SEMIBOLD : FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    auto old = SelectObject(dc, font); SetTextColor(dc, color); SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, text.c_str(), -1, &r, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX | align);
    SelectObject(dc, old); DeleteObject(font);
}

void Paragraph(HDC dc, const std::wstring& text, RECT r, COLORREF color) {
    auto font = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    auto old = SelectObject(dc, font); SetTextColor(dc, color); SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, text.c_str(), -1, &r, DT_WORDBREAK | DT_END_ELLIPSIS | DT_NOPREFIX);
    SelectObject(dc, old); DeleteObject(font);
}

std::wstring Time(int64_t ticks) {
    auto seconds = std::max<int64_t>(0, ticks / 10000000);
    return std::to_wstring(seconds / 60) + L":" + (seconds % 60 < 10 ? L"0" : L"") + std::to_wstring(seconds % 60);
}

void DrawCover(HDC dc, const std::shared_ptr<Cover>& cover) {
    RECT r{16, 14, 72, 70};
    Box(dc, r, RGB(39,42,48), 12);
    if (!cover || cover->pixels.empty()) {
        Text(dc, L"♪", r, 26, RGB(132,139,151), false, DT_CENTER); return;
    }
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = cover->width; info.bmiHeader.biHeight = -static_cast<LONG>(cover->height);
    info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
    int saved = SaveDC(dc);
    IntersectClipRect(dc, r.left, r.top, r.right, r.bottom);
    int side = static_cast<int>(std::min(cover->width, cover->height));
    StretchDIBits(dc, r.left, r.top, 56, 56, (cover->width-side)/2, (cover->height-side)/2,
        side, side, cover->pixels.data(), &info, DIB_RGB_COLORS, SRCCOPY);
    RestoreDC(dc, saved);
}

} // namespace

void PaintControls(HDC dc, const RenderState& state) {
    if (!dc || !state.config || !state.layout) return;
    const auto& config = *state.config;
    const auto& layout = *state.layout;
    const int width = layout.width();
    const auto muted = MutedColor(config), white = TextColor(config), accent = AccentColor(config);
    Text(dc, state.settings ? L"‹" : L"•••", layout.settingsButton(), 20, muted, false, DT_CENTER);
    Box(dc, layout.closeButton(), RGB(54,35,43), 10);
    Text(dc, L"×", layout.closeButton(), 21, RGB(255,180,185), false, DT_CENTER);
    if (state.settings) {
        // SettingsUI retains a mutable reference for input; Render only reads it.
        SettingsUI(const_cast<Config&>(config)).Render(nullptr, dc, state.settingsPage);
        return;
    }
    if (!state.mediaState) return;
    const auto& s = *state.mediaState;
    if (!state.expanded) {
        if (config.cover) DrawCover(dc, s.cover);
        int left = config.cover ? 80 : 16;
        if (config.title) Text(dc, s.title, {left,8,width-60,32}, 14, white, true);
        if (config.artist) Text(dc, s.artist, {left,34,layout.compactTextRight(),60}, 12, muted);
        if (!config.title && !config.artist && !config.cover) Text(dc, L"Media", {16,8,width-60,32}, 16, muted);
        bool enabled[] = {s.previous, s.toggle, s.next};
        const wchar_t* icons[] = {L"Ⅰ◀", s.playing ? L"Ⅱ" : L"▶", L"▶Ⅰ"};
        for (int i = 0; i < 3; ++i) if (layout.compactButtonVisible(i, config))
            Text(dc, icons[i], layout.compactButton(i), i == 1 ? 18 : 13, enabled[i] && !state.busy ? white : muted, false, DT_CENTER);
        Text(dc, L"⌄", layout.expandButton(), 20, white, false, DT_CENTER);
        if (config.compactLyrics) {
            Box(dc, {16,75,width-16,77}, accent, 2);
            Text(dc, state.compactLyricsText, {16,79,width-16,99}, 14, white);
        }
        return;
    }
    DrawCover(dc, s.cover);
    Text(dc, s.title, {84,16,280,41}, 17, white, true);
    Text(dc, s.artist.empty() ? (s.session ? L"Медиаплеер" : L"Нет активного трека") : s.artist, {84,43,296,66}, 13, muted);
    bool enabled[] = {s.previous, s.toggle, s.next};
    for (int i = 0; i < 3; ++i) {
        RECT r = layout.button(i); bool active = enabled[i] && !state.busy;
        if (i == 1) Box(dc, r, active ? accent : RGB(36,39,46), 48);
        auto color = active ? (i == 1 ? RGB(23,25,30) : white) : RGB(80,85,95);
        int cx = (r.left+r.right)/2, cy = (r.top+r.bottom)/2;
        if (i == 1 && s.playing) {
            Box(dc, {cx-7,cy-8,cx-2,cy+8}, color); Box(dc, {cx+2,cy-8,cx+7,cy+8}, color);
        } else {
            int direction = i == 0 ? -1 : 1;
            POINT points[] = {{cx-5*direction,cy-8},{cx-5*direction,cy+8},{cx+7*direction,cy}};
            auto brush = CreateSolidBrush(color); auto ob = SelectObject(dc, brush); auto op = SelectObject(dc, GetStockObject(NULL_PEN));
            Polygon(dc, points, 3); SelectObject(dc, op); SelectObject(dc, ob); DeleteObject(brush);
            if (i != 1) { int x = cx+8*direction; Box(dc, {x-1,cy-8,x+2,cy+8}, color); }
        }
    }
    // Layout regions include the original slider hit padding (6 left, 7 right).
    const auto seek = layout.seekRegion();
    const int left = seek.left + 6, right = seek.right - 7, seekY = seek.top + 8;
    int64_t position = state.seekDrag ? state.seekPosition : s.position;
    double fraction = s.end > s.start ? double(position-s.start)/(s.end-s.start) : 0;
    int x = left + int((right-left)*std::clamp(fraction, 0.0, 1.0));
    Box(dc, {left,seekY,right,seekY+4}, RGB(58,62,72), 4);
    if (x > left) Box(dc, {left,seekY,x,seekY+4}, s.seek ? accent : muted, 4);
    if (s.seek) Box(dc, {x-5,seekY-3,x+5,seekY+7}, accent, 10);
    Text(dc, Time(position-s.start), {16,143,100,161}, 12, muted);
    Text(dc, Time(s.end-s.start), {250,143,324,161}, 12, muted, false, DT_RIGHT);
    if (config.volume) {
        float level = state.volumeDrag && state.volumeAvailable ? state.volumePreview : state.volumeLevel;
        Text(dc, !state.volumeAvailable ? L"Недоступно" : state.volumeMuted ? L"Звук выкл." : L"Источник " + std::to_wstring(int(level*100+0.5f)) + L"%", {16,162,118,187}, 12, muted);
        const auto volume = layout.volumeRegion();
        const int volumeLeft = volume.left + 6, volumeRight = volume.right - 7, volumeY = volume.top + 11;
        int vx = volumeLeft + int(level*(volumeRight-volumeLeft));
        Box(dc, {volumeLeft,volumeY,volumeRight,volumeY+4}, RGB(58,62,72), 4);
        if (state.volumeAvailable) {
            if (vx > volumeLeft) Box(dc, {volumeLeft,volumeY,vx,volumeY+4}, accent, 4);
            Box(dc, {vx-5,volumeY-3,vx+5,volumeY+7}, accent, 10);
        }
    }
    std::wstring status = !s.error.empty() ? s.error : state.busy ? L"Отправка команды…" : !s.session ? L"Нажмите Play в музыкальном плеере" : !s.seek ? L"Плеер не поддерживает перемотку" : L"";
    Text(dc, status, {16,190,324,208}, 12, !s.error.empty() ? RGB(255,159,159) : muted);
    if (config.sourceLyrics) {
        Text(dc, L"Текст • LRCLIB", {16,216,324,240}, 13, accent, true);
        Paragraph(dc, state.lyricsText, {16,244,324,350}, white);
    } else if (config.asr) {
        Text(dc, L"Текст • Whisper (звук ПК)", {16,220,324,242}, 13, accent, true);
        Paragraph(dc, state.lyricsText, {16,244,324,350}, state.lyricsError ? RGB(255,159,159) : white);
    }
}

} // namespace media::appearance_detail