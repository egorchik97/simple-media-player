#include "Application.h"

#include "../media/MediaSession.h"
#include "../media/VolumeController.h"
#include "../audio/LyricsEngine.h"
#include "../audio/AsrEngine.h"
#include "../config/Config.h"
#include "../ui/PopupWindow.h"
#include "../ui/Appearance.h"
#include "../utils/Logger.h"

namespace media {

Application::Application() {
    context_.config = std::make_unique<Config>();
    context_.logger = std::make_unique<Logger>();
    context_.mediaSession = std::make_unique<MediaSession>(context_.logger.get());
    context_.volumeController = std::make_unique<VolumeController>(context_.logger.get(), context_.mediaSession.get());
    context_.lyricsEngine = std::make_unique<LyricsEngine>();
    context_.asrEngine = std::make_unique<AsrEngine>();
}

Application::~Application() {
    Shutdown();
}

int Application::Run(HINSTANCE instance, PWSTR cmdLine, int showCmd) {
    if (!Initialize(instance, cmdLine)) return 1;
    ShowWindow(mainWindow_, showCmd);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    Shutdown();
    return static_cast<int>(msg.wParam);
}

bool Application::Initialize(HINSTANCE instance, PWSTR cmdLine) {
    // Initialize GDI+ for high-quality rendering
    if (!appearance_detail::InitGdiplus()) {
        return false;
    }

    // Load configuration
    ConfigManager configMgr;
    configMgr.Load(*context_.config);

    // Handle --no-asr command line override
    if (cmdLine) {
        std::wstring args(cmdLine);
        if (args == L"--no-asr") context_.config->asr = false;
    }

    // Start background workers
    context_.mediaSession->Start();
    context_.volumeController->Start();

    // Create popup window
    popupWindow_ = std::make_unique<PopupWindow>(context_);
    if (!popupWindow_->Create(instance, SW_SHOW)) return false;
    mainWindow_ = popupWindow_->GetWindow();

    return true;
}

void Application::Shutdown() {
    if (popupWindow_) {
        popupWindow_->Destroy();
    }
    context_.mediaSession->Stop();
    context_.volumeController->Stop();
    context_.asrEngine->Stop();

    // Shutdown GDI+
    appearance_detail::ShutdownGdiplus();
}

} // namespace media
