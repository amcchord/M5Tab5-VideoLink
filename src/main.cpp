// M5Tab5-VideoLink -- application entry point.
//
// Bring-up order is intentional and will grow as subsystems are added:
//   BSP (display/touch/audio) -> NVS/settings -> WiFi (esp_hosted strict
//   order) -> mDNS discovery -> media pipelines -> UI.

#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "nvs_flash.h"

#include "board/board.h"
#include "ui/ui.h"
#include "app/settings.h"
#include "app/app_config.h"
#include "net/wifi_manager.h"
#include "net/discovery.h"
#include "media/video_pipeline.h"
#include "rtsp/link.h"

static const char *TAG = "videolink";

static void on_wifi_status(bool connected, esp_ip4_addr_t ip, const char *msg)
{
    char line[64];
    if (connected) {
        snprintf(line, sizeof(line), "%s  " IPSTR, msg, IP2STR(&ip));
    } else {
        snprintf(line, sizeof(line), "%s", msg);
    }
    ui::set_status(line);
}

static void on_peer_found(const net::Peer &peer)
{
    char line[80];
    snprintf(line, sizeof(line), "peer: %s", peer.name);
    ui::set_status(line);
    rtsp::link_on_peer(peer);
}

static void init_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "Erasing NVS (%s)", esp_err_to_name(err));
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "M5Tab5 VideoLink v%s booting", APP_VERSION);
    ESP_LOGI(TAG, "Internal free heap: %u bytes", (unsigned) heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    ESP_LOGI(TAG, "PSRAM free heap:    %u bytes", (unsigned) heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    init_nvs();

    ESP_ERROR_CHECK(board::init());
    ui::init();

    ESP_ERROR_CHECK(settings::init());
    const settings::Config &cfg = settings::get();

    if (net::wifi_init(on_wifi_status) == ESP_OK) {
        net::wifi_apply();
        const char *codec = cfg.codec == settings::VideoCodec::H264 ? "h264" : "mjpeg";
        net::discovery_start(cfg.device_name, VIDEOLINK_RTSP_PORT, codec, on_peer_found);
    } else {
        ui::set_status("WiFi unavailable");
    }

    rtsp::link_start(VIDEOLINK_RTSP_PORT);

    media::Codec tx_codec = cfg.codec == settings::VideoCodec::H264 ? media::Codec::H264
                                                                    : media::Codec::MJPEG;
    esp_err_t merr = media::pipeline_start(tx_codec, cfg.quality, rtsp::link_on_encoded);
    if (merr != ESP_OK) {
        ESP_LOGW(TAG, "media pipeline did not start: %s", esp_err_to_name(merr));
    }

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        ESP_LOGI(TAG, "heap: int=%u psram=%u",
                 (unsigned) heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 (unsigned) heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    }
}
