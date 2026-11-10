# ToneLab

<img src="assets/tonelab.png" width="96" alt="ToneLab stereo wave icon">

[下载 ToneLab 信号发生器](https://github.com/lishuo4096/ToneLab/releases/tag/v1.0.0) · [下载 ToneScope 分析器（含 CLI）](https://github.com/lishuo4096/ToneLab/releases/tag/tonescope-v0.1.0) · [界面预览](assets/ui-wasapi.png)

新增独立 **ToneScope 音频分析器**：实时双声道 PSD、录音、WAV 时间段分析、THD/THD+N/SNR/SINAD，信号源直接调用现有 ToneLab CLI。使用说明与 MATLAB 验证数据见 [docs/ANALYZER.md](docs/ANALYZER.md)。本地程序为 `dist/ToneScope.exe`，分析 CLI 为 `dist/tonescope-cli.exe`。

纯命令行用户可下载 Release 中的 `ToneScope-CLI-0.1.0-windows-x64.zip`，无需启动 GUI：

```powershell
.\tonescope-cli.exe analyze --input capture.wav --fft 262144 --json report.json --csv psd.csv
.\tonescope-cli.exe measure --input-device 2 --output-device 0 --rate 384000 --seconds 2 --tone 997 --amp 0.05 --drive left --output capture.wav --json report.json
```

本项目原创代码与文档采用 [MIT License](LICENSE)；第三方源码保留原有许可，详见 [THIRD_PARTY.md](THIRD_PARTY.md)。

Windows x64 双声道音调发生器，C++17 / Dear ImGui + DirectX 11，提供桌面界面和独立 CLI。使用本机 MinGW-w64 编译，无需安装音频软件框架。

已加入设备音量、Windows 默认格式跟随、格式能力探测、后台 ASIO 枚举与驱动重置恢复。设备实际回环验收记录见 `docs/VALIDATION.md`。

## 设备格式、音量与后台运行

- WASAPI 从 `PKEY_AudioEngine_DeviceFormat` 读取 Windows 控制面板默认格式，而非把共享引擎的 float mix format 当成设备位深。默认跟随，停止时每 4 秒更新；手动选择采样率/格式后关闭跟随，播放中不自动改率。
- 按 19 个常见采样率及 Windows 默认率，逐项用 `IsFormatSupported(EXCLUSIVE)` 探测位深/采样率组合；下拉框只显示驱动接受的组合。兼容要求传统 `WAVEFORMATEX` 的驱动。CLI 也可以请求列表之外的整数采样率，打开时仍严格验证。
- Windows 默认格式用于共享模式。独占流可以选择其他驱动支持的格式，驱动据此切换实际流采样率；不会改写 Windows 默认共享格式。
- `Device volume` 和 `Device mute` 直接控制 Windows 设备端点音量，与左右信号的数字峰值独立，并每秒读取外部变化。设备有硬件音量控制时适用于独占流；仅有软件端点音量时，界面提示独占模式下可能无效。
- ASIO 位深按原生输出通道类型选择，采样率由宿主请求并经驱动验证。ASIO4ALL 原生 PCM32 不再沿用其他后端的 PCM16。驱动开关、面板与销毁在同一 STA 线程；重置请求合并处理，最多重新打开两次；真正的外部时钟变化仍停止流。
- 枚举、格式探测、开停流均在后台，不阻塞 UI。播放后可按 `ASIO panel` 打开驱动面板。
- 增加 `24in32`：24-bit 有效精度左对齐于 32-bit 容器，与真 PCM32 区分，支持对应 WASAPI 格式和 WAV 导出。

```powershell
.\dist\tonelab-cli.exe inspect --backend wasapi --device 0
.\dist\tonelab-cli.exe inspect --backend asio --device 129
.\dist\tonelab-cli.exe volume --device 0
.\dist\tonelab-cli.exe volume --device 0 --level 20

# 不指定采样率/位深时，播放采用 Windows 默认或 ASIO 原生格式。
.\dist\tonelab-cli.exe play --backend wasapi --seconds 5
.\dist\tonelab-cli.exe play --backend asio --format auto --seconds 5

# 隐藏窗口直接渲染到文件；不注入鼠标键盘、不切换前台窗口、不播放。
$p = Start-Process .\dist\ToneLab.exe -ArgumentList '--snapshot=build/ui.png' -WindowStyle Hidden -PassThru
$p.WaitForExit()
# ASIO 状态快照：--snapshot-asio=build/ui-asio.png

# 物理输出接回输入后执行；输出峰值 0.05 FS 的 997/2003 Hz 测试音。
.\dist\loopback-test.exe 384000
.\dist\loopback-test.exe 48000
```

快照模式附带 `.png.json`，记录渲染帧数、最大 UI 构建耗时和设备是否就绪。全流程不需要 Computer Use。`volume` 修改的是该设备全局音量。

## 使用

编译好的程序位于 `dist/`：

- `ToneLab.exe`：图形界面。先选后端、设备、采样率和格式，再启动输出；左右频率、波形、幅度、静音可实时独立调整。默认峰值 0.1 FS（−20 dBFS）。
- `tonelab-cli.exe`：命令行；`--help` 查看完整参数。
- `dsp-tests.exe`：离线 DSP 验证。

```powershell
.\dist\tonelab-cli.exe list --backend all

# 设备编号以 list 结果为准，硬件和驱动必须接受此格式。
.\dist\tonelab-cli.exe play --backend wasapi --device 0 --rate 384000 --format 32 --left-freq 1000 --right-freq 2000 --left-wave sine --right-wave triangle --left-amp 0.1 --right-amp 0.05 --seconds 5

# ASIO 格式必须与驱动当前输出样本类型完全一致。
.\dist\tonelab-cli.exe play --backend asio --rate 192000 --format auto --seconds 5

.\dist\tonelab-cli.exe play --backend ds --format 16 --seconds 5

# 无声卡也能生成原生 384 kHz PCM32 文件。
.\dist\tonelab-cli.exe render --output native384.wav --rate 384000 --format 32 --seconds 2
```

不指定 `--seconds` 的播放用 Ctrl+C 停止；不指定 `--device` 则使用该后端枚举到的首个双声道设备。幅度是线性数字满量程峰值，不是电压。每个声道有独立 `Output ON` 开关，关闭后从下一处理块开始逐字节写 0，不经过静音渐变；已经提交给驱动的缓冲仍会播放完。另一声道不受影响，两个开关均关闭时保留音频流并连续输出全 0。CLI 用 `--left-mute 1` / `--right-mute 1` 分别关闭；重新开启采用短渐入。CLI 支持 8–768 kHz 范围内整数采样率，GUI 列出常用档位；实际可用格式由驱动决定。

## 原生采样率与精度

每个输出样本直接在目标时刻计算，振荡器相位增量为 `frequency / sampleRate`，无低采样率中间缓冲、插值或重采样。内部用 double 运算，PCM32 从 double 直接取整为有符号 32-bit，不经 float32 或 PCM16/24 中转。选择 `f32` 则是 IEEE float32（约 24-bit 尾数），与整数 PCM32 区分显示。

| 后端 | 输出路径 | 格式保证 |
|---|---|---|
| WASAPI Exclusive | 自写独占、事件驱动、MMCSS 音频线程 | `IsFormatSupported` 与 `Initialize` 精确接受双声道采样率、容器位数及有效位数才启动；不退回共享模式 |
| ASIO | RtAudio ASIO；左右映射输出通道 1、2 | 本地补丁校验两通道原生样本类型及设置后的实际采样率；类型不符直接拒绝，不偷偷降位深 |
| DirectSound | RtAudio DirectSound，PCM16 | 本程序仍按流采样率生成，但 Windows 混音器/驱动可能重采样，因此不能保证硬件原生率或位精度 |

例如“384 kHz / PCM32”只有在 WASAPI 独占或 ASIO 驱动接受后才能启动。不支持则报错，不会改成 48 kHz 再上采样。ASIO 外部时钟改变时，后端停止流，需要按新设置重新启动，避免继续按旧采样率生成。

这里验证的是软件到驱动的流格式。DAC 模拟有效位数、驱动内部处理及 DAC 内部过采样不由应用控制，32-bit 容器也不意味着模拟输出有 32-bit ENOB。没有驱动支持的 384 kHz 设备时，可以验证文件生成，但不能据此声称硬件 384 kHz 输出已经验收。

正弦直接计算；方波/锯齿波使用 PolyBLEP 降低混叠（非理想砖墙滤波），三角波使用奈奎斯特以下至多 64 个奇次谐波。高频非正弦波自然丢失无法表示的高次谐波。增益约 5 ms 满量程斜率限制，频率变化保持相位连续；切换波形可能产生瞬态。所有频率必须严格小于 Fs/2。

界面波形是两周期数学预览，并非硬件回采。Underruns 在 WASAPI 下统计超过两倍缓冲时长的调度延误，在 ASIO/DirectSound 下记录后端状态报告，并非硬件测量仪表。

## 构建与验证

依赖源码已放入 `third_party/`，构建无需联网。本机已用 `C:\mingw64\bin\g++.exe` 构建。

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build.ps1 -Test
python .\tests\cli_tests.py
python .\tests\icon_resources.py
```

`build.ps1` 自动创建对象目录，再执行 `mingw32-make -j4`。生成程序静态链接 MinGW 运行库；系统提供 DX11、WASAPI、DirectSound，ASIO 需要设备厂商安装 x64 ASIO 驱动。

也提供标准 CMake 工程（需另装 CMake 3.20+）：

```powershell
cmake -S . -B build-cmake -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build-cmake -j 4
ctest --test-dir build-cmake --output-on-failure
```

目录：

```text
include/tone/       公共接口：DSP、音频后端、UI
src/               实现及 GUI/CLI 入口
tests/             DSP 数值验证和 CLI/WAV 集成验证
scripts/           MinGW 构建脚本
docs/              架构、验收记录
third_party/       固定版本 RtAudio、Dear ImGui 源码与上游许可
build/             对象文件（生成）
dist/              可运行程序（生成）
CMakeLists.txt     标准 CMake 构建入口
Makefile           无 CMake 的 MinGW 构建入口
THIRD_PARTY.md     上游来源、版本、许可及本地补丁说明
```

图标源文件为 `assets/tonelab.svg`，`assets/tonelab.ico` 包含 16、20、24、32、40、48、64、128、256 px 九种尺寸。所有四个 EXE 均链接 `resources/tonelab.rc`；GUI 同时设置标题栏和任务栏图标。正常构建直接使用已生成的 ICO，不增加运行依赖。修改 SVG 后，可用 `uv pip install --target build/icon-tools -r scripts/requirements-icons.txt` 安装仅用于开发的渲染工具，再运行 `python scripts/generate_icons.py`。

设备拔出、被占用、不支持独占/格式等情况都会显示错误。修改设备或流格式前停止输出；随后重新启动即按新采样率重建生成器。实时修改左右声道参数不需要重启流。
