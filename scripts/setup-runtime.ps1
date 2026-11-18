# Official pinned Windows x64 CPU runtime; no capture or microphone access.
[CmdletBinding()]
param([switch]$SkipSmokeTest)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = Split-Path $PSScriptRoot -Parent
Set-Location -LiteralPath $root
foreach ($d in 'downloads','models','samples','licenses','docs','whisper','smoke') {
    New-Item -ItemType Directory -Force -Path "runtime/$d" | Out-Null
}
function Get-Verified($Url, $Path, $Size, $Hash) {
    if (Test-Path -LiteralPath $Path) {
        if ((Get-Item -LiteralPath $Path).Length -eq $Size -and (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash -eq $Hash) { return }
    }
    $tmp = "$Path.part"
    & curl.exe --fail --location --silent --show-error --retry 3 --output $tmp $Url
    if ($LASTEXITCODE -ne 0) { throw "Download failed: $Url (curl exit $LASTEXITCODE)" }
    if ((Get-Item -LiteralPath $tmp).Length -ne $Size -or (Get-FileHash -LiteralPath $tmp -Algorithm SHA256).Hash -ne $Hash) {
        throw "Size/SHA256 verification failed: $Url; not installed"
    }
    Move-Item -LiteralPath $tmp -Destination $Path -Force
}
$c = '2eeeba56e9edd762b4b38467bab96c2517163158'
$hf = '5359861c739e955e79d9a303bcbc70fb988958b1'
$raw = "https://raw.githubusercontent.com/ggml-org/whisper.cpp/$c"
Get-Verified 'https://github.com/ggml-org/whisper.cpp/releases/download/v1.8.3/whisper-bin-x64.zip' 'runtime/downloads/whisper-bin-x64.zip' 3968674 'd824b1e37599f882b396e73f1ee0bfd5d0529f700314c48311dcbd00b803321d'
Get-Verified "https://huggingface.co/ggerganov/whisper.cpp/resolve/$hf/ggml-base.bin" 'runtime/models/ggml-base.bin' 147951465 '60ed5bc3dd14eea856493d334349b405782ddcaf0028d4b5df4088345fba2efe'
Get-Verified "$raw/samples/jfk.wav" 'runtime/samples/jfk.wav' 352078 '59dfb9a4acb36fe2a2affc14bacbee2920ff435cb13cc314a08c13f66ba7860e'
Get-Verified "$raw/LICENSE" 'runtime/licenses/whisper.cpp-LICENSE' 1078 'e562a2ddfaf8280537795ac5ecd34e3012b6582a147ef69ba6a6a5c08c84757d'
Get-Verified 'https://raw.githubusercontent.com/openai/whisper/86098128c0b4f24f0e2aa2994de830614b474227/LICENSE' 'runtime/licenses/openai-whisper-LICENSE' 1063 'b5d65a59060e68c4ff940e1eddfa6f94b2d68fdf58ed7f4dd57721c997e35e9d'
Get-Verified "$raw/README.md" 'runtime/docs/whisper-README.md' 35529 '3d55ada7f4065454dc76d37051c83798c9a7287d799f3d62805db15057efec37'
Get-Verified "$raw/models/README.md" 'runtime/docs/models-README.md' 5745 'bad04fef13f1556cc9cc93c46022e47dbcea94814032faaa4aac2e168314b2b7'
Get-Verified "https://huggingface.co/ggerganov/whisper.cpp/raw/$hf/README.md" 'runtime/docs/model-card.md' 3196 '21fd967098804f33fc84e803fb0e5ab7666d71801f4027cf28a65e7af09c1758'
Expand-Archive -LiteralPath 'runtime/downloads/whisper-bin-x64.zip' -DestinationPath 'runtime/downloads/extracted' -Force
foreach ($f in 'whisper-cli.exe','whisper.dll','ggml.dll','ggml-base.dll','ggml-cpu.dll') {
    Copy-Item -LiteralPath "runtime/downloads/extracted/Release/$f" -Destination "runtime/whisper/$f" -Force
}
if (!$SkipSmokeTest) {
    Remove-Item runtime/smoke/jfk.txt -Force -ErrorAction SilentlyContinue
    $args = '-m runtime/models/ggml-base.bin -f runtime/samples/jfk.wav -l en -t 4 -ng -otxt -of runtime/smoke/jfk'
    $p = Start-Process -FilePath "$root/runtime/whisper/whisper-cli.exe" -WorkingDirectory $root -ArgumentList $args -Wait -PassThru -NoNewWindow -RedirectStandardOutput 'runtime/smoke/stdout.txt' -RedirectStandardError 'runtime/smoke/stderr.txt'
    "exit_code=$($p.ExitCode)" | Set-Content runtime/smoke/result.txt
    if ($p.ExitCode -ne 0) { throw "CLI smoke failed: exit $($p.ExitCode); see runtime/smoke/stderr.txt" }
    $text = (Get-Content runtime/smoke/jfk.txt -Raw).Trim()
    $expected = 'And so my fellow Americans, ask not what your country can do for you, ask what you can do for your country.'
    if ($text -ne $expected) { throw "CLI transcript mismatch: $text" }
    'transcript_exact_match=True' | Add-Content runtime/smoke/result.txt
    Write-Host "PASS: exit 0; exact JFK transcript match. $text"
}
Write-Host 'Verified official v1.8.3 CPU runtime and multilingual base model installed.'
