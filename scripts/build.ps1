param([switch]$Test)
$ErrorActionPreference = 'Stop'
Push-Location (Split-Path $PSScriptRoot -Parent)
try {
    if (-not (Get-Command g++.exe -ErrorAction SilentlyContinue)) { $env:PATH = 'C:\mingw64\bin;' + $env:PATH }
    $folders = @('dist','build/src','build/src/analyzer','build/tests','build/third_party/rtaudio/include','build/third_party/imgui/backends','build/third_party/kissfft')
    foreach ($folder in $folders) { New-Item -ItemType Directory -Force $folder | Out-Null }
    & mingw32-make.exe -j4 all
    if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
    if ($Test) { & .\dist\dsp-tests.exe; if ($LASTEXITCODE -ne 0) { throw 'DSP tests failed' } }
    if ($Test) { & .\dist\analysis-tests.exe; if ($LASTEXITCODE -ne 0) { throw 'Audio analysis tests failed' } }
} finally { Pop-Location }
