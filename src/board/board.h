// Thin wrapper over the M5Stack Tab5 BSP for the bits the app needs:
// display + touch (LVGL), backlight, the shared I2C bus, and powering the
// ESP32-C6 WiFi co-processor. Keeping BSP calls behind this module makes the
// rest of the app independent of BSP specifics.
#pragma once

#include "esp_err.h"
#include "lvgl.h"

namespace board {

// Initializes I2C, the IO expander, the display + touch panel and starts the
// LVGL port task. Must be called once, early, from app_main.
esp_err_t init();

lv_display_t *display();

// LVGL is not thread-safe; take/release this lock around any lv_* call made
// outside of LVGL's own callbacks. timeout_ms == 0 blocks indefinitely.
bool lock(uint32_t timeout_ms = 0);
void unlock();

// 0..100. Also turns the backlight on.
void set_brightness(int percent);

// Powers the ESP32-C6 WiFi module (Tab5 routes its enable through the IO
// expander). Safe to call before bringing up esp_hosted / WiFi.
esp_err_t wifi_power_enable(bool enable);

} // namespace board
