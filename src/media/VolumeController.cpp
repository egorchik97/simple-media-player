#include "VolumeController.h"
#include "MediaSession.h"
#include <audiopolicy.h>
#include <appmodel.h>
#include <mmdeviceapi.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <winrt/base.h>
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")
namespace media {
namespace volume_detail {
    struct Shared
    {
        std::mutex mutex;
        VolumeState snapshot;
        std::wstring endpointId, commandId, sourceId;
        std::vector<VolumeChoice> choices;
        std::wstring manualKey, manualSource;
        Session manualSession{nullptr};
        Session sourceSession{nullptr};
        float commandLevel = 0.0f;
        bool pending = false;
    };
    struct Apartment
    {
        HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        ~Apartment() { if (SUCCEEDED(result)) CoUninitialize(); }
        Apartment() = default;
        Apartment(const Apartment&) = delete;
        Apartment& operator=(const Apartment&) = delete;
    };
    struct EndpointId
    {
        LPWSTR value = nullptr;
        ~EndpointId() { CoTaskMemFree(value); }
        EndpointId() = default;
        EndpointId(const EndpointId&) = delete;
        EndpointId& operator=(const EndpointId&) = delete;
    };
    static void Disable(Shared& data)
    {
        std::lock_guard<std::mutex> lock(data.mutex);
        data.snapshot = {};
        data.sourceId.clear(); data.sourceSession = nullptr;
        data.endpointId.clear();
        data.commandId.clear();
        data.pending = false;
    }

