#include "MediaPopup.cpp"
#include <cassert>
#include <iostream>

struct FakeAsync {
    winrt::Windows::Foundation::AsyncStatus status;
    mutable bool canceled=false, results=false;
    auto Status() const { return status; }
    winrt::hresult ErrorCode() const { return E_ACCESSDENIED; }
    void Cancel() const { canceled=true; }
    int GetResults() const { results=true; return 42; }
};
int main() {
    using winrt::Windows::Foundation::AsyncStatus;
    std::atomic<bool> stop{false};
    FakeAsync done{AsyncStatus::Completed};
    assert(MediaDiagnostics::Wait(done,std::chrono::milliseconds(0),stop,"test.complete")==42 && done.results);
    FakeAsync stuck{AsyncStatus::Started};
    try { MediaDiagnostics::Wait(stuck,std::chrono::milliseconds(0),stop,"test.timeout"); assert(false); }
    catch(const winrt::hresult_error& e) { assert(e.code()==HRESULT_FROM_WIN32(ERROR_TIMEOUT)); }
    assert(stuck.canceled && !stuck.results);
    FakeAsync failed{AsyncStatus::Error};
    try { MediaDiagnostics::Wait(failed,std::chrono::milliseconds(0),stop,"test.error"); assert(false); }
    catch(const winrt::hresult_error& e) { assert(e.code()==E_ACCESSDENIED); }
    assert(!failed.results);
    stop=true; FakeAsync canceled{AsyncStatus::Started};
    try { MediaDiagnostics::Wait(canceled,std::chrono::seconds(10),stop,"test.stop"); assert(false); }
    catch(const winrt::hresult_error& e) { assert(e.code()==HRESULT_FROM_WIN32(ERROR_CANCELLED)); }
    assert(canceled.canceled && !canceled.results);
    assert(Width==280 && Height==70);
    RECT work{-1920,0,0,1080}; POINT anchor{-100,1040};
    auto compact=PopupRect(anchor,280,70,work), expanded=PopupRect(anchor,340,210,work);
    assert(compact.right==0 && expanded.right==0 && expanded.bottom==1080);
    auto restored=PopupRect(anchor,280,70,work); assert(EqualRect(&compact,&restored));
    auto fixed=PopupRect({-1800,50},340,210,work); assert(fixed.left==-1800 && fixed.top==50);
    HoverState hover;
    assert(!hover.Collapse(false,false,0,700));
    assert(!hover.Collapse(false,false,699,700));
    assert(hover.Collapse(false,false,700,700));
    assert(!hover.Collapse(false,true,800,700));
    assert(!hover.Collapse(false,false,900,700));
    assert(!hover.Collapse(true,false,1700,700));
    hover.armed=false; hover.Collapse(true,false,1800,700); assert(!hover.armed);
    wchar_t temp[MAX_PATH], path[MAX_PATH]; assert(GetTempPathW(MAX_PATH,temp));
    assert(GetTempFileNameW(temp,L"mpt",0,path));
    PopupConfig cfg; cfg.cover=cfg.title=cfg.artist=cfg.pause=cfg.hover=cfg.volume=false; cfg.pinned=true; cfg.delay=1200;
    assert(SaveConfig(path,cfg)); auto loaded=LoadConfig(path);
    assert(!loaded.cover && !loaded.title && !loaded.artist && !loaded.pause && !loaded.hover && !loaded.volume && loaded.pinned && loaded.delay==1200);
    assert(!loaded.asr);
    cfg.compactPrevious=true; cfg.compactNext=true; cfg.pause=false;
    assert(SaveConfig(path,cfg)); loaded=LoadConfig(path);
    assert(loaded.compactPrevious && loaded.compactNext && !loaded.pause);
    cfg.asr=true; assert(SaveConfig(path,cfg)); assert(LoadConfig(path).asr);
    cfg.asr=false; assert(SaveConfig(path,cfg)); assert(!LoadConfig(path).asr);
    cfg.sourceLyrics=true; cfg.theme=4; cfg.opacity=65; cfg.glass=false; cfg.glow=80; cfg.roundness=24;
    assert(SaveConfig(path,cfg)); loaded=LoadConfig(path);
    assert(loaded.sourceLyrics && loaded.theme==4 && loaded.opacity==65 && !loaded.glass && loaded.glow==80 && loaded.roundness==24);
    cfg.opacity=1; assert(!SaveConfig(path,cfg)); cfg.opacity=65;
    cfg.theme=5; cfg.compactLyrics=true; cfg.customBackground=RGB(1,2,3); cfg.customAccent=RGB(4,5,6);
    cfg.customText=RGB(240,230,220); cfg.customMuted=RGB(100,110,120);
    cfg.glassStrength=80; cfg.glassTint=65; cfg.glassSpeed=0; cfg.glassAnimated=false;
    assert(SaveConfig(path,cfg)); loaded=LoadConfig(path);
    assert(loaded.theme==5 && loaded.compactLyrics && loaded.customBackground==RGB(1,2,3));
    assert(loaded.customAccent==cfg.customAccent && loaded.customText==cfg.customText && loaded.customMuted==cfg.customMuted);
    assert(loaded.glassStrength==80 && loaded.glassTint==65 && loaded.glassSpeed==0 && !loaded.glassAnimated);
    cfg.customText=0x1000000; assert(!SaveConfig(path,cfg)); cfg.customText=RGB(240,230,220);
    cfg.glassStrength=101; assert(!SaveConfig(path,cfg)); cfg.glassStrength=80;
    assert(WritePrivateProfileStringW(L"Settings",L"glassTint",L"999",path)); assert(LoadConfig(path).glassTint==35);
    cfg.compactWidth=350; cfg.compactHeight=150; cfg.compactLyricsHeight=200;
    assert(SaveConfig(path,cfg)); loaded=LoadConfig(path);
    assert(loaded.compactWidth==350 && loaded.compactHeight==150 && loaded.compactLyricsHeight==200);
    cfg.compactWidth=100; assert(!SaveConfig(path,cfg)); cfg.compactWidth=280;
    cfg.compactHeight=1; assert(!SaveConfig(path,cfg)); cfg.compactHeight=70;
    cfg.compactLyricsHeight=50; assert(!SaveConfig(path,cfg)); cfg.compactLyricsHeight=102;
    assert(ColorHex(RGB(1,2,255))==L"#0102FF");
    assert(WritePrivateProfileStringW(L"Settings",L"theme",L"999",path)); assert(LoadConfig(path).theme==0);
    assert(WritePrivateProfileStringW(L"Settings",L"delay",L"broken",path));
    assert(LoadConfig(path).delay==700);
    assert(!SaveConfig(L"",cfg)); assert(DeleteFileW(path));
    g_expanded=true; Width=340; Height=210;
    State s;
    s.start = 100000000;
    s.end = 1100000000;
    s.minimum = s.start;
    s.maximum = s.end;
    assert(SeekPosition(Left, s) == s.start);
    assert(SeekPosition(Right, s) == s.end);
    assert(SeekPosition((Left + Right) / 2, s) == 600000000);
    assert(SeekPosition(-100, s) == s.start);
    assert(SeekPosition(999, s) == s.end);
    s.minimum = 200000000; s.maximum = 900000000;
    assert(SeekPosition(Left, s) == s.minimum);
    assert(SeekPosition(Right, s) == s.maximum);
    assert(Time(650000000) == L"1:05");
    assert(Time(-100) == L"0:00");
    for (int i = 0; i < 3; ++i) {
        auto r = Button(i);
        RECT overlap{};
        auto settings = SettingsButton();
        assert(!IntersectRect(&overlap, &r, &settings));
        assert(r.bottom < SeekRegion().top);
        if (i < 2) assert(r.right < Button(i + 1).left);
    }
    auto settings = SettingsButton();
    assert(settings.left >= 280 && settings.right <= Width);
    assert(Width == 340 && Height == 210);
    assert(VolumeLevel(-100)==0 && VolumeLevel(999)==1);
    assert(VolumeLevel(225)==0.5f);
    RECT intersection{}, seek=SeekRegion(), volume=VolumeRegion();
    assert(!IntersectRect(&intersection,&seek,&volume));
    assert(Hit(seek,{Left,136}) && Hit(volume,{Right,175}));
    QueueVolume(0.5f); assert(!volume_detail::Data().pending);
    auto& audio=volume_detail::Data();
    audio.snapshot={true,0.4f,false}; audio.endpointId=L"test";
    // A stale snapshot must never authorize writing without a selected media source.
    QueueVolume(-1); assert(!audio.pending);
    QueueVolume(2); assert(!audio.pending);
    assert(volume_detail::Equal(L"PLAYER.EXE",L"player.exe"));
    assert(!volume_detail::Equal(L"player.exe",L"other.exe"));
    assert(!volume_detail::Equal(L"",L""));
    volume_detail::Disable(); assert(!VolumeSnapshot().available && !audio.pending);
    HWND window=CreateWindowExW(0,L"STATIC",L"test",WS_POPUP,0,0,Width,Height,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    assert(window);
    // Exercise the real layout without starting ASR or capturing audio.
    auto checkLayout=[&](int width,int height) {
        Layout(window);
        assert(Width==width && Height==height);
        RECT rect{}; assert(GetWindowRect(window,&rect));
        assert(rect.right-rect.left==width && rect.bottom-rect.top==height);
    };
    for(bool asr : {false,true}) {
        g_config.asr=asr;
        g_expanded=false; checkLayout(280,70);
        g_expanded=true; checkLayout(340,asr ? 360 : 210);
        g_settings=true; checkLayout(340,392);
        g_settings=false; checkLayout(340,asr ? 360 : 210);
        g_expanded=false; checkLayout(280,70);
    }
    g_expanded=true; checkLayout(340,360);
    g_config.asr=false; checkLayout(340,210);
    g_config.sourceLyrics=true; checkLayout(340,360);
    g_config.sourceLyrics=false; checkLayout(340,210);
    g_expanded=false; g_config.compactLyrics=true; checkLayout(280,102);
    State noTrack;
    assert(CompactLyricsText(noTrack)==L"Выберите режим текста в настройках");
    g_config.sourceLyrics=true; assert(CompactLyricsText(noTrack)==L"Ожидание трека…");
    g_config.sourceLyrics=false; g_config.compactLyrics=false; checkLayout(280,70);
    g_expanded=true; checkLayout(340,210);
    assert(!g_asr.Snapshot().active);
    g_volumeDrag=true; SetCapture(window); CancelDrag(window);
    assert(!g_volumeDrag && GetCapture()!=window);
    g_drag=true; WndProc(window,WM_CAPTURECHANGED,0,0); assert(!g_drag);
    g_settings=true; WndProc(window,WM_KEYDOWN,VK_ESCAPE,0); assert(!g_settings);
    WndProc(window,WM_LBUTTONDOWN,0,MAKELPARAM(220,175)); assert(!g_volumeDrag);
    DestroyWindow(window);
    HDC screen = GetDC(nullptr);
    HDC dc = CreateCompatibleDC(screen);
    HBITMAP bitmap = CreateCompatibleBitmap(screen, 340, 392);
    auto old = SelectObject(dc, bitmap);
    Render(dc);
    g_settings = true;
    Render(dc);
    for(int page=1;page<=4;++page) { g_settingsPage=page; Render(dc); }
    g_settingsPage=0;
    g_config.theme=5; g_config.customBackground=RGB(8,16,24); g_config.customText=RGB(250,240,230);
    assert(BackgroundColor()==RGB(8,16,24) && TextColor()==RGB(250,240,230));
    for(bool glass : {false,true}) { g_config.glass=glass; Render(dc); }
    g_settings = false;
    g_expanded=false; g_config.compactLyrics=true; Width=280; Height=102; Render(dc);
    auto cover = std::make_shared<Cover>();
    cover->width = 2; cover->height = 2;
    cover->pixels = winrt::com_array<uint8_t>(16, 255);
    DrawCover(dc, cover);
    SelectObject(dc, old); DeleteObject(bitmap); DeleteDC(dc); ReleaseDC(nullptr, screen);
    std::cout << "All seek, time and hit-region tests passed.\n";
}