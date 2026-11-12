# Official runtime assets and actual smoke test

## Pins and sources

- whisper.cpp **v1.8.3**, commit `2eeeba56e9edd762b4b38467bab96c2517163158`. Official Windows x64 CPU archive: https://github.com/ggml-org/whisper.cpp/releases/download/v1.8.3/whisper-bin-x64.zip
- Release verification: https://api.github.com/repos/ggml-org/whisper.cpp/releases/tags/v1.8.3 publishes the archive SHA256 and size below; both matched locally.
- Multilingual, unquantized **base** (not base.en): https://huggingface.co/ggerganov/whisper.cpp/resolve/5359861c739e955e79d9a303bcbc70fb988958b1/ggml-base.bin
- Model revision `5359861c739e955e79d9a303bcbc70fb988958b1`. Hugging Face API LFS metadata publishes SHA256 `60ed5bc3dd14eea856493d334349b405782ddcaf0028d4b5df4088345fba2efe` and 147951465 bytes; both matched. SHA1 also matched official models/README.md: `465707469ff3a37a2b9b8d8f89f2f99de7299dac`.
- Official sample: https://raw.githubusercontent.com/ggml-org/whisper.cpp/2eeeba56e9edd762b4b38467bab96c2517163158/samples/jfk.wav
- Sample SHA256 below is locally computed, not an upstream-published SHA256. Independently verified Git blob SHA1 (including Git blob header) against official GitHub contents API at the pinned commit: `3184d372cd2f8b804d3a540c70ec50d927b335d2` matched.
- Inspected official README, models/README and model card, retained in `docs/`. They document Windows support, CLI/sample usage, official model source and multilingual naming.

## Sizes and SHA256

Sizes are exact bytes. Installed executable/DLL hashes are local hashes of files extracted from the upstream-SHA256-verified archive (not separately published upstream hashes).

| Path | Bytes | SHA256 |
|---|---:|---|
| downloads/whisper-bin-x64.zip | 3968674 | d824b1e37599f882b396e73f1ee0bfd5d0529f700314c48311dcbd00b803321d |
| models/ggml-base.bin | 147951465 | 60ed5bc3dd14eea856493d334349b405782ddcaf0028d4b5df4088345fba2efe |
| samples/jfk.wav | 352078 | 59dfb9a4acb36fe2a2affc14bacbee2920ff435cb13cc314a08c13f66ba7860e |
| whisper/whisper-cli.exe | 480768 | 0ff971e410240a0b97117432d771245698f376e06105c011959d2bfc4bb23311 |
| whisper/whisper.dll | 483840 | 4037a6567fbb08fc7efda18e4d128a95df9c31ba171af20439d1a93b785d007e |
| whisper/ggml.dll | 67584 | bcfeaf6e5b59c315fc36e290715aa7a84b22461e968543c13dfcf57df7b1b81d |
| whisper/ggml-base.dll | 536064 | 61c9c57f49f380896ed54ed3f71735e10a2d986f482c2b0a55e72b47f731b193 |
| whisper/ggml-cpu.dll | 683008 | eb2f1d023e51c40b27463b5ab165600eb48cb60323740f400b1b62e1c8706c56 |

Installed CLI plus four DLLs: **2251264 bytes**. The original archive and extracted release are retained under downloads; only CLI and required whisper/ggml DLLs are deployed into whisper. No capture executables were run.

## Licenses

- whisper.cpp and ggml: MIT, official pinned repository LICENSE retained as `licenses/whisper.cpp-LICENSE` (1078 bytes; SHA256 `e562a2ddfaf8280537795ac5ecd34e3012b6582a147ef69ba6a6a5c08c84757d`). Source: pinned whisper.cpp commit above, `/LICENSE`.
- Whisper model: MIT according to official conversion repository model card; OpenAI license retained as `licenses/openai-whisper-LICENSE` (1063 bytes; SHA256 `b5d65a59060e68c4ff940e1eddfa6f94b2d68fdf58ed7f4dd57721c997e35e9d`). Source: https://raw.githubusercontent.com/openai/whisper/86098128c0b4f24f0e2aa2994de830614b474227/LICENSE
- JFK sample: distributed in official MIT-licensed whisper.cpp repository; no separate sample-specific license declaration was verified. Do not interpret repository MIT license as an independently established recording-rights clearance.

## Reproduction and actual results

From project root:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\setup-runtime.ps1
```

The script pins download URLs, sizes and SHA256, verifies cached files, verifies downloads before installation, installs the five CPU files and runs the smoke test by default. Download/hash/CLI/transcript failures throw. `-SkipSmokeTest` explicitly skips execution. No system-wide execution-policy change is made.

Exact CLI command (working directory is project root):

```text
runtime/whisper/whisper-cli.exe -m runtime/models/ggml-base.bin -f runtime/samples/jfk.wav -l en -t 4 -ng -otxt -of runtime/smoke/jfk
```

Actual run: **exit 0**, exact trimmed transcript match **True**. Standard output:

```text
[00:00:00.000 --> 00:00:10.600]   And so my fellow Americans, ask not what your country can do for you, ask what you can do for your country.
```

Latest script-run logs: `smoke/stdout.txt`, `smoke/stderr.txt`, `smoke/jfk.txt`, `smoke/result.txt`. Model diagnostics confirm `type = 2 (base)`, `n_langs = 99`, `use gpu = 0`. Input: 176000 audio samples, 11.0 seconds. Four CPU threads. Latest run reported load **430.35 ms**, total **5912.02 ms**; initial direct run also passed (total 3472.13 ms). Timing varies per run.

Initial direct PowerShell script invocation was blocked by this machine's execution policy; explicit process-local `-ExecutionPolicy Bypass` invocation above succeeded. No runtime/model/CLI failure occurred. No audio capture, microphone access or application integration test was performed. Build scripts, project README and C++ files were not modified.
