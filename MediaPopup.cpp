#define UNICODE
#define _UNICODE
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#pragma comment(lib, "comdlg32.lib")
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <memory>
#include <algorithm>
#include <atomic>
#include <mutex>
#include <thread>
#include <string>
#include <chrono>
#include "MediaDiagnostics.h"

using namespace winrt::Windows::Media::Control;
using Session = GlobalSystemMediaTransportControlsSession;
struct Cover { winrt::com_array<uint8_t> pixels; uint32_t width = 0, height = 0; };
static std::shared_ptr<Cover> LoadCover(const GlobalSystemMediaTransportControlsSessionMediaProperties& properties,
                                      const std::atomic<bool>& stop) {
    using namespace winrt::Windows::Graphics::Imaging;
    auto thumbnail = properties.Thumbnail();
    if (!thumbnail) return {};
    auto stream = MediaDiagnostics::Wait(thumbnail.OpenReadAsync(), std::chrono::milliseconds(1500), stop, "cover.open");
    auto decoder = MediaDiagnostics::Wait(BitmapDecoder::CreateAsync(stream), std::chrono::milliseconds(1500), stop, "cover.decode");
    if (!decoder.PixelWidth() || !decoder.PixelHeight()) return {};
    auto cover = std::make_shared<Cover>();
    double scale = 160.0 / std::max(decoder.PixelWidth(), decoder.PixelHeight());
    cover->width = std::max(1u, static_cast<uint32_t>(decoder.PixelWidth() * scale));
    cover->height = std::max(1u, static_cast<uint32_t>(decoder.PixelHeight() * scale));
    BitmapTransform transform;
    transform.ScaledWidth(cover->width); transform.ScaledHeight(cover->height);
    cover->pixels = MediaDiagnostics::Wait(decoder.GetPixelDataAsync(BitmapPixelFormat::Bgra8, BitmapAlphaMode::Ignore,
        transform, ExifOrientationMode::IgnoreExifOrientation, ColorManagementMode::DoNotColorManage),
        std::chrono::milliseconds(1500), stop, "cover.pixels").DetachPixelData();
    return cover;
}
struct State {
    std::wstring title = L"Откройте музыку в плеере", artist, source, error;
    bool playing = false, toggle = false, previous = false, next = false, seek = false;
    int64_t start = 0, end = 0, position = 0, minimum = 0, maximum = 0;
    Session session{nullptr};
    std::shared_ptr<Cover> cover;
};
struct Command { int action = 0; int64_t position = 0; Session session{nullptr}; };
static std::mutex g_mutex;
static State g_state;
static Command g_command;
static std::atomic<bool> g_stop{false}, g_busy{false};
#include "Volume.h"
#include "Asr.h"
static AsrEngine g_asr;
static bool g_volumeDrag = false;
static float g_volumePreview = 0;
static bool g_drag = false;
static bool g_settings = false;
static bool g_settingsDrag = false, g_settingsDragMoved = false;
static POINT g_settingsDragStart{};
static int g_dragX = 0;
static State g_dragState;
#include "PopupConfig.h"
static PopupConfig g_config;
#include "Appearance.h"
#include "Lyrics.h"
static LyricsEngine g_lyrics;
static std::wstring g_asrTrack;
static bool g_asrRunning = false;
static int g_settingsPage = 0;
static std::wstring g_configPath;
static bool g_expanded=false, g_moving=false;
static POINT g_anchor{};
static HoverState g_hover;
static int Width = 280, Height = 70;
static constexpr int Left = 16, Right = 324;
static RECT CloseButton() { return {Width-28,4,Width-4,28}; }
static RECT ExpandButton() { return {Width-54,38,Width-30,66}; }
static bool CompactButtonVisible(int index) {
    return index==0 ? g_config.compactPrevious : index==1 ? g_config.pause : index==2 ? g_config.compactNext : false;
}
// Semantic indices: previous=0, pause=1, next=2. Hidden buttons have an empty rect.
static RECT CompactButton(int index) {
    if(!CompactButtonVisible(index)) return {};
    int right=Width-56;
    for(int i=2;i>index;--i) if(CompactButtonVisible(i)) right-=28;
    return {right-26,34,right,66};
}
static int CompactTextRight() {
    for(int i=0;i<3;++i) if(CompactButtonVisible(i)) return CompactButton(i).left-6;
    return ExpandButton().left-6;
}
static int SettingsParentPage() { return g_settingsPage==2 || g_settingsPage==3 ? 1 : 0; }
static RECT SettingRow(int i) { return {16,48+i*30,324,78+i*30}; }

static void Layout(HWND h) {
    Width = g_settings || g_expanded ? 340 : g_config.compactWidth;
    Height = g_settings ? 392 : g_expanded ? ((g_config.asr || g_config.sourceLyrics) ? 360 : 210) : (g_config.compactLyrics ? g_config.compactLyricsHeight : g_config.compactHeight);
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(MonitorFromPoint(g_anchor,MONITOR_DEFAULTTONEAREST),&mi);
    RECT r=PopupRect(g_anchor,Width,Height,mi.rcWork);
    SetWindowPos(h,nullptr,r.left,r.top,r.right-r.left,r.bottom-r.top,SWP_NOZORDER|SWP_NOACTIVATE);
    ApplyAppearance(h,Width,Height);
    InvalidateRect(h,nullptr,FALSE);
}
static void Persist(HWND h) {
    if (!SaveConfig(g_configPath,g_config))
        MessageBoxW(h,L"Не удалось сохранить настройки. Изменения действуют только до выхода.",L"Media Popup",MB_OK|MB_ICONERROR);
}
static void ChooseThemeColor(HWND h, int& color) {
    static COLORREF custom[16]{};
    CHOOSECOLORW dialog{}; dialog.lStructSize=sizeof(dialog); dialog.hwndOwner=h;
    dialog.rgbResult=static_cast<COLORREF>(color); dialog.lpCustColors=custom;
    dialog.Flags=CC_FULLOPEN|CC_RGBINIT;
    if(ChooseColorW(&dialog)) { color=static_cast<int>(dialog.rgbResult); g_config.theme=5; }
}
static std::wstring ColorHex(int color) {
    wchar_t text[8]{};
    swprintf_s(text,L"#%02X%02X%02X",unsigned(GetRValue(color)),unsigned(GetGValue(color)),unsigned(GetBValue(color)));
    return text;
}
static void HoverTick(HWND h) {
    POINT p; GetCursorPos(&p); RECT r; GetWindowRect(h,&r);
    bool inside=PtInRect(&r,p)!=FALSE;
    if (!inside) g_hover.armed=true;
    bool blocked=g_settings||g_moving||g_drag||g_volumeDrag||GetCapture()==h||g_config.pinned;
    if (!blocked && inside && !g_expanded && g_config.hover && g_hover.armed) {
        g_expanded=true; g_hover.armed=false; Layout(h);
        // Re-evaluate only on the next tick, using actual screen bounds.
        g_hover.outsideSince=0; return;
    }
    if (g_hover.Collapse(inside,blocked,GetTickCount64(),g_config.delay) && g_expanded) {
        g_expanded=false; g_hover.armed=false; Layout(h);
    }
}

