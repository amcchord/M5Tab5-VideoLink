// Persisted application settings, backed by NVS. A single versioned blob is
// stored so the whole struct can evolve with a migration check.
#pragma once

#include <cstdint>
#include "esp_err.h"

namespace settings {

enum class WifiMode : uint8_t {
    Join = 0,    // STA: join an existing network
    Create = 1,  // SoftAP: create a network for the peer to join
};

enum class VideoCodec : uint8_t {
    MJPEG = 0,   // hardware encode + hardware decode (bring-up / fallback)
    H264 = 1,    // hardware encode + software decode (preferred runtime)
};

struct Config {
    WifiMode wifi_mode;
    char ssid[33];          // network to join (STA mode)
    char password[65];
    char ap_ssid[33];       // network to create (AP mode)
    char ap_password[65];
    char device_name[33];   // mDNS instance + hostname base
    uint8_t volume;         // speaker volume, 0..100
    uint8_t mic_gain;       // microphone gain, 0..100
    bool mic_muted;
    VideoCodec codec;       // preferred outgoing codec
    uint8_t quality;        // 0..100 quality/bitrate knob
};

// Loads settings from NVS (or seeds defaults on first boot). Must run after
// nvs_flash_init().
esp_err_t init();

const Config &get();

// Replace the whole config and persist it.
esp_err_t set(const Config &cfg);

// Persist the current in-memory config (after mutating via get()/helpers).
esp_err_t save();

// Convenience mutators that persist immediately.
esp_err_t set_volume(uint8_t volume);
esp_err_t set_mic_gain(uint8_t gain);
esp_err_t set_mic_muted(bool muted);

} // namespace settings