    // Exact identities and explicit well-known desktop browser aliases only.
    static bool Equal(const std::wstring& a, const std::wstring& b)
    { return !a.empty() && !b.empty() && CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, TRUE) == CSTR_EQUAL; }
    static bool MatchesIdentity(const std::wstring& source, const std::wstring& image, const std::wstring& aumid = {})
    {
        if (Equal(source, aumid) || Equal(source, image)) return true;
        const auto exe = image.substr(image.find_last_of(L"\\/") + 1);
        if (source.find_first_of(L"\\/!") == std::wstring::npos && Equal(source, exe)) return true;
        struct Alias { const wchar_t* id; const wchar_t* exe; };
        static constexpr Alias aliases[] = {
            {L"Chrome", L"chrome.exe"}, {L"Chrome.Shell", L"chrome.exe"}, {L"Google Chrome", L"chrome.exe"},
            {L"Microsoft Edge", L"msedge.exe"}, {L"MSEdge", L"msedge.exe"},
            {L"Yandex", L"browser.exe"}, {L"YandexBrowser", L"browser.exe"},
            {L"Yandex.Browser", L"browser.exe"}, {L"Brave", L"brave.exe"},
            {L"Opera", L"opera.exe"}, {L"Opera GX", L"opera.exe"},
            {L"Vivaldi", L"vivaldi.exe"}, {L"Firefox", L"firefox.exe"},
            {L"Spotify", L"Spotify.exe"}, {L"AIMP", L"AIMP.exe"},
            {L"VLC", L"vlc.exe"}, {L"VLC media player", L"vlc.exe"},
            {L"foobar2000", L"foobar2000.exe"}, {L"MusicBee", L"MusicBee.exe"}
        };
        for (const auto& alias : aliases) {
            if (!Equal(source, alias.id) || !Equal(exe, alias.exe)) continue;
            // browser.exe is generic: require the known Yandex install suffix.
            if (Equal(exe, L"browser.exe")) {
                const std::wstring suffix = L"\\Yandex\\YandexBrowser\\Application\\browser.exe";
                if (image.size() < suffix.size() || !Equal(image.substr(image.size()-suffix.size()), suffix)) continue;
            }
            return true;
        }
        return false;
    }
    static bool ProcessIdentity(DWORD pid, std::wstring& image, std::wstring& aumid)
    {
        struct Handle { HANDLE h; ~Handle() { if (h) CloseHandle(h); } } process{
            OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)};
        if (!process.h) return false;
        UINT32 size = 0;
        if (GetApplicationUserModelId(process.h, &size, nullptr) == ERROR_INSUFFICIENT_BUFFER && size && size <= 32768)
        {
            std::vector<wchar_t> id(size);
            if (GetApplicationUserModelId(process.h, &size, id.data()) == ERROR_SUCCESS) aumid = id.data();
        }
        std::vector<wchar_t> path(32768);
        DWORD length = static_cast<DWORD>(path.size());
        if (!QueryFullProcessImageNameW(process.h, 0, path.data(), &length)) return false;
        image.assign(path.data(), length);
        return true;
    }
    static bool Matches(DWORD pid, const std::wstring& source)
    {
        std::wstring image, aumid;
        ProcessIdentity(pid, image, aumid);
        return MatchesIdentity(source, image, aumid);
    }
    struct Target
    {
        std::wstring key;
        std::wstring label;
        bool automatic = false;
        DWORD pid = 0;
        winrt::com_ptr<IAudioSessionControl2> control;
        winrt::com_ptr<ISimpleAudioVolume> volume;
    };
    static void Append(std::wstring& key, const std::wstring& value)
    { key += std::to_wstring(value.size()) + L":" + value; }
    static bool Poll(Shared& data, MediaSession& media, const std::atomic<bool>& running)
    {
        Session selected{nullptr};
        std::wstring source;
        std::wstring manualKey;
        {
            const auto current = media.Snapshot();
            selected = current.session; source = current.source;
            std::lock_guard<std::mutex> audioLock(data.mutex);
            if (selected != data.manualSession || source != data.manualSource) {
                data.manualKey.clear(); data.choices.clear();
                data.manualSession = selected; data.manualSource = source;
            }
            manualKey = data.manualKey;
            if (selected != data.sourceSession || source != data.sourceId)
            { data.snapshot = {}; data.pending = false; data.commandId.clear(); data.endpointId.clear(); }
        }
        if (!selected || source.empty()) return false;
        winrt::com_ptr<IMMDeviceEnumerator> enumerator;
        if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER,
            __uuidof(IMMDeviceEnumerator), enumerator.put_void()))) return false;
        winrt::com_ptr<IMMDeviceCollection> devices;
        if (FAILED(enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, devices.put()))) return false;
        UINT count = 0;
        if (FAILED(devices->GetCount(&count))) return false;
        std::vector<Target> targets;
        for (UINT i = 0; i < count; ++i)
        {
            winrt::com_ptr<IMMDevice> device;
            EndpointId endpoint;
            winrt::com_ptr<IAudioSessionManager2> manager;
            winrt::com_ptr<IAudioSessionEnumerator> sessions;
            int n = 0;
            if (FAILED(devices->Item(i, device.put())) || FAILED(device->GetId(&endpoint.value)) || !endpoint.value) continue;
            if (FAILED(device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_INPROC_SERVER, nullptr, manager.put_void()))
                || FAILED(manager->GetSessionEnumerator(sessions.put())) || FAILED(sessions->GetCount(&n))) continue;
            for (int j = 0; j < n; ++j)
            {
                winrt::com_ptr<IAudioSessionControl> base;
                Target target;
                if (FAILED(sessions->GetSession(j, base.put()))) continue;
                if (FAILED(base->QueryInterface(__uuidof(IAudioSessionControl2), target.control.put_void()))) continue;
                AudioSessionState state;
                if (FAILED(target.control->GetState(&state)) || state == AudioSessionStateExpired) continue;
                if (target.control->IsSystemSoundsSession() != S_FALSE) continue;
                // S_OK only: AUDCLNT_S_NO_SINGLE_PROCESS cannot be safely attributed.
                if (target.control->GetProcessId(&target.pid) != S_OK || !target.pid) continue;
                std::wstring image, aumid;
                if (!ProcessIdentity(target.pid, image, aumid)) continue;
                target.automatic = MatchesIdentity(source, image, aumid);
                target.label = image.substr(image.find_last_of(L"\\/") + 1) + L" (PID " + std::to_wstring(target.pid)
                    + L", output " + std::to_wstring(i+1) + L", session " + std::to_wstring(j+1) + L")";
                EndpointId instance;
                if (FAILED(target.control->GetSessionInstanceIdentifier(&instance.value)) || !instance.value
                    || FAILED(base->QueryInterface(__uuidof(ISimpleAudioVolume), target.volume.put_void()))) continue;
                Append(target.key, endpoint.value); Append(target.key, instance.value);
                targets.push_back(std::move(target));
            }
        }
        std::sort(targets.begin(), targets.end(), [](const Target& a, const Target& b) { return a.key < b.key; });
        {
            const auto current = media.Snapshot();
            if (selected != current.session || source != current.source) return false;
            std::lock_guard<std::mutex> lock(data.mutex);
            if (manualKey != data.manualKey) return false;
            data.choices.clear();
            for (const auto& target : targets) data.choices.push_back({target.key, target.label, target.key == manualKey});
        }
        targets.erase(std::remove_if(targets.begin(), targets.end(), [&](const Target& target) {
            return manualKey.empty() ? !target.automatic : target.key != manualKey;
        }), targets.end());
        if (targets.empty()) return false;
        std::wstring binding;
        for (const auto& target : targets) Append(binding, target.key);
        bool apply = false;
        float requested = 0.0f;
        {
            // Fetch a fresh media snapshot before taking the volume lock.
            const auto current = media.Snapshot();
            if (!running.load() || selected != current.session || source != current.source) return false;
            std::lock_guard<std::mutex> lock(data.mutex);
            if (manualKey != data.manualKey) return false;
            apply = data.pending && data.sourceSession == selected && data.sourceId == source
                && data.commandId == binding && data.endpointId == binding;
            requested = data.commandLevel;
            data.pending = false; data.commandId.clear();
        }
        // Core Audio and process queries can block: neither shared mutex is held.
        for (const auto& target : targets)
        {
            DWORD pid = 0; AudioSessionState state;
            if (target.control->GetProcessId(&pid) != S_OK || pid != target.pid
                || FAILED(target.control->GetState(&state)) || state == AudioSessionStateExpired
                || (manualKey.empty() && !Matches(pid, source))) return false;
        }
        float sum = 0; bool allMuted = true;
        for (const auto& target : targets)
        {
            // Application-level only: browser sessions may include other tabs/windows.
            // Never SetMute; changing volume preserves each session's mute setting.
            if (apply)
            {
                {
                    const auto current = media.Snapshot();
                    if (!running.load() || selected != current.session || source != current.source) return false;
                    std::lock_guard<std::mutex> lock(data.mutex);
                    if (manualKey != data.manualKey) return false;
                }
                // Selection may change after this check. The captured COM target
                // still belongs to the original binding, never the new source.
                if (FAILED(target.volume->SetMasterVolume(requested, nullptr))) return false;
            }
            float level = 0; BOOL muted = FALSE;
            if (FAILED(target.volume->GetMasterVolume(&level)) || !std::isfinite(level)
                || FAILED(target.volume->GetMute(&muted))) return false;
            sum += (std::max)(0.0f, (std::min)(1.0f, level));
            allMuted = allMuted && muted != FALSE;
        }
        const auto current = media.Snapshot();
        if (!running.load() || selected != current.session || source != current.source) return false;
        std::lock_guard<std::mutex> lock(data.mutex);
        if (manualKey != data.manualKey) return false;
        // A command queued during COM work must not survive a changed target set.
        if (data.sourceSession != selected || data.sourceId != source || data.endpointId != binding)
        { data.pending = false; data.commandId.clear(); }
        data.sourceSession = selected; data.sourceId = source; data.endpointId = std::move(binding);
        // Mixed sessions show mean volume; muted means all matched sessions muted.
        data.snapshot = {true, sum / static_cast<float>(targets.size()), allMuted};
        return true;
    }
}

