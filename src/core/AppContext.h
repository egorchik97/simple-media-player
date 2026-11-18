#pragma once

#include "Types.h"

namespace media {

struct Config;
class MediaSession;
class VolumeController;
class LyricsEngine;
class AsrEngine;
class Logger;

/// Central context holding all application dependencies.
/// Owned by Application; injected into components via constructors.
struct AppContext {
    std::unique_ptr<Config> config;
    std::unique_ptr<MediaSession> mediaSession;
    std::unique_ptr<VolumeController> volumeController;
    std::unique_ptr<LyricsEngine> lyricsEngine;
    std::unique_ptr<AsrEngine> asrEngine;
    std::unique_ptr<Logger> logger;
};

} // namespace media
