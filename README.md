# Simple Media Player

Windows popup for the current media session. It controls playback and the source application's volume, shows LRCLIB or local Whisper lyrics, and draws a small themed card with glass patterns and optional snowfall.

The window is a borderless Win32 layered popup. It does not change the Windows master volume.

[Releases](https://github.com/egorchik97/simple-media-player/releases) · [Issues](https://github.com/egorchik97/simple-media-player/issues)

## Features

- Reads the active Windows media session (GSMTC): title, artist, cover, playback and seek.
- Previous / play-pause / next, seek bar, and source volume through WASAPI.
- If the source cannot be matched, volume shows "Недоступно". There is no system-volume fallback.
- Themes, custom colors, glow, rounded corners, eight glass patterns (including Legacy), and snowfall.
- Lyrics from the LRCLIB catalog, or local Whisper 1.8.3 recognition. Audio is not uploaded.
- Settings are stored in `%LOCALAPPDATA%\SimpleMediaPlayer\settings.ini`.
- An existing `%LOCALAPPDATA%\MediaPopup\settings.ini` is copied once when the new file does not exist.
- Multiple instances are allowed. Launch with `--no-asr` to skip recognition for that run.

## Requirements

- Windows 10 1809 or newer, 64-bit.
- For a release build: the Visual C++ 2015–2022 x64 runtime, if it is not already installed. The release notes say whether those DLLs are bundled.
- To build: Visual Studio 2022 or 2026 with the C++ desktop workload, Windows SDK, and CMake.
- Whisper is optional. Without `runtime/whisper` and `runtime/models/ggml-base.bin` next to the executable, everything except local recognition still works.

## Run a release build

1. Download the Windows x64 zip from [Releases](https://github.com/egorchik97/simple-media-player/releases).
2. Unzip it to any folder.
3. Run `SimpleMediaPlayer.exe`.

Open `•••` for settings. Playback must already be running in a player that publishes a Windows media session.

## Build from source

From the repository root, with the VS developer environment available to CMake:

```powershell
cmake -S . -B ..\simple-media-player-build -G "Visual Studio 18 2026" -A x64
cmake --build ..\simple-media-player-build --config Release --parallel
ctest --test-dir ..\simple-media-player-build -C Release --output-on-failure
..\simple-media-player-build\Release\SimpleMediaPlayer.exe
```

Use an out-of-source build directory. `scripts/build.ps1` wraps the same configure, build, and test steps. Visual Studio 2022 generators also work if CMake can find them.

Optional Whisper install:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\setup-runtime.ps1
```

CMake copies an installed `runtime/whisper`, `runtime/models`, and `runtime/licenses` next to the executable. Set `-DSMP_DEPLOY_RUNTIME=OFF` to skip that copy.

## Layout

```
src/entrypoint.cpp    WinMain
src/core/             Application, types, diagnostics
src/config/           settings.ini load/save
src/media/            GSMTC session and WASAPI volume
src/audio/            LRCLIB and Whisper
src/ui/               window, layout, appearance, settings
tests/                CTest targets
runtime/              optional Whisper files, not required to compile
```

## Limitations

- Windows only. No installer.
- Glass and snowfall are drawn in the window. They are not desktop blur or acrylic.
- Browser volume is the whole browser process, not one tab.
- Whisper hears the default playback device, not a single application, and requires the bundled 1.8.3 ABI.
- LRCLIB needs network access and matching catalog metadata. GSMTC does not provide lyrics itself.
- Automated tests do not replace a manual check of real audio, lyrics, and the on-screen card.

## License

MIT. See [LICENSE](LICENSE).

Optional Whisper files keep their own notices under `runtime/licenses` (whisper.cpp and the OpenAI Whisper model license).