// Choices are individual non-system application sessions. Automatic selection
// may cover multiple sessions of the same app. Neither mode is tab-specific.
static std::vector<VolumeChoice> VolumeChoices(volume_detail::Shared& data, MediaSession& media)
{
    const auto current = media.Snapshot();
    std::lock_guard<std::mutex> lock(data.mutex);
    if (data.manualSession != current.session || data.manualSource != current.source) return {};
    return data.choices;
}

static void CycleVolumeSource(volume_detail::Shared& data, MediaSession& media)
{
    const auto current = media.Snapshot();
    std::lock_guard<std::mutex> lock(data.mutex);
    if (!current.session || data.manualSession != current.session || data.manualSource != current.source) return;
    auto next = data.choices.begin();
    if (!data.manualKey.empty()) {
        next = std::find_if(data.choices.begin(), data.choices.end(), [&](const VolumeChoice& c) { return c.id == data.manualKey; });
        if (next != data.choices.end()) ++next;
    }
    data.manualKey = next == data.choices.end() ? L"" : next->id;
    for (auto& c : data.choices) c.selected = c.id == data.manualKey;
    data.snapshot = {}; data.pending = false; data.commandId.clear(); data.endpointId.clear();
}

static std::wstring VolumeSourceLabel(volume_detail::Shared& data, MediaSession& media)
{
    const auto current = media.Snapshot();
    std::lock_guard<std::mutex> lock(data.mutex);
    if (data.manualSession != current.session || data.manualSource != current.source || data.manualKey.empty())
        return L"Auto: current media app (not individual tab)";
    for (const auto& c : data.choices) if (c.id == data.manualKey) return L"Manual: " + c.label;
    return L"Manual session unavailable (cycle to reselect)";
}

