#include "AsrEngine.h"
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <ks.h>
#include <ksmedia.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <mutex>
#include <thread>
#include <stdexcept>
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")
using Microsoft::WRL::ComPtr;
namespace {
void Check(HRESULT hr) { if (FAILED(hr)) throw std::runtime_error("Windows audio error " + std::to_string(static_cast<unsigned long>(hr))); }
struct Handle {
    HANDLE h = nullptr;
    explicit Handle(HANDLE v = nullptr) : h(v) {}
    ~Handle() { if (h && h != INVALID_HANDLE_VALUE) CloseHandle(h); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};
std::wstring Wide(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (!n) return L"Invalid UTF-8 output/error";
    std::wstring w(n, 0); MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n); return w;
}
std::filesystem::path ExeDirectory() {
    std::vector<wchar_t> b(32768);
    DWORD n = GetModuleFileNameW(nullptr, b.data(), static_cast<DWORD>(b.size()));
    if (!n || n >= b.size()) throw std::runtime_error("Cannot locate executable");
    return std::filesystem::path(std::wstring(b.data(), n)).parent_path();
}
}
namespace media { namespace asr_detail {
Format ParseFormat(const WAVEFORMATEX& w) {
    bool f = w.wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
    bool p = w.wFormatTag == WAVE_FORMAT_PCM;
    if (w.wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        if (w.cbSize < sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)) throw std::runtime_error("Short extensible format");
        auto& e = reinterpret_cast<const WAVEFORMATEXTENSIBLE&>(w);
        f = IsEqualGUID(e.SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
        p = IsEqualGUID(e.SubFormat, KSDATAFORMAT_SUBTYPE_PCM);
        if (e.Samples.wValidBitsPerSample != 0 && e.Samples.wValidBitsPerSample != w.wBitsPerSample) throw std::runtime_error("Unsupported valid bits");
    }
    if ((!f && !p) || (f && w.wBitsPerSample != 32) || (p && w.wBitsPerSample != 16) ||
        !w.nChannels || w.nChannels > 32 || w.nSamplesPerSec < 8000 || w.nSamplesPerSec > 384000 ||
        w.nBlockAlign != w.nChannels * (w.wBitsPerSample / 8)) throw std::runtime_error("Unsupported audio format (requires float32 or PCM16)");
    return {w.nSamplesPerSec, w.nChannels, unsigned(w.wBitsPerSample / 8), f};
}
std::vector<float> DecodeMono(const Format& f, const BYTE* data, size_t frames, bool silent) {
    if (!f.channels || (f.bytes != (f.floating ? 4u : 2u)) || (!data && frames && !silent)) throw std::runtime_error("Invalid PCM buffer");
    std::vector<float> out(frames, 0);
    if (silent) return out;
    for (size_t i = 0; i < frames; ++i) {
        double sum = 0;
        for (unsigned c = 0; c < f.channels; ++c) {
            const BYTE* s = data + (i * f.channels + c) * f.bytes;
            float x;
            if (f.floating) std::memcpy(&x, s, 4);
            else { int16_t v; std::memcpy(&v, s, 2); x = v / 32768.f; }
            sum += std::isfinite(x) ? std::clamp(x, -1.f, 1.f) : 0.f;
        }
        out[i] = static_cast<float>(sum / f.channels);
    }
    return out;
}
Resampler::Resampler(unsigned rate) : rate_(rate) { if (rate < 8000 || rate > 384000) throw std::runtime_error("Invalid sample rate"); }
std::vector<int16_t> Resampler::Push(std::span<const float> mono) {
    std::vector<int16_t> out;
    for (float x : mono) {
        if (!std::isfinite(x)) x = 0;
        x = std::clamp(x, -1.f, 1.f);
        while (outputCount_ * rate_ <= inputCount_ * 16000) {
            double position = static_cast<double>(outputCount_ * rate_) / 16000.0;
            double t = inputCount_ ? position - static_cast<double>(inputCount_ - 1) : 1;
            double v = previous_ + (x - previous_) * t;
            out.push_back(static_cast<int16_t>(std::clamp(std::lround(v * 32768), -32768L, 32767L)));
            ++outputCount_;
        }
        previous_ = x; ++inputCount_;
    }
    return out;
}
bool HasEnergy(std::span<const int16_t> s, double threshold) {
    if (s.empty()) return false;
    double sum = 0; for (auto v : s) { double x = v / 32768.0; sum += x*x; }
    return std::sqrt(sum / s.size()) >= threshold;
}
void PushBounded(std::deque<std::vector<int16_t>>& q, std::vector<int16_t> c) {
    if (c.size() > ChunkSamples) throw std::runtime_error("Chunk exceeds 4 seconds");
    while (q.size() >= 2) q.pop_front(); q.push_back(std::move(c));
}
std::wstring QuoteArgument(const std::wstring& a) {
    std::wstring r = L"\""; size_t slashes = 0;
    for (wchar_t c : a) {
        if (c == L'\\') { ++slashes; continue; }
        r.append(slashes * (c == L'\"' ? 2 : 1), L'\\'); slashes = 0;
        if (c == L'\"') r += L'\\'; r += c;
    }
    r.append(slashes * 2, L'\\'); return r + L'\"';
}
}

} // namespace media
#include "StreamingWhisper.h"
#include <condition_variable>

