#include "../src/audio/LyricsEngine.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
#include <limits>

// cl /std:c++20 /EHsc /utf-8 /W4 LyricsTests.cpp
// Offline, deterministic tests: no request with valid metadata is made.
int main() {
    using namespace media; using namespace media::lyrics_detail;
    winrt::init_apartment();
    auto lines = ParseLrc(L"[ar:Artist]\n[00:10.50][00:20.123]Hello\r\n[00:05]First\n[offset:-500]\n[00:30]\n[bad]Ignore");
    assert(lines.size() == 4);
    assert(lines[0].seconds == 4.5 && lines[0].text == L"First");
    assert(lines[1].seconds == 10 && lines[1].text == L"Hello");
    assert(std::abs(lines[2].seconds-19.623)<0.00001);
    assert(lines[3].text.empty());
    assert(ParseLrc(L"[00:60]bad\n[00:no]bad\n[1:02:03.4]valid").size()==1);
    assert(ParseLrc(L"[00:00.1]x\n[offset:-1000]")[0].seconds==0);
    assert(ParseLrc(std::wstring(512*1024+1,L'x')).empty());
    assert(ParseLrc(L"[1.5:20]bad\n[00:01.]bad\n[00:.1]bad\n[00:1.2.3]bad").empty());
    assert(ParseLrc(L"[00:03]third\n[00:01]first\n[offset:+1000]")[0].seconds == 2);
    assert(Encode(L"A&B + / \u0416") == L"A%26B%20%2B%20%2F%20%D0%96");
    LyricsSnapshot s; s.lines=lines;
    assert(s.DisplayAt(0)==L"First\nHello");
    assert(s.DisplayAt(10)==L"Hello\nHello");
    assert(s.DisplayAt(30).empty());
    assert(s.DisplayAt(5)==L"First\nHello"); // seek backwards
    s=ParseJson(R"({"plainLyrics":"one\ntwo","syncedLyrics":null,"instrumental":false})");
    assert(s.text==L"one\ntwo" && s.lines.empty());
    assert(s.DisplayAt(123)==L"one\ntwo");
    s=ParseJson(R"({"plainLyrics":null,"syncedLyrics":"[00:01]Hi"})");
    assert(s.lines.size()==1 && s.DisplayAt(1)==L"Hi");
    s=ParseJson(R"({"instrumental":true})"); assert(s.text.empty() && s.lines.empty());
    bool failed=false; try { ParseJson("not json"); } catch (...) { failed=true; } assert(failed);
    s={}; s.text=std::wstring(1000,L'x'); assert(s.DisplayAt(0).size()==321);
    {
        LyricsEngine engine;
        engine.Request(L"",L"",0);
        engine.Request(L"Title",L"Artist",std::numeric_limits<double>::quiet_NaN());
        assert(engine.Snapshot().text.empty());
        engine.Clear(); assert(engine.Snapshot().lines.empty());
    }
    winrt::uninit_apartment();
    std::cout << "Lyrics tests passed\n";
}