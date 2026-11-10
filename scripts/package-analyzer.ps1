param([string]$Version = '0.1.0')
$ErrorActionPreference = 'Stop'
Push-Location (Split-Path $PSScriptRoot -Parent)
try {
    $stage = Join-Path 'build' ('ToneScope-' + $Version)
    New-Item -ItemType Directory -Force -Path $stage,($stage+'/licenses'),($stage+'/assets') | Out-Null
    foreach ($exe in @('ToneScope.exe','tonescope-cli.exe','tonelab-cli.exe','analysis-tests.exe')) { Copy-Item -LiteralPath ('dist/'+$exe) -Destination $stage }
    Copy-Item -LiteralPath docs/ANALYZER.md -Destination ($stage+'/README.md')
    $readme = Join-Path $stage 'README.md'
    (Get-Content -LiteralPath $readme -Raw -Encoding UTF8).Replace('../assets/','assets/') | Set-Content -LiteralPath $readme -Encoding UTF8
    Copy-Item -LiteralPath THIRD_PARTY.md,LICENSE -Destination $stage
    Copy-Item -LiteralPath assets/tonelab.ico,assets/tonescope-ui.png -Destination ($stage+'/assets')
    Copy-Item -LiteralPath third_party/kissfft/LICENSES/BSD-3-Clause -Destination ($stage+'/licenses/KISS-FFT.txt')
    Copy-Item -LiteralPath third_party/dr_libs/LICENSE -Destination ($stage+'/licenses/dr_libs.txt')
    Copy-Item -LiteralPath third_party/imgui/LICENSE.txt -Destination ($stage+'/licenses/Dear-ImGui.txt')
    Copy-Item -LiteralPath third_party/rtaudio/LICENSE -Destination ($stage+'/licenses/RtAudio.txt')
    Copy-Item -LiteralPath third_party/rtaudio/include/asioinfo.txt -Destination ($stage+'/licenses/ASIO-info.txt')
    $archive='dist/ToneScope-'+$Version+'-windows-x64.zip'
    Compress-Archive -Path ($stage+'/*') -DestinationPath $archive -Force
    $cliStage=Join-Path 'build' ('ToneScope-CLI-'+$Version)
    New-Item -ItemType Directory -Force -Path $cliStage | Out-Null
    Copy-Item -LiteralPath dist/tonescope-cli.exe,dist/tonelab-cli.exe,LICENSE,THIRD_PARTY.md -Destination $cliStage
    @'
# ToneScope CLI

本包只包含命令行分析器 tonescope-cli.exe 和信号源 tonelab-cli.exe，无需图形界面或 MATLAB。
原创项目代码采用 MIT License；第三方组件保留各自许可，见 LICENSE、THIRD_PARTY.md 和 licenses/。

```powershell
.\tonescope-cli.exe --help
.\tonescope-cli.exe list
.\tonelab-cli.exe list --backend wasapi
.\tonescope-cli.exe analyze --input capture.wav --fft 262144 --json report.json --csv psd.csv
.\tonescope-cli.exe record --input-device 2 --rate 96000 --seconds 3 --output capture.wav
.\tonescope-cli.exe measure --input-device 2 --output-device 0 --rate 384000 --seconds 2 --tone 997 --amp 0.05 --drive left --output capture.wav --json report.json
```

输入和输出设备编号分别由对应的 list 命令查询，示例编号仅为演示。
完整指标定义、测量条件与验证记录：https://github.com/lishuo4096/ToneLab/blob/main/docs/ANALYZER.md
'@ | Set-Content -Encoding UTF8 -LiteralPath ($cliStage+'/README.md')
    Copy-Item -LiteralPath ($stage+'/licenses') -Destination $cliStage -Recurse -Force
    $cliArchive='dist/ToneScope-CLI-'+$Version+'-windows-x64.zip'
    Compress-Archive -Path ($cliStage+'/*') -DestinationPath $cliArchive -Force
    $files=@($archive,$cliArchive,'dist/ToneScope.exe','dist/tonescope-cli.exe','dist/tonelab-cli.exe')
    $hashes=foreach($file in $files){$hash=Get-FileHash -LiteralPath $file -Algorithm SHA256;$hash.Hash.ToLowerInvariant()+'  '+[IO.Path]::GetFileName($file)}
    $hashes | Set-Content -Encoding ascii -LiteralPath dist/ToneScope-SHA256SUMS.txt
    Get-FileHash -LiteralPath $archive -Algorithm SHA256 | Format-List
} finally { Pop-Location }