static VolumeState VolumeSnapshot(volume_detail::Shared& data, MediaSession& media)
{
    const auto current = media.Snapshot();
    std::lock_guard<std::mutex> lock(data.mutex);
    if (current.session != data.sourceSession || current.source != data.sourceId)
    { data.snapshot = {}; data.pending = false; data.commandId.clear(); data.endpointId.clear(); }
    return data.snapshot;
}

static void QueueVolume(float level, volume_detail::Shared& data, MediaSession& media, const std::atomic<bool>& running)
{
    if (!std::isfinite(level)) return;
    level = level < 0.0f ? 0.0f : (level > 1.0f ? 1.0f : level);
    const auto current = media.Snapshot();
    std::lock_guard<std::mutex> lock(data.mutex);
    if (current.session != data.sourceSession || current.source != data.sourceId)
    { data.snapshot = {}; data.pending = false; data.commandId.clear(); data.endpointId.clear(); }
    if (!data.snapshot.available || !current.session || current.source.empty() || !running.load()) return;
    try
    {
        data.commandId = data.endpointId;
        data.commandLevel = level;
        data.pending = true; // Last queued scalar wins.
    }
    catch (...)
    {
        data.pending = false;
        data.commandId.clear();
        data.endpointId.clear();
        data.snapshot = {};
    }
}

VolumeController::VolumeController(Logger* logger, MediaSession* mediaSession)
    : logger_(logger), mediaSession_(mediaSession), data_(std::make_unique<volume_detail::Shared>()) {}
VolumeController::~VolumeController() { Stop(); }
void VolumeController::Start() {
    std::lock_guard<std::mutex> lock(lifecycleMutex_);
    if (running_.load() || !mediaSession_) return;
    running_.store(true);
    try { workerThread_ = std::thread(&VolumeController::Worker, this); }
    catch (...) { running_.store(false); throw; }
}
void VolumeController::Worker() {
    // Poll owns all Core Audio interfaces; release before apartment teardown.
    while (running_.load()) {
        volume_detail::Apartment apartment;
        if (SUCCEEDED(apartment.result)) {
            while (running_.load()) {
                try {
                    if (!volume_detail::Poll(*data_, *mediaSession_, running_)) volume_detail::Disable(*data_);
                } catch (...) { volume_detail::Disable(*data_); }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        } else {
            volume_detail::Disable(*data_);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    volume_detail::Disable(*data_);
}
void VolumeController::Stop() {
    std::lock_guard<std::mutex> lifecycle(lifecycleMutex_);
    running_.store(false);
    if (workerThread_.joinable()) workerThread_.join();
    volume_detail::Disable(*data_);
    std::lock_guard<std::mutex> lock(data_->mutex);
    data_->choices.clear(); data_->manualKey.clear(); data_->manualSource.clear(); data_->manualSession = nullptr;
}
VolumeState VolumeController::Snapshot() const {
    return mediaSession_ ? VolumeSnapshot(*data_, *mediaSession_) : VolumeState{};
}
std::wstring VolumeController::SourceLabel() const {
    return mediaSession_ ? VolumeSourceLabel(*data_, *mediaSession_) : L"Auto: current media app";
}
std::vector<VolumeChoice> VolumeController::Choices() const {
    return mediaSession_ ? VolumeChoices(*data_, *mediaSession_) : std::vector<VolumeChoice>{};
}
void VolumeController::SetLevel(float level) {
    if (mediaSession_) QueueVolume(level, *data_, *mediaSession_, running_);
}
void VolumeController::CycleManualSource() {
    if (mediaSession_ && running_.load()) CycleVolumeSource(*data_, *mediaSession_);
}
void VolumeController::SetManualSource(const std::wstring& key) {
    if (!mediaSession_ || !running_.load()) return;
    const auto current = mediaSession_->Snapshot();
    auto& data = *data_;
    std::lock_guard<std::mutex> lock(data.mutex);
    if (!current.session || data.manualSession != current.session || data.manualSource != current.source) return;
    if (!key.empty() && std::none_of(data.choices.begin(), data.choices.end(),
        [&](const VolumeChoice& choice) { return choice.id == key; })) return;
    data.manualKey = key;
    for (auto& choice : data.choices) choice.selected = choice.id == key;
    data.snapshot = {}; data.pending = false; data.commandId.clear(); data.endpointId.clear();
}
} // namespace media
