#pragma once

#include "../core/AppContext.h"
#include "../core/Types.h"

#include <memory>
#include <windows.h>

namespace media {

class PopupWindow;

/// Application lifecycle: owns all components, runs the Win32 message loop.
class Application {
public:
    Application();
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    int Run(HINSTANCE instance, PWSTR cmdLine, int showCmd);

private:
    bool Initialize(HINSTANCE instance, PWSTR cmdLine);
    void Shutdown();

    AppContext context_;
    std::unique_ptr<PopupWindow> popupWindow_;
    HWND mainWindow_ = nullptr;
    bool running_ = false;
};

} // namespace media
