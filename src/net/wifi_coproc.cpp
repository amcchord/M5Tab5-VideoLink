#include "net/wifi_coproc.h"

#include <cstdio>
#include <cstdint>

#include "ui/ui.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_hosted.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs.h"

static const char *TAG = "coproc";

// The embedded C6 firmware is ESP-Hosted slave v2.12.8 (matches the host lib).
#define EMBEDDED_FW_MAJOR 2
#define EMBEDDED_FW_CODE  21208u // 2.12.8 -- bump if c6_firmware/network_adapter.bin changes

// Records the last embedded firmware we successfully flashed, so that if a
// flash "succeeds" but the C6 still boots an old image we don't loop forever.
static uint32_t flashed_marker_get()
{
    nvs_handle_t h;
    uint32_t v = 0;
    if (nvs_open("videolink", NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u32(h, "c6fw", &v);
        nvs_close(h);
    }
    return v;
}

static void flashed_marker_set(uint32_t v)
{
    nvs_handle_t h;
    if (nvs_open("videolink", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u32(h, "c6fw", v);
        nvs_commit(h);
        nvs_close(h);
    }
}

// Embedded ESP32-C6 network_adapter.bin (see platformio.ini board_build.embed_files).
extern const uint8_t s_fw_start[] asm("_binary_network_adapter_bin_start");
extern const uint8_t s_fw_end[] asm("_binary_network_adapter_bin_end");

namespace net {

bool coproc_update_if_needed()
{
    esp_hosted_coprocessor_fwver_t ver = {};
    if (esp_hosted_get_coprocessor_fwversion(&ver) != 0) {
        ESP_LOGW(TAG, "could not read C6 firmware version; skipping update");
        return false;
    }
    ESP_LOGI(TAG, "C6 ESP-Hosted firmware v%u.%u.%u", (unsigned) ver.major1,
             (unsigned) ver.minor1, (unsigned) ver.patch1);

    if ((int) ver.major1 >= EMBEDDED_FW_MAJOR) {
        return false; // current enough; data path should work
    }

    if (flashed_marker_get() == EMBEDDED_FW_CODE) {
        ESP_LOGW(TAG, "already flashed embedded C6 fw but it still reports old; "
                      "skipping update to avoid a loop (WiFi may be unreliable)");
        return false;
    }

    const size_t len = (size_t) (s_fw_end - s_fw_start);
    ESP_LOGW(TAG, "C6 firmware is outdated; flashing embedded v2.12.8 (%u bytes)", (unsigned) len);
    ui::set_status("Updating WiFi firmware 0%");

    if (esp_hosted_slave_ota_begin() != ESP_OK) {
        ESP_LOGE(TAG, "ota_begin failed");
        ui::set_status("WiFi firmware update failed");
        return false;
    }

    const size_t CHUNK = 1500; // matches esp_hosted's OTA example (RPC payload limit)
    size_t off = 0;
    int last_pct = -1;
    while (off < len) {
        size_t n = (len - off) > CHUNK ? CHUNK : (len - off);
        if (esp_hosted_slave_ota_write((uint8_t *) (s_fw_start + off), (uint32_t) n) != ESP_OK) {
            ESP_LOGE(TAG, "ota_write failed at offset %u", (unsigned) off);
            esp_hosted_slave_ota_end();
            ui::set_status("WiFi firmware update failed");
            return false;
        }
        off += n;
        int pct = (int) (off * 100 / len);
        if (pct != last_pct && (pct % 5 == 0 || off == len)) {
            char s[40];
            snprintf(s, sizeof(s), "Updating WiFi firmware %d%%", pct);
            ui::set_status(s);
            last_pct = pct;
        }
    }

    if (esp_hosted_slave_ota_end() != ESP_OK) {
        ESP_LOGE(TAG, "ota_end failed");
        ui::set_status("WiFi firmware update failed");
        return false;
    }

    flashed_marker_set(EMBEDDED_FW_CODE);
    ESP_LOGI(TAG, "C6 firmware written; activating and rebooting");
    ui::set_status("WiFi firmware updated, rebooting...");
    esp_hosted_slave_ota_activate(); // reboots the C6 (RPC may time out -- expected)
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart(); // restart the P4 so everything re-inits against the new C6 fw
    return true;   // not reached
}

} // namespace net
