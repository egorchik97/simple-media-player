#pragma once

#include "../core/AppContext.h"
#include "../core/Types.h"
#include "../ui/Layout.h"
#include "../ui/Appearance.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <windows.h>
#include <gdiplus.h>

namespace media {

class PopupWindow {
public:
    explicit PopupWindow(AppContext& context);
    ~PopupWindow();

    PopupWindow(const PopupWindow&) = delete;
    PopupWindow& operator=(const PopupWindow&) = delete;

    bool Create(HINSTANCE instance, int showCmd);
    void Destroy();
    HWND GetWindow() const { return window_; }

    // Message routing from static WndProc
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);

private:
    // Window procedure (static, delegates to instance)
    static LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

    // Message handlers
    LRESULT OnCreate(HWND hWnd);
    LRESULT OnTimer();
    LRESULT OnLButtonDown(POINT p);
    LRESULT OnMouseMove(POINT p);
    LRESULT OnLButtonUp(POINT p);
    LRESULT OnKeyDown(WPARAM vKey);
    LRESULT OnDestroy();
    LRESULT OnEnterSizeMove();
    LRESULT OnExitSizeMove();
    LRESULT OnDisplayChange();
    LRESULT OnSettingChange();

    // Rendering - unified frame rendering for layered window
    void RenderFrame();
    std::wstring CompactLyricsText(const MediaState& s);
    void ApplyLayout();
    void Persist();
    bool configSaveErrorShown_ = false;
    void LyricsTick();
    void HoverTick();
    void SyncFrameTimer();

    // Input handling
    void HandleSettingsClick(POINT p);
    void CancelDrag();

    AppContext& context_;
    HWND window_ = nullptr;
    Layout layout_;
    bool settings_ = false;
    int settingsPage_ = 0;
    bool expanded_ = false;
    bool moving_ = false;
    bool dragging_ = false;
    bool volumeDrag_ = false;
    bool settingsDrag_ = false;
    bool settingsDragMoved_ = false;
    POINT settingsDragStart_{};
    MediaState dragState_;
    int dragX_ = 0;
    float volumePreview_ = 0.0f;
    HoverState hover_;
    std::atomic<bool> stop_{false};
    UINT_PTR timerId_ = 0;
    UINT timerInterval_ = 150;
    std::wstring asrTrack_;
    bool asrRunning_ = false;
};

} // namespace media
