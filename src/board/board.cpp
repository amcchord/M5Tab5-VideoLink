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

    // Put the LVGL draw buffers in PSRAM (the BSP default puts them in internal
    // DMA RAM). Internal SRAM is precious -- the WiFi co-processor link, the
    // H.264 encoder's reference buffer and the I2S audio DMA all need it -- so
    // freeing ~70-140KB here lets all of them coexist.
    bsp_display_cfg_t disp_cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size = BSP_LCD_H_RES * CONFIG_BSP_LCD_DRAW_BUF_HEIGHT,
        .double_buffer = true,
        .flags = {
            .buff_dma = false,
            .buff_spiram = true,
            .sw_rotate = true,
        },
    };
    s_disp = bsp_display_start_with_config(&disp_cfg);
    if (s_disp == nullptr) {
        ESP_LOGE(TAG, "bsp_display_start failed");
        return ESP_FAIL;
    }

    // The panel is natively portrait (720x1280); rotate 90 degrees for
    // landscape use. The BSP enables software rotation and esp_lvgl_port also
    // rotates touch input to match. (Use LV_DISP_ROTATION_270 to flip 180.)
    bsp_display_rotate(s_disp, LV_DISP_ROTATION_90);

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
