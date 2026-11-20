# Bad Apple!! on UNIHIKER K10

[![build](https://github.com/SmallStackApple/badapple-on-k10/actions/workflows/build.yml/badge.svg)](https://github.com/SmallStackApple/badapple-on-k10/actions/workflows/build.yml)
[![license: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
![platform: ESP32-S3](https://img.shields.io/badge/platform-ESP32--S3-informational)
![ESP-IDF v6.1](https://img.shields.io/badge/ESP--IDF-v6.1-blue)

Play the full 3:39 *Bad Apple!!* shadow-art video — with sound — on a **DFRobot UNIHIKER K10**.
Video and audio live in the board's internal 16 MB flash: no SD card, no Arduino core, no external DAC.

**English** | [简体中文](README.zh-CN.md)

> [!NOTE]
> This repository ships **no** video, audio, or frames from the original work. You supply your own copy locally and build the media blob yourself. See [License & third-party content](#license--third-party-content).

## At a glance

| | |
|---|---|
| Board | UNIHIKER K10 — ESP32-S3 (N16R8), 240 MHz, 16 MB flash, 8 MB PSRAM |
| Display | 2.8" 320×240 IPS (ILI9341), SPI @ 40 MHz |
| Video | 304×228, 30 fps, 1-bit full-frame RLE — 6,574 frames, ~982 B/frame |
| Audio | 16 kHz mono s16le PCM → NS4168, biquad EQ + soft limiter |
| Media | 12.87 MiB blob in a dedicated 14.6 MiB flash partition |
| Firmware | ESP-IDF v6.1, FreeRTOS, two pinned tasks, no WiFi/BT |
| Boot to first frame | ≈1.4 s from reset (≈0.9 s of that is panel init) |
| Controls | **A** pause/resume, **B** restart; loops automatically |

## How it works

```
 flash `media` partition @ 0x210000                    core 1
 ┌────────────────┬──────────────────────────┐   ┌─────────────────────┐
 │ BAV1 header    │ frame index + RLE frames │──▶│ RLE decode          │
 │ + frame index  │ 16 kHz mono PCM          │   │ → RGB565 framebuffer│
 └────────────────┴───────────┬──────────────┘   └──────────┬──────────┘
                              │ core 0                       │ SPI DMA 40 MHz
                     ┌────────▼─────────┐            ┌────────▼─────────┐
                     │ biquad EQ        │            │ ILI9341          │
                     │ volume 0.45      │──I2S──▶    │ 304×228 centered │
                     │ soft clip ±0.95  │  NS4168    │ on 320×240 panel │
                     └──────────────────┘            └──────────────────┘
```

- **Video path** — a `uint32` index gives O(1) access to any frame. The frame is decoded into a
  full RGB565 buffer (304×228×2 = 138,624 B) and pushed as one DMA transfer, so there is no
  half-drawn frame on screen.
- **Audio path** — the audio task reads 2,048 mono samples at a time, runs one peaking biquad
  (−5 dB @ 3.5 kHz, Q = 1.2), scales to 45 %, soft-clips at ±0.95, duplicates to stereo and
  feeds a 4 × 128-frame I2S DMA ring.
- **Sync** — one shared start timestamp. The video task sleeps until `start + frame/30`, and
  skips ahead if it ever falls more than three frames behind, so audio never drifts.
- **Bitrate** — 6.46 MB of RLE over 219 s is ~29 KB/s of flash reads; the bottleneck is the
  panel link, not storage.

## Performance budget

| Stage | Cost | Budget |
|---|---|---|
| Full-frame SPI transfer (138,624 B @ 40 MHz) | ≈27.7 ms | 33.3 ms/frame |
| RLE decode + RGB565 fill (69,312 px) | < 2 ms | — |
| Flash read (average frame) | ≈1 ms | — |
| Internal RAM | ≈160 KB (framebuffer 135 KB, audio 8 KB, RLE 4 KB) | 320 KB DRAM |
| Firmware image | 246 KB | 2 MB app partition |

A full 320×240 frame does not fit the 40 MHz SPI budget at 30 fps, hence the 304×228 video area
with an 8/6 px border.

## Repository layout

```
firmware/
  partitions.csv        2 MB app + 14.6 MB media
  sdkconfig.defaults    16 MB flash, DIO @ 80 MHz, USB-JTAG console
  main/
    main.c              boot order: board → LCD → media → audio → player
    k10_pins.h          every pin and TCA9555 bit
    k10_board.c/.h      I2C + TCA9555 (backlight, amp, buttons)
    k10_lcd.c/.h        ILI9341 init + chunked DMA blitting
    k10_audio.c/.h      I2S std-mode output
    media.c/.h          partition reader, frame index, PCM access
    player.c/.h         RLE decode, 30 fps timeline, audio task
tools/
  build.py              one-command media + build + flash
  build_media.py        ffmpeg → 1-bit RLE + PCM packer
  serial_capture.py     serial log capture (debug helper)
.github/workflows/      CI: firmware build, media pipeline test, tools check
```

## Quick start

### Prerequisites

| Tool | Version | Notes |
|---|---|---|
| [ESP-IDF](https://docs.espressif.com/projects/esp-idf/) | v6.1 | `idf.py` must be on `PATH` |
| Python | 3.11+ | the ESP-IDF virtualenv is fine |
| ffmpeg / ffprobe | 6+ | on `PATH` |
| yt-dlp | optional | only if `--source` is a URL |
| USB cable | — | native USB-Serial/JTAG; no extra driver |

### One command

```powershell
git clone https://github.com/SmallStackApple/badapple-on-k10
cd badapple-on-k10
. C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1   # or: . $HOME/esp/esp-idf/export.sh

python tools\build.py --port COM8 --source "D:\videos\badapple!!.mp4"
```

That single command generates the media blob, builds the firmware, flashes bootloader + app +
partition table, and writes the media image to `0x210000`.

| Flag | Meaning |
|---|---|
| `--port COM8` | serial port (or set `ESPPORT`) |
| `--source PATH\|URL` | source video; defaults to the only `*.mp4` in the repo root |
| `--skip-media` | firmware only (media already flashed) |
| `--no-flash` | convert + build, touch no hardware |
| `--force` | rebuild media even if it looks up to date |
| `--preview PATH` | where to write one 304×228 PNG (default `media/preview.png`) |
| `--baud N` | media flash baud (default 921600) |

### Where to get the source video

*Bad Apple!!* is copyrighted, so it cannot be committed here. Use a copy you are allowed to use
(your own disc rip or recording). If you pass a URL, `tools/build.py` invokes `yt-dlp` and caches
the result in `.cache/` (gitignored) — you are responsible for complying with the source
platform's terms and local law.

### Manual build (no helper script)

```powershell
python tools\build_media.py "badapple!!.mp4" -o media\badapple.bin --preview media\preview.png

cd firmware
idf.py set-target esp32s3      # first time only
idf.py build
idf.py -p COM8 flash
esptool --chip esp32s3 -p COM8 -b 921600 write-flash 0x210000 ..\media\badapple.bin
```

`idf.py flash` writes bootloader, partition table and app. The media blob is separate, so a
firmware-only update never has to rewrite 13 MB of flash.

## Media format (`BAV1` v1)

`media/badapple.bin` is 13,492,054 bytes (12.87 MiB), little-endian throughout:

| Offset | Type | Field | Value |
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
| `0x24` | `u32[6574]` | frame_offsets | absolute offsets |
| `0x66DC` | — | frames | `u16 len` + RLE, 4-byte aligned |
| `0x62DDA0` | — | PCM | mono s16le, 7,012,790 B |

**RLE token** — one byte per run:

```
  bit 7      bits 6..0
┌────────┬────────────────┐
│ color  │ run length 1-127│
└────────┴────────────────┘
  1 = white, 0 = black
```

Runs are emitted in raster order and fill exactly 69,312 pixels; runs longer than 127 are split.
The decoder zero-fills the remainder if a frame is short.

## Configuration

### Pinout

| Function | Pin / bit |
|---|---|
| LCD (ILI9341, SPI2) | MOSI 21, SCK 12, CS 14, DC 13, RST NC, 40 MHz |
| I2S (NS4168) | MCLK 3, BCLK 0, WS 38, DOUT 45 |
| I2C | SDA 47, SCL 48, TCA9555 @ `0x20` |
| Backlight | TCA9555 **P0.0** (drive high) |
| Amplifier enable | TCA9555 **P1.7** (drive high) |
| Button A | TCA9555 **P1.4** (active low) |
| Button B | TCA9555 **P0.2** (active low) |

The TCA9555 is quasi-bidirectional: after configuring a button pin as an input you must also set
its output latch to `1` to enable the weak pull-up. The firmware writes `OUT0 = OUT1 = 0xFF`,
which is what makes A/B read reliably.

Panel setup: `MADCTL = 0xE0` (MX | MY | MV), inversion off, BGR bit clear.

### Partition table

| Name | Type | Offset | Size |
|---|---|---|---|
| `nvs` | data/nvs | `0x9000` | 16 KB |
| `phy_init` | data/phy | `0xD000` | 4 KB |
| `factory` | app | `0x10000` | 2 MB |
| `media` | data (spiffs subtype) | `0x210000` | 14.6 MB |

The media partition overwrites the factory `model` / `voice_data` / `fr` AI partitions, so the
stock AI features are unavailable while this firmware is installed. Restore with the backup
described below.

### Audio tuning

All tunables are macros at the top of `firmware/main/player.c`:

| Macro | Default | Effect |
|---|---|---|
| `AUDIO_VOLUME` | `45` | output level in percent |
| `EQ_FREQ_HZ` | `3500.0f` | peaking filter centre (the small speaker's harsh band) |
| `EQ_Q` | `1.2f` | filter width |
| `EQ_GAIN_DB` | `-5.0f` | cut depth |

The on-board 2 W speaker + fixed-gain NS4168 is the limiting factor; a small external speaker
sounds noticeably better.

## CI

`.github/workflows/build.yml` runs on every push/PR to `main` and on demand:

| Job | What it does |
|---|---|
| `firmware (esp32s3)` | builds in `espressif/idf:release-v6.1`, prints `idf.py size`, uploads `badapple-firmware` (app, bootloader, partition table, `flash_args`) |
| `media pipeline` | packs a synthetic 4 s ffmpeg test clip and validates the `BAV1` header/offsets |
| `tools (python)` | byte-compiles `tools/` and checks `build_media.py --help` |

CI never needs the copyrighted source video or a board.

## Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| Solid red screen at boot | `media_open()` failed — blank partition | flash `media/badapple.bin` at `0x210000` |
| `bad media magic` in the log | media written at the wrong offset or truncated | reflash with `tools/build.py --force` |
| No sound | amplifier not enabled | check TCA9555 `P1.7` and the `0xFF` output latches |
| Harsh / distorted audio | speaker limit, clipping | lower `AUDIO_VOLUME`, deepen `EQ_GAIN_DB` |
| A/B unresponsive | input latches not set | keep `OUT0/OUT1 = 0xFF` |
| `--port` not found | wrong USB socket | use the native USB port; `idf.py -p COM8 monitor` lists nothing = driver/cable issue |
| Flash fails at 921600 | cable or hub | retry with `--baud 460800` |
| AI features gone | media partition replaced the model partitions | restore the factory image (below) |

## Backup & restore

A full 16 MB factory image was read and verified before the first flash:

```
backups/k10_factory_16mb.bin
SHA256 6C003CF214726BD593CE744A1B1394063557F2039DBECE9BFD3B984752E2E24E
```

Restore the whole chip:

```powershell
esptool --chip esp32s3 -p COM8 -b 921600 write-flash 0 `
  "F:\Code\badapple-on-k10\backups\k10_factory_16mb.bin"
```

`backups/` is gitignored — keep your own copy somewhere safe. DFRobot's *Mind+ → Restore
factory settings* also works if you never backed up.

## License & third-party content

Source code and documentation are [MIT licensed](LICENSE). The *Bad Apple!!* music, video and
characters are **not** covered by that license and are **not** distributed here; see
[NOTICE.md](NOTICE.md) for details. You are responsible for the media you generate.

## Credits

- *Bad Apple!!* — original theme by ZUN (Touhou Project); arrangement by Masayoshi Minoshima /
  Alstroemeria Records; the monochrome silhouette PV belongs to its respective creators.
- DFRobot for the UNIHIKER K10 hardware documentation.
- Espressif for ESP-IDF and the ILI9341/I2S drivers used here.
