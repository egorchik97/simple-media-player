#include "MediaSession.h"

#include "../core/AppContext.h"
#include "../core/Types.h"
#include "../core/MediaDiagnostics.h"
#include "SessionSelection.h"
#include <winrt/Windows.Foundation.Collections.h>

#include <chrono>
#include <thread>

#pragma comment(lib, "windowsapp.lib")

namespace media {

using namespace winrt::Windows::Media::Control;

namespace {

std::shared_ptr<Cover> LoadCover(
    const GlobalSystemMediaTransportControlsSessionMediaProperties& properties,
    const std::atomic<bool>& stop) {
    using namespace winrt::Windows::Graphics::Imaging;
    auto thumbnail = properties.Thumbnail();
    if (!thumbnail) return {};
    auto stream = MediaDiagnostics::Wait(thumbnail.OpenReadAsync(),
        std::chrono::milliseconds(1500), stop, "cover.open");
    auto decoder = MediaDiagnostics::Wait(BitmapDecoder::CreateAsync(stream),
        std::chrono::milliseconds(1500), stop, "cover.decode");
    if (!decoder.PixelWidth() || !decoder.PixelHeight()) return {};
    auto cover = std::make_shared<Cover>();
    double scale = 160.0 / std::max(decoder.PixelWidth(), decoder.PixelHeight());
    cover->width = std::max(1u, static_cast<uint32_t>(decoder.PixelWidth() * scale));
    cover->height = std::max(1u, static_cast<uint32_t>(decoder.PixelHeight() * scale));
    BitmapTransform transform;
    transform.ScaledWidth(cover->width);
    transform.ScaledHeight(cover->height);
    cover->pixels = MediaDiagnostics::Wait(decoder.GetPixelDataAsync(
        BitmapPixelFormat::Bgra8, BitmapAlphaMode::Ignore,
        transform, ExifOrientationMode::IgnoreExifOrientation,
        ColorManagementMode::DoNotColorManage),
        std::chrono::milliseconds(1500), stop, "cover.pixels").DetachPixelData();
    return cover;
}

} // namespace

MediaSession::MediaSession(Logger* logger) : logger_(logger) {}

MediaSession::~MediaSession() {
    Stop();
}

void MediaSession::Start() {
    if (running_.exchange(true)) return;
    metadataStop_.store(false);

    workerThread_ = std::thread([this] {
        try {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
            struct Apartment { ~Apartment() { winrt::uninit_apartment(); } } apartment;

            struct MetadataJoin {
                std::atomic<bool>& stop;
                std::thread& thread;
                ~MetadataJoin() {
                    stop.store(true);
                    if (thread.joinable()) thread.join();
                }
            } join{metadataStop_, metadataThread_};

            metadataThread_ = std::thread([this] {
                try {
                    winrt::init_apartment(winrt::apartment_type::multi_threaded);
                    Apartment apartment;
                    while (!metadataStop_.load() && !running_.load()) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(25));
                    }
                    while (!metadataStop_.load()) {
                        Session session{nullptr};
                        uint64_t version = 0;
                        {
                            std::lock_guard<std::mutex> lock(mutex_);
                            if (pending_) {
                                session = requested_;
                                version = generation_;
                                pending_ = false;
                            }
                        }
                        if (!session) {
                            std::this_thread::sleep_for(std::chrono::milliseconds(25));
                            continue;
                        }
                        const char* stage = "metadata.properties";
                        try {
                            auto properties = MediaDiagnostics::Wait(
                                session.TryGetMediaPropertiesAsync(),
                                std::chrono::milliseconds(1500), metadataStop_, stage);
                            if (metadataStop_.load() || !running_.load()) break;

                            std::wstring title = properties.Title().c_str();
                            if (title.empty()) title = L"\u0411\u0435\u0437 \u043d\u0430\u0437\u0432\u0430\u043d\u0438\u044f";
                            std::wstring artist = properties.Artist().c_str();
                            stage = "metadata.source";
                            std::wstring source = session.SourceAppUserModelId().c_str();

                            {
                                std::lock_guard<std::mutex> lock(mutex_);
                                if (version != generation_ || state_.session != session) continue;
                                if (state_.title != title || state_.artist != artist || state_.source != source) {
                                    state_.cover.reset();
                                }
                                state_.title = std::move(title);
                                state_.artist = std::move(artist);
                                state_.source = std::move(source);
                            }
                            stage = "metadata.cover";
                            auto cover = LoadCover(properties, metadataStop_);
                            if (metadataStop_.load() || !running_.load()) break;
                            {
                                std::lock_guard<std::mutex> lock(mutex_);
                                if (version == generation_ && state_.session == session) {
                                    state_.cover = std::move(cover);
                                }
                            }
                        } catch (const winrt::hresult_error& e) {
                            MediaDiagnostics::Log(stage, e.code());
                        } catch (...) {
                            MediaDiagnostics::Log(stage, E_FAIL);
                        }
                    }
                } catch (const winrt::hresult_error& e) {
                    MediaDiagnostics::Log("metadata.apartment", e.code());
                } catch (...) {
                    MediaDiagnostics::Log("metadata.thread", E_FAIL);
                }
            });

            GlobalSystemMediaTransportControlsSessionManager manager{nullptr};
            auto changed = std::make_shared<std::atomic<uint64_t>>(0);
            uint64_t observed = 0;
            GlobalSystemMediaTransportControlsSessionManager::SessionsChanged_revoker sessionsChanged;
            GlobalSystemMediaTransportControlsSessionManager::CurrentSessionChanged_revoker currentChanged;
            Session watched{nullptr};
            Session::MediaPropertiesChanged_revoker propertiesChanged;
            Session::PlaybackInfoChanged_revoker playbackChanged;
            Session::TimelinePropertiesChanged_revoker timelineChanged;
            int lastCount = -1;
            auto pause = [this](std::chrono::milliseconds delay) {
                auto until = std::chrono::steady_clock::now() + delay;
                while (running_.load() && std::chrono::steady_clock::now() < until) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(25));
                }
            };
            std::wstring error;
            auto errorUntil = std::chrono::steady_clock::now();
            metadataDue_ = std::chrono::steady_clock::now();
            int retryMs = 250;

