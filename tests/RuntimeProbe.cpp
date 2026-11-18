#include "../src/audio/StreamingWhisper.h"
#include "../src/audio/AsrEngine.h"
#include "../src/audio/LyricsEngine.h"
#include "../src/media/MediaSession.h"
#include "../src/media/VolumeController.h"
#include <fstream>
#include <iostream>
#include <cstring>

int main(int argc, char** argv) {
    if (argc != 3) { std::cerr << "RuntimeProbe whisper|capture|lyrics|volume absolute-repository-root\n"; return 2; }
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    struct Apartment { ~Apartment() { winrt::uninit_apartment(); } } apartment;
    try {
        const std::filesystem::path root = std::filesystem::absolute(argv[2]);
        if (std::string(argv[1]) == "capture") {
            media::AsrEngine asr(root); asr.Start(); bool inferred=false;
            for(int i=0;i<20;++i) {
                std::this_thread::sleep_for(std::chrono::seconds(1)); auto s=asr.Snapshot();
                inferred |= s.inferenceMs>0;
                std::cout << "active=" << s.active << " status=" << winrt::to_string(s.status)
                    << " inference_ms=" << s.inferenceMs << " text=" << winrt::to_string(s.provisional.empty()?s.text:s.provisional) << std::endl;
            }
            asr.Stop(); return inferred?0:1;
        }
        if (std::string(argv[1]) == "whisper") {
            std::ifstream file(root / "runtime/samples/jfk.wav", std::ios::binary);
            char header[12]{}; file.read(header, 12);
            if (!file || std::memcmp(header,"RIFF",4) || std::memcmp(header+8,"WAVE",4)) throw std::runtime_error("Invalid WAV");
            bool format = false; std::vector<float> pcm;
            while (file && pcm.empty()) {
                char id[4]; uint32_t size = 0; file.read(id,4); file.read(reinterpret_cast<char*>(&size),4);
                if (!file || size > 32*1024*1024) break;
                std::vector<char> bytes(size); file.read(bytes.data(),size); if (!file) break;
                if (size & 1) file.get();
                if (!std::memcmp(id,"fmt ",4) && size >= 16) {
                    uint16_t tag, channels, bits; uint32_t rate;
                    std::memcpy(&tag,bytes.data(),2); std::memcpy(&channels,bytes.data()+2,2);
                    std::memcpy(&rate,bytes.data()+4,4); std::memcpy(&bits,bytes.data()+14,2);
                    format = tag==1 && channels==1 && rate==16000 && bits==16;
                }
                if (!std::memcmp(id,"data",4) && format) {
                    for (size_t i=0;i+1<bytes.size();i+=2) { int16_t v; std::memcpy(&v,bytes.data()+i,2); pcm.push_back(v/32768.0f); }
                }
            }
            if (pcm.empty()) throw std::runtime_error("Expected mono 16kHz PCM16 sample");
            streaming::Recognizer recognizer(root);
            double elapsed=0; std::string text;
            for(size_t offset=0;offset<pcm.size();offset+=96000) {
                auto result=recognizer.Infer(std::span<const float>(pcm).subspan(offset,(std::min)(size_t(96000),pcm.size()-offset)));
                if(result.aborted) return 1;
                elapsed+=result.inferenceMs;
                for(auto const& token:result.tokens) text+=token.text;
            }
            std::cout << "model_ms=" << recognizer.ModelLoadMs() << " inference_ms=" << elapsed << " text=" << text << '\n';
            return text.empty() ? 1 : 0;
        }
        if (std::string(argv[1]) == "lyrics") {
            media::LyricsEngine lyrics;
            lyrics.Request(L"Imagine",L"John Lennon",183);
            for(int i=0;i<20;++i) {
                std::this_thread::sleep_for(std::chrono::seconds(1)); const auto s=lyrics.Snapshot();
                std::cout << "status=" << winrt::to_string(s.status) << " lines=" << s.lines.size() << " text_chars=" << s.text.size() << std::endl;
                if (!s.lines.empty() || !s.text.empty()) return 0;
            }
            return 1;
        }
        if (std::string(argv[1]) == "volume") {
            media::MediaSession session(nullptr); media::VolumeController volume(nullptr,&session);
            session.Start(); volume.Start(); bool available=false;
            for(int i=0;i<6;++i) {
                std::this_thread::sleep_for(std::chrono::seconds(1)); const auto s=volume.Snapshot(); available |= s.available;
                std::cout << "source=" << winrt::to_string(session.Snapshot().source) << " available=" << s.available << " level=" << s.level << " choices=" << volume.Choices().size() << std::endl;
            }
            volume.Stop(); session.Stop(); return available ? 0 : 1;
        }
        return 2;
    } catch (std::exception const& e) { std::cerr << e.what() << '\n'; return 1; }
      catch (winrt::hresult_error const& e) { std::cerr << winrt::to_string(e.message()) << '\n'; return 1; }
}