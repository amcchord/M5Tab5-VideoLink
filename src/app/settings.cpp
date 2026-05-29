#include "app/settings.h"

#include <cstring>
#include <cstdio>

#include "nvs.h"
#include "esp_mac.h"
#include "esp_log.h"

static const char *TAG = "settings";

#define NVS_NAMESPACE "videolink"
#define NVS_BLOB_KEY  "config"
#define CONFIG_VERSION 2u

namespace {

struct StoredConfig {
    uint32_t version;
    settings::Config cfg;
};

settings::Config s_cfg;
bool s_loaded = false;

void make_default_name(char *out, size_t out_len)
{
    // Use the P4's own efuse base MAC: it is unique and available at boot
    // (the WiFi MAC comes from the C6 and isn't ready until ESP-Hosted is up).
    uint8_t mac[6] = {0};
    if (esp_read_mac(mac, ESP_MAC_BASE) != ESP_OK) {
        esp_read_mac(mac, ESP_MAC_EFUSE_FACTORY);
    }
    snprintf(out, out_len, "Tab5-%02X%02X%02X", mac[3], mac[4], mac[5]);
}

void load_defaults(settings::Config &c)
{
    memset(&c, 0, sizeof(c));
    char name[16];
    make_default_name(name, sizeof(name));

    c.wifi_mode = settings::WifiMode::Create;
    c.ssid[0] = '\0';
    c.password[0] = '\0';
    snprintf(c.ap_ssid, sizeof(c.ap_ssid), "VideoLink-%s", name + 5); // suffix only
    strncpy(c.ap_password, "videolink", sizeof(c.ap_password) - 1);
    strncpy(c.device_name, name, sizeof(c.device_name) - 1);
    c.volume = 70;
    c.mic_gain = 60;
    c.mic_muted = false;
    c.codec = settings::VideoCodec::MJPEG; // bring-up default; H.264 once validated
    c.quality = 60;
}

esp_err_t persist(const settings::Config &c)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    StoredConfig stored = {.version = CONFIG_VERSION, .cfg = c};
    err = nvs_set_blob(h, NVS_BLOB_KEY, &stored, sizeof(stored));
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

} // namespace

namespace settings {

esp_err_t init()
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        return err;
    }

    StoredConfig stored = {};
    size_t len = sizeof(stored);
    err = nvs_get_blob(h, NVS_BLOB_KEY, &stored, &len);
    nvs_close(h);

    if (err == ESP_OK && len == sizeof(stored) && stored.version == CONFIG_VERSION) {
        s_cfg = stored.cfg;
        ESP_LOGI(TAG, "Loaded settings (mode=%s, name=%s)",
                 s_cfg.wifi_mode == WifiMode::Create ? "AP" : "STA", s_cfg.device_name);
    } else {
        ESP_LOGW(TAG, "No valid settings found (%s); seeding defaults", esp_err_to_name(err));
        load_defaults(s_cfg);
        persist(s_cfg);
    }
    s_loaded = true;
    return ESP_OK;
}

const Config &get()
{
    return s_cfg;
}

esp_err_t set(const Config &cfg)
{
    s_cfg = cfg;
    return persist(s_cfg);
}

esp_err_t save()
{
    return persist(s_cfg);
}

esp_err_t set_volume(uint8_t volume)
{
    s_cfg.volume = volume > 100 ? 100 : volume;
    return persist(s_cfg);
}

esp_err_t set_mic_gain(uint8_t gain)
{
    s_cfg.mic_gain = gain > 100 ? 100 : gain;
    return persist(s_cfg);
}

esp_err_t set_mic_muted(bool muted)
{
    s_cfg.mic_muted = muted;
    return persist(s_cfg);
}

} // namespace settings