            while (running_) {
                try {
                    if (!manager) {
                        try {
                            manager = MediaDiagnostics::Wait(GlobalSystemMediaTransportControlsSessionManager::RequestAsync(),
                                std::chrono::milliseconds(3000), metadataStop_, "manager.request");
                            sessionsChanged = manager.SessionsChanged(winrt::auto_revoke,
                                [changed](auto const&, auto const&) { ++*changed; });
                            currentChanged = manager.CurrentSessionChanged(winrt::auto_revoke,
                                [changed](auto const&, auto const&) { ++*changed; });
                            retryMs = 250;
                        } catch (...) {
                            MediaDiagnostics::Log("manager.retry", winrt::to_hresult());
                            pause(std::chrono::milliseconds(retryMs));
                            retryMs = std::min(retryMs * 2, 5000);
                            continue;
                        }
                    }
                    MediaCommand command;
                    {
                        std::lock_guard<std::mutex> lock(mutex_);
                        command = command_;
                        command_ = {};
                    }

                    if (command.action) {
                        bool ok = false;
                        try {
                            if (!command.session) winrt::throw_hresult(E_POINTER);
                            if (command.action == 1) {
                                ok = MediaDiagnostics::Wait(
                                    command.session.TrySkipPreviousAsync(),
                                    std::chrono::milliseconds(1500), metadataStop_, "command.previous");
                            }
                            if (command.action == 2) {
                                ok = MediaDiagnostics::Wait(
                                    command.session.TryTogglePlayPauseAsync(),
                                    std::chrono::milliseconds(1500), metadataStop_, "command.toggle");
                            }
                            if (command.action == 3) {
                                ok = MediaDiagnostics::Wait(
                                    command.session.TrySkipNextAsync(),
                                    std::chrono::milliseconds(1500), metadataStop_, "command.next");
                            }
                            if (command.action == 4) {
                                ok = MediaDiagnostics::Wait(command.session.TryChangePlaybackPositionAsync(command.position),
                                    std::chrono::milliseconds(1500), metadataStop_, "command.seek");
                            }
                            if (!ok) winrt::throw_hresult(E_FAIL);
                        } catch (const winrt::hresult_error& e) {
                            MediaDiagnostics::Log("session.controls", e.code());
                            error = L"Media control failed: " + std::wstring(e.message());
                            errorUntil = std::chrono::steady_clock::now() + std::chrono::seconds(3);
                            std::lock_guard<std::mutex> lock(mutex_);
                            state_.error = L"\u041c\u0435\u0434\u0438\u0430\u0441\u0435\u0441\u0441\u0438\u044f \u043d\u0435\u0434\u043e\u0441\u0442\u0443\u043f\u0435\u043d\u0430. \u041e\u0442\u043a\u0440\u043e\u044f\u0442\u0435 \u043f\u043b\u0435\u044f\u0440.";
                        } catch (...) {
                            MediaDiagnostics::Log("session.controls", E_FAIL);
                            error = L"Media control failed";
                            errorUntil = std::chrono::steady_clock::now() + std::chrono::seconds(3);
                            std::lock_guard<std::mutex> lock(mutex_);
                            state_.error = L"\u041c\u0435\u0434\u0438\u0430\u0441\u0435\u0441\u0441\u0438\u044f \u043d\u0435\u0434\u043e\u0441\u0442\u0443\u043f\u0435\u043d\u0430. \u041e\u0442\u043a\u0440\u043e\u044f\u0442\u0435 \u043f\u043b\u0435\u044f\u0440.";
                        }
                        std::lock_guard<std::mutex> lock(mutex_);
                        busy_ = false;
                    }

                    Session current{nullptr};
                    auto recommended = manager.GetCurrentSession();
                    std::vector<Session> available;
                    std::vector<SessionCandidate> candidates;
                    for (auto const& session : manager.GetSessions()) {
                        try {
                            auto playing = session.GetPlaybackInfo().PlaybackStatus() ==
                                GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing;
                            available.push_back(session);
                            candidates.push_back({playing, session == recommended, session == watched});
                        } catch (...) { MediaDiagnostics::Log("session.disappeared", winrt::to_hresult()); }
                    }
                    if (lastCount != static_cast<int>(available.size())) {
                        lastCount = static_cast<int>(available.size());
                        MediaDiagnostics::Log("manager.sessions", S_OK, lastCount);
                    }
                    auto index = SelectSession(candidates);
                    if (index < available.size()) current = available[index];
                    if (watched != current) {
                        propertiesChanged.revoke(); playbackChanged.revoke(); timelineChanged.revoke();
                        watched = current;
                        ++*changed;
                        MediaDiagnostics::Log(current ? "selection.changed" : "selection.empty", S_OK, lastCount);
                        if (current) {
                            propertiesChanged = current.MediaPropertiesChanged(winrt::auto_revoke,
                                [changed](auto const&, auto const&) { ++*changed; });
                            playbackChanged = current.PlaybackInfoChanged(winrt::auto_revoke,
                                [](auto const&, auto const&) { MediaDiagnostics::Log("event.playback"); });
                            timelineChanged = current.TimelinePropertiesChanged(winrt::auto_revoke,
                                [](auto const&, auto const&) { MediaDiagnostics::Log("event.timeline"); });
                        }
                    }

                    MediaState s;
                    if (current) {
                        try {
                            auto info = current.GetPlaybackInfo();
                            auto status = info.PlaybackStatus();
                            s.playing = status == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing;
                            auto controls = info.Controls();
                            s.previous = controls.IsPreviousEnabled();
                            s.toggle = controls.IsPlayPauseToggleEnabled();
                            s.next = controls.IsNextEnabled();
                            s.seek = controls.IsPlaybackPositionEnabled();

                            auto timeline = current.GetTimelineProperties();
                            s.start = timeline.StartTime().count();
                            s.end = timeline.EndTime().count();
                            s.position = timeline.Position().count();
                            s.minimum = timeline.MinSeekTime().count();
                            s.maximum = timeline.MaxSeekTime().count();
                            s.source = current.SourceAppUserModelId().c_str();

                            s.session = current;
                        } catch (...) {
                            s.error = L"\u041c\u0435\u0434\u0438\u0430\u0441\u0435\u0441\u0441\u0438\u044f \u043d\u0435\u0434\u043e\u0441\u0442\u0443\u043f\u0435\u043d\u0430. \u041e\u0442\u043a\u0440\u043e\u044f\u0442\u0435 \u043f\u043b\u0435\u044f\u0440.";
                        }
                    } else {
                        s.error = L"\u041c\u0435\u0434\u0438\u0430\u0441\u0435\u0441\u0441\u0438\u044f \u043d\u0435\u0434\u043e\u0441\u0442\u0443\u043f\u0435\u043d\u0430. \u041e\u0442\u043a\u0440\u043e\u044f\u0442\u0435 \u043f\u043b\u0435\u044f\u0440.";
                    }

                    if (std::chrono::steady_clock::now() < errorUntil) {
                        s.error = error;
                    }

                    {
                        std::lock_guard<std::mutex> lock(mutex_);
                        const auto revision = changed->load();
                        if (revision != observed) {
                            observed = revision;
                            ++generation_;
                            metadataDue_ = std::chrono::steady_clock::now();
                        }
                        if (s.session != requested_) {
                            ++generation_;
                            requested_ = s.session;
                            pending_ = false;
                            metadataDue_ = std::chrono::steady_clock::now();
                        }
                        if (s.session && s.session == state_.session) {
                            s.title = state_.title;
                            s.artist = state_.artist;
                            s.source = state_.source;
                            s.cover = state_.cover;
                        }
                        state_ = std::move(s);

                        if (requested_ && std::chrono::steady_clock::now() >= metadataDue_) {
                            pending_ = true;
                            metadataDue_ = std::chrono::steady_clock::now() + std::chrono::seconds(1);
                        }
                    }
                } catch (const winrt::hresult_error& e) {
                    MediaDiagnostics::Log("worker.poll", e.code());
                    std::lock_guard<std::mutex> lock(mutex_);
                    ++generation_;
                    requested_ = nullptr;
                    pending_ = false;
                    state_ = {};
                    state_.error = L"\u041c\u0435\u0434\u0438\u0430\u0441\u0435\u0441\u0441\u0438\u044f \u043d\u0435\u0434\u043e\u0441\u0442\u0443\u043f\u0435\u043d\u0430. \u041e\u0442\u043a\u0440\u043e\u044f\u0442\u0435 \u043f\u043b\u0435\u044f\u0440.";
                    busy_ = false;
                } catch (...) {
                    MediaDiagnostics::Log("worker.poll", E_FAIL);
                    std::lock_guard<std::mutex> lock(mutex_);
                    ++generation_;
                    requested_ = nullptr;
                    pending_ = false;
                    state_ = {};
                    state_.error = L"\u041c\u0435\u0434\u0438\u0430\u0441\u0435\u0441\u0441\u0438\u044f \u043d\u0435\u0434\u043e\u0441\u0442\u0443\u043f\u0435\u043d\u0430. \u041e\u0442\u043a\u0440\u043e\u044f\u0442\u0435 \u043f\u043b\u0435\u044f\u0440.";
                    busy_ = false;
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(150));
            }

            busy_ = false;
        } catch (const winrt::hresult_error& e) {
            MediaDiagnostics::Log("worker", e.code());
            std::lock_guard<std::mutex> lock(mutex_);
            state_.error = L"\u041e\u0448\u0438\u0431\u043a\u0430 Windows Media: " + std::wstring(e.message());
            busy_ = false;
        } catch (...) {
            MediaDiagnostics::Log("worker", E_FAIL);
            std::lock_guard<std::mutex> lock(mutex_);
            state_.error = L"\u041c\u0435\u0434\u0438\u0430\u0441\u0435\u0441\u0441\u0438\u044f \u043d\u0435\u0434\u043e\u0441\u0442\u0443\u043f\u0435\u043d\u0430. \u041e\u0442\u043a\u0440\u043e\u044f\u0442\u0435 \u043f\u043b\u0435\u044f\u0440.";
            busy_ = false;
        }
    });
}

void MediaSession::Stop() {
    if (!running_.exchange(false)) return;
    metadataStop_.store(true);
    if (workerThread_.joinable()) workerThread_.join();
    if (metadataThread_.joinable()) metadataThread_.join();
}

MediaState MediaSession::Snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

void MediaSession::QueueCommand(int action) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!running_ || !state_.session || action < 1 || action > 3) return;
    if (busy_.exchange(true)) return;
    command_ = {action, 0, state_.session};
}

void MediaSession::QueueSeek(int64_t position) {
    QueueSeek(position, Snapshot().session);
}

void MediaSession::QueueSeek(int64_t position, const Session& target) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!running_ || !state_.session || state_.session != target || !state_.seek) return;
    const auto minimum = std::max(state_.start, state_.minimum);
    const auto maximum = state_.maximum > minimum ? state_.maximum : state_.end;
    if (maximum <= minimum) return;
    position = std::clamp(position, minimum, maximum);
    if (busy_.exchange(true)) return;
    command_ = {4, position, state_.session};
}

} // namespace media
