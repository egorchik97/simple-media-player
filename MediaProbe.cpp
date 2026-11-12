#include "MediaPopup.cpp"
#include <iostream>
int main(int argc, char**) {
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    bool pump = argc > 1;
    MediaDiagnostics::Log(pump ? "probe.MTA.pump" : "probe.MTA.no-pump");
    try {
        for (int n=0;n<3;++n) {
            auto manager=MediaDiagnostics::Wait(GlobalSystemMediaTransportControlsSessionManager::RequestAsync(),std::chrono::milliseconds(3000),g_stop,"probe.request");
            auto sessions=manager.GetSessions();
            MediaDiagnostics::Log("probe.enumeration",S_OK,sessions.Size());
            std::cout << "sessions=" << sessions.Size() << std::endl;
            for(auto session:sessions) {
                auto info=session.GetPlaybackInfo();
                MediaDiagnostics::Log(info.PlaybackStatus()==GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing ? "probe.session.playing" : "probe.session.not-playing",S_OK,sessions.Size());
                try {
                    auto props=MediaDiagnostics::Wait(session.TryGetMediaPropertiesAsync(),std::chrono::milliseconds(1500),g_stop,"probe.properties");
                    auto cover=LoadCover(props,g_stop);
                    MediaDiagnostics::Log(cover ? "probe.cover.present" : "probe.cover.absent");
                } catch(const winrt::hresult_error& e) { MediaDiagnostics::Log("probe.metadata.error",e.code()); }
            }
            for(int i=0;i<50;++i) {
                if(pump) { MSG msg; while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)) {TranslateMessage(&msg); DispatchMessageW(&msg);} }
                Sleep(20);
            }
        }
    } catch(const winrt::hresult_error& e) { MediaDiagnostics::Log("probe.error",e.code()); return 1; }
    return 0;
}
