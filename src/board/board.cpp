#include "board/board.h"

#include "bsp/m5stack_tab5.h"
#include "bsp/display.h"
#include "esp_log.h"

static const char *TAG = "board";

static lv_display_t *s_disp = nullptr;

namespace board {

esp_err_t init()
{
    // The display panel, touch controller and IO expander all sit on the
    // shared I2C bus, so bring it up first. bsp_i2c_init() is idempotent.
    esp_err_t err = bsp_i2c_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "bsp_i2c_init failed: %s", esp_err_to_name(err));
        return err;
    }

    s_disp = bsp_display_start();
    if (s_disp == nullptr) {
        ESP_LOGE(TAG, "bsp_display_start failed");
        return ESP_FAIL;
    }

    set_brightness(80);
    ESP_LOGI(TAG, "Display up: %dx%d",
             (int) lv_display_get_horizontal_resolution(s_disp),
             (int) lv_display_get_vertical_resolution(s_disp));
    return ESP_OK;
}

lv_display_t *display()
{
    return s_disp;
}

bool lock(uint32_t timeout_ms)
{
    return bsp_display_lock(timeout_ms);
}

void unlock()
{
    bsp_display_unlock();
}

void set_brightness(int percent)
{
    if (percent < 0) {
        percent = 0;
    }
    if (percent > 100) {
        percent = 100;
    }
    bsp_display_brightness_set(percent);
}

esp_err_t wifi_power_enable(bool enable)
{
    return bsp_feature_enable(BSP_FEATURE_WIFI, enable);
}

} // namespace board
