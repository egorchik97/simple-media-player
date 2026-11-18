# Changelog

The public GitHub history before this working tree is the original MediaPopup sources. The items below describe the current tree. Version 0.3.0 is proposed and has not been tagged.

## [0.3.0] - unreleased

### Player

- Modular Win32 player: media session, source volume, lyrics, settings, and a layered popup.
- Source volume never falls back to the Windows master slider.
- Settings load and save at `%LOCALAPPDATA%\SimpleMediaPlayer\settings.ini`, with a one-time copy from the older MediaPopup file.
- Window position is remembered. `--no-asr` disables recognition for one launch.
- Space toggles play/pause when settings are not open.

### Appearance

- Eight glass patterns: Soft Frost, Flowing Glass, Crystal, Liquid Waves, Fine Noise, Minimal Glass, Random, and Legacy.
- Legacy is the original curved highlight from the first public sources.
- Rounded window corners use per-pixel analytic coverage instead of a four-level sample mask.
- Glow is the original inset rounded border, drawn on the window rim.
- Optional snowfall: count, speed, size, opacity, wind, and blur. It is drawn over the background and under the controls.

### Tests

- CTest covers settings persistence, session selection, layout, settings clicks, rendering, lyrics helpers, and ASR helpers.
- MediaSessionProbe and RuntimeProbe are diagnostic programs. They are not pass/fail tests.

## [0.1.0] - initial public sources

- Single-file MediaPopup with GSMTC, source volume, themes, liquid-glass illustration, LRCLIB, and optional Whisper.

## Known documentation corrections

Earlier draft notes claimed a settings version label, DPI-scaled layout metrics, and reduced-motion support. Those are not implemented in this tree and are not part of 0.3.0.
