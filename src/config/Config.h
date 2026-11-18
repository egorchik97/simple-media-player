#pragma once

#include <shlobj.h>
#include <string>
#include <windows.h>

namespace media {

/// Application configuration matching original PopupConfig format.
struct Config {
    bool cover = true;
    bool title = true;
    bool artist = true;
    bool pause = true;
    bool compactPrevious = false;
    bool compactNext = false;
    bool hover = true;
    bool pinned = false;
    bool volume = true;
    bool asr = false;
    int delay = 700;
    int theme = 0;
    int opacity = 100;
    int glow = 35;
    int roundness = 18;
    bool glass = true;
    bool sourceLyrics = false;
    int customBackground = RGB(20, 23, 32);
    int customAccent = RGB(114, 164, 255);
    int customText = RGB(239, 242, 247);
    int customMuted = RGB(166, 175, 192);
    bool compactLyrics = false;
    int compactWidth = 280;
    int compactHeight = 70;
    int compactLyricsHeight = 102;
    int glassStrength = 50;
    int glassTint = 35;
    int glassSpeed = 30;
    bool glassAnimated = true;

    // Glass pattern settings
    int glassPattern = 0;          // 0=SoftFrost, 1=FlowingGlass, 2=Crystal, 3=LiquidWaves, 4=FineNoise, 5=MinimalGlass, 6=RandomPattern, 7=Legacy
    int glassSeed = 0;             // Seed for procedural generation (0 = auto)
    bool glassRandomSeed = true;   // Use random seed on startup

    // Window position persistence
    int windowX = 0;
    int windowY = 0;
    bool windowPositionSaved = false;

    // Snowfall effect settings
    bool snowfallEnabled = false;           // Enable Snowfall effect
    int snowfallCount = 70;                 // Number of snowflakes (30-150)
    int snowfallSpeed = 4;                  // Falling speed (1-10)
    int snowfallSize = 2;                   // Snowflake size in pixels (1-5)
    int snowfallOpacity = 65;               // Opacity percentage (10-100)
    int snowfallWindDirection = 0;          // Wind direction: 0=None, 1=Left, 2=Right
    int snowfallWindStrength = 50;          // Wind strength (0-100)
    int snowfallBlur = 0;                   // Blur radius in pixels (0-3)
};

class ConfigManager {
public:
    ConfigManager();
    // Absolute LocalAppData root injection for isolated tests; never a CWD fallback.
    explicit ConfigManager(std::wstring localAppDataRoot);
    ~ConfigManager();

    ConfigManager(const ConfigManager&) = delete;
    ConfigManager& operator=(const ConfigManager&) = delete;

    bool Load(Config& config);
    bool Save(const Config& config);
    std::wstring ConfigPath() const;

private:
    bool Prepare();
    std::wstring configPath_;
    std::wstring legacyPath_;
    DWORD pathError_ = ERROR_SUCCESS;
    bool pathValid_ = false;
};

} // namespace media
