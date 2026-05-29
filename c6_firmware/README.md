# ESP32-C6 Wi-Fi co-processor firmware (ESP-Hosted slave)

`network_adapter.bin` is the ESP-Hosted **slave** firmware for the Tab5's
on-board ESP32-C6, built from the **same `espressif/esp_hosted` version
(2.12.8)** as the host library this project links against. It is embedded into
the P4 application and flashed to the C6 over the SDIO link on first boot if the
C6 is running an older/mismatched image (see `src/net/wifi_coproc.cpp`).

Host and slave ESP-Hosted versions must match for the data path (DHCP, real
traffic) to work; the Tab5 ships with an old C6 image that reports version
`0.0.0`, which associates but never passes data.

## Rebuilding (only needed when bumping the esp_hosted version)

The slave is a native ESP-IDF (`idf.py`) project bundled inside the esp_hosted
component at `managed_components/espressif__esp_hosted/slave`. Build it for the
C6 with the same ESP-IDF used by this project (v5.5.4):

```bash
# Copy the slave project + its sibling `common/` out of the managed component
cp -r managed_components/espressif__esp_hosted/slave   /tmp/hosted/slave
cp -r managed_components/espressif__esp_hosted/common  /tmp/hosted/common

cd /tmp/hosted/slave
idf.py set-target esp32c6     # default config is SDIO transport (Tab5-compatible)
idf.py build

cp build/network_adapter.bin  <repo>/c6_firmware/network_adapter.bin
```

The default SDIO slave config matches the Tab5 (the C6's SDIO pins are fixed by
the chip; the Tab5-specific GPIOs are on the P4/host side). No menuconfig
changes are required.
