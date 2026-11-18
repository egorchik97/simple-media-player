# Audio modules

The public Start/Stop/Snapshot and Request/Clear/Snapshot APIs are preserved.
Original root-level sources are retained; do not compile them into the modular target.

## Build integration

Compile `AsrEngine.cpp`, `StreamingWhisper.cpp`, and `LyricsEngine.cpp` exactly once.
Use C++20 and Windows SDK C++/WinRT headers; MSVC `/EHsc /utf-8`, `NOMINMAX`.
Link `ole32`, `uuid`, `winhttp`, `windowsapp`. No Whisper import libraries.
The wrapper includes `../../runtime/include/whisper.h` and its ggml dependencies.
It requires matching Whisper **1.8.3** DLLs and rejects other versions before inference.
Deploy runtime/whisper/{ggml-base,ggml-cpu,ggml,whisper}.dll and
runtime/models/ggml-base.bin beneath the executable directory (or supplied root).

## Lifecycle

ASR uses a lifecycle worker and a WASAPI render-loopback capture thread, with
thread-local COM initialization and releases before CoUninitialize. Inference
uses a reusable CPU Whisper context, bounded four-second audio history,
overlapping windows, UTF-8 token aggregation and cancellation callbacks.
Stop invalidates results immediately; destruction joins workers. Model loading
itself has no cancellation callback, so destruction can wait for model loading.
Capture discontinuity resets history. Only float32/PCM16 mix formats are accepted;
unsupported device formats fail explicitly. Default endpoint changes require restart.

Lyrics uses HTTPS LRCLIB get, UTF-8 percent encoding, rounded track duration,
typed JSON, a 512 KiB body cap, and RAII WinHTTP handles. Latest request wins;
Clear invalidates in-flight results. Synchronous HTTP cancellation is cooperative
between operations, with two-second WinHTTP timeouts and a twelve-second checked
deadline, not a hard real-time shutdown guarantee. No cross-thread handle closing.

## Tests

CMake/CTest builds and executes the modular audio tests in the repository tests directory. Tests cover format
validation, packet-boundary-independent resampling, missing-runtime lifecycle,
LRC offsets/repeated timestamps/hour formats/order/malformed tags, UTF-8 URLs,
JSON null/type/instrumental behavior, display seeking and invalid metadata.
These offline tests do not establish microphone/loopback transcription accuracy,
live LRCLIB availability, or cancellation timing during an active HTTP operation.
The Microsoft OpenMP runtime remains pinned until process exit to prevent the reproduced VCOMP140 unload-time access violation.
