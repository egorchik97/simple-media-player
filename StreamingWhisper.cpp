// Compile normally OR include once in Asr.cpp, never both.
#ifndef STREAMING_WHISPER_IMPLEMENTATION
#define STREAMING_WHISPER_IMPLEMENTATION
#endif
#ifndef STREAMING_WHISPER_IMPLEMENTATION_INCLUDED
#define STREAMING_WHISPER_IMPLEMENTATION_INCLUDED
#include "StreamingWhisper.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#if __has_include("runtime/include/whisper.h")
#include "runtime/include/whisper.h"
#else
#include <whisper.h> // May be supplied through /I by an external benchmark build.
#endif
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <system_error>
#include <thread>

namespace streaming {
namespace whisper_detail {
using Clock = std::chrono::steady_clock;
static double Milliseconds(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}
struct Module {
    HMODULE handle = nullptr;
    explicit Module(const std::filesystem::path& path) {
        if (!path.is_absolute()) throw std::invalid_argument("DLL path must be absolute");
        handle = LoadLibraryExW(path.c_str(), nullptr,
            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!handle) {
            const auto error = GetLastError();
            throw std::system_error(static_cast<int>(error), std::system_category(),
                "LoadLibraryExW: " + path.filename().string());
        }
    }
    ~Module() { if (handle) FreeLibrary(handle); }
    Module(const Module&) = delete;
    Module& operator=(const Module&) = delete;
    template<class T> T Resolve(const char* name) const {
        const auto address = GetProcAddress(handle, name);
        if (!address) throw std::runtime_error(std::string("Missing DLL export: ") + name);
        static_assert(sizeof(T) == sizeof(address));
        T result;
        std::memcpy(&result, &address, sizeof(result));
        return result;
    }
};
static void QuietLog(ggml_log_level, const char*, void*) noexcept {}
// Wide-path reader supports Unicode roots without ANSI filename conversion.
struct ModelFile {
    FILE* file = nullptr;
    explicit ModelFile(const std::filesystem::path& path) {
        if (_wfopen_s(&file, path.c_str(), L"rb") != 0 || !file)
            throw std::runtime_error("Cannot open runtime/models/ggml-base.bin");
    }
    ~ModelFile() { if (file) std::fclose(file); }
    ModelFile(const ModelFile&) = delete;
    ModelFile& operator=(const ModelFile&) = delete;
    static size_t Read(void* user, void* out, size_t size) noexcept {
        return std::fread(out, 1, size, static_cast<ModelFile*>(user)->file);
    }
    static bool Eof(void* user) noexcept {
        auto* f = static_cast<ModelFile*>(user)->file;
        return std::feof(f) != 0 || std::ferror(f) != 0;
    }
    // C++ owner closes even on failure before loader.close is called.
    static void Close(void*) noexcept {}
};
struct Cancellation {
    const std::atomic_bool* flag;
    std::atomic_bool observed{false};
    bool Check() noexcept {
        if (flag && flag->load(std::memory_order_relaxed))
            observed.store(true, std::memory_order_relaxed);
        return observed.load(std::memory_order_relaxed);
    }
    static bool Abort(void* user) noexcept {
        return static_cast<Cancellation*>(user)->Check();
    }
    static bool Encoder(whisper_context*, whisper_state*, void* user) noexcept {
        return !static_cast<Cancellation*>(user)->Check();
    }
};
} // namespace whisper_detail

struct Recognizer::Impl {
    const std::filesystem::path root;
    // Reverse destruction order unloads dependents before dependencies.
    whisper_detail::Module base, cpu, ggml, whisper;
#define SW_API(name) decltype(&::name) name = whisper.Resolve<decltype(&::name)>(#name)
    SW_API(whisper_version);
    SW_API(whisper_context_default_params);
    SW_API(whisper_init_with_params);
    SW_API(whisper_free);
    SW_API(whisper_full_default_params);
    SW_API(whisper_full);
    SW_API(whisper_full_n_segments);
    SW_API(whisper_full_n_tokens);
    SW_API(whisper_full_get_token_data);
    SW_API(whisper_full_get_token_text);
    SW_API(whisper_full_get_segment_t0);
    SW_API(whisper_full_get_segment_t1);
    SW_API(whisper_token_eot);
    SW_API(whisper_log_set);
#undef SW_API
    decltype(&::ggml_log_set) ggmlLog = base.Resolve<decltype(&::ggml_log_set)>("ggml_log_set");
    struct ContextDeleter {
        decltype(&::whisper_free) free;
        void operator()(whisper_context* p) const noexcept { if (p) free(p); }
    };
    std::unique_ptr<whisper_context, ContextDeleter> context{nullptr, {whisper_free}};
    std::mutex mutex;
    double modelLoadMs = 0;
    explicit Impl(const std::filesystem::path& inputRoot)
        : root(std::filesystem::canonical(inputRoot)),
          base(root / L"runtime/whisper/ggml-base.dll"),
          cpu(root / L"runtime/whisper/ggml-cpu.dll"),
          ggml(root / L"runtime/whisper/ggml.dll"),
          whisper(root / L"runtime/whisper/whisper.dll") {
        const char* version = whisper_version();
        if (!version || std::strcmp(version, "1.8.3") != 0)
            throw std::runtime_error("Whisper DLL version must be exactly 1.8.3");
        // Global hooks remain installed; wrapper host must outlive DLL users.
        static std::mutex logging;
        {
            std::lock_guard lock(logging);
            whisper_log_set(&whisper_detail::QuietLog, nullptr);
            ggmlLog(&whisper_detail::QuietLog, nullptr);
        }
        const auto start = whisper_detail::Clock::now();
        whisper_detail::ModelFile file(root / L"runtime/models/ggml-base.bin");
        whisper_model_loader loader{&file, &whisper_detail::ModelFile::Read,
            &whisper_detail::ModelFile::Eof, &whisper_detail::ModelFile::Close};
        auto params = whisper_context_default_params();
        params.use_gpu = false;
        params.flash_attn = false;
        context.reset(whisper_init_with_params(&loader, params));
        if (!context || std::ferror(file.file))
            throw std::runtime_error("Whisper failed to load ggml-base.bin");
        modelLoadMs = whisper_detail::Milliseconds(start);
    }
};

Recognizer::Recognizer(const std::filesystem::path& root)
    : impl_(std::make_unique<Impl>(root)) {}
Recognizer::~Recognizer() = default;
double Recognizer::ModelLoadMs() const { return impl_->modelLoadMs; }

Result Recognizer::Infer(std::span<const float> pcm, const std::atomic_bool* abort) {
    if (pcm.size() > 96000 || (!pcm.empty() && !pcm.data()))
        throw std::invalid_argument("PCM must contain at most 96000 mono 16 kHz samples");
    for (float sample : pcm) {
        if (!std::isfinite(sample) || sample < -1.0f || sample > 1.0f)
            throw std::invalid_argument("PCM must be finite normalized floats in [-1, 1]");
    }
    whisper_detail::Cancellation cancellation{abort};
    Result result;
    if (cancellation.Check()) { result.aborted = true; return result; }
    if (pcm.empty()) return result;
    auto& api = *impl_;
    std::unique_lock lock(api.mutex, std::defer_lock);
    // Queued calls can be cancelled while another inference is running.
    while (!lock.try_lock()) {
        if (cancellation.Check()) { result.aborted = true; return result; }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (cancellation.Check()) { result.aborted = true; return result; }
    const auto start = whisper_detail::Clock::now();
    auto finish = [&]() {
        result.aborted = cancellation.Check();
        if (result.aborted) result.tokens.clear();
        result.inferenceMs = whisper_detail::Milliseconds(start);
        return std::move(result);
    };
    auto params = api.whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    params.n_threads = static_cast<int>((std::max)(1u,
        (std::min)(8u, std::thread::hardware_concurrency())));
    params.no_context = true;
    params.n_max_text_ctx = 0;
    params.no_timestamps = false;
    params.token_timestamps = true;
    params.print_special = false;
    params.print_progress = false;
    params.print_realtime = false;
    params.print_timestamps = false;
    params.debug_mode = false;
    params.translate = false;
    params.language = "auto";
    // true would request language detection ONLY.
    params.detect_language = false;
    params.initial_prompt = nullptr;
    params.prompt_tokens = nullptr;
    params.prompt_n_tokens = 0;
    params.carry_initial_prompt = false;
    params.abort_callback = &whisper_detail::Cancellation::Abort;
    params.abort_callback_user_data = &cancellation;
    params.encoder_begin_callback = &whisper_detail::Cancellation::Encoder;
    params.encoder_begin_callback_user_data = &cancellation;
    const int status = api.whisper_full(api.context.get(), params, pcm.data(),
        static_cast<int>(pcm.size()));
    if (cancellation.Check()) return finish();
    if (status != 0)
        throw std::runtime_error("whisper_full failed: " + std::to_string(status));
    const int segments = api.whisper_full_n_segments(api.context.get());
    if (segments < 0) throw std::runtime_error("Invalid Whisper segment count");
    const auto eot = api.whisper_token_eot(api.context.get());
    const int64_t durationMs = static_cast<int64_t>((pcm.size() + 15) / 16);
    auto toMs = [durationMs](int64_t ticks) -> int64_t {
        if (ticks <= 0) return 0;
        if (ticks > durationMs / 10) return durationMs;
        return ticks * 10;
    };
    for (int s = 0; s < segments; ++s) {
        const int count = api.whisper_full_n_tokens(api.context.get(), s);
        if (count < 0) throw std::runtime_error("Invalid Whisper token count");
        for (int t = 0; t < count; ++t) {
            if (cancellation.Check()) return finish();
            const auto data = api.whisper_full_get_token_data(api.context.get(), s, t);
            if (data.id < 0 || data.id >= eot) continue;
            const char* text = api.whisper_full_get_token_text(api.context.get(), s, t);
            if (!text) throw std::runtime_error("Whisper returned null token text");
            if (!*text) continue;
            const auto begin = toMs(data.t0 >= 0 ? data.t0 :
                api.whisper_full_get_segment_t0(api.context.get(), s));
            const auto end = toMs(data.t1 >= 0 ? data.t1 :
                api.whisper_full_get_segment_t1(api.context.get(), s));
            result.tokens.push_back({static_cast<int>(data.id), text, begin,
                (std::max)(begin, end)});
        }
    }
    return finish();
}
} // namespace streaming
#endif // STREAMING_WHISPER_IMPLEMENTATION_INCLUDED