static RECT SeekRegion() { return {Left-6,126,Right+7,143}; }
static RECT VolumeRegion() { return {120,162,Right+7,187}; }
static bool Hit(RECT r, POINT p) { return PtInRect(&r,p) != FALSE; }
static float VolumeLevel(int x) { return float(std::clamp(x,126,Right)-126) / (Right-126); }
static void CancelDrag(HWND h) {
    g_drag=false; g_volumeDrag=false; g_dragState={};
    if (GetCapture()==h) ReleaseCapture();
}
static State Snapshot() { std::lock_guard<std::mutex> lock(g_mutex); return g_state; }
static void LyricsTick() {
    auto s=Snapshot();
    const bool valid=s.session && !s.title.empty() && s.title!=L"Без названия" && s.title!=L"Откройте музыку в плеере";
    if(g_config.sourceLyrics && valid)
        g_lyrics.Request(s.title,s.artist,std::max(0.0,double(s.end-s.start)/10000000.0));
    else g_lyrics.Clear();
    std::wstring key=valid ? s.source+L"\n"+s.artist+L"\n"+s.title : L"";
    bool capture=g_config.asr && valid && s.playing;
    if(g_asrRunning && (!capture || key!=g_asrTrack)) { g_asr.Stop(); g_asrRunning=false; }
    if(capture && !g_asrRunning) { g_asr.Start(); g_asrRunning=true; }
    g_asrTrack=std::move(key);
}


