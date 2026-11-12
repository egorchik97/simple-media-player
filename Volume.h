#pragma once
#include <windows.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <appmodel.h>
#include <vector>
#include <algorithm>
#include <winrt/base.h>
#include <atomic>
#include <mutex>
#include <thread>
#include <chrono>
#include <string>
#include <cmath>
#include <cwctype>
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")

// Include after g_state, g_mutex and g_stop. UI and worker must use the same translation unit.
struct VolumeState
{
    bool available = false;
    float level = 0.0f;
    bool muted = false;
};

struct VolumeChoice
{
    std::wstring id, label;
    bool selected = false;
};

namespace volume_detail
{
    struct Shared
    {
        std::mutex mutex;
        VolumeState snapshot;
        std::wstring endpointId, commandId, sourceId;
        std::vector<VolumeChoice> choices;
        std::wstring manualKey, manualSource;
        decltype(g_state.session) manualSession{nullptr};
        decltype(g_state.session) sourceSession{nullptr};
        float commandLevel = 0.0f;
        bool pending = false;
        std::atomic<bool> running{false};
    };
    static Shared& Data() { static Shared data; return data; }
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
    };
    struct RunningGuard
    {
        Shared& shared;
        ~RunningGuard() { shared.running.store(false); }
    };
    static void Disable()
    {
        auto& data = Data();
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
    static std::wstring Lower(const std::wstring& s) { std::wstring r=s; for (wchar_t& c : r) c = (wchar_t)towlower(c); return r; }
    // Case-insensitive substring fallback: still source-specific, never system volume.
    static bool IContains(const std::wstring& hay, const std::wstring& needle) {
        if (needle.empty()) return false;
        return Lower(hay).find(Lower(needle)) != std::wstring::npos;
    }
    static bool MatchesIdentity(const std::wstring& source, const std::wstring& image, const std::wstring& aumid = {})
    {
        if (Equal(source, aumid) || Equal(source, image)) return true;
        const auto exe = image.substr(image.find_last_of(L"\\/") + 1);
        if (source.find_first_of(L"\\/!") == std::wstring::npos && Equal(source, exe)) return true;
        struct Alias { const wchar_t* id; const wchar_t* exe; };
        static constexpr Alias aliases[] = {
            {L"Chrome", L"chrome.exe"}, {L"Google Chrome", L"chrome.exe"},
            {L"Microsoft Edge", L"msedge.exe"}, {L"MSEdge", L"msedge.exe"},
            {L"Yandex", L"browser.exe"}, {L"YandexBrowser", L"browser.exe"},
            {L"Yandex.Browser", L"browser.exe"}, {L"Brave", L"brave.exe"},
            {L"Opera", L"opera.exe"}, {L"Opera GX", L"opera.exe"},
            {L"Vivaldi", L"vivaldi.exe"}, {L"Firefox", L"firefox.exe"}
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
        // Lenient fallback: many GSMTC sources (e.g. "Chrome.Shell", "Yandex.Browser") differ from the
        // running process AUMID/exe. Match by case-insensitive containment both directions so the source
        // volume actually controls the playing app instead of staying unavailable.
        if (!source.empty()) {
            if (!aumid.empty() && (IContains(aumid, source) || IContains(source, aumid))) return true;
            if (!image.empty() && (IContains(image, source) || IContains(source, image))) return true;
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
    static bool Poll()
    {
        decltype(g_state.session) selected{nullptr};
        std::wstring source;
        std::wstring manualKey;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            selected = g_state.session; source = g_state.source;
            auto& data = Data();
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
            std::lock_guard<std::mutex> mediaLock(g_mutex);
            if (selected != g_state.session || source != g_state.source) return false;
            auto& data = Data();
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
        auto& data = Data();
        bool apply = false;
        float requested = 0.0f;
        {
            // Only snapshot/consume shared state under locks, always media -> volume.
            std::lock_guard<std::mutex> mediaLock(g_mutex);
            if (g_stop.load() || selected != g_state.session || source != g_state.source) return false;
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
                    std::lock_guard<std::mutex> mediaLock(g_mutex);
                    if (g_stop.load() || selected != g_state.session || source != g_state.source) return false;
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
        std::lock_guard<std::mutex> mediaLock(g_mutex);
        if (g_stop.load() || selected != g_state.session || source != g_state.source) return false;
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
static std::vector<VolumeChoice> VolumeChoices()
{
    std::lock_guard<std::mutex> mediaLock(g_mutex);
    auto& data = volume_detail::Data();
    std::lock_guard<std::mutex> lock(data.mutex);
    if (data.manualSession != g_state.session || data.manualSource != g_state.source) return {};
    return data.choices;
}

static void CycleVolumeSource()
{
    std::lock_guard<std::mutex> mediaLock(g_mutex);
    auto& data = volume_detail::Data();
    std::lock_guard<std::mutex> lock(data.mutex);
    if (!g_state.session || data.manualSession != g_state.session || data.manualSource != g_state.source) return;
    auto next = data.choices.begin();
    if (!data.manualKey.empty()) {
        next = std::find_if(data.choices.begin(), data.choices.end(), [&](const VolumeChoice& c) { return c.id == data.manualKey; });
        if (next != data.choices.end()) ++next;
    }
    data.manualKey = next == data.choices.end() ? L"" : next->id;
    for (auto& c : data.choices) c.selected = c.id == data.manualKey;
    data.snapshot = {}; data.pending = false; data.commandId.clear(); data.endpointId.clear();
}

static std::wstring VolumeSourceLabel()
{
    std::lock_guard<std::mutex> mediaLock(g_mutex);
    auto& data = volume_detail::Data();
    std::lock_guard<std::mutex> lock(data.mutex);
    if (data.manualSession != g_state.session || data.manualSource != g_state.source || data.manualKey.empty())
        return L"Auto: current media app (not individual tab)";
    for (const auto& c : data.choices) if (c.id == data.manualKey) return L"Manual: " + c.label;
    return L"Manual session unavailable (cycle to reselect)";
}

static VolumeState VolumeSnapshot()
{
    std::lock_guard<std::mutex> mediaLock(g_mutex);
    auto& data = volume_detail::Data();
    std::lock_guard<std::mutex> lock(data.mutex);
    if (g_state.session != data.sourceSession || g_state.source != data.sourceId)
    { data.snapshot = {}; data.pending = false; data.commandId.clear(); data.endpointId.clear(); }
    return data.snapshot;
}

static void QueueVolume(float level)
{
    if (!std::isfinite(level)) return;
    level = level < 0.0f ? 0.0f : (level > 1.0f ? 1.0f : level);
    std::lock_guard<std::mutex> mediaLock(g_mutex);
    auto& data = volume_detail::Data();
    std::lock_guard<std::mutex> lock(data.mutex);
    if (g_state.session != data.sourceSession || g_state.source != data.sourceId)
    { data.snapshot = {}; data.pending = false; data.commandId.clear(); data.endpointId.clear(); }
    if (!data.snapshot.available || !g_state.session || g_state.source.empty() || g_stop.load()) return;
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

// The owner must join this worker before application/static-data teardown.
static void VolumeWorker()
{
    auto& data = volume_detail::Data();
    if (data.running.exchange(true)) return;
    volume_detail::RunningGuard running{data};
    volume_detail::Disable();
    while (!g_stop.load())
    {
        volume_detail::Apartment apartment;
        if (SUCCEEDED(apartment.result))
        {
            while (!g_stop.load())
            {
                try
                {
                    if (!volume_detail::Poll()) volume_detail::Disable();
                }
                catch (...)
                {
                    volume_detail::Disable();
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
        else
        {
            volume_detail::Disable();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    volume_detail::Disable();
}

