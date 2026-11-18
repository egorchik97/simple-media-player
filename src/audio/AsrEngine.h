#pragma once

#include "../core/Types.h"
#include <mmreg.h>
#include <deque>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace media {

namespace asr_detail {
// Small bounded window used by the streaming recognizer. It is intentionally
// short enough to publish provisional words while the song is playing.
constexpr size_t ChunkSamples = 16000 * 4;
// Rejects unsupported/compressed layouts. Throws std::runtime_error.
struct Format { unsigned rate, channels, bytes; bool floating; };
Format ParseFormat(const WAVEFORMATEX& format);
std::vector<float> DecodeMono(const Format&, const BYTE* data, size_t frames, bool silent);
// Stateful linear interpolation, bounded history; input packet boundaries are transparent.
class Resampler {
public:
    explicit Resampler(unsigned inputRate);
    std::vector<int16_t> Push(std::span<const float> mono);
private:
    uint64_t inputCount_ = 0, outputCount_ = 0;
    unsigned rate_;
    float previous_ = 0;
};
bool HasEnergy(std::span<const int16_t> samples, double rmsThreshold = 0.008);
// Caller supplies synchronization. Full queue drops oldest (never the executing job).
void PushBounded(std::deque<std::vector<int16_t>>& queue, std::vector<int16_t> chunk);
std::wstring QuoteArgument(const std::wstring& argument);
}

/// Whisper ASR engine - local speech recognition via whisper.cpp DLL.
class AsrEngine {
public:
    explicit AsrEngine(std::filesystem::path root = {});
    ~AsrEngine();

    AsrEngine(const AsrEngine&) = delete;
    AsrEngine& operator=(const AsrEngine&) = delete;

    void Start();
    void Stop();
    AsrSnapshot Snapshot() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace media
