# Third-party content and trademarks

This repository contains original source code and documentation only. It does **not** include
any music, video, still frame, or other media from *Bad Apple!!* or any other work.

## Bad Apple!!

- *Bad Apple!!* — original theme by ZUN (Touhou Project); arrangement by Masayoshi Minoshima /
  Alstroemeria Records; the monochrome silhouette PV belongs to its respective creators.
- All rights remain with the respective copyright holders. Nothing in the MIT license in
  `LICENSE` grants any rights to that material.
- `tools/build_media.py` is a generic converter. Running it requires a source file that **you**
  supply. Do not redistribute generated media (`media/badapple.bin`), recordings of the device
  playing, or extracted frames unless you have the necessary rights.
- A preview frame is never committed: `docs/preview.png`, `media/*.png`, and video files are
  excluded by `.gitignore`.

## Hardware documentation

Pin assignments, the TCA9555 layout, and the flash partition map were derived from publicly
available DFRobot UNIHIKER K10 documentation and from measurements on a board owned by the
author. This project is not affiliated with or endorsed by DFRobot.

## Trademarks

UNIHIKER and DFRobot are trademarks of their respective owners. ESP32 and ESP-IDF are trademarks
of Espressif Systems. Other names are the property of their respective owners and are used only
for identification.

## Your responsibility

If you build, flash, or publish anything derived from third-party media, you are responsible for
complying with the applicable copyright law and the terms of the platform you obtained it from.
