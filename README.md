# MediaPopup

### Build

```powershell
# 1. Install Whisper runtime (optional, for ASR)
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\setup-runtime.ps1

# 2. Build app and run all tests
.\build.cmd
```

`build.cmd` compiles:
- `MediaPopup.exe` — main popup
- `MediaPopupTests.exe` — UI/geometry/INI tests
- `AsrTests.exe` — ASR helper tests
- `LyricsTests.exe` — LRCLIB parser tests
- `MediaProbe.exe` — read-only GSMTC diagnostics probe

Or compile only the app:

```powershell
cl /std:c++20 /utf-8 /O2 /EHsc /W4 MediaPopup.cpp Asr.cpp user32.lib gdi32.lib windowsapp.lib /link /SUBSYSTEM:WINDOWS /OUT:MediaPopup.exe
```

### Usage

```powershell
# Normal run (restores ASR preference from settings)
.\MediaPopup.exe

# Diagnostic run (ignores persisted ASR preference)
.\MediaPopup.exe --no-asr
```

Open the popup → `•••` → **Настройки** (Settings).
- «Кнопки в карточке» — toggle Prev / Pause / Next buttons in compact view, and set compact width/heights.
- «Оформление и режим текста» — themes, custom colors, liquid glass, compact lyrics, LRCLIB / Whisper / Off.

### Configuration

All settings are stored in `%LOCALAPPDATA%\MediaPopup\settings.ini`:

| Key | Type | Default |
|-----|------|---------|
| `cover`, `title`, `artist`, `pause` | bool | `true` |
| `compactPrevious`, `compactNext` | bool | `false` |
| `hover`, `pinned`, `volume`, `asr` | bool | `true` / `false` |
| `delay` | int (ms) | `700` |
| `theme` | int | `0` (0–5, 5 = Custom) |
| `opacity` | int | `100` (45–100) |
| `glow` | int | `35` (0–100) |
| `roundness` | int | `18` (0–28) |
| `glass`, `sourceLyrics`, `compactLyrics`, `glassAnimated` | bool | `true` / `false` |
| `customBackground`, `customAccent`, `customText`, `customMuted` | int | theme defaults |
| `compactWidth` | int | `280` (200–900) |
| `compactHeight` | int | `70` (50–600) |
| `compactLyricsHeight` | int | `102` (80–600) |
| `glassStrength`, `glassTint`, `glassSpeed` | int | `50` / `35` / `30` (0–100) |

### Limitations

- LRCLIB is an external catalog lookup by metadata — GSMTC itself does **not** provide lyrics.
- Whisper recognizes **all** default render audio, not just the selected source; quality depends on vocals and background music.
- Browser volume is application-wide, not per-tab.
- Glass is a GDI illustration (highlights + tinted border), not true desktop blur/acrylic.


Lightweight Win32 media popup player with **source-specific volume control** (not system volume), customizable appearance (themes, liquid glass, transparency, glow), and two lyrics modes — **LRCLIB catalog** and **local Whisper ASR**.

Built with **C++20**, **Win32 GDI**, and **Windows Media Control (GSMTC)**. No external UI frameworks — everything is drawn with GDI.

