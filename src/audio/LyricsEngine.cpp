#include "LyricsEngine.h"
namespace media {
std::wstring LyricsSnapshot::DisplayAt(double seconds) const {
    std::wstring result;
    if (lines.empty()) {
        result = text;
    } else {
        if (!std::isfinite(seconds)) seconds = 0;
        auto it = std::upper_bound(lines.begin(), lines.end(), seconds,
            [](double t, const LyricsLine& l) { return t < l.seconds; });
        size_t i = it == lines.begin() ? 0 : static_cast<size_t>(it - lines.begin() - 1);
        result = lines[i].text;
        if (i + 1 < lines.size()) result += L"\n" + lines[i + 1].text;
    }
    size_t end = 0, newlines = 0;
    for (; end < result.size() && end < 320; ++end)
        if (result[end] == L'\n' && ++newlines == 3) break;
    if (end < result.size()) {
        if (end && result[end - 1] >= 0xD800 && result[end - 1] <= 0xDBFF) --end;
        result.resize(end);
        result += L"\u2026";
    }
    return result;
}

namespace lyrics_detail {
std::wstring Trim(std::wstring_view s) {
    while (!s.empty() && (iswspace(s.front()) || s.front() == 0xFEFF)) s.remove_prefix(1);
    while (!s.empty() && iswspace(s.back())) s.remove_suffix(1);
    return std::wstring(s);
}
bool Number(std::wstring_view s, double& v) {
    if (s.empty()) return false;
    v = 0; double factor = 0; bool digit = false, decimal = false;
    for (wchar_t c : s) {
        if (c == L'.' && !decimal && digit) { decimal = true; factor = 0.1; digit = false; continue; }
        if (c < L'0' || c > L'9') return false;
        digit = true;
        if (factor) { v += (c-L'0') * factor; factor *= 0.1; }
        else v = v * 10 + c-L'0';
        if (v > 10000000) return false;
    }
    return digit;
}
// Supports repeated timestamps, mm:ss.xxx / hh:mm:ss.xxx and global offset ms.
// Malformed tags are ignored; blank timed cues remain to mark instrumental gaps.
std::vector<LyricsLine> ParseLrc(std::wstring_view input) {
    std::vector<LyricsLine> out;
    if (input.size() > 512 * 1024) return out;
    double offset = 0;
    for (size_t start = 0; start < input.size();) {
        auto end = input.find(L'\n', start);
        if (end == input.npos) end = input.size();
        std::wstring line = Trim(input.substr(start, end-start)); start = end + 1;
        std::vector<double> times;
        size_t p = 0;
        while (p < line.size() && line[p] == L'[') {
            auto close = line.find(L']', p);
            if (close == line.npos) break;
            std::wstring_view tag(line.data()+p+1, close-p-1);
            if (tag.starts_with(L"offset:")) {
                auto value = tag.substr(7); double sign = 1, n = 0;
                if (!value.empty() && (value.front() == L'-' || value.front() == L'+')) {
                    if (value.front() == L'-') sign = -1;
                    value.remove_prefix(1);
                }
                if (Number(value, n)) offset = sign*n/1000;
            } else {
                auto colon = tag.find(L':'); double a=0,b=0,c=0;
                if (colon != tag.npos && tag.substr(0,colon).find(L'.') == tag.npos && Number(tag.substr(0,colon),a)) {
                    auto rest = tag.substr(colon+1); auto second = rest.find(L':');
                    if (second == rest.npos) {
                        if (Number(rest,b) && b < 60) times.push_back(a*60+b);
                    } else if (rest.substr(0,second).find(L'.') == rest.npos && Number(rest.substr(0,second),b) && b < 60 &&
                               Number(rest.substr(second+1),c) && c < 60) times.push_back(a*3600+b*60+c);
                }
            }
            p = close+1;
        }
        for (double t : times) {
            if (out.size() == 10000) return {};
            out.push_back({t, Trim(std::wstring_view(line).substr(p))});
        }
    }
    for (auto& l : out) l.seconds = (std::max)(0.0, l.seconds+offset);
    std::stable_sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.seconds < b.seconds; });
    return out;
}
std::wstring Encode(std::wstring_view value) {
    std::string bytes = winrt::to_string(value);
    constexpr wchar_t hex[] = L"0123456789ABCDEF";
    std::wstring out;
    for (unsigned char c : bytes) {
        if ((c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') || c=='-' || c=='_' || c=='.' || c=='~') out += wchar_t(c);
        else { out += L'%'; out += hex[c>>4]; out += hex[c&15]; }
    }
    return out;
}
LyricsSnapshot ParseJson(std::string_view bytes) {
    if (bytes.size() > 512 * 1024) throw std::runtime_error("json size");
    using namespace winrt::Windows::Data::Json;
    LyricsSnapshot s; JsonObject obj;
    if (!JsonObject::TryParse(winrt::to_hstring(bytes), obj)) throw std::runtime_error("json");
    auto string = [&](const wchar_t* name) -> std::wstring {
        auto value = obj.GetNamedValue(name, nullptr);
        return value && value.ValueType() == JsonValueType::String ? std::wstring(value.GetString()) : L"";
    };
    auto instrumental = obj.GetNamedValue(L"instrumental", nullptr);
    if (instrumental && instrumental.ValueType() == JsonValueType::Boolean && instrumental.GetBoolean()) {
        s.status = L"Instrumental (LRCLIB)"; return s;
    }
    s.text = string(L"plainLyrics"); s.lines = ParseLrc(string(L"syncedLyrics"));
    s.status = !s.lines.empty() ? L"Synced lyrics (LRCLIB)" : !s.text.empty() ? L"Plain lyrics (LRCLIB, not synchronized)" : L"No lyrics in catalog record";
    return s;
}
struct Handle {
    HINTERNET value;
    explicit Handle(HINTERNET h) : value(h) { if (!h) throw std::runtime_error("http"); }
    ~Handle() { WinHttpCloseHandle(value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};
}

struct LyricsEngine::Impl {
public:
    Impl() : worker_([this] { Run(); }) {}
    ~Impl() {
        { std::lock_guard lock(mutex_); stopping_ = true; ++generation_; }
        cv_.notify_one(); worker_.join();
    }


    // Nonblocking, one pending slot (latest wins). Identical metadata, including
    // failed/no-match lookups, is not retried each tick. Clear permits retry.
    void Request(std::wstring title, std::wstring artist, double durationSeconds) {
        title = lyrics_detail::Trim(title); artist = lyrics_detail::Trim(artist);
        if (title.empty() || artist.empty() || title.size()>1024 || artist.size()>1024 ||
            !std::isfinite(durationSeconds) || durationSeconds<=0 || durationSeconds>86400) { Clear(); return; }
        Job job{std::move(title), std::move(artist), static_cast<int>(std::llround(durationSeconds)), 0};
        std::lock_guard lock(mutex_);
        if (last_ && job.title == last_->title && job.artist == last_->artist && job.duration == last_->duration) return;
        job.generation = ++generation_; last_ = job; pending_ = std::move(job);
        snapshot_ = {}; snapshot_.status = L"Looking up lyrics (LRCLIB)"; cv_.notify_one();
    }
    void Clear() {
        std::lock_guard lock(mutex_);
        if (!last_ && !pending_) return;
        ++generation_; pending_.reset(); last_.reset(); snapshot_ = {};
    }
    LyricsSnapshot Snapshot() const { std::lock_guard lock(mutex_); return snapshot_; }
private:
    struct Job { std::wstring title, artist; int duration; uint64_t generation; };
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::atomic<uint64_t> generation_{0};
    bool stopping_ = false;
    std::optional<Job> pending_, last_;
    LyricsSnapshot snapshot_;
    std::thread worker_;
    LyricsSnapshot Fetch(const Job& job) {
        using namespace lyrics_detail;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(12);
        auto check = [&] {
            if (generation_.load() != job.generation) throw std::runtime_error("cancelled");
            if (std::chrono::steady_clock::now() >= deadline) throw std::runtime_error("timeout");
        };
        auto ok = [&](BOOL value) { check(); if (!value) throw std::runtime_error("http"); };
        check();
        Handle session(WinHttpOpen(L"MediaPopup/1.0 (LRCLIB desktop lyrics client)", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
        ok(WinHttpSetTimeouts(session.value, 2000, 2000, 2000, 2000));
        Handle connection(WinHttpConnect(session.value, L"lrclib.net", INTERNET_DEFAULT_HTTPS_PORT, 0));
        std::wstring path = L"/api/get?track_name=" + Encode(job.title) + L"&artist_name=" + Encode(job.artist) +
            L"&duration=" + std::to_wstring(job.duration);
        Handle request(WinHttpOpenRequest(connection.value, L"GET", path.c_str(), nullptr,
            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
        DWORD redirects = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
        ok(WinHttpSetOption(request.value, WINHTTP_OPTION_REDIRECT_POLICY, &redirects, sizeof(redirects)));
        DWORD retries = 0;
        ok(WinHttpSetOption(request.value, WINHTTP_OPTION_CONNECT_RETRIES, &retries, sizeof(retries)));
        check();
        ok(WinHttpSendRequest(request.value, L"Accept: application/json\r\n", static_cast<DWORD>(-1),
            WINHTTP_NO_REQUEST_DATA, 0, 0, 0));
        ok(WinHttpReceiveResponse(request.value, nullptr));
        DWORD status=0, size=sizeof(status);
        ok(WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX));
        LyricsSnapshot result;
        if (status == 404) { result.status = L"No matching lyrics in LRCLIB"; return result; }
        if (status == 429) { result.status = L"LRCLIB rate limit; retry after changing track or clearing"; return result; }
        if (status != 200) throw std::runtime_error("status");
        std::string body; char buffer[8192];
        for (;;) {
            check(); DWORD read=0;
            ok(WinHttpReadData(request.value, buffer, sizeof(buffer), &read));
            if (!read) break;
            if (body.size()+read > 512*1024) throw std::runtime_error("size");
            body.append(buffer, read);
        }
        check(); return ParseJson(body);
    }
    void Run() noexcept {
        bool apartment = false;
        try { winrt::init_apartment(winrt::apartment_type::multi_threaded); apartment = true; } catch (...) {}
        for (;;) {
            Job job;
            {
                std::unique_lock lock(mutex_);
                cv_.wait(lock, [&] { return stopping_ || pending_.has_value(); });
                if (stopping_) break;
                job = std::move(*pending_); pending_.reset();
            }
            LyricsSnapshot result;
            try {
                if (!apartment) throw std::runtime_error("apartment");
                result = Fetch(job);
            } catch (...) { result.status = L"Lyrics unavailable (network or catalog error)"; }
            std::lock_guard lock(mutex_);
            if (!stopping_ && generation_.load() == job.generation) snapshot_ = std::move(result);
        }
        if (apartment) winrt::uninit_apartment();
    }
    // Cancellation is cooperative between bounded synchronous WinHTTP operations.
    // Never close an in-flight synchronous handle from another thread (MS docs).
    // Clear invalidates instantly; destructor joins after the active call unwinds.
};
LyricsEngine::LyricsEngine() : impl_(std::make_unique<Impl>()) {}
LyricsEngine::~LyricsEngine() = default;
void LyricsEngine::Request(std::wstring title, std::wstring artist, double duration) {
    impl_->Request(std::move(title), std::move(artist), duration);
}
void LyricsEngine::Clear() { impl_->Clear(); }
LyricsSnapshot LyricsEngine::Snapshot() const { return impl_->Snapshot(); }
} // namespace media