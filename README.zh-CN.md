# Bad Apple!! on UNIHIKER K10

[![build](https://github.com/SmallStackApple/badapple-on-k10/actions/workflows/build.yml/badge.svg)](https://github.com/SmallStackApple/badapple-on-k10/actions/workflows/build.yml)
[![license: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
![platform: ESP32-S3](https://img.shields.io/badge/platform-ESP32--S3-informational)
![ESP-IDF v6.1](https://img.shields.io/badge/ESP--IDF-v6.1-blue)

在 **DFRobot 行空板 K10** 上播放完整的 3 分 39 秒《Bad Apple!!》影绘 MV，带声音。
视频和音频都存放在板载 16 MB flash 里：不用 TF 卡，不用 Arduino 框架，也不用额外接线。

[English](README.md) | **简体中文**

> [!NOTE]
> 仓库**不包含**原视频、音频或任何视频帧。请自备素材并在本地生成媒体包，详见[许可证与第三方内容](#许可证与第三方内容)。

## 一览

| | |
|---|---|
| 开发板 | UNIHIKER K10 — ESP32-S3（N16R8），240 MHz，16 MB flash，8 MB PSRAM |
| 屏幕 | 2.8 寸 320×240 彩色 LCD（ILI9341），SPI @ 40 MHz |
| 视频 | 304×228，30 fps，1-bit 全帧 RLE — 6574 帧，平均约 982 B/帧 |
| 音频 | 16 kHz 单声道 s16le PCM → NS4168，双二阶 EQ + 软限幅 |
| 媒体 | 12.87 MiB，写入独立的 13.94 MiB（14.61 MB）分区 |
| 固件 | ESP-IDF v6.1，FreeRTOS，两个绑核任务，无 WiFi/BT |
| 从上电到首帧 | ≈1.4 秒（约 0.9 秒启动+屏幕初始化，再加 0.5 秒同步门） |
| 按键 | **A** 暂停/继续，**B** 从头播放；播完自动循环 |

## 实现思路

```
 flash `media` 分区 @ 0x210000                        core 1
 ┌────────────────┬──────────────────────────┐   ┌─────────────────────┐
 │ BAV1 头        │ 帧索引 + RLE 帧数据      │──▶│ RLE 解码            │
 │ + 帧索引       │ 16 kHz 单声道 PCM        │   │ → RGB565 帧缓冲     │
 └────────────────┴───────────┬──────────────┘   └──────────┬──────────┘
                              │ core 0                       │ SPI DMA 40 MHz
                     ┌────────▼─────────┐            ┌────────▼─────────┐
                     │ 双二阶 EQ        │            │ ILI9341          │
                     │ 音量 0.45        │──I2S──▶    │ 304×228 居中显示 │
                     │ 软限幅 ±0.95     │  NS4168    │ 于 320×240 面板  │
                     └──────────────────┘            └──────────────────┘
```

- **视频通路**：`uint32` 帧索引可以 O(1) 定位任意帧。整帧先解码到 RGB565 缓冲
  （304×228×2 = 138,624 B），再按 32 行一块（约 19 KB，低于 DMA 上限）推给屏幕，
  第一像素发出前整帧已经准备好。
- **音频通路**：音频任务每次读 2048 个单声道样本，过一个峰值双二阶滤波器
  （3.5 kHz、−5 dB、Q=1.2），乘 0.45 音量，在 ±0.95 处软限幅，再复制成立体声，
  写入 4 × 128 帧的 I2S DMA 环形缓冲。
- **同步**：只有一个共享起始时间。视频任务睡到 `start + frame/30`；如果落后超过三帧，
  直接跳到当前时间对应的帧，因此音频不会逐渐漂移。
- **码率**：219 秒共 6.46 MB RLE，平均约 29 KB/s 的 flash 读取；瓶颈在屏幕链路，
  不在存储。

## 性能余量

| 环节 | 开销 | 预算 |
|---|---|---|
| 整帧 SPI 传输（138,624 B @ 40 MHz，32 行一块） | ≈27.7 ms | 33.3 ms/帧 |
| RLE 解码 + RGB565 填充（69,312 像素） | ≈1–2 ms（估算） | — |
| flash 读取（平均一帧） | ≈2 ms（估算） | — |
| 内部 RAM | ≈151 KB（帧缓冲 135 KB、PCM 4 KB + 立体声 8 KB、RLE 4 KB） | 512 KB SRAM |
| 固件体积 | 246 KiB | 2 MiB app 分区 |

320×240 的整帧光传输就要 ≈30.7 ms，33.3 ms 的帧预算只剩 ≈2.6 ms 留给解码，所以视频区缩到
304×228，四周留 8/6 像素黑边。

## 目录结构

```
firmware/
  partitions.csv        2 MiB app + 13.9 MiB media
  sdkconfig.defaults    16 MB flash、DIO @ 80 MHz、USB-JTAG 控制台
  main/
    main.c              启动顺序：board → LCD → media → audio → player
    k10_pins.h          所有引脚和 TCA9555 位定义
    k10_board.c/.h      I2C + TCA9555（背光、功放、按键）
    k10_lcd.c/.h        ILI9341 初始化 + 分块 DMA 推屏
    k10_audio.c/.h      I2S 标准模式输出
    media.c/.h          分区读取、帧索引、PCM 访问
    player.c/.h         RLE 解码、30 fps 时间轴、音频任务
tools/
  build.py             一条命令完成媒体生成 + 编译 + 烧录
  build_media.py       ffmpeg → 1-bit RLE + PCM 打包
  serial_capture.py    串口日志抓取（调试用）
.github/workflows/      CI：固件编译、媒体流水线测试、工具检查
```

## 快速开始

### 环境要求

| 工具 | 版本 | 说明 |
|---|---|---|
| [ESP-IDF](https://docs.espressif.com/projects/esp-idf/) | v6.1 | `idf.py` 要在 `PATH` 里 |
| Python | 3.11+ | 用 ESP-IDF 自带的虚拟环境即可 |
| ffmpeg / ffprobe | 较新版本（4.3+） | 在 `PATH` 里 |
| yt-dlp | 可选 | 只有 `--source` 传 URL 时才需要 |
| USB 线 | — | 用原生 USB-Serial/JTAG，不需要额外驱动 |

### 一条命令

> [!WARNING]
> 烧录会把原厂的 `model` / `voice_data` / `fr` 分区换成媒体镜像，**Mind+ 恢复不了**。
> 请先按[备份与恢复](#备份与恢复)做一份完整的 16 MB flash 备份。`tools/build.py` 在写媒体镜像前会要求确认。

```powershell
git clone https://github.com/SmallStackApple/badapple-on-k10
cd badapple-on-k10
# 这里的路径取决于你自己的 ESP-IDF 安装位置。
# 下面只是 Windows 安装器的默认路径；请换成实际的 profile 脚本，再确认 `idf.py --version`
. C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1
# Linux/macOS 通常是: . $HOME/esp/esp-idf/export.sh

# D:\videos\... 只是示例，把 --source 换成你自己那份视频的路径
# （本地路径即可；装了 yt-dlp 也可以直接传 http(s) URL）。
python tools\build.py --port COM8 --source "D:\videos\badapple!!.mp4"
```

> 以 `.` 开头那一行是加载 ESP-IDF 环境，路径取决于你的安装位置；上面的只是 Windows 安装器的默认值。
> 激活后跑一下 `idf.py --version`，确认输出是 `v6.1`。

这一条命令会：生成媒体包 → 编译固件 → 烧写 bootloader/app/分区表 → 把媒体写到 `0x210000`。

| 参数 | 作用 |
|---|---|
| `--port COM8` | 串口（也可用环境变量 `ESPPORT`） |
| `--source PATH\|URL` | 源视频；不传时自动使用仓库根目录唯一的 `*.mp4` |
| `--yes` | 仅在已做好整片备份后，才用它跳过 AI 分区确认 |
| `--skip-media` | 只更新固件（媒体已烧过） |
| `--no-flash` | 只生成 + 编译，不碰硬件 |
| `--force` | 即使媒体看起来是最新的也重新生成、重烧 |
| `--preview PATH` | 导出一帧 304×228 PNG（默认 `media/preview.png`） |
| `--baud N` | 媒体烧写波特率（默认 921600） |

### 源视频从哪来

《Bad Apple!!》有版权，不能放进仓库。请使用你有权使用的副本（例如自己抓取/录制的版本）。
`--source` 传 URL 时，脚本会调用 `yt-dlp` 下载到 `.cache/`（已 gitignore）——请自行确认
符合来源平台条款和当地法律。

### 手动编译（不用脚本）

```powershell
python tools\build_media.py "badapple!!.mp4" -o media\badapple.bin --preview media\preview.png

cd firmware
idf.py set-target esp32s3      # 仅首次
idf.py build
idf.py -p COM8 flash
esptool --chip esp32s3 -p COM8 -b 921600 write-flash 0x210000 ..\media\badapple.bin
```

`idf.py flash` 写 bootloader、分区表和 app；媒体是独立镜像，所以只改固件时不用重写 13 MB。

## 媒体格式（`BAV1` v1）

`media/badapple.bin` 共 13,492,054 字节（12.87 MiB），全部小端：

| 偏移 | 类型 | 字段 | 值 |
|---|---|---|---|
| `0x00` | `char[4]` | magic | `BAV1` |
| `0x04` | `u16` | version | `1` |
| `0x06` | `u16` | width | `304` |
| `0x08` | `u16` | height | `228` |
| `0x0A` | `u16` | fps | `30` |
| `0x0C` | `u32` | frame_count | `6574` |
| `0x10` | `u32` | audio_rate | `16000` |
| `0x14` | `u32` | audio_samples | `3,506,395` |
| `0x18` | `u32` | frame_data_off | `0x66DC` |
| `0x1C` | `u32` | audio_off | `0x62DDA0` |
| `0x20` | `u32` | audio_bytes | `7,012,790` |
| `0x24` | `u32[6574]` | frame_offsets | 绝对偏移 |
| `0x66DC` | — | 帧数据 | `u16 len` + RLE，4 字节对齐 |
| `0x62DDA0` | — | PCM | 单声道 s16le，7,012,790 B |

**RLE token**：每字节表示一段连续像素。

```
  bit 7      bits 6..0
┌────────┬────────────────┐
│  颜色  │ 连续像素数 1-127│
└────────┴────────────────┘
  1 = 白，0 = 黑
```

按光栅顺序填充，整帧恰好 69,312 像素；超过 127 的连续段会拆成多个 token。
如果一帧数据不够，解码器会用黑色补齐。

## 配置

### 引脚

| 功能 | 引脚 / 位 |
|---|---|
| LCD（ILI9341，SPI2） | MOSI 21、SCK 12、CS 14、DC 13、RST NC、40 MHz |
| I2S（NS4168） | MCLK 3、BCLK 0、WS 38、DOUT 45 |
| I2C | SDA 47、SCL 48、TCA9555 @ `0x20` |
| 背光 | TCA9555 **P0.0**（输出高） |
| 功放使能 | TCA9555 **P1.7**（输出高） |
| A 键 | TCA9555 **P1.4**（低有效） |
| B 键 | TCA9555 **P0.2**（低有效） |

TCA9555 是准双向口：按键引脚配置成输入后，输出锁存器也必须写 `1` 才能启用弱上拉。
固件把 `OUT0`、`OUT1` 都写成 `0xFF`，这是 A/B 能稳定读取的关键。

屏幕参数：`MADCTL = 0xE0`（MX | MY | MV），关闭反色，BGR 位清零。

### 分区表

| 名称 | 类型 | 偏移 | 大小 |
|---|---|---|---|
| `nvs` | data/nvs | `0x9000` | 16 KB |
| `phy_init` | data/phy | `0xD000` | 4 KB |
| `factory` | app | `0x10000` | 2 MiB |
| `media` | data（spiffs 子类型） | `0x210000` | 13.94 MiB（14.61 MB） |

媒体分区覆盖了原厂的 `model` / `voice_data` / `fr` 三个 AI 模型分区，装了这个固件后
板载 AI 功能就不可用。**只有整片备份能恢复它们**：Mind+ 只能刷回它自己的固件，
不会还原原厂的 AI 模型分区。所以请在第一次烧录前做好备份（见下文）。

### 音频调参

所有可调参数都是 `firmware/main/player.c` 顶部的宏：

| 宏 | 默认值 | 作用 |
|---|---|---|
| `AUDIO_VOLUME` | `45` | 输出音量百分比 |
| `EQ_FREQ_HZ` | `3500.0f` | 峰值滤波器中心频率（小喇叭最刺耳的频段） |
| `EQ_Q` | `1.2f` | 滤波器带宽 |
| `EQ_GAIN_DB` | `-5.0f` | 衰减深度 |

瓶颈是板载 2 W 小喇叭和固定增益的 NS4168；外接小音箱会明显更好。

## CI

`.github/workflows/build.yml` 在 push/PR 到 `main` 和手动触发时运行：

| Job | 内容 |
|---|---|
| `firmware (esp32s3)` | 在 `espressif/idf:release-v6.1` 镜像里编译，输出 `idf.py size`，上传 `badapple-firmware`（app、bootloader、分区表、`flash_args`） |
| `media pipeline` | 用 ffmpeg 生成 4 秒合成测试视频，打包后校验 `BAV1` 头和偏移 |
| `tools (python)` | 对 `tools/` 做字节码检查，并跑 `build_media.py --help` / `build.py --help` |

CI 不需要源视频，也不需要真机。

## 故障排查

| 现象 | 可能原因 | 处理 |
|---|---|---|
| 开机纯蓝屏 | `media_open()` 失败，媒体分区是空的 | 把 `media/badapple.bin` 写到 `0x210000` |
| 日志出现 `bad media magic` | 媒体写错偏移或被截断 | `tools/build.py --force` 重烧 |
| 没有声音 | 功放没使能 | 检查 TCA9555 `P1.7` 和 `0xFF` 输出锁存 |
| 声音刺耳/破音 | 小喇叭到极限 | 降低 `AUDIO_VOLUME`，加深 `EQ_GAIN_DB` |
| A/B 没反应 | 输入锁存没置高 | 保持 `OUT0/OUT1 = 0xFF` |
| 找不到串口 | USB 口插错 | 用原生 USB 口；`idf.py -p COM8 monitor` 无输出就检查线材 |
| 921600 烧写失败 | 线材或 USB Hub | 用 `--baud 460800` 重试 |
| AI 功能没了 | 媒体分区覆盖了模型分区 | 恢复整片备份（见下文） |

> 出错画面填充的是 `k10_rgb565(255, 0, 0)`。这块屏的红蓝通道是反的（颜色顺序和 MADCTL 设置对不上），
> 所以实际显示为**蓝色**。视频只有黑白色，正常播放不受影响。

## 备份与恢复

首次改动 flash 前读取并校验了完整 16 MB 出厂镜像：

```
backups/k10_factory_16mb.bin
SHA256 6C003CF214726BD593CE744A1B1394063557F2039DBECE9BFD3B984752E2E24E
```

整片恢复：

```powershell
esptool --chip esp32s3 -p COM8 -b 921600 write-flash 0 `
  "F:\Code\badapple-on-k10\backups\k10_factory_16mb.bin"
```

`backups/` 已 gitignore，请自己另存一份（上面的路径改成你实际存放的位置）。AI 分区**没有 Mind+ 兜底**：
Mind+ 只能恢复它自己的固件，恢复不了原厂的 `model` / `voice_data` / `fr` 数据。如果没备份过，
这几个分区就找不回来了。

## 许可证与第三方内容

代码和文档采用 [MIT License](LICENSE)。《Bad Apple!!》的音乐、影像和角色**不在**该许可范围内，
本仓库也**不分发**它们，细节见 [NOTICE.md](NOTICE.md)。生成的媒体包如何使用由你自行负责。

## 致谢

- 《Bad Apple!!》——原曲 ZUN（东方 Project），编曲 Masayoshi Minoshima / Alstroemeria Records；
  黑白影绘 PV 版权归其作者所有。
- DFRobot 提供 UNIHIKER K10 的硬件资料。
- Espressif 提供 ESP-IDF 及本项目用到的 ILI9341/I2S 驱动。