[English](#english) · [Русский](#русский)

## English

### Description

`MediaPopup` is a small always-on-top popup that reads the active Windows Media Session (GSMTC) and lets you control playback, volume, and appearance without switching to the player window.

Unlike the old "system volume" slider, this popup controls the **audio session of the actual media source** (browser, Spotify, AIMP, etc.). If the source cannot be reliably identified, the volume slider shows `Недоступно` instead of changing Windows system volume.

### Features

- **Source volume** — controls `ISimpleAudioVolume` of the app behind the current media session (AUMID / process path / EXE name matching). Browser volume is app-wide, not per-tab.
- **Compact view** — 280×70 px (or custom width/height) with cover, title, artist, play/pause, prev/next buttons.
- **Compact lyrics (1 line)** — current lyric line from LRCLIB or Whisper, shown under the compact card without overlapping controls.
- **Expanded view** — 340×210 px with seek bar, time, volume slider, and full lyrics block.
- **Appearance settings** — 6 themes (Ocean, Lavender, Mint, Amber, Rose, Custom), custom RGB colors, liquid glass effect (strength, tint, animation, speed), glow, rounded corners (min 8 px so edges are not pixelated), opacity 45–100%.
- **Two lyrics modes:**
  - **LRCLIB** — searches the public LRCLIB catalog by title, artist, and duration. No audio is uploaded; only metadata is sent. Synced LRC lines follow playback position.
  - **Whisper** — local speech recognition via `whisper.cpp` DLL API. Captures default render audio (loopback), not microphone. Nothing is sent over the network.
- **Settings** — saved to `%LOCALAPPDATA%\MediaPopup\settings.ini` via atomic UTF-16 INI replacement.
- **No ASR fallback** — launch with `--no-asr` to ignore persisted ASR preference for one run.

### Requirements

- Windows 10 1809 or newer
- Visual Studio 2022/2026 (MSVC x64) or any C++20 compiler with Windows SDK
- Windows SDK with C++/WinRT (`windowsapp.lib`)
- For Whisper ASR: `runtime/whisper/whisper.dll` + `runtime/models/ggml-base.bin` (installed by `setup-runtime.ps1`)


## Русский

### Описание

`MediaPopup` — лёгкая всплывающая панель поверх всех окон, которая читает активную медиасессию Windows (GSMTC) и позволяет управлять воспроизведением, громкостью и оформлением, не переключаясь на окно проигрывателя.

В отличие от старого «системного» ползунка, эта панель управляет **аудиосессией реального источника медиа** (браузер, Spotify, AIMP и т.д.). Если источник не удаётся надёжно идентифицировать, ползунок показывает «Недоступно», а системная громкость Windows не меняется.

### Возможности

- **Громкость источника** — управление `ISimpleAudioVolume` приложения за текущей медиасессией (сопоставление по AUMID / пути к процессу / имени EXE). Для браузера — громкость всего приложения, а не отдельной вкладки.
- **Компактный вид** — 280×70 px (или пользовательская ширина/высота) с обложкой, названием, артистом, паузой, назад/вперёд.
- **Текст в компактном виде (1 строка)** — текущая строка из LRCLIB или Whisper под карточкой, без перекрытия кнопок.
- **Развёрнутый вид** — 340×210 px с полосой позиции, временем, ползунком громкости и блоком текста.
- **Настройки оформления** — 6 тем (Океан, Лаванда, Мята, Янтарь, Роза, Своя), свои цвета RGB, жидкое стекло (сила, оттенок, анимация, скорость), свечение, скругление углов (минимум 8 px, чтобы углы не были пиксельными), прозрачность 45–100%.
- **Два режима текста:**
  - **LRCLIB** — поиск в публичном каталоге LRCLIB по названию, артисту и длительности. Аудио не отправляется; в сеть уходят только метаданные. Синхронизированные строки LRC следуют за позицией проигрывания.
  - **Whisper** — локальное распознавание речи через DLL API `whisper.cpp`. Захват звука устройства вывода по умолчанию (loopback), не микрофон. Ничего не отправляется в сеть.
- **Настройки** — сохраняются в `%LOCALAPPDATA%\MediaPopup\settings.ini` через атомарную замену UTF-16 INI.
- **Диагностический запуск** — `MediaPopup.exe --no-asr` игнорирует сохранённое включение ASR в этом запуске.

### Требования

- Windows 10 1809 или новее
- Visual Studio 2022/2026 (MSVC x64) или любой компилятор C++20 с Windows SDK
- Windows SDK с C++/WinRT (`windowsapp.lib`)
- Для ASR Whisper: `runtime/whisper/whisper.dll` + `runtime/models/ggml-base.bin` (устанавливается через `setup-runtime.ps1`)

### Сборка

```powershell
# 1. Установить runtime Whisper (опционально, для ASR)
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\setup-runtime.ps1

# 2. Собрать приложение и запустить все тесты
.\build.cmd
```

`build.cmd` компилирует:
- `MediaPopup.exe` — главная панель
- `MediaPopupTests.exe` — тесты UI, геометрии, INI
- `AsrTests.exe` — тесты вспомогательных функций ASR
- `LyricsTests.exe` — тесты парсера LRCLIB
- `MediaProbe.exe` — зонд только для чтения GSMTC

Или только приложение:

```powershell
cl /std:c++20 /utf-8 /O2 /EHsc /W4 MediaPopup.cpp Asr.cpp user32.lib gdi32.lib windowsapp.lib /link /SUBSYSTEM:WINDOWS /OUT:MediaPopup.exe
```

### Использование

```powershell
# Обычный запуск (восстанавливает настройку ASR из настроек)
.\MediaPopup.exe

# Диагностический запуск (игнорирует сохранённое включение ASR)
.\MediaPopup.exe --no-asr
```

Открыть панель → `•••` → **Настройки**.
- «Кнопки в карточке» — переключатели Назад / Пауза / Вперёд в компактном виде, а также ширина и высота компактного окна.
- «Оформление и режим текста» — темы, свои цвета, жидкое стекло, текст в компактном виде, LRCLIB / Whisper / Выключено.

### Настройки

Все настройки хранятся в `%LOCALAPPDATA%\MediaPopup\settings.ini`:

| Ключ | Тип | По умолчанию |
|------|-----|-------------|
| `cover`, `title`, `artist`, `pause` | bool | `true` |
| `compactPrevious`, `compactNext` | bool | `false` |
| `hover`, `pinned`, `volume`, `asr` | bool | `true` / `false` |
| `delay` | int (мс) | `700` |
| `theme` | int | `0` (0–5, 5 = Своя) |
| `opacity` | int | `100` (45–100) |
| `glow` | int | `35` (0–100) |
| `roundness` | int | `18` (0–28) |
| `glass`, `sourceLyrics`, `compactLyrics`, `glassAnimated` | bool | `true` / `false` |
| `customBackground`, `customAccent`, `customText`, `customMuted` | int | по умолчанию темы |
| `compactWidth` | int | `280` (200–900) |
| `compactHeight` | int | `70` (50–600) |
| `compactLyricsHeight` | int | `102` (80–600) |
| `glassStrength`, `glassTint`, `glassSpeed` | int | `50` / `35` / `30` (0–100) |

### Ограничения

- LRCLIB — внешний каталог, поиск по метаданных; GSMTC сам по себе **не предоставляет** текст песни.
- Whisper распознаёт **весь** звук устройства вывода по умолчанию, а не только выбранный источник; качество зависит от вокала и фоновой музыки.
- Громкость браузера относится ко всему приложению, а не к отдельной вкладке.
- Стекло — GDI-иллюстрация (блики + цветная окантовка), не настоящее размытие/акрил рабочего стола.

### Лицензия

Распространяется под лицензией [MIT](LICENSE).
