# Architecture

Application owns AppContext's config, MediaSession, VolumeController, LyricsEngine, AsrEngine and Logger, plus PopupWindow. Shutdown destroys the HWND and stops workers; worker destructors join. No single-instance lock is used.

## Media path

MediaSession requests the GSMTC manager on an MTA worker with bounded waits and retry backoff. GetSessions enumeration selects playing sessions before paused sessions, then the Windows current session and previous selection as tie-breakers. Manager changes and selected-session media changes invalidate metadata generations. Playback/timeline events are logged; 150ms polling provides recovery when producers miss events. A separate MTA metadata worker publishes title/artist first, then decoded BGRA artwork, checking session/generation before publication. UI reads mutex-protected snapshots on its timer and invalidates the GDI surface. Playback and seek commands retain their target session; seek rejects a stale drag target.

## UI

WM_NCCREATE attaches the object before WM_CREATE. Layout calculates compact/expanded/settings bounds and hit regions; PopupWindow applies size/appearance, renders supersampled GDI, handles hover/drag/transport/seek/volume and dispatches complete settings pages to SettingsUI. Appearance owns color, layered-window and glass helpers. UI tracks metadata/playback changes to start/stop ASR and request/clear lyrics. Configuration retains the original INI format.

## Audio

VolumeController owns per-instance synchronized state and a COM worker. Every poll enumerates active render endpoints and audio sessions, matches exact process/AUMID identities or explicit browser aliases, and revalidates media/source bindings before writes. Manual selection is supported. Browser tabs are not individually identifiable through GSMTC. No master-volume fallback.

AsrEngine has a lifecycle worker and loopback capture worker, bounded audio history, resampling, overlapping inference and generation cancellation. StreamingWhisper dynamically loads the matching official runtime, owns model/context and abort callbacks. VCOMP140 is pinned until process exit after a reproduced unload-time crash; model/context ownership remains RAII. Device-format support is float32/PCM16; endpoint changes require ASR restart. Model loading itself cannot be cancelled.

LyricsEngine uses WinHTTP HTTPS with bounded response size/timeouts, UTF-8 URL encoding, typed JSON and LRC parsing. Latest request wins and Clear invalidates pending results. HTTP cancellation is cooperative between calls, not a hard deadline.

## Build and tests

CMake explicitly lists modular translation units, preserves WIN32 subsystem and links Windows SDK libraries. Optional installed runtime is copied beside the executable. CTest covers selection, audio conversion/resampling/error lifecycle, lyrics parsing and layout/hover. Runtime probes are separate opt-in executables; they do not assert live service availability as a deterministic unit test.
