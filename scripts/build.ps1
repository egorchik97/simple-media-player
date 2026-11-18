param(
    [ValidateSet('Debug','Release')][string]$Configuration='Release',
    [string]$BuildDirectory=(Join-Path (Split-Path $PSScriptRoot -Parent) '..\simple-media-player-build'),
    [string]$CMake='cmake',
    [switch]$Clean
)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
& $CMake -S $root -B $BuildDirectory -G 'Visual Studio 18 2026' -A x64
if($LASTEXITCODE){throw "CMake configuration failed: $LASTEXITCODE"}
$arguments=@('--build',$BuildDirectory,'--config',$Configuration,'--parallel')
if($Clean){$arguments+='--clean-first'}
& $CMake @arguments
if($LASTEXITCODE){throw "Build failed: $LASTEXITCODE"}
$ctest=Join-Path (Split-Path (Get-Command $CMake).Source) 'ctest.exe'
& $ctest --test-dir $BuildDirectory -C $Configuration --output-on-failure
if($LASTEXITCODE){throw "Tests failed: $LASTEXITCODE"}