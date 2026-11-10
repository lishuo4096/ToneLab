param([string]$Version = '1.0.0')
$ErrorActionPreference = 'Stop'
Push-Location (Split-Path $PSScriptRoot -Parent)
try {
    $stage = Join-Path 'build' ('package-' + $Version)
    New-Item -ItemType Directory -Force -Path $stage,($stage + '/licenses'),($stage + '/assets'),($stage + '/docs') | Out-Null
    foreach ($exe in @('ToneLab.exe','tonelab-cli.exe','dsp-tests.exe','loopback-test.exe')) {
        Copy-Item -LiteralPath ('dist/' + $exe) -Destination $stage
    }
    Copy-Item -LiteralPath README.md,THIRD_PARTY.md,LICENSE -Destination $stage
    Copy-Item -LiteralPath assets/tonelab.svg,assets/tonelab.png,assets/tonelab.ico -Destination ($stage + '/assets')
    Copy-Item -LiteralPath assets/ui-wasapi.png,assets/ui-asio.png -Destination ($stage + '/assets')
    Get-ChildItem -LiteralPath docs -File | Where-Object { $_.Extension -in @('.md','.json') } | Copy-Item -Destination ($stage + '/docs')
    Copy-Item -LiteralPath third_party/rtaudio/LICENSE -Destination ($stage + '/licenses/RtAudio-LICENSE.txt')
    Copy-Item -LiteralPath third_party/rtaudio/include/asioinfo.txt -Destination ($stage + '/licenses/ASIO-info.txt')
    Copy-Item -LiteralPath third_party/imgui/LICENSE.txt -Destination ($stage + '/licenses/Dear-ImGui-LICENSE.txt')
    $archive = 'dist/ToneLab-' + $Version + '-windows-x64.zip'
    Compress-Archive -Path ($stage + '/*') -DestinationPath $archive -Force
    $files = @($archive,'dist/ToneLab.exe','dist/tonelab-cli.exe','dist/dsp-tests.exe','dist/loopback-test.exe')
    $lines = foreach ($file in $files) { $hash=Get-FileHash -LiteralPath $file -Algorithm SHA256; $hash.Hash.ToLowerInvariant() + '  ' + [IO.Path]::GetFileName($file) }
    $lines | Set-Content -Encoding ascii -LiteralPath dist/SHA256SUMS.txt
    Write-Output $archive
} finally { Pop-Location }
