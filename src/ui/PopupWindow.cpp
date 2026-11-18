#include "PopupWindow.h"
#include "SettingsUI.h"

#include "../core/AppContext.h"
#include "../core/MediaDiagnostics.h"
#include "../ui/Appearance.h"
#include "../media/MediaSession.h"
#include "../media/VolumeController.h"
#include "../audio/LyricsEngine.h"
#include "../audio/AsrEngine.h"

#include <windowsx.h>
#include <commdlg.h>
#include <algorithm>
#include <chrono>
#include <thread>
#include <sstream>
#include <iomanip>

#pragma comment(lib, "comdlg32.lib")

namespace media {

static constexpr int Left = 16, Right = 324;
static std::wstring CompactSingleLine(std::wstring text, bool firstLine=false) {
    if(firstLine) text=text.substr(0,text.find_first_of(L"\r\n"));
    std::wstring result;
    bool space=false;
    for(wchar_t c:text) {
        if(c==L'\r' || c==L'\n' || c==L'\t' || c==L' ') { space=!result.empty(); continue; }
        if(space) result+=L' ';
        result+=c; space=false;
    }
    return result;
}
std::wstring PopupWindow::CompactLyricsText(const MediaState& s) {
    if(!(*context_.config).sourceLyrics && !(*context_.config).asr) return L"Выберите режим текста в настройках";
    if(!s.session) return L"Ожидание трека…";
    if((*context_.config).sourceLyrics) {
        auto lyrics=(*context_.lyricsEngine).Snapshot();
        auto text=lyrics.DisplayAt(double(s.position-s.start)/10000000.0);
        return CompactSingleLine(text.empty()?lyrics.status:text,true);
    }
    if(!s.playing) return L"Пауза — распознавание остановлено";
    auto a=(*context_.asrEngine).Snapshot();
    auto text=a.provisional.empty()?a.text:a.provisional;
    if(text.empty()) return CompactSingleLine(a.status);
    text=CompactSingleLine(text);
    if(text.size()>120) {
        size_t offset=text.size()-120;
        if(text[offset]>=0xDC00 && text[offset]<=0xDFFF) ++offset;
        text=L"…"+text.substr(offset);
    }
    return text;
}
static int64_t SeekPosition(int x, const MediaState& s) {
    auto position = s.start + static_cast<int64_t>((s.end - s.start) *
        (std::clamp(x, Left, Right) - Left) / double(Right - Left));
    return std::clamp(position, s.minimum, s.maximum);
}

static std::wstring Time(int64_t ticks) {
    auto seconds = std::max<int64_t>(0, ticks / 10000000);
    return std::to_wstring(seconds / 60) + L":" + (seconds % 60 < 10 ? L"0" : L"") + std::to_wstring(seconds % 60);
}


PopupWindow::PopupWindow(AppContext& context) : context_(context) {}

PopupWindow::~PopupWindow() {
    Destroy();
}

bool PopupWindow::Create(HINSTANCE instance, int showCmd) {
    if (window_) return false;
    WNDCLASSW wc{};
    wc.hInstance = instance;
    wc.lpfnWndProc = PopupWindow::WndProc;
    wc.lpszClassName = L"MediaPopupClass";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);

    if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;

    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);

    POINT anchor{work.right - 360, work.bottom - 230};
    if (context_.config->windowPositionSaved) {
        anchor.x = context_.config->windowX;
        anchor.y = context_.config->windowY;
    }

    DWORD style = WS_POPUP;
    DWORD ex = WS_EX_TOPMOST | WS_EX_TOOLWINDOW;
    expanded_ = context_.config->pinned;
    stop_ = false;
    layout_.Update(0, 0, *context_.config, expanded_, settings_, settingsPage_, anchor, work);
    const auto bounds = layout_.windowRect();
    RECT size{0, 0, layout_.width(), layout_.height()};
    AdjustWindowRectEx(&size, style, FALSE, ex);

    HWND h = CreateWindowExW(
        ex, wc.lpszClassName, L"Media Popup", style,
        bounds.left, bounds.top,
        size.right - size.left, size.bottom - size.top,
        nullptr, nullptr, instance, this);
    if (!h) return false;

    ApplyLayout();
    LyricsTick();

    ShowWindow(h, showCmd);
    UpdateWindow(h);

    return true;
}

void PopupWindow::Destroy() {
    if (window_) {
        if (timerId_) KillTimer(window_, timerId_);
        CancelDrag();
        DestroyWindow(window_);
        window_ = nullptr;
    }
}

