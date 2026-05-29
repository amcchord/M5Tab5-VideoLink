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

Early development. See [the design notes](docs/architecture.md) for the full
architecture.

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

## License

[MIT](LICENSE)
