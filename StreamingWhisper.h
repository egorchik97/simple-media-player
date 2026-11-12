#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace streaming {

struct Token {
    int id;
    std::string text; // UTF-8 token bytes; a token need not be a complete character.
    int64_t beginMs, endMs; // Relative to this Infer() input, not a stream clock.
};

struct Result {
    std::vector<Token> tokens;
    double inferenceMs = 0;
    bool aborted = false;
};

// Windows, C++20, official whisper.cpp v1.8.3 headers and matching x64 DLLs.
// root/runtime/whisper/{ggml-base,ggml-cpu,ggml,whisper}.dll
// root/runtime/models/ggml-base.bin
// Owns one reusable CPU context. Infer calls on an instance are serialized;
// destruction must not overlap any member call. Does not retain PCM or abort.
// The caller must keep PCM unchanged and abort alive until Infer returns.
// Invalid PCM throws invalid_argument; loader/inference failures throw exceptions.
// Empty PCM is a successful no-op. Cancellation returns no partial tokens.
// DLL logging is silenced process-wide (Whisper/ggml expose global log hooks).
class Recognizer {
public:
    explicit Recognizer(const std::filesystem::path& root);
    ~Recognizer();
    Recognizer(const Recognizer&) = delete;
    Recognizer& operator=(const Recognizer&) = delete;
    Recognizer(Recognizer&&) = delete;
    Recognizer& operator=(Recognizer&&) = delete;

    double ModelLoadMs() const;
    Result Infer(std::span<const float> pcm,
                 const std::atomic_bool* abort = nullptr);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace streaming

// Integration: either compile StreamingWhisper.cpp as a normal TU, OR include it
// once in Asr.cpp (optionally #define STREAMING_WHISPER_IMPLEMENTATION first).
// Never do both. No Whisper/ggml import libraries are required.
// Standalone benchmark build, from a VS developer prompt:
// cl /std:c++20 /EHsc /O2 /utf-8 benchmark.cpp StreamingWhisper.cpp
// Header dependency: runtime/include/whisper.h and its official v1.8.3 ggml
// transitive headers. This wrapper deliberately provides no guessed ABI fallback.
// Compile-only check: cl /std:c++20 /EHsc /W4 /Zs StreamingWhisper.cpp
// (StreamingWhisper.cpp alone is a library TU, not an executable with main.)
