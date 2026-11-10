# Third-party sources

根目录 `LICENSE` 的 MIT 许可适用于本项目原创代码、图标和文档。`third_party/` 内的库、SDK 接口文件以及改编代码中保留的上游版权声明仍受各自原有许可约束，不因新增根许可证而改变。

## RtAudio

采用：https://github.com/thestk/rtaudio （RtAudio 的 MIT-style license，最近提交 2026-10）。
理由：成熟的 Windows DirectSound/ASIO 后端，可直接使用 MinGW 构建。
接入：Windows 构建所需源码子集固定在 `third_party/rtaudio/`，上游 commit `8ce4f2635a3b8f52687ec34625c8027e18024fe9`。

原许可见 `third_party/rtaudio/LICENSE`。只编译 DirectSound、ASIO，不编译其 WASAPI 重采样实现。本地补丁：要求原生 format 等于用户 format、各输出通道 sample type 相同、ASIOGetSampleRate 读回值等于请求值；探测原生输出通道格式（上游优先查输入）；标准采样率候选扩展到 768 kHz；通过 `include/tone/asio_events.hpp` 把重置和时钟通知交给宿主 STA 线程，避免重复回调并发关闭流。主机自己设置采样率产生的通知与真正外部时钟变化区分处理。

`include/asio*`、`include/iasi*` 等是上游附带的 Steinberg ASIO SDK 2.3 接口文件，保留原文件头版权，不能把这些文件当成 RtAudio 的 MIT 代码。上游 `include/asioinfo.txt` 指向 https://www.steinberg.net/developers/ 的 SDK/许可；对外分发含 ASIO 的产品前须按所采用 SDK 许可处理。ToneLab 不附带或安装任何 ASIO 驱动。

## Dear ImGui

采用：https://github.com/ocornut/imgui （MIT，固定版本 v1.91.9b，2025-03）。
理由：轻量、成熟、可直接使用 Win32/DirectX 11，避免引入 Qt 运行库。
接入：核心及 Win32/DX11 后端源码子集固定在 `third_party/imgui/`，commit `f5befd2d29e66809cd1110a152e375a7f1981f06`。

许可见 `third_party/imgui/LICENSE.txt`。`src/gui_main.cpp` 改编自该版本 `examples/example_win32_directx11/main.cpp`，使用其窗口、交换链、设备销毁和消息处理代码；业务界面在 `src/ui.cpp`。其余上游源码未修改。

## Windows API reference

- https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudioclient-isformatsupported
- https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudioclient-initialize
- https://learn.microsoft.com/en-us/windows/win32/coreaudio/exclusive-mode-streams

应用 DSP/CLI/UI 和独占 WASAPI 实现为本项目编写，没有复制第三方音调发生器的业务代码。

端点音量参考：https://learn.microsoft.com/en-us/windows/win32/coreaudio/endpoint-volume-controls

默认设备格式与独占格式探测：https://learn.microsoft.com/en-us/windows/win32/coreaudio/device-formats

## Icon tooling

图标是本项目原创 SVG。仅重新生成图标时使用 `resvg_py==0.5.0`（MIT，https://github.com/baseplate-admin/resvg-py）；安装于忽略的 `build/icon-tools/`，不随程序分发。普通构建使用已提交的 ICO。PNG 帧封装为多尺寸 ICO 使用 Python 标准库，无额外运行依赖。

## KISS FFT (ToneScope)

采用：https://github.com/mborgerding/kissfft （BSD-3-Clause，固定 131.2.0，commit `7bce4153c6bc8aba2db0e889e576f9d00505cbe1`）。
理由：成熟、轻量、支持 double，无 GPL 运行依赖，适合 FFT 和频谱的可复现实验。
接入：所需 C 源码、头文件和许可证放入 `third_party/kissfft/`，编译定义 `kiss_fft_scalar=double`，未修改算法源文件。

## dr_wav (ToneScope)

采用：https://github.com/mackron/dr_libs （MIT-0 或 Unlicense 双许可，commit `4ffbded724cc3e3d2838eebca70858bf5984385a`，2026-11-10）。
理由：已有成熟 WAV/RF64/PCM/float 解码，无需自行实现文件格式解析。
接入：`third_party/dr_libs/dr_wav.h` 与 `LICENSE`，分析器通过 PCM32 或原生 float64 解码保留有效精度。