namespace media {
struct AsrEngine::Impl {
    using Clock = std::chrono::steady_clock;
    std::filesystem::path root;
    mutable std::mutex mutex;
    std::condition_variable wake;
    AsrSnapshot state;
    bool exiting = false, requested = false;
    uint64_t generation = 0, epoch = 0, total = 0;
    std::atomic_bool abort{false};
    Handle stop{CreateEventW(nullptr, TRUE, TRUE, nullptr)};
    std::thread manager;
    std::deque<float> ring;
    Clock::time_point latest{};
    explicit Impl(std::filesystem::path p) : root(std::filesystem::absolute(p.empty() ? ExeDirectory() : p)) {
        if (!stop.h) throw std::runtime_error("Cannot create stop event");
        manager = std::thread([this] { Run(); });
    }
    ~Impl() {
        { std::lock_guard lock(mutex); exiting = true; requested = false; abort.store(true); SetEvent(stop.h); }
        wake.notify_all(); if (manager.joinable()) manager.join();
    }
    bool Stopping() { return WaitForSingleObject(stop.h, 0) == WAIT_OBJECT_0; }
    void Fail(uint64_t g, const std::exception& e) {
        std::lock_guard lock(mutex);
        if (g != generation) return;
        requested = false; state.active = false; state.status = L"ASR error: " + Wide(e.what());
        abort.store(true); SetEvent(stop.h);
    }    void Capture(uint64_t g) {
        HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        struct ComEnd { HRESULT hr; ~ComEnd() { if (SUCCEEDED(hr)) CoUninitialize(); } } end{com};
        try {
            Check(com);
            ComPtr<IMMDeviceEnumerator> enumerator; ComPtr<IMMDevice> device;
            ComPtr<IAudioClient> client; ComPtr<IAudioCaptureClient> source;
            Check(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator)));
            Check(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device));
            Check(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(client.GetAddressOf())));
            WAVEFORMATEX* raw = nullptr; Check(client->GetMixFormat(&raw));
            std::unique_ptr<WAVEFORMATEX, decltype(&CoTaskMemFree)> format(raw, CoTaskMemFree);
            auto f = asr_detail::ParseFormat(*raw);
            Check(client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, 1000000, 0, raw, nullptr));
            Check(client->GetService(IID_PPV_ARGS(&source)));
            asr_detail::Resampler resampler(f.rate);

            if (Stopping()) return;
            Check(client->Start());
            struct AudioEnd { IAudioClient* c; ~AudioEnd() { c->Stop(); } } audioEnd{client.Get()};
            { std::lock_guard lock(mutex); if (generation == g) state.status = L"Listening to system audio"; }
            while (!Stopping()) {
                UINT32 available = 0; Check(source->GetNextPacketSize(&available));
                while (available && !Stopping()) {
                    BYTE* data = nullptr; UINT32 frames = 0; DWORD flags = 0;
                    Check(source->GetBuffer(&data, &frames, &flags, nullptr, nullptr));
                    std::vector<float> mono;
                    try { mono = asr_detail::DecodeMono(f, data, frames, (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0); }
                    catch (...) { source->ReleaseBuffer(frames); throw; }
                    Check(source->ReleaseBuffer(frames));
                    if (flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) {
                        resampler = asr_detail::Resampler(f.rate);
                        std::lock_guard lock(mutex);
                        if (generation != g || !requested) return;
                        ring.clear(); total = 0; ++epoch;
                        abort.store(true); state.text.clear(); state.provisional.clear();
                    }
                    auto pcm = resampler.Push(mono);
                    {
                        std::lock_guard lock(mutex);
                        if (generation != g || !requested) return;
                        for (auto v : pcm) { ring.push_back(v / 32768.f); if (ring.size() > asr_detail::ChunkSamples) ring.pop_front(); }
                        total += pcm.size(); latest = Clock::now();
                    }                    Check(source->GetNextPacketSize(&available));
                }
                WaitForSingleObject(stop.h, 10);
            }
        } catch (const std::exception& e) { Fail(g, e); }
        catch (...) { Fail(g, std::runtime_error("Unknown capture failure")); }
    }

    void Run() {
        std::unique_ptr<streaming::Recognizer> recognizer;
        for (;;) {
            uint64_t g;
            {
                std::unique_lock lock(mutex);
                wake.wait(lock, [&] { return exiting || requested; });
                if (exiting) return;
                g = generation; ResetEvent(stop.h); abort.store(false);
                ring.clear(); total = 0; ++epoch;
            }
            std::thread capture;
            try {
                if (!recognizer) recognizer = std::make_unique<streaming::Recognizer>(root);
                { std::lock_guard lock(mutex); if (g != generation || !requested) continue;
                  state.modelLoadMs = recognizer->ModelLoadMs(); }
                capture = std::thread([this, g] { Capture(g); });
                uint64_t seenEpoch = 0, processed = 0;
                // Publish provisional text twice per second. If CPU inference
                // is slower, adaptive cadence skips stale work instead of
                // building a backlog.
                double cadenceMs = 500;
                std::vector<streaming::Token> previous;
                int64_t committedEnd = -1;
                std::string stable;
                while (!Stopping()) {
                    std::vector<float> pcm;
                    uint64_t e = 0, end = 0;
                    Clock::time_point captured;
                    {
                        std::lock_guard lock(mutex);
                        if (g != generation || !requested) break;
                        if (seenEpoch != epoch) {
                            seenEpoch = epoch; processed = 0; previous.clear(); stable.clear(); committedEnd = -1;
                        }
                        if (total >= 16000 && total - processed >= static_cast<uint64_t>(cadenceMs * 16)) {
                            pcm.assign(ring.begin(), ring.end()); end = total; e = epoch; captured = latest;
                            processed = end; abort.store(false);
                        }
                    }
                    if (pcm.empty()) { WaitForSingleObject(stop.h, 20); continue; }
                    streaming::Result result;
                    if (std::any_of(pcm.begin(), pcm.end(), [](float v) { return v != 0.f; }))
                        result = recognizer->Infer(pcm, &abort);
                    cadenceMs = std::clamp(result.inferenceMs * 1.10, 500.0, 4000.0);
                    std::lock_guard lock(mutex);
                    if (g != generation || e != epoch || !requested || result.aborted) continue;
                    int64_t startMs = static_cast<int64_t>((end - pcm.size()) / 16);
                    std::vector<streaming::Token> current;
                    for (auto t : result.tokens) {
                        t.beginMs += startMs; t.endMs += startMs;
                        if (t.beginMs >= committedEnd && t.endMs > committedEnd) current.push_back(std::move(t));
                    }
                    // Confirm only a contiguous prefix repeated in overlapping windows.
                    // Timestamp proximity prevents matching a repeated phrase elsewhere.
                    size_t matched = 0;
                    for (size_t p = 0; p < previous.size() && !current.empty(); ++p) {
                        if (previous[p].id != current[0].id || std::abs(previous[p].beginMs-current[0].beginMs) > 350) continue;
                        while (matched < current.size() && p+matched < previous.size() &&
                            current[matched].id == previous[p+matched].id &&
                            std::abs(current[matched].beginMs-previous[p+matched].beginMs) <= 350 &&
                            current[matched].endMs < static_cast<int64_t>(end/16)-750) ++matched;
                        break;
                    }
                    for (size_t i = 0; i < matched; ++i) { stable += current[i].text; committedEnd = current[i].endMs; }
                    current.erase(current.begin(), current.begin()+matched);
                    previous = current;
                    std::string provisional;
                    for (const auto& t : current) provisional += t.text;
                    if (stable.size() > 32000) { auto cut=stable.find(' ', stable.size()-24000); if(cut!=std::string::npos) stable.erase(0,cut+1); }
                    state.text = stable.empty() ? L"" : Wide(stable);
                    state.provisional = provisional.empty() ? L"" : Wide(provisional);
                    state.inferenceMs = result.inferenceMs;
                    state.audioAgeMs = std::chrono::duration<double,std::milli>(Clock::now()-captured).count();
                    state.status = L"В эфире · локально · обновление текста";
                }
            } catch (const std::exception& e) { Fail(g,e); }
              catch (...) { Fail(g,std::runtime_error("Unknown ASR failure")); }
            SetEvent(stop.h); abort.store(true);
            if (capture.joinable()) capture.join();
        }
    }
};
AsrEngine::AsrEngine(std::filesystem::path root) : impl_(std::make_unique<Impl>(std::move(root))) {}
AsrEngine::~AsrEngine() = default;
void AsrEngine::Start() {
    auto& p=*impl_; std::lock_guard lock(p.mutex);
    if (p.requested) return;
    std::error_code ec;
    if (!std::filesystem::is_regular_file(p.root / L"runtime/whisper/whisper.dll", ec) ||
        !std::filesystem::is_regular_file(p.root / L"runtime/models/ggml-base.bin", ec)) {
        p.state = {}; p.state.status = L"ASR error: Missing whisper DLL or model"; return;
    }
    ++p.generation; p.requested=true; p.state={}; p.state.active=true; p.state.status=L"Starting / loading model";
    p.abort.store(true); SetEvent(p.stop.h); p.wake.notify_one();
}
void AsrEngine::Stop() {
    auto& p=*impl_; std::lock_guard lock(p.mutex);
    ++p.generation; p.requested=false; p.abort.store(true); SetEvent(p.stop.h);
    p.state={}; p.ring.clear(); p.total=0; ++p.epoch;
}
AsrSnapshot AsrEngine::Snapshot() const { std::lock_guard lock(impl_->mutex); return impl_->state; }
} // namespace media
