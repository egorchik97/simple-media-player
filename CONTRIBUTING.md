# Contributing

## Build

1. Clone the repository.
2. Install Visual Studio 2022 or 2026 with the C++ desktop workload, the Windows SDK, and CMake.
3. From the repository root:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1
```

The script configures an out-of-source build, compiles Release, and runs CTest. Use `-Clean` to rebuild from scratch.

## Tests

CTest targets:

- `ConfigPersistence` — settings round-trip, migration, and failed saves
- `SessionSelection` — which media session is chosen
- `UILayoutTests` — popup geometry and settings-page parents
- `SettingsUITests` — settings clicks, including glass patterns and snowfall
- `RenderingTests` — off-screen paint, glass patterns, corner coverage, snowfall motion
- `AudioLyricsTests` — LRCLIB helpers
- `AudioAsrTests` — ASR helpers that do not need a microphone

`MediaSessionProbe` and `RuntimeProbe` are manual diagnostics. They are built, but they are not CTest checks.

## Style

- C++20, MSVC, Win32 and GDI/GDI+ only.
- No Qt, ImGui, DirectX, or OpenGL.
- Keep ownership in the existing classes. Do not add a second settings format.

## License

MIT. Optional Whisper notices stay under `runtime/licenses`.
