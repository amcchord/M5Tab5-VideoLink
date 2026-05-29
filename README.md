# M5Tab5-VideoLink

Bidirectional, standards-based audio/video intercom between two
[M5Stack Tab5](https://docs.m5stack.com/en/core/Tab5) (ESP32-P4) devices.

Each Tab5 captures its camera + microphone, hardware-encodes the video, and
publishes an RTSP stream. At the same time it discovers its peer over mDNS,
pulls the peer's RTSP stream, decodes it, and shows it on the display with
audio playback. Both devices act as sender **and** receiver simultaneously, so
you get a two-way video call between two tablets on the same WiFi (or with one
tablet hosting its own WiFi network).

## Status

Functional first pass, **verified booting on real Tab5 hardware**: display +
touch, camera (SC202CS), MJPEG codec, RTSP server, audio (ES7210/ES8388), and
WiFi via ESP-Hosted (SoftAP + mDNS) all initialize and run stably. The two-way
live link still needs two units to fully exercise — see "Hardware bring-up
status" in [the design notes](docs/architecture.md).

## Features (target)

- Bidirectional video + audio over WiFi (640x360 @ 15fps target).
- H.264 preferred at runtime (hardware encode, software decode) with MJPEG
  (hardware encode **and** decode) as the bring-up path and automatic fallback.
- Standards-based transport: RTSP (RFC 2326) + RTP/RTCP (RFC 3550), with
  H.264 (RFC 6184) / JPEG (RFC 2435) video payloads and Opus (RFC 7587) audio.
- WiFi that can either **create** a network (SoftAP) or **join** one (STA).
- Peer discovery via mDNS / Bonjour (`_rtsp._tcp`).
- On-device settings (gear icon) with an on-screen keyboard.
- Settings persisted across reboots (NVS).
- Volume + microphone gain control.

## Hardware

- 2x M5Stack Tab5 (ESP32-P4 main SoC + ESP32-C6 WiFi co-processor).
- SC2356 MIPI-CSI camera, MIPI-DSI 1280x720 display, ES8388/ES7210 audio.

## Toolchain

This project builds with **PlatformIO** using the
[pioarduino](https://github.com/pioarduino/platform-espressif32) fork of the
`espressif32` platform (it adds ESP32-P4 + ESP-IDF v5.5.x support). The exact
platform release and the Tab5 board definition are pinned in
[`platformio.ini`](platformio.ini) and [`boards/`](boards) so the build is
self-contained.

### Build

```bash
# Using the PlatformIO CLI (installed with the PlatformIO IDE extension):
pio run                     # build
pio run -t upload           # build + flash over USB-C
pio device monitor          # serial console @ 115200
```

> The ESP32-C6 co-processor must be running `esp_hosted` slave firmware for
> WiFi to work. See [docs/architecture.md](docs/architecture.md) for details.

## Pairing two devices

Flash both Tab5s. Then, in the on-device settings (tap the gear icon):

1. On **device A**, set WiFi mode to **Create network (AP)**, give it a name +
   password, and save. It now hosts a WiFi network.
2. On **device B**, set WiFi mode to **Join network (STA)** and enter device A's
   network name + password, and save.
3. Once B joins A's network, the two discover each other over mDNS
   (`_rtsp._tcp`) and the video link starts automatically in both directions.

Alternatively, set **both** devices to **Join** the same existing WiFi network.

## Continuous integration

A ready-to-use PlatformIO build workflow lives at
[`docs/ci/build.yml`](docs/ci/build.yml). Copy it to `.github/workflows/build.yml`
(it is shipped outside that folder because adding workflows requires a token
with the `workflow` scope) to build the firmware on every push/PR.

## Project layout

```
src/
  main.cpp            app entry / bring-up order
  app/                settings (NVS), shared config
  board/              BSP wrapper (display, touch, backlight, C6 power)
  net/                wifi_manager (AP/STA), discovery (mDNS)
  media/              camera (V4L2), codec abstraction (MJPEG/H.264),
                      audio (RTP/L16), dual-core pipeline
  rtsp/               rtp (RFC 2435/6184), rtsp_server, rtsp_client, link
  ui/                 LVGL screens (video + gear-icon settings)
boards/               vendored m5stack-tab5-p4 board definition
```

## License

[MIT](LICENSE)
