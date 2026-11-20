# Bad Apple!! on UNIHIKER K10

在 DFRobot 行空板 K10（ESP32-S3 N16R8）上播放 Bad Apple!!。整段 3 分 39 秒的视频和音频存在板载 16MB flash 里，不需要 TF 卡。

![preview](docs/preview.png)

## 效果

- 304×228 @ 30fps，居中显示在 320×240 横屏上
- 原视频是纯黑白剪影，转成 1-bit 全帧 RLE，平均约 982 字节/帧
- 16kHz 单声道 16-bit PCM 音频，通过板载 NS4168 功放播放
- A 键暂停/继续，B 键从头重播，播完自动循环

## 硬件与引脚

| 功能 | 引脚/参数 |
|---|---|
| LCD (ILI9341, SPI2) | MOSI=21, SCK=12, CS=14, DC=13, RST=NC, 40MHz |
| I2S (NS4168) | MCLK=3, BCLK=0, WS=38, DOUT=45 |
| I2C | SDA=47, SCL=48, TCA9555 @ 0x20 |
| 背光 | TCA9555 P0.0（输出 1） |
| 功放使能 | TCA9555 P1.7（输出 1） |
| A 键 | TCA9555 P1.4（低有效） |
| B 键 | TCA9555 P0.2（低有效） |

TCA9555 是准双向口：按键引脚配置成输入后，**输出锁存器也要写 1** 才能启用弱上拉。代码里 `OUT0/OUT1 = 0xFF`，这是 A/B 能稳定读到的关键。

屏幕参数：`MADCTL=0xE0`（MX|MY|MV，横屏 180° 翻转），关闭反色（不发送 `0x21`），BGR 位清零。

## 工程结构

```
firmware/
  partitions.csv        2MB app + 14.61MB media
  sdkconfig.defaults    16MB flash / DIO / 80MHz / USB-JTAG console
  main/
    main.c              初始化 + 启动播放器
    k10_pins.h          所有引脚定义
    k10_board.c/.h      I2C + TCA9555（背光/功放/按键）
    k10_lcd.c/.h        ILI9341 初始化 + 分块 DMA 推屏
    k10_audio.c/.h      I2S 输出
    media.c/.h          读 media 分区、帧索引、PCM
    player.c/.h         RLE 解码、30fps 时间轴、音画任务
tools/
  build_media.py        ffmpeg 转码 + RLE + 打包
  serial_capture.py     串口日志抓取（调试用）
```

## 编译与烧录

需要已安装 ESP-IDF v6.1（本机路径 `F:\.espressif\v6.1\esp-idf`）。

```powershell
$env:PYTHONUTF8=1
. "C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1"

cd firmware
idf.py set-target esp32s3      # 仅首次
idf.py build
idf.py -p COM8 flash
```

媒体生成与烧录：

```powershell
python tools\build_media.py "badapple!!.mp4" -o media\badapple.bin --preview media\preview.png

esptool --chip esp32s3 -p COM8 -b 921600 write-flash 0x210000 media\badapple.bin
```

`idf.py flash` 会写入 bootloader、分区表和 app；媒体单独写在 `0x210000`。之后只更新固件时不需要重新写媒体。

## 媒体格式

`media/badapple.bin` 约 12.87 MiB：

| 偏移 | 内容 |
|---|---|
| 0x00 | `BAV1` + 版本 + 宽高/fps |
| 0x0C | 帧数、音频采样率/样本数、各段偏移 |
| 0x24 | `uint32 frame_offsets[6574]` |
| 0x62DDA0 之前 | 每帧 `uint16 len` + RLE 数据（4 字节对齐） |
| 0x62DDA0 | 16kHz mono s16le PCM，7,012,790 字节 |

RLE token：`bit7 = 颜色(1=白)`，`bit0-6 = 连续像素数(1..127)`，整帧顺序填充 304×228。

## 备份与恢复

改动 flash 前的完整 16MB 出厂备份在 `backups/`（已 gitignore）：

```
backups/k10_factory_16mb.bin
SHA256 6C003CF214726BD593CE744A1B1394063557F2039DBECE9BFD3B984752E2E24E
```

恢复整片 flash：

```powershell
esptool --chip esp32s3 -p COM8 -b 921600 write-flash 0 `
  "F:\Code\badapple-on-k10\backups\k10_factory_16mb.bin"
```

注意：`media` 分区覆盖了原厂的 `model` / `voice_data` / `fr` 三个 AI 模型分区。Bad Apple 固件下 AI 功能不可用；需要时用上面的整片备份或 Mind+「恢复初始设置」恢复。

## 已知限制

- 画面是 304×228，四周有 8/6 像素黑边；320×240 全屏 30fps 受 40MHz SPI 带宽限制
- 音频使用 16kHz PCM（约 7MB），目的是不引入编解码器并留出 flash 余量
- 首次播放前约 0.5 秒黑屏（任务启动 + 音频 DMA 预充）
