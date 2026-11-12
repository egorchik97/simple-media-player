#include "Asr.h"
#include <ks.h>
#include <ksmedia.h>
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace asr_detail;
template<class F> void Throws(F f) { bool caught = false; try { f(); } catch (const std::exception&) { caught = true; } assert(caught); }
int main() {
    WAVEFORMATEX w{}; w.wFormatTag = WAVE_FORMAT_PCM; w.nChannels = 2;
    w.nSamplesPerSec = 48000; w.wBitsPerSample = 16; w.nBlockAlign = 4;
    auto f = ParseFormat(w);
    int16_t samples[] = {-32768, 32767, 16384, 16384};
    auto mono = DecodeMono(f, reinterpret_cast<BYTE*>(samples), 2, false);
    assert(std::abs(mono[0]) < 0.0001 && mono[1] == 0.5f);
    assert(DecodeMono(f, nullptr, 10, true) == std::vector<float>(10));
    Throws([&] { DecodeMono(f, nullptr, 1, false); });
    w.wBitsPerSample = 24; Throws([&] { ParseFormat(w); });
    WAVEFORMATEXTENSIBLE e{}; e.Format = w; e.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    e.Format.cbSize = 22; e.Format.wBitsPerSample = 32; e.Format.nBlockAlign = 8;
    e.Samples.wValidBitsPerSample = 32; e.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
    f = ParseFormat(e.Format);
    float floats[] = {1, -1, std::numeric_limits<float>::quiet_NaN(), 1};
    mono = DecodeMono(f, reinterpret_cast<BYTE*>(floats), 2, false);
    assert(mono[0] == 0 && mono[1] == 0.5f);
    e.Format.cbSize = 0; Throws([&] { ParseFormat(e.Format); });
    for (unsigned rate : {8000u, 16000u, 44100u, 48000u, 96000u}) {
        std::vector<float> input(rate); for (size_t i = 0; i < input.size(); ++i) input[i] = float(std::sin(double(i)*0.01));
        Resampler whole(rate), split(rate); auto expected = whole.Push(input); std::vector<int16_t> actual;
        for (size_t i = 0; i < input.size(); i += 137) {
            auto part = split.Push(std::span<const float>(input).subspan(i, (std::min)(size_t(137), input.size()-i)));
            actual.insert(actual.end(), part.begin(), part.end());
        }
        assert(actual == expected); assert(actual.size() >= 15999 && actual.size() <= 16000);
    }
    Throws([] { Resampler r(0); });
    assert(!HasEnergy(std::vector<int16_t>(ChunkSamples)));
    assert(HasEnergy(std::vector<int16_t>(ChunkSamples, 1000)));
    assert(!HasEnergy({}));
    std::deque<std::vector<int16_t>> q;
    PushBounded(q, {1}); PushBounded(q, {2}); PushBounded(q, {3});
    assert(q.size() == 2 && q.front()[0] == 2 && q.back()[0] == 3);
    Throws([&] { PushBounded(q, std::vector<int16_t>(ChunkSamples+1)); });
    assert(QuoteArgument(L"") == L"\"\"");
    assert(QuoteArgument(L"a b") == L"\"a b\"");
    assert(QuoteArgument(L"a\"b") == L"\"a\\\"b\"");
    assert(QuoteArgument(L"a\\") == L"\"a\\\\\"");
    AsrEngine engine(std::filesystem::temp_directory_path() / L"asr-unit-test-missing-runtime");
    assert(!engine.Snapshot().active); assert(engine.Snapshot().text.empty());
    engine.Start(); assert(!engine.Snapshot().active); assert(engine.Snapshot().status.starts_with(L"ASR error:"));
    engine.Stop(); engine.Stop(); assert(engine.Snapshot().status == L"Stopped");
    engine.Start(); engine.Stop(); assert(engine.Snapshot().text.empty());
    std::cout << "ASR unit tests passed\n";
}
