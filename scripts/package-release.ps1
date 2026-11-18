# Package MediaPopup for Lite/Full release
# Usage: .\scripts\package-release.ps1 -Mode Lite|Full -Version <version>

[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)]
    [ValidateSet('Lite','Full')]
    [string]$Mode,

    [Parameter(Mandatory=$true)]
    [string]$Version,
    [string]$BuildDirectory=(Join-Path (Split-Path (Split-Path $PSScriptRoot -Parent) -Parent) 'simple-media-player-build'),
    [ValidateSet('Debug','Release')][string]$Configuration='Release'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$root = $PSScriptRoot/..
$dist = "$root/dist"
$exe = Join-Path $BuildDirectory "$Configuration/SimpleMediaPlayer.exe"

# Clean output
if (Test-Path "$dist/SimpleMediaPlayer-$Mode-v$Version") {
    Remove-Item "$dist/SimpleMediaPlayer-$Mode-v$Version" -Recurse -Force
}
New-Item -ItemType Directory -Force -Path "$dist/SimpleMediaPlayer-$Mode-v$Version" | Out-Null

# Copy executable
if (-not (Test-Path $exe)) {
    throw "CMake executable not found: $exe. Build first."
}
Copy-Item $exe -Destination "$dist/SimpleMediaPlayer-$Mode-v$Version/SimpleMediaPlayer.exe" -Force

# Copy runtime for Full mode
if ($Mode -eq 'Full') {
    $runtimeSrc = "$root/runtime"
    $runtimeDst = "$dist/SimpleMediaPlayer-$Mode-v$Version/runtime"
    if (Test-Path $runtimeSrc) {
        Copy-Item $runtimeSrc -Destination $runtimeDst -Recurse -Force
    }
}

# Copy license and docs
Copy-Item "$root/LICENSE" -Destination "$dist/SimpleMediaPlayer-$Mode-v$Version/LICENSE" -Force
Copy-Item "$root/README.md" -Destination "$dist/SimpleMediaPlayer-$Mode-v$Version/README.md" -Force

# Generate SHA256 checksums
$files = Get-ChildItem -Path "$dist/SimpleMediaPlayer-$Mode-v$Version" -Recurse -File
foreach ($file in $files) {
    $hash = Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256
    $relPath = $file.FullName.Substring("$dist/SimpleMediaPlayer-$Mode-v$Version".Length + 1)
    "$($hash.Hash)  $relPath" | Out-File -FilePath "$dist/SimpleMediaPlayer-$Mode-v$Version.sha256" -Append -Encoding utf8
}

Write-Host "Package created: $dist/SimpleMediaPlayer-$Mode-v$Version/"
Write-Host "SHA256: $dist/SimpleMediaPlayer-$Mode-v$Version.sha256"