#include "../src/media/MediaSession.h"
#include <iostream>
#include <thread>
int main() {
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    {
        media::MediaSession media(nullptr);
        media.Start();
        for (int i = 0; i < 12; ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            auto s = media.Snapshot();
            std::cout << "session=" << bool(s.session) << " playing=" << s.playing
                << " source=" << winrt::to_string(s.source) << " title=" << winrt::to_string(s.title)
                << " artist=" << winrt::to_string(s.artist) << " cover=" << bool(s.cover)
                << " error=" << winrt::to_string(s.error) << std::endl;
        }
        media.Stop();
        media.Start();
        std::this_thread::sleep_for(std::chrono::seconds(2));
        media.Stop();
    }
    winrt::uninit_apartment();
}