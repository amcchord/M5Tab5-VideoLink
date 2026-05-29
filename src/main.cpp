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

static const char *TAG = "videolink";

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

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