LRESULT CALLBACK PopupWindow::WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto self = reinterpret_cast<PopupWindow*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        self = static_cast<PopupWindow*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        if (!self) return FALSE;
        self->window_ = hWnd;
        SetLastError(ERROR_SUCCESS);
        if (!SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self)) && GetLastError()) {
            self->window_ = nullptr;
            return FALSE;
        }
    }
    if (!self) return DefWindowProcW(hWnd, message, wParam, lParam);
    const auto result = self->HandleMessage(message, wParam, lParam);
    if (message == WM_NCDESTROY) {
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, 0);
        self->window_ = nullptr;
    }
    return result;
}

LRESULT PopupWindow::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        return OnCreate(window_);
    case WM_TIMER:
        if (wParam == timerId_) return OnTimer();
        break;
    case WM_ENTERSIZEMOVE:
        return OnEnterSizeMove();
    case WM_EXITSIZEMOVE:
        return OnExitSizeMove();
    case WM_DISPLAYCHANGE:
        return OnDisplayChange();
    case WM_SETTINGCHANGE:
        return OnSettingChange();
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        BeginPaint(window_, &paint);
        RenderFrame();
        EndPaint(window_, &paint);
        return 0;
    }
    case WM_LBUTTONDOWN: {
        POINT p{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        return OnLButtonDown(p);
    }
    case WM_MOUSEMOVE: {
        POINT p{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        return OnMouseMove(p);
    }
    case WM_LBUTTONUP: {
        POINT p{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        return OnLButtonUp(p);
    }
    case WM_KEYDOWN:
        return OnKeyDown(wParam);
    case WM_CANCELMODE:
        CancelDrag();
        return 0;
    case WM_CAPTURECHANGED:
        {
        const bool moved = settingsDragMoved_;
        settingsDrag_ = false;
        settingsDragMoved_ = false;
        if (moved) OnExitSizeMove();
        dragging_ = false;
        volumeDrag_ = false;
        dragState_ = {};
        InvalidateRect(window_, nullptr, FALSE);
        return 0;
        }
    case WM_DESTROY:
        return OnDestroy();
    }
    return DefWindowProcW(window_, message, wParam, lParam);
}

LRESULT PopupWindow::OnCreate(HWND) {
    SyncFrameTimer();
    return timerId_ ? 0 : -1;
}

void PopupWindow::SyncFrameTimer() {
    if (!window_ || !context_.config) return;
    const UINT interval = context_.config->snowfallEnabled ? 33u : 150u;
    if (timerId_ && timerInterval_ == interval) return;
    if (timerId_) KillTimer(window_, timerId_);
    timerInterval_ = interval;
    timerId_ = SetTimer(window_, 1, interval, nullptr);
}

LRESULT PopupWindow::OnTimer() {
    SyncFrameTimer();
    LyricsTick();
    HoverTick();
    if (volumeDrag_ && !context_.volumeController->Snapshot().available) {
        CancelDrag();
    }
    RenderFrame();
    return 0;
}

LRESULT PopupWindow::OnEnterSizeMove() {
    moving_ = true;
    return 0;
}

LRESULT PopupWindow::OnExitSizeMove() {
    moving_ = false;
    RECT r{};
    GetWindowRect(window_, &r);
    POINT anchor{r.left, r.top};
    context_.config->windowX = r.left;
    context_.config->windowY = r.top;
    context_.config->windowPositionSaved = true;
    Persist();
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    layout_.Update(layout_.width(), layout_.height(), *context_.config,
                   expanded_, settings_, settingsPage_, anchor, work);
    ApplyLayout();
    InvalidateRect(window_, nullptr, FALSE);
    return 0;
}

LRESULT PopupWindow::OnDisplayChange() {
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    layout_.Update(layout_.width(), layout_.height(), *context_.config,
                   expanded_, settings_, settingsPage_, layout_.anchor(), work);
    ApplyLayout();
    return 0;
}

LRESULT PopupWindow::OnSettingChange() {
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    layout_.Update(layout_.width(), layout_.height(), *context_.config,
                   expanded_, settings_, settingsPage_, layout_.anchor(), work);
    ApplyLayout();
    return 0;
}

LRESULT PopupWindow::OnDestroy() {
    context_.asrEngine->Stop();
    asrRunning_ = false;
    CancelDrag();
    if (timerId_) KillTimer(window_, timerId_);
    timerId_ = 0;
    stop_ = true;
    PostQuitMessage(0);
    return 0;
}

LRESULT PopupWindow::OnKeyDown(WPARAM vKey) {
    if (vKey == VK_ESCAPE) {
        CancelDrag();
        if (settings_) {
            if (settingsPage_ > 0) {
                settingsPage_ = layout_.settingsParentPage(settingsPage_);
                RECT work{};
                SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
                layout_.Update(layout_.width(), layout_.height(), *context_.config,
                               expanded_, settings_, settingsPage_, layout_.anchor(), work);
    ApplyLayout();
            } else {
                settings_ = false;
                RECT work{};
                SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
                layout_.Update(layout_.width(), layout_.height(), *context_.config,
                               expanded_, settings_, settingsPage_, layout_.anchor(), work);
    ApplyLayout();
            }
        } else if (!context_.config->pinned) {
            expanded_ = false;
            hover_.armed = false;
            RECT work{};
            SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
            layout_.Update(layout_.width(), layout_.height(), *context_.config,
                           expanded_, settings_, settingsPage_, layout_.anchor(), work);
    ApplyLayout();
        }
        return 0;
    }
    if (vKey == VK_SPACE) {
        if (!settings_ && !dragging_ && !volumeDrag_ && !settingsDrag_) {
            context_.mediaSession->QueueCommand(2);
            InvalidateRect(window_, nullptr, FALSE);
            return 0;
        }
    }
    return 0;
}

LRESULT PopupWindow::OnLButtonDown(POINT p) {
    RECT close = layout_.closeButton();
    RECT settings = layout_.settingsButton();

    if (layout_.Hit(close, p)) {
        DestroyWindow(window_);
        return 0;
    }

    if (layout_.Hit(settings, p)) {
        CancelDrag();
        if (settings_ && settingsPage_ > 0) {
            settingsPage_ = layout_.settingsParentPage(settingsPage_);
        } else {
            settings_ = !settings_;
        }
        RECT work{};
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
        layout_.Update(layout_.width(), layout_.height(), *context_.config,
                       expanded_, settings_, settingsPage_, layout_.anchor(), work);
    ApplyLayout();
        return 0;
    }

    if (settings_) {
        settingsDrag_ = true;
        settingsDragMoved_ = false;
        settingsDragStart_ = p;
        SetCapture(window_);
        return 0;
    }

    if (!expanded_) {
        auto s = context_.mediaSession->Snapshot();
        bool enabled[] = {s.previous, s.toggle, s.next};
        for (int i = 0; i < 3; ++i) {
            if (layout_.compactButtonVisible(i, *context_.config) &&
                layout_.Hit(layout_.compactButton(i), p)) {
                if (enabled[i] && !stop_ && !context_.mediaSession->Busy()) {
                    if (i == 0) context_.mediaSession->QueueCommand(1);
                    else if (i == 1) context_.mediaSession->QueueCommand(2);
                    else if (i == 2) context_.mediaSession->QueueCommand(3);
                }
                InvalidateRect(window_, nullptr, FALSE);
                return 0;
            }
        }
        if (layout_.Hit(layout_.expandButton(), p)) {
            expanded_ = true;
            hover_.armed = false;
            RECT work{};
            SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
            layout_.Update(layout_.width(), layout_.height(), *context_.config,
                           expanded_, settings_, settingsPage_, layout_.anchor(), work);
    ApplyLayout();
            return 0;
        }
        SendMessageW(window_, WM_NCLBUTTONDOWN, HTCAPTION, 0);
        return 0;
    }

    if (context_.config->volume && layout_.Hit(layout_.volumeRegion(), p) &&
        context_.volumeController->Snapshot().available) {
        volumeDrag_ = true;
        volumePreview_ = layout_.VolumeLevel(p.x);
        context_.volumeController->SetLevel(volumePreview_);
        SetCapture(window_);
        InvalidateRect(window_, nullptr, FALSE);
        return 0;
    }

    auto s = context_.mediaSession->Snapshot();
    if (stop_ || context_.mediaSession->Busy()) return 0;
    bool enabled[] = {s.previous, s.toggle, s.next};
    for (int i = 0; i < 3; ++i) {
        RECT r = layout_.button(i);
        if (layout_.Hit(r, p) && enabled[i]) {
            if (i == 0) context_.mediaSession->QueueCommand(1);
            else if (i == 1) context_.mediaSession->QueueCommand(2);
            else if (i == 2) context_.mediaSession->QueueCommand(3);
            InvalidateRect(window_, nullptr, FALSE);
            return 0;
        }
    }

    if (s.seek && layout_.Hit(layout_.seekRegion(), p)) {
        dragState_ = s;
        dragging_ = true;
        dragX_ = p.x;
        SetCapture(window_);
        InvalidateRect(window_, nullptr, FALSE);
        return 0;
    }

    if (!layout_.Hit(layout_.seekRegion(), p) &&
        !(context_.config->volume && layout_.Hit(layout_.volumeRegion(), p)) &&
        !layout_.Hit(layout_.button(0), p) && !layout_.Hit(layout_.button(1), p) &&
        !layout_.Hit(layout_.button(2), p)) {
        SendMessageW(window_, WM_NCLBUTTONDOWN, HTCAPTION, 0);
    }
    return 0;
}

LRESULT PopupWindow::OnMouseMove(POINT p) {
    if (settingsDrag_) {
        int x = p.x, y = p.y;
        if (!settingsDragMoved_ &&
            (abs(x - settingsDragStart_.x) > 4 || abs(y - settingsDragStart_.y) > 4)) {
            settingsDragMoved_ = true;
        }
        if (settingsDragMoved_) {
            RECT r{};
            GetWindowRect(window_, &r);
            SetWindowPos(window_, nullptr,
                r.left + (x - settingsDragStart_.x),
                r.top + (y - settingsDragStart_.y),
                0, 0, SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);
            POINT anchor{r.left + (x - settingsDragStart_.x),
                         r.top + (y - settingsDragStart_.y)};
            moving_ = true;
            InvalidateRect(window_, nullptr, FALSE);
        }
        return 0;
    }

    if (volumeDrag_) {
        volumePreview_ = layout_.VolumeLevel(p.x);
        context_.volumeController->SetLevel(volumePreview_);
        InvalidateRect(window_, nullptr, FALSE);
        return 0;
    }

    if (dragging_) {
        dragX_ = p.x;
        InvalidateRect(window_, nullptr, FALSE);
        return 0;
    }

    return 0;
}

LRESULT PopupWindow::OnLButtonUp(POINT p) {
    if (settingsDrag_) {
        bool moved = settingsDragMoved_;
        ReleaseCapture();
        settingsDrag_ = false;
        if (!moved) HandleSettingsClick(settingsDragStart_);
        return 0;
    }

    if (volumeDrag_) {
        context_.volumeController->SetLevel(layout_.VolumeLevel(p.x));
        CancelDrag();
        InvalidateRect(window_, nullptr, FALSE);
        return 0;
    }

    if (dragging_) {
        auto target = dragState_;
        int64_t position = 0;
        if (target.end > target.start) {
            double frac = double(p.x - 16) / double(324 - 16);
            position = target.start + static_cast<int64_t>(
                (target.end - target.start) * std::clamp(frac, 0.0, 1.0));
            position = std::clamp(position, target.minimum, target.maximum);
        }
        dragging_ = false;
        ReleaseCapture();
        context_.mediaSession->QueueSeek(position, target.session);
        dragState_ = {};
        InvalidateRect(window_, nullptr, FALSE);
        return 0;
    }

    return 0;
}

void PopupWindow::CancelDrag() {
    const bool moved = settingsDragMoved_;
    settingsDrag_ = false;
    settingsDragMoved_ = false;
    if (moved) OnExitSizeMove();
    dragging_ = false;
    volumeDrag_ = false;
    dragState_ = {};
    if (GetCapture() == window_) ReleaseCapture();
}

void PopupWindow::HandleSettingsClick(POINT p) {
    if (!settings_ || !SettingsUI(*context_.config).HandleClick(window_, p, settingsPage_)) return;
    if (context_.config->pinned) expanded_ = true;
    Persist();
    SyncFrameTimer();
    LyricsTick();
    OnDisplayChange();
}

void PopupWindow::LyricsTick() {
    auto s=context_.mediaSession->Snapshot();
    const bool valid=s.session && !s.title.empty() && s.title!=L"Без названия" && s.title!=L"Откройте музыку в плеере";
    if((*context_.config).sourceLyrics && valid)
        context_.lyricsEngine->Request(s.title,s.artist,std::max(0.0,double(s.end-s.start)/10000000.0));
    else context_.lyricsEngine->Clear();
    std::wstring key=valid ? s.source+L"\n"+s.artist+L"\n"+s.title : L"";
    bool capture=(*context_.config).asr && valid && s.playing;
    if(asrRunning_ && (!capture || key!=asrTrack_)) { context_.asrEngine->Stop(); asrRunning_=false; }
    if(capture && !asrRunning_) { context_.asrEngine->Start(); asrRunning_=true; }
    asrTrack_=std::move(key);
}
void PopupWindow::HoverTick() {
    POINT p; GetCursorPos(&p); RECT r; GetWindowRect(window_,&r);
    bool inside=PtInRect(&r,p)!=FALSE;
    if (!inside) hover_.armed=true;
    bool blocked=settings_||moving_||dragging_||volumeDrag_||GetCapture()==window_||(*context_.config).pinned;
    if (!blocked && inside && !expanded_ && (*context_.config).hover && hover_.armed) {
        expanded_=true; hover_.armed=false; OnDisplayChange();
        // Re-evaluate only on the next tick, using actual screen bounds.
        hover_.outsideSince=0; return;
    }
    if (hover_.Collapse(inside,blocked,GetTickCount64(),(*context_.config).delay) && expanded_) {
        expanded_=false; hover_.armed=false; OnDisplayChange();
    }
}

void PopupWindow::ApplyLayout() {
    if (!window_) return;
    const auto r = layout_.windowRect();
    SetWindowPos(window_, nullptr, r.left, r.top, r.right-r.left, r.bottom-r.top, SWP_NOZORDER | SWP_NOACTIVATE);
    RenderFrame();
}

void PopupWindow::RenderFrame() {
    if (!window_) return;

    auto s = context_.mediaSession->Snapshot();
    auto volume = context_.volumeController->Snapshot();
    appearance_detail::RenderState state;
    state.config = context_.config.get();
    state.mediaState = &s;
    state.layout = &layout_;
    state.settings = settings_;
    state.settingsPage = settingsPage_;
    state.expanded = expanded_;
    state.volumeDrag = volumeDrag_;
    state.volumePreview = volumePreview_;
    state.volumeAvailable = volume.available;
    state.volumeLevel = volume.level;
    state.volumeMuted = volume.muted;
    state.busy = context_.mediaSession->Busy();
    state.hover = hover_;
    state.seekDrag = dragging_;
    state.seekPosition = dragging_ ? SeekPosition(dragX_, dragState_) : s.position;
    state.compactLyricsText = CompactLyricsText(s);
    if (context_.config->sourceLyrics) {
        auto lyrics = context_.lyricsEngine->Snapshot();
        state.lyricsText = lyrics.DisplayAt(double(s.position - s.start) / 10000000.0);
        if (state.lyricsText.empty()) state.lyricsText = lyrics.status;
    } else if (context_.config->asr) {
        auto asr = context_.asrEngine->Snapshot();
        state.lyricsText = asr.text;
        if (!asr.provisional.empty()) {
            if (!state.lyricsText.empty()) state.lyricsText += L" ";
            state.lyricsText += L"▸ " + asr.provisional;
        }
        state.lyricsError = asr.status.rfind(L"ASR error:", 0) == 0;
        if (state.lyricsError || state.lyricsText.empty())
            state.lyricsText = asr.status.empty() ? L"Распознавание остановлено" : asr.status;
        if (!s.playing) state.lyricsText = L"Пауза — распознавание приостановлено";
        if (state.lyricsText.size() > 450) state.lyricsText = L"…" + state.lyricsText.substr(state.lyricsText.size() - 450);
    }

    // Copy cover data
    if (s.cover) {
        state.cover.width = s.cover->width;
        state.cover.height = s.cover->height;
        state.cover.pixels.assign(s.cover->pixels.begin(), s.cover->pixels.end());
    }

    appearance_detail::RenderFrame(window_, state);
}

void PopupWindow::Persist() {
    ConfigManager manager;
    if (manager.Save(*context_.config)) { configSaveErrorShown_ = false; return; }
    const DWORD error = GetLastError();
    wchar_t detail[512]{};
    FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, error, 0, detail, 512, nullptr);
    const auto message = L"Settings could not be saved. Changes apply only until exit.\n\nPath: "
        + manager.ConfigPath() + L"\nWindows error " + std::to_wstring(error) + L": " + detail;
    OutputDebugStringW(message.c_str());
    if (!configSaveErrorShown_) {
        configSaveErrorShown_ = true; // Set before modal dispatch; reset only after successful save.
        MessageBoxW(window_, message.c_str(), L"Media Popup", MB_OK | MB_ICONERROR);
    }
}

} // namespace media
