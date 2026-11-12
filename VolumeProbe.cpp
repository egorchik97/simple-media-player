#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.Control.h>
#include <mutex>
#include <atomic>
#include <iostream>
#include <cassert>
using Session = winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSession;
struct { Session session{nullptr}; std::wstring source; } g_state;
std::mutex g_mutex;
std::atomic<bool> g_stop{false};
#include "Volume.h"
int main()
{
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    assert(volume_detail::MatchesIdentity(L"Chrome", L"C:\\Chrome\\chrome.exe"));
    assert(volume_detail::MatchesIdentity(L"Microsoft Edge", L"C:\\Edge\\msedge.exe"));
    assert(volume_detail::MatchesIdentity(L"Yandex", L"C:\\Users\\X\\AppData\\Local\\Yandex\\YandexBrowser\\Application\\browser.exe"));
    assert(!volume_detail::MatchesIdentity(L"Yandex", L"C:\\Other\\browser.exe"));
    assert(!volume_detail::MatchesIdentity(L"ChromeFake", L"C:\\Chrome\\chrome.exe"));
    assert(!volume_detail::MatchesIdentity(L"Chrome", L"C:\\Chrome\\chrome.exe.bad"));
    assert(!volume_detail::MatchesIdentity(L"", L""));
    assert(volume_detail::MatchesIdentity(L"Package!App", L"C:\\App.exe", L"Package!App"));
    assert(!volume_detail::MatchesIdentity(L"Package!Other", L"C:\\App.exe", L"Package!App"));
    std::wcout << L"Identity tests passed\n";
    try {
        auto media = winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
        std::vector<std::wstring> sources;
        for (auto s : media.GetSessions()) {
            sources.emplace_back(s.SourceAppUserModelId());
            std::wcout << L"GSMTC source=" << sources.back() << L"\n";
        }
        g_state.session = media.GetCurrentSession();
        if (g_state.session) {
            g_state.source = g_state.session.SourceAppUserModelId();
            const bool ok = volume_detail::Poll(); // Read-only: no QueueVolume call.
            auto snapshot = VolumeSnapshot();
            std::wcout << L"Poll=" << ok << L" available=" << snapshot.available << L" level=" << snapshot.level << L" muted=" << snapshot.muted << L" choices=" << VolumeChoices().size() << L"\n";
        }
        winrt::com_ptr<IMMDeviceEnumerator> e;
        winrt::check_hresult(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER, __uuidof(IMMDeviceEnumerator), e.put_void()));
        winrt::com_ptr<IMMDeviceCollection> devices;
        winrt::check_hresult(e->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, devices.put()));
        UINT count = 0; devices->GetCount(&count);
        for (UINT i = 0; i < count; ++i) {
            winrt::com_ptr<IMMDevice> d; winrt::com_ptr<IAudioSessionManager2> m;
            winrt::com_ptr<IAudioSessionEnumerator> sessions;
            if (FAILED(devices->Item(i,d.put())) || FAILED(d->Activate(__uuidof(IAudioSessionManager2), CLSCTX_INPROC_SERVER, nullptr, m.put_void())) || FAILED(m->GetSessionEnumerator(sessions.put()))) continue;
            int n=0; sessions->GetCount(&n);
            std::wcout << L"endpoint=" << i << L" sessions=" << n << L"\n";
            for(int j=0;j<n;++j) {
                winrt::com_ptr<IAudioSessionControl> b;
                if(FAILED(sessions->GetSession(j,b.put()))) continue;
                auto c=b.try_as<IAudioSessionControl2>(); if(!c) continue;
                DWORD pid=0; auto hr=c->GetProcessId(&pid); AudioSessionState state{}; c->GetState(&state);
                HANDLE p=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
                wchar_t image[32768]{}; DWORD size=32768; UINT32 len=0;
                if(p) QueryFullProcessImageNameW(p,0,image,&size);
                LONG app=p ? GetApplicationUserModelId(p,&len,nullptr) : -1;
                std::vector<wchar_t> id(len ? len : 1);
                if(p && len) app=GetApplicationUserModelId(p,&len,id.data());
                if(p) CloseHandle(p);
                std::wcout << L"pid=" << pid << L" hr=" << hr << L" state=" << state << L" image=" << image << L" aumidError=" << app << L" aumid=" << id.data() << L"\n";
                for(auto& source:sources) std::wcout << L" match[" << source << L"]=" << volume_detail::Matches(pid,source) << L"\n";
            }
        }
    } catch(const winrt::hresult_error& e) { std::wcerr << e.message().c_str(); return 1; }
}