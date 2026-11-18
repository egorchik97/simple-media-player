#pragma once

#include "../core/Types.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace media {

class Logger;
class MediaSession;
namespace volume_detail { struct Shared; }

/// Manages source-specific audio session volume via ISimpleAudioVolume.
/// Decoupled from UI; matches media session to audio source by AUMID/process path/EXE.
class VolumeController {
public:
    VolumeController(Logger* logger, MediaSession* mediaSession);
    ~VolumeController();

    VolumeController(const VolumeController&) = delete;
    VolumeController& operator=(const VolumeController&) = delete;

    void Start();
    void Stop();

    VolumeState Snapshot() const;
    std::wstring SourceLabel() const;
    std::vector<VolumeChoice> Choices() const;

    void SetLevel(float level);
    void SetManualSource(const std::wstring& key);
    void CycleManualSource();

private:
    void Worker();

    Logger* logger_;
    MediaSession* mediaSession_;
    std::atomic<bool> running_{false};
    std::thread workerThread_;

    std::mutex lifecycleMutex_;
    std::unique_ptr<volume_detail::Shared> data_;
};

} // namespace media
