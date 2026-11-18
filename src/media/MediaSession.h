#pragma once

#include "../core/Types.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <thread>

namespace media {

class Logger;

/// Encapsulates Windows GSMTC interaction:
/// - Getting the active media session
/// - Tracking track changes
/// - Play/Pause/Next/Previous commands
/// - Metadata and source info
/// - Event handling
///
/// Handles fast media session switching and stale async results.
class MediaSession {
public:
    explicit MediaSession(Logger* logger);
    ~MediaSession();

    MediaSession(const MediaSession&) = delete;
    MediaSession& operator=(const MediaSession&) = delete;

    /// Start the background polling thread. Must be called after COM/MTA is initialized.
    void Start();

    /// Stop the background thread and wait for it to finish.
    void Stop();

    /// Get a thread-safe snapshot of current media state.
    MediaState Snapshot() const;

    /// Queue a media control command (1=previous, 2=toggle, 3=next).
    void QueueCommand(int action);

    /// Queue a seek command to a specific position (100-nanosecond units).
    void QueueSeek(int64_t position);
    void QueueSeek(int64_t position, const Session& target);
    bool Busy() const { return busy_.load(); }

private:
    Logger* logger_;
    std::atomic<bool> running_{false};
    std::thread workerThread_;
    std::thread metadataThread_;
    std::atomic<bool> metadataStop_{false};

    mutable std::mutex mutex_;
    MediaState state_;
    MediaCommand command_;
    std::atomic<bool> busy_{false};

    Session requested_{nullptr};
    uint64_t generation_ = 0;
    bool pending_ = false;
    std::chrono::steady_clock::time_point metadataDue_;
};

} // namespace media