// Обработка клика в настройках. Вызывается по отпускании кнопки, если окно не перетаскивалось.
static bool HandleSettingsClick(HWND h, POINT p) {
    if (g_settingsPage == 4) {
        bool* values[]={&g_config.compactPrevious,&g_config.pause,&g_config.compactNext};
        for(int i=0;i<3;++i) if(Hit(SettingRow(i),p)) {
            *values[i]=!*values[i]; Persist(h); InvalidateRect(h,nullptr,FALSE); return true;
        }
        int direction=p.x<170?-1:1;
        if(Hit(SettingRow(3),p)) { g_config.compactWidth=std::clamp(g_config.compactWidth+direction*20,200,900); Persist(h); Layout(h); return true; }
        if(Hit(SettingRow(4),p)) { g_config.compactHeight=std::clamp(g_config.compactHeight+direction*10,50,600); Persist(h); Layout(h); return true; }
        if(Hit(SettingRow(5),p)) { g_config.compactLyricsHeight=std::clamp(g_config.compactLyricsHeight+direction*10,80,600); Persist(h); Layout(h); return true; }
        if(Hit(SettingRow(6),p)) { g_settingsPage=0; InvalidateRect(h,nullptr,FALSE); return true; }
        return false;
    }
    if(g_settingsPage==2 || g_settingsPage==3) {
        for(int i=0;i<8;++i) if(Hit(SettingRow(i),p)) {
            PopupConfig defaults; int direction=p.x<170?-1:1;
            if(g_settingsPage==2) {
                int* colors[]={&g_config.customBackground,&g_config.customAccent,&g_config.customText,&g_config.customMuted};
                if(i<4) ChooseThemeColor(h,*colors[i]);
                if(i==4) g_config.theme=5;
                if(i==5) { g_config.customBackground=BackgroundColor(); g_config.customAccent=AccentColor(); g_config.customText=TextColor(); g_config.customMuted=MutedColor(); g_config.theme=5; }
                if(i==6) { g_config.customBackground=defaults.customBackground; g_config.customAccent=defaults.customAccent; g_config.customText=defaults.customText; g_config.customMuted=defaults.customMuted; }
            } else {
                if(i==0) g_config.glass=!g_config.glass;
                if(i==1) g_config.glassStrength=std::clamp(g_config.glassStrength+direction*5,0,100);
                if(i==2) g_config.glassTint=std::clamp(g_config.glassTint+direction*5,0,100);
                if(i==3) g_config.glassAnimated=!g_config.glassAnimated;
                if(i==4) g_config.glassSpeed=std::clamp(g_config.glassSpeed+direction*5,0,100);
                if(i==5) g_config.opacity=std::clamp(g_config.opacity+direction*5,45,100);
                if(i==6) { g_config.glass=defaults.glass; g_config.glassStrength=defaults.glassStrength; g_config.glassTint=defaults.glassTint; g_config.glassAnimated=defaults.glassAnimated; g_config.glassSpeed=defaults.glassSpeed; g_config.opacity=defaults.opacity; }
            }
            if(i==7) g_settingsPage=1;
            Persist(h); Layout(h); return true;
        }
        return false;
    }
    if(g_settingsPage==1) {
        for(int i=0;i<8;++i) if(Hit(SettingRow(i),p)) {
            int direction=p.x<170?-1:1;
            if(i==0) g_config.theme=(g_config.theme+direction+6)%6;
            if(i==1) g_settingsPage=2;
            if(i==2) g_settingsPage=3;
            if(i==3) g_config.glow=std::clamp(g_config.glow+direction*5,0,100);
            if(i==4) g_config.roundness=std::clamp(g_config.roundness+direction*2,0,28);
            if(i==5) { int mode=g_config.sourceLyrics?1:g_config.asr?2:0; mode=(mode+1)%3; g_config.sourceLyrics=mode==1; g_config.asr=mode==2; LyricsTick(); }
            if(i==6) g_config.compactLyrics=!g_config.compactLyrics;
            if(i==7) g_settingsPage=0;
            Persist(h); Layout(h); return true;
        }
        return false;
    }
    // Основная страница.
    const wchar_t* labels[]={L"Обложка в карточке",L"Название в карточке",L"Исполнитель в карточке",L"Кнопки в карточке  >",L"Разворачивать при наведении",L"Закрепить развёрнутым",L"Громкость источника",L"Локальное распознавание"};
    bool* values[]={&g_config.cover,&g_config.title,&g_config.artist,&g_config.pause,&g_config.hover,&g_config.pinned,&g_config.volume,&g_config.asr};
    for(int i=0;i<10;++i) if(Hit(SettingRow(i),p)) {
        if(i==3) { g_settingsPage=4; Persist(h); Layout(h); return true; }
        if(i==9) { g_settingsPage=1; Persist(h); Layout(h); return true; }
        if(i<8) { *values[i]=!*values[i]; if(i==7) { if(g_config.asr) g_config.sourceLyrics=false; LyricsTick(); } }
        else g_config.delay=std::clamp(g_config.delay+(p.x<170?-100:100),0,10000);
        if(g_config.pinned) g_expanded=true;
        Persist(h); InvalidateRect(h,nullptr,FALSE); return true;
    }
    return false;
}
static std::wstring CompactSingleLine(std::wstring text, bool firstLine=false) {
    if(firstLine) text=text.substr(0,text.find_first_of(L"\r\n"));
    std::wstring result;
    bool space=false;
    for(wchar_t c:text) {
        if(c==L'\r' || c==L'\n' || c==L'\t' || c==L' ') { space=!result.empty(); continue; }
        if(space) result+=L' ';
        result+=c; space=false;
    }
    return result;
}
static std::wstring CompactLyricsText(const State& s) {
    if(!g_config.sourceLyrics && !g_config.asr) return L"Выберите режим текста в настройках";
    if(!s.session) return L"Ожидание трека…";
    if(g_config.sourceLyrics) {
        auto lyrics=g_lyrics.Snapshot();
        auto text=lyrics.DisplayAt(double(s.position-s.start)/10000000.0);
        return CompactSingleLine(text.empty()?lyrics.status:text,true);
    }
    if(!s.playing) return L"Пауза — распознавание остановлено";
    auto a=g_asr.Snapshot();
    auto text=a.provisional.empty()?a.text:a.provisional;
    if(text.empty()) return CompactSingleLine(a.status);
    text=CompactSingleLine(text);
    if(text.size()>120) {
        size_t offset=text.size()-120;
        if(text[offset]>=0xDC00 && text[offset]<=0xDFFF) ++offset;
        text=L"…"+text.substr(offset);
    }
    return text;
}
static int64_t SeekPosition(int x, const State& s) {
    auto position = s.start + static_cast<int64_t>((s.end - s.start) *
        (std::clamp(x, Left, Right) - Left) / double(Right - Left));
    return std::clamp(position, s.minimum, s.maximum);
}
static void Queue(int action, const State& s, int64_t position = 0) {
    if (!s.session || g_busy.exchange(true)) return;
    std::lock_guard<std::mutex> lock(g_mutex);
    g_command = {action, position, s.session};
}
static void Worker() {
    try {
        using Clock = std::chrono::steady_clock;
        using Ms = std::chrono::milliseconds;
        struct Apartment {
            Apartment() { winrt::init_apartment(winrt::apartment_type::multi_threaded); }
            ~Apartment() { winrt::uninit_apartment(); }
        } apartment; // Explicit MTA: neither thread needs a message pump.
        std::atomic<bool> metadataStop{false};
        Session requested{nullptr};
        uint64_t generation = 0;
        bool pending = false;
        std::thread metadata;
        struct Join {
            std::atomic<bool>& stop;
            std::thread& thread;
            ~Join() { stop.store(true); if (thread.joinable()) thread.join(); }
        } join{metadataStop, metadata};
        metadata = std::thread([&] {
            try {
                Apartment metadataApartment;
                while (!metadataStop.load() && !g_stop.load()) {
                    Session session{nullptr};
                    uint64_t version = 0;
                    {
                        std::lock_guard<std::mutex> lock(g_mutex);
                        if (pending) {
                            session = requested; version = generation; pending = false;
                        }
                    }
                    if (!session) { std::this_thread::sleep_for(Ms(25)); continue; }
                    const char* stage = "metadata.properties";
                    try {
                        auto properties = MediaDiagnostics::Wait(session.TryGetMediaPropertiesAsync(),
                            Ms(1500), metadataStop, stage);
                        if (metadataStop.load() || g_stop.load()) break;
                        std::wstring title = properties.Title().c_str();
                        if (title.empty()) title = L"Без названия";
                        std::wstring artist = properties.Artist().c_str();
                        stage = "metadata.source";
                        std::wstring source = session.SourceAppUserModelId().c_str();
                        {
                            std::lock_guard<std::mutex> lock(g_mutex);
                            if (version != generation || g_state.session != session) continue;
                            if (g_state.title != title || g_state.artist != artist || g_state.source != source)
                                g_state.cover.reset();
                            g_state.title = std::move(title);
                            g_state.artist = std::move(artist);
                            g_state.source = std::move(source);
                        } // Publish text before starting any cover work.
                        stage = "metadata.cover";
                        auto cover = LoadCover(properties, metadataStop);
                        if (metadataStop.load() || g_stop.load()) break;
                        {
                            std::lock_guard<std::mutex> lock(g_mutex);
                            if (version == generation && g_state.session == session)
                                g_state.cover = std::move(cover);
                        }
                    } catch (const winrt::hresult_error& e) {
                        MediaDiagnostics::Log(stage, e.code());
                    } catch (...) { MediaDiagnostics::Log(stage, E_FAIL); }
                }
            } catch (const winrt::hresult_error& e) {
                MediaDiagnostics::Log("metadata.apartment", e.code());
            } catch (...) { MediaDiagnostics::Log("metadata.thread", E_FAIL); }
        });
        GlobalSystemMediaTransportControlsSessionManager manager{nullptr};
        auto pause = [&](Ms delay) {
            auto until = Clock::now() + delay;
            while (!g_stop.load() && Clock::now() < until) std::this_thread::sleep_for(Ms(25));
        };
        std::wstring error;
        auto errorUntil = Clock::now();
        auto metadataDue = Clock::now();
        int retryMs = 250;
        while (!g_stop) {
            try {
                Command command;
                { std::lock_guard<std::mutex> lock(g_mutex); command = g_command; g_command = {}; }
                if (command.action) {
                    bool ok = false;
                    try {
                        if (!command.session) winrt::throw_hresult(E_POINTER);
                        // Each command is consumed once, including timeout/unknown completion.
                        if (command.action == 1) ok = MediaDiagnostics::Wait(command.session.TrySkipPreviousAsync(), Ms(1500), g_stop, "command.previous");
                        if (command.action == 2) ok = MediaDiagnostics::Wait(command.session.TryTogglePlayPauseAsync(), Ms(1500), g_stop, "command.toggle");
                        if (command.action == 3) ok = MediaDiagnostics::Wait(command.session.TrySkipNextAsync(), Ms(1500), g_stop, "command.next");
                        if (command.action == 4) ok = MediaDiagnostics::Wait(command.session.TryChangePlaybackPositionAsync(command.position), Ms(1500), g_stop, "command.seek");
                        error = ok ? L"" : L"Плеер отклонил команду";
                    } catch (const winrt::hresult_error& e) {
                        MediaDiagnostics::Log("command", e.code());
                        error = L"Не удалось отправить команду плееру";
                    } catch (...) {
                        MediaDiagnostics::Log("command", E_FAIL);
                        error = L"Не удалось отправить команду плееру";
                    }
                    errorUntil = std::chrono::steady_clock::now() + std::chrono::seconds(4);
                    g_busy = false;
                }
                if (!manager) {
                    try {
                        manager = MediaDiagnostics::Wait(
                            GlobalSystemMediaTransportControlsSessionManager::RequestAsync(),
                            Ms(1500), g_stop, "manager.request");
                        retryMs = 250;
                    } catch (const winrt::hresult_error& e) {
                        MediaDiagnostics::Log("manager.request", e.code());
                        {
                            std::lock_guard<std::mutex> lock(g_mutex);
                            g_state.error = L"Ошибка Windows Media: " + std::wstring(e.message());
                        }
                        pause(Ms(retryMs)); retryMs = std::min(retryMs * 2, 2000);
                        continue;
                    }
                }
                State s;
                auto current = manager.GetCurrentSession();
                auto sessions = manager.GetSessions();
                MediaDiagnostics::Log("sessions.enumerate", S_OK, static_cast<int>(sessions.Size()));
                auto isPlaying = [](const Session& session) {
                    if (!session) return false;
                    try {
                        return session.GetPlaybackInfo().PlaybackStatus() ==
                            GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing;
                    } catch (const winrt::hresult_error& e) {
                        MediaDiagnostics::Log("sessions.playback", e.code());
                    } catch (...) { MediaDiagnostics::Log("sessions.playback", E_FAIL); }
                    return false;
                };
                if (isPlaying(current)) s.session = current;
                Session first{nullptr};
                for (uint32_t i = 0; i < sessions.Size(); ++i) {
                    try {
                        auto candidate = sessions.GetAt(i);
                        if (!first) first = candidate;
                        if (!s.session && candidate != current && isPlaying(candidate)) s.session = candidate;
                    } catch (const winrt::hresult_error& e) {
                        MediaDiagnostics::Log("sessions.item", e.code());
                    } catch (...) { MediaDiagnostics::Log("sessions.item", E_FAIL); }
                }
                if (!s.session) s.session = current ? current : first;
                if (s.session) {
                    try {
                    auto info = s.session.GetPlaybackInfo();
                    auto controls = info.Controls();
                    s.playing = info.PlaybackStatus() == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing;
                    s.toggle = controls.IsPlayPauseToggleEnabled();
                    s.previous = controls.IsPreviousEnabled();
                    s.next = controls.IsNextEnabled();
                    auto timeline = s.session.GetTimelineProperties();
                    s.start = timeline.StartTime().count(); s.end = timeline.EndTime().count();
                    s.minimum = std::max(s.start, timeline.MinSeekTime().count());
                    s.maximum = std::min(s.end, timeline.MaxSeekTime().count());
                    s.position = timeline.Position().count();
                    if (s.playing) {
                        auto elapsed = winrt::clock::now() - timeline.LastUpdatedTime();
                        auto rate = info.PlaybackRate();
                        if (elapsed.count() > 0) s.position += static_cast<int64_t>(elapsed.count() * (rate ? rate.Value() : 1.0));
                    }
                    s.position = std::clamp(s.position, s.start, std::max(s.start, s.end));
                    s.seek = controls.IsPlaybackPositionEnabled() && s.end > s.start && s.maximum > s.minimum;
                    } catch (const winrt::hresult_error& e) {
                        MediaDiagnostics::Log("session.controls", e.code());
                        s.error = L"Медиасессия недоступна. Откройте плеер.";
                    } catch (...) {
                        MediaDiagnostics::Log("session.controls", E_FAIL);
                        s.error = L"Медиасессия недоступна. Откройте плеер.";
                    }
                }
                if (Clock::now() < errorUntil) s.error = error;
                {
                    std::lock_guard<std::mutex> lock(g_mutex);
                    if (s.session != requested) {
                        ++generation;
                        requested = s.session;
                        pending = false;
                        metadataDue = Clock::now();
                    }
                    if (s.session && s.session == g_state.session) {
                        s.title = g_state.title; s.artist = g_state.artist;
                        s.source = g_state.source; s.cover = g_state.cover;
                    }
                    g_state = std::move(s); // Controls are visible before metadata is scheduled.
                    if (requested && Clock::now() >= metadataDue) {
                        pending = true;
                        metadataDue = Clock::now() + std::chrono::seconds(1);
                    }
                }
            } catch (const winrt::hresult_error& e) {
                MediaDiagnostics::Log("worker.poll", e.code());
                manager = nullptr;
                std::lock_guard<std::mutex> lock(g_mutex);
                ++generation; requested = nullptr; pending = false;
                g_state = {}; g_state.error = L"Медиасессия недоступна. Откройте плеер.";
                g_busy = false;
            } catch (...) {
                MediaDiagnostics::Log("worker.poll", E_FAIL);
                manager = nullptr;
                std::lock_guard<std::mutex> lock(g_mutex);
                ++generation; requested = nullptr; pending = false;
                g_state = {}; g_state.error = L"Медиасессия недоступна. Откройте плеер.";
                g_busy = false;
            }
            pause(Ms(150));
        }
        g_busy = false;
    } catch (const winrt::hresult_error& e) {
        MediaDiagnostics::Log("worker", e.code());
        std::lock_guard<std::mutex> lock(g_mutex);
        g_state.error = L"Ошибка Windows Media: " + std::wstring(e.message());
        g_busy = false;
    } catch (...) {
        MediaDiagnostics::Log("worker", E_FAIL);
        std::lock_guard<std::mutex> lock(g_mutex);
        g_state.error = L"Медиасессия недоступна. Откройте плеер.";
        g_busy = false;
    }
}
static void Box(HDC dc, RECT r, COLORREF color, int radius = 0) {
    HBRUSH brush = CreateSolidBrush(color);
    auto oldBrush = SelectObject(dc, brush); auto oldPen = SelectObject(dc, GetStockObject(NULL_PEN));
    RoundRect(dc, r.left, r.top, r.right, r.bottom, radius, radius);
    SelectObject(dc, oldPen); SelectObject(dc, oldBrush); DeleteObject(brush);
}
static void Text(HDC dc, std::wstring text, RECT r, int size, COLORREF color, bool bold = false, UINT align = DT_LEFT) {
    auto font = CreateFontW(-size, 0, 0, 0, bold ? FW_SEMIBOLD : FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    auto old = SelectObject(dc, font); SetTextColor(dc, color); SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, text.c_str(), -1, &r, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX | align);
    SelectObject(dc, old); DeleteObject(font);
}
static void SettingsHeader(HDC dc, const std::wstring& title) {
    Text(dc,title,{16,8,Width-64,40},19,TextColor(),true);
}
static void SettingsCard(HDC dc, int index, const std::wstring& label, bool accent=false, int textRight=312) {
    auto r=SettingRow(index);
    Box(dc,{r.left,r.top+2,r.right,r.bottom-2},appearance_detail::Mix(BackgroundColor(),TextColor(),6),10);
    Text(dc,label,{r.left+10,r.top,textRight,r.bottom},14,accent?AccentColor():TextColor());
}
static void SettingsToggle(HDC dc, int index, const std::wstring& label, bool value) {
    SettingsCard(dc,index,label,false,276);
    auto r=SettingRow(index);
    Box(dc,{282,r.top+7,314,r.bottom-7},value?AccentColor():appearance_detail::Mix(BackgroundColor(),MutedColor(),40),16);
    int x=value?300:285;
    Box(dc,{x,r.top+10,x+11,r.bottom-10},TextColor(),10);
}
static void Paragraph(HDC dc, const std::wstring& text, RECT r, COLORREF color) {
    auto font=CreateFontW(-14,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    auto old=SelectObject(dc,font); SetTextColor(dc,color); SetBkMode(dc,TRANSPARENT);
    DrawTextW(dc,text.c_str(),-1,&r,DT_WORDBREAK|DT_END_ELLIPSIS|DT_NOPREFIX);
    SelectObject(dc,old); DeleteObject(font);
}
static std::wstring Time(int64_t ticks) {
    auto seconds = std::max<int64_t>(0, ticks / 10000000);
    return std::to_wstring(seconds / 60) + L":" + (seconds % 60 < 10 ? L"0" : L"") + std::to_wstring(seconds % 60);
}
static RECT Button(int index) { int x = 88 + index * 60; return {x, 78, x + 44, 122}; }
static RECT SettingsButton() { return {Width-56,4,Width-30,28}; }
static void DrawCover(HDC dc, const std::shared_ptr<Cover>& cover) {
    RECT r{16,14,72,70};
    Box(dc,r,RGB(39,42,48),12);
    if (!cover || cover->pixels.empty()) {
        Text(dc,L"♪",r,26,RGB(132,139,151),false,DT_CENTER); return;
    }
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = cover->width; info.bmiHeader.biHeight = -static_cast<LONG>(cover->height);
    info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
    int saved = SaveDC(dc);
    IntersectClipRect(dc,r.left,r.top,r.right,r.bottom);
    int side = static_cast<int>(std::min(cover->width,cover->height));
    StretchDIBits(dc,r.left,r.top,56,56,(cover->width-side)/2,(cover->height-side)/2,
        side,side,cover->pixels.data(),&info,DIB_RGB_COLORS,SRCCOPY);
    RestoreDC(dc,saved);
}
static void Paint(HDC dc) {
    auto s = Snapshot();
    const auto muted = MutedColor(), white = TextColor(), accent = AccentColor();
    PaintAppearance(dc,Width,Height);
    Text(dc,g_settings ? L"‹" : L"•••",SettingsButton(),20,muted,false,DT_CENTER);
    Box(dc,CloseButton(),RGB(54,35,43),10);
    Text(dc,L"×",CloseButton(),21,RGB(255,180,185),false,DT_CENTER);
    if (g_settings) {
        if(g_settingsPage==4) {
            SettingsHeader(dc,L"Кнопки в карточке");
            SettingsToggle(dc,0,L"Предыдущий трек",g_config.compactPrevious);
            SettingsToggle(dc,1,L"Пауза / воспроизведение",g_config.pause);
            SettingsToggle(dc,2,L"Следующий трек",g_config.compactNext);
            SettingsCard(dc,3,L"Ширина свёрнутого: " + std::to_wstring(g_config.compactWidth) + L"   − / +");
            SettingsCard(dc,4,L"Высота свёрнутого: " + std::to_wstring(g_config.compactHeight) + L"   − / +");
            SettingsCard(dc,5,L"Высота с текстом: " + std::to_wstring(g_config.compactLyricsHeight) + L"   − / +");
            SettingsCard(dc,6,L"‹ Основные настройки");
            return;
        }
        if(g_settingsPage==2) {
            SettingsHeader(dc,L"Своя тема");
            int colors[]={g_config.customBackground,g_config.customAccent,g_config.customText,g_config.customMuted};
            const wchar_t* names[]={L"Фон",L"Акцент",L"Основной текст",L"Вторичный текст"};
            for(int i=0;i<4;++i) {
                auto r=SettingRow(i);
                SettingsCard(dc,i,std::wstring(names[i])+L"  "+ColorHex(colors[i]),false,284);
                Box(dc,{290,r.top+6,320,r.bottom-6},static_cast<COLORREF>(colors[i]),6);
            }
            SettingsCard(dc,4,L"Применить свою тему",true);
            SettingsCard(dc,5,L"Скопировать текущие цвета");
            SettingsCard(dc,6,L"Сбросить свои цвета");
            SettingsCard(dc,7,L"‹ Оформление и текст");
            Paragraph(dc,L"Нажмите на цвет для выбора RGB.\nВыбранный цвет применяется сразу.\nПодберите контрастный текст к фону.",{16,304,324,382},muted);
            return;
        }
        if(g_settingsPage==3) {
            SettingsHeader(dc,L"Жидкое стекло");
            std::wstring labels[]={L"Эффект: "+std::wstring(g_config.glass?L"вкл.":L"выкл."),
                L"Сила бликов: "+std::to_wstring(g_config.glassStrength)+L"%  − / +",
                L"Цветовой оттенок: "+std::to_wstring(g_config.glassTint)+L"%  − / +",
                L"Движение бликов: "+std::wstring(g_config.glassAnimated?L"вкл.":L"выкл."),
                L"Скорость: "+std::to_wstring(g_config.glassSpeed)+L"%  − / +",
                L"Непрозрачность: "+std::to_wstring(g_config.opacity)+L"%  − / +",
                L"Сбросить эффект",L"‹ Оформление и текст"};
            for(int i=0;i<8;++i) {
                if(i==0) SettingsToggle(dc,i,L"Эффект",g_config.glass);
                else if(i==3) SettingsToggle(dc,i,L"Движение бликов",g_config.glassAnimated);
                else SettingsCard(dc,i,labels[i]);
            }
            Paragraph(dc,L"Плавные блики и цветная кромка.\nGDI-стилизация: без размытия и\nпреломления рабочего стола.",{16,304,324,382},muted);
            return;
        }
        if(g_settingsPage==1) {
            SettingsHeader(dc,L"Оформление и текст");
            const wchar_t* themes[]={L"Океан",L"Лаванда",L"Мята",L"Янтарь",L"Роза",L"Своя"};
            std::wstring labels[]={L"Тема: "+std::wstring(themes[g_config.theme])+L"  ‹ / ›",
                L"Настроить свою тему  ›",
                L"Жидкое стекло  ›",
                L"Свечение: "+std::to_wstring(g_config.glow)+L"%  − / +",
                L"Скругление: "+std::to_wstring(g_config.roundness)+L"  − / +",
                L"Текст: "+std::wstring(g_config.sourceLyrics?L"из каталога LRCLIB":g_config.asr?L"локальный Whisper":L"выключен")+L"  ›",
                L"Текст в свёрнутом виде: "+std::wstring(g_config.compactLyrics?L"вкл.":L"выкл."),L"‹ Основные настройки"};
            for(int i=0;i<8;++i) {
                if(i==6) SettingsToggle(dc,i,L"Текст в свёрнутом виде",g_config.compactLyrics);
                else SettingsCard(dc,i,labels[i],i==5);
            }
            Paragraph(dc,L"Каталог: поиск по названию и артисту в сети.\nWhisper: локально, слышит весь звук ПК.\nСтекло — блики, без размытия рабочего стола.",{16,298,324,382},muted);
            return;
        }
        SettingsHeader(dc,L"Настройки");
        const wchar_t* labels[]={L"Обложка в карточке",L"Название в карточке",L"Исполнитель в карточке",L"Пауза в карточке",L"Разворачивать при наведении",L"Закрепить развёрнутым",L"Громкость источника",L"Локальное распознавание"};
        bool values[]={g_config.cover,g_config.title,g_config.artist,g_config.pause,g_config.hover,g_config.pinned,g_config.volume,g_config.asr};
        for(int i=0;i<8;++i) {
            if(i==3) SettingsCard(dc,i,L"Кнопки в карточке  >",true);
            else SettingsToggle(dc,i,labels[i],values[i]);
        }
        SettingsCard(dc,8,L"Задержка: " + std::to_wstring(g_config.delay) + L" мс   − / +");
        SettingsCard(dc,9,L"Оформление и режим текста  ›",true);
        Text(dc,L"Esc — назад · клик слева −, справа +",{16,354,324,386},12,muted);
        return;
    }
    if (!g_expanded) {
        if(g_config.cover) DrawCover(dc,s.cover);
        int left=g_config.cover?80:16;
        if(g_config.title) Text(dc,s.title,{left,8,Width-60,32},14,white,true);
        if(g_config.artist) Text(dc,s.artist,{left,34,CompactTextRight(),60},12,muted);
        if(!g_config.title && !g_config.artist && !g_config.cover) Text(dc,L"Media",{16,8,Width-60,32},16,muted);
        bool enabled[]={s.previous,s.toggle,s.next};
        const wchar_t* icons[]={L"Ⅰ◀",s.playing?L"Ⅱ":L"▶",L"▶Ⅰ"};
        for(int i=0;i<3;++i) if(CompactButtonVisible(i))
            Text(dc,icons[i],CompactButton(i),i==1?18:13,enabled[i]&&!g_busy?white:muted,false,DT_CENTER);
        Text(dc,L"⌄",ExpandButton(),20,white,false,DT_CENTER);
        if(g_config.compactLyrics) {
            Box(dc,{16,75,Width-16,77},accent,2);
            Text(dc,CompactLyricsText(s),{16,79,Width-16,99},14,white);
        }
        return;
    }
    DrawCover(dc,s.cover);
    Text(dc, s.title, {84,16,280,41}, 17, white, true);
    Text(dc, s.artist.empty() ? (s.session ? L"Медиаплеер" : L"Нет активного трека") : s.artist, {84,43,296,66}, 13, muted);
    bool enabled[] = {s.previous, s.toggle, s.next};
    for (int i = 0; i < 3; ++i) {
        RECT r = Button(i); bool active = enabled[i] && !g_busy;
        if (i == 1) Box(dc, r, active ? accent : RGB(36,39,46), 48);
        auto color = active ? (i == 1 ? RGB(23,25,30) : white) : RGB(80,85,95);
        int cx = (r.left+r.right)/2, cy = (r.top+r.bottom)/2;
        if (i == 1 && s.playing) {
            Box(dc,{cx-7,cy-8,cx-2,cy+8},color); Box(dc,{cx+2,cy-8,cx+7,cy+8},color);
        } else {
            int direction = i == 0 ? -1 : 1;
            POINT points[] = {{cx-5*direction,cy-8},{cx-5*direction,cy+8},{cx+7*direction,cy}};
            auto brush = CreateSolidBrush(color); auto ob = SelectObject(dc,brush); auto op = SelectObject(dc,GetStockObject(NULL_PEN));
            Polygon(dc,points,3); SelectObject(dc,op); SelectObject(dc,ob); DeleteObject(brush);
            if (i != 1) { int x = cx+8*direction; Box(dc,{x-1,cy-8,x+2,cy+8},color); }
        }
    }
    int64_t position = g_drag ? SeekPosition(g_dragX,g_dragState) : s.position;
    double fraction = s.end > s.start ? double(position-s.start)/(s.end-s.start) : 0;
    int x = Left + int((Right-Left)*std::clamp(fraction,0.0,1.0));
    Box(dc,{Left,134,Right,138},RGB(58,62,72),4);
    if (x > Left) Box(dc,{Left,134,x,138},s.seek ? accent : muted,4);
    if (s.seek) Box(dc,{x-5,131,x+5,141},accent,10);
    Text(dc,Time(position-s.start),{16,143,100,161},12,muted);
    Text(dc,Time(s.end-s.start),{250,143,324,161},12,muted,false,DT_RIGHT);
    if (g_config.volume) {
    auto volume = VolumeSnapshot();
    float level = g_volumeDrag && volume.available ? g_volumePreview : volume.level;
    Text(dc,!volume.available ? L"Недоступно" : volume.muted ? L"Звук выкл." : L"Источник " + std::to_wstring(int(level*100+0.5f)) + L"%",{16,162,118,187},12,muted);
    int vx = 126 + int(level*(Right-126));
    Box(dc,{126,173,Right,177},RGB(58,62,72),4);
    if (volume.available) {
        if (vx>126) Box(dc,{126,173,vx,177},accent,4);
        Box(dc,{vx-5,170,vx+5,180},accent,10);
    }
    }
    std::wstring status = !s.error.empty() ? s.error : g_busy ? L"Отправка команды…" : !s.session ? L"Нажмите Play в музыкальном плеере" : !s.seek ? L"Плеер не поддерживает перемотку" : L"";
    Text(dc,status,{16,190,324,208},12,!s.error.empty() ? RGB(255,159,159) : muted);
    if (g_config.sourceLyrics) {
        Text(dc,L"Текст • LRCLIB",{16,216,324,240},13,accent,true);
        auto lyrics=g_lyrics.Snapshot();
        auto display=lyrics.DisplayAt(double(s.position-s.start)/10000000.0);
        if(display.empty()) display=lyrics.status;
        Paragraph(dc,display,{16,244,324,350},white);
    } else if (g_config.asr) {
        auto a=g_asr.Snapshot();
        Text(dc,L"Текст • Whisper (звук ПК)",{16,220,324,242},13,accent,true);
        std::wstring lyrics=a.text;
        if(!a.provisional.empty()) { if(!lyrics.empty()) lyrics += L" "; lyrics += L"▸ "; lyrics += a.provisional; }
        const bool error = a.status.rfind(L"ASR error:",0)==0;
        if(error || lyrics.empty()) lyrics = a.status.empty() ? L"Распознавание остановлено" : a.status;
        if(!s.playing) lyrics=L"Пауза — распознавание приостановлено";
        if(lyrics.size()>450) lyrics=L"…"+lyrics.substr(lyrics.size()-450);
        Paragraph(dc,lyrics,{16,244,324,350},error ? RGB(255,159,159) : white);
    }
}
static void Render(HDC dc) {
    constexpr int scale=3;
    auto buffer=CreateCompatibleDC(dc);
    auto bitmap=CreateCompatibleBitmap(dc,Width*scale,Height*scale);
    if (!buffer || !bitmap) { if (bitmap) DeleteObject(bitmap); if (buffer) DeleteDC(buffer); Paint(dc); return; }
    auto old=SelectObject(buffer,bitmap);
    SetMapMode(buffer,MM_ANISOTROPIC);
    SetWindowExtEx(buffer,Width,Height,nullptr);
    SetViewportExtEx(buffer,Width*scale,Height*scale,nullptr);
    Paint(buffer);
    SetMapMode(buffer,MM_TEXT);
    int saved=SaveDC(dc);
    SetStretchBltMode(dc,HALFTONE); SetBrushOrgEx(dc,0,0,nullptr);
    StretchBlt(dc,0,0,Width,Height,buffer,0,0,Width*scale,Height*scale,SRCCOPY);
    RestoreDC(dc,saved);
    SelectObject(buffer,old); DeleteObject(bitmap); DeleteDC(buffer);
}
static LRESULT CALLBACK WndProc(HWND h, UINT message, WPARAM w, LPARAM l) {
    switch (message) {
    case WM_CREATE: SetTimer(h,1,150,nullptr); return 0;
    case WM_TIMER: if (g_volumeDrag && !VolumeSnapshot().available) CancelDrag(h); LyricsTick(); HoverTick(h); InvalidateRect(h,nullptr,FALSE); return 0;
    case WM_ENTERSIZEMOVE: g_moving=true; return 0;
    case WM_EXITSIZEMOVE: { g_moving=false; RECT r; GetWindowRect(h,&r); g_anchor={r.left,r.top}; Layout(h); return 0; }
    case WM_DISPLAYCHANGE: Layout(h); return 0;
    case WM_SETTINGCHANGE: Layout(h); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps; auto dc=BeginPaint(h,&ps); Render(dc); EndPaint(h,&ps); return 0;
    }
    case WM_LBUTTONDOWN: {
        POINT p{GET_X_LPARAM(l),GET_Y_LPARAM(l)};
        RECT settings = SettingsButton();
        if (Hit(CloseButton(),p)) { DestroyWindow(h); return 0; }
        if (PtInRect(&settings,p)) { CancelDrag(h); if(g_settings && g_settingsPage>0) g_settingsPage=SettingsParentPage(); else g_settings=!g_settings; Layout(h); return 0; }
        if (g_settings) {
            g_settingsDrag=true; g_settingsDragMoved=false; g_settingsDragStart=p; SetCapture(h); return 0;
        }
        if (!g_expanded) {
            auto s=Snapshot();
            bool enabled[]={s.previous,s.toggle,s.next};
            for(int i=0;i<3;++i) if(CompactButtonVisible(i) && Hit(CompactButton(i),p)) {
                if(enabled[i] && !g_busy) Queue(i+1,s);
                InvalidateRect(h,nullptr,FALSE); return 0;
            }
            if(Hit(ExpandButton(),p)) { g_expanded=true; g_hover.armed=false; Layout(h); return 0; }
            SendMessageW(h,WM_NCLBUTTONDOWN,HTCAPTION,0); return 0;
        }
        if (g_config.volume && Hit(VolumeRegion(),p) && VolumeSnapshot().available) {
            g_volumeDrag=true; g_volumePreview=VolumeLevel(p.x); QueueVolume(g_volumePreview); SetCapture(h); InvalidateRect(h,nullptr,FALSE); return 0;
        }
        auto s = Snapshot(); if (g_busy) return 0;
        bool enabled[] = {s.previous,s.toggle,s.next};
        for (int i=0;i<3;++i) { RECT r=Button(i); if (PtInRect(&r,p) && enabled[i]) { Queue(i+1,s); InvalidateRect(h,nullptr,FALSE); return 0; } }
        if (s.seek && Hit(SeekRegion(),p)) {
            g_dragState=s; g_drag=true; g_dragX=p.x; SetCapture(h); InvalidateRect(h,nullptr,FALSE); return 0;
        }
        if (!Hit(SeekRegion(),p) && !(g_config.volume && Hit(VolumeRegion(),p)) &&
            !Hit(Button(0),p) && !Hit(Button(1),p) && !Hit(Button(2),p))
            SendMessageW(h,WM_NCLBUTTONDOWN,HTCAPTION,0);
        return 0;
    }
    case WM_MOUSEMOVE: {
        if (g_settingsDrag) {
            int x=GET_X_LPARAM(l), y=GET_Y_LPARAM(l);
            if (!g_settingsDragMoved && (abs(x-g_settingsDragStart.x)>4 || abs(y-g_settingsDragStart.y)>4)) g_settingsDragMoved=true;
            if (g_settingsDragMoved) {
                RECT r; GetWindowRect(h,&r);
                SetWindowPos(h,nullptr,r.left+(x-g_settingsDragStart.x),r.top+(y-g_settingsDragStart.y),0,0,SWP_NOZORDER|SWP_NOSIZE|SWP_NOACTIVATE);
                g_anchor={r.left+(x-g_settingsDragStart.x), r.top+(y-g_settingsDragStart.y)}; g_moving=true; InvalidateRect(h,nullptr,FALSE);
            }
            return 0;
        }
        if (g_volumeDrag) { g_volumePreview=VolumeLevel(GET_X_LPARAM(l)); QueueVolume(g_volumePreview); InvalidateRect(h,nullptr,FALSE); }
        if (g_drag) { g_dragX=GET_X_LPARAM(l); InvalidateRect(h,nullptr,FALSE); }
        return 0;
    }
    case WM_LBUTTONUP:
        if (g_settingsDrag) {
            bool moved=g_settingsDragMoved; ReleaseCapture(); g_settingsDrag=false;
            if (!moved) HandleSettingsClick(h, g_settingsDragStart);
            return 0;
        }
        if (g_volumeDrag) { QueueVolume(VolumeLevel(GET_X_LPARAM(l))); CancelDrag(h); InvalidateRect(h,nullptr,FALSE); return 0; }
        if (g_drag) { auto target=g_dragState; auto position=SeekPosition(GET_X_LPARAM(l),target); g_drag=false; ReleaseCapture(); Queue(4,target,position); g_dragState={}; InvalidateRect(h,nullptr,FALSE); } return 0;
    case WM_CANCELMODE: if (g_settingsDrag) { ReleaseCapture(); g_settingsDrag=false; } CancelDrag(h); return 0;
    case WM_CAPTURECHANGED: if (g_settingsDrag) { ReleaseCapture(); g_settingsDrag=false; } g_volumeDrag=false; g_drag=false; g_dragState={}; InvalidateRect(h,nullptr,FALSE); return 0;
    case WM_KEYDOWN: if (w == VK_ESCAPE) CancelDrag(h); if (w == VK_ESCAPE && g_settings) { if(g_settingsPage>0) g_settingsPage=SettingsParentPage(); else g_settings=false; Layout(h); } else if(w==VK_ESCAPE && !g_config.pinned) { g_expanded=false; g_hover.armed=false; Layout(h); } return 0;
    case WM_DESTROY: g_asr.Stop(); CancelDrag(h); KillTimer(h,1); g_stop=true; PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h,message,w,l);
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR arguments,int) {
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    WNDCLASSW wc{}; wc.hInstance=instance; wc.lpfnWndProc=WndProc; wc.lpszClassName=L"MediaPopupClass"; wc.hCursor=LoadCursor(nullptr,IDC_ARROW);
    if (!RegisterClassW(&wc)) return 1;
    g_configPath=ConfigPath(); g_config=LoadConfig(g_configPath); g_expanded=g_config.pinned;
    // Diagnostic launch never starts capture, regardless of persisted preference.
    if (std::wstring(arguments) == L"--no-asr") g_config.asr=false;
    DWORD style=WS_POPUP, ex=WS_EX_TOPMOST|WS_EX_TOOLWINDOW;
    RECT size{0,0,Width,Height}; AdjustWindowRectEx(&size,style,FALSE,ex);
    RECT work; SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);
    HWND h=CreateWindowExW(ex,wc.lpszClassName,L"Media Popup",style,work.right-(size.right-size.left)-20,work.bottom-(size.bottom-size.top)-20,size.right-size.left,size.bottom-size.top,nullptr,nullptr,instance,nullptr);
    if (!h) return 1;
    g_anchor={work.right-360,work.bottom-230}; Layout(h);
    if(g_configPath.empty()) MessageBoxW(h,L"Папка настроек недоступна. Сохранение настроек невозможно.",L"Media Popup",MB_OK|MB_ICONWARNING);
    std::thread worker(Worker), volumeWorker(VolumeWorker); ShowWindow(h,SW_SHOW);
    LyricsTick();
    MSG msg; while (GetMessageW(&msg,nullptr,0,0)>0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    g_stop=true; worker.join(); volumeWorker.join(); return 0;
}
