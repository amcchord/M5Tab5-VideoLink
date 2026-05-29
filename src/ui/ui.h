// LVGL user interface. Two screens managed by a simple switcher: a full-screen
// video screen with a gear button (top-right) that loads the settings screen,
// and a settings screen with a back button. All lv_* calls must hold the
// board display lock (the public helpers below take it for you).
#pragma once

#include <cstdint>

namespace ui {

void init();

// Update the small status line on the main screen (thread-safe).
void set_status(const char *text);

// Draw an RGB565 frame into the main video area (thread-safe). Re-allocates the
// backing canvas if the frame size changes.
void video_set_frame(const uint8_t *rgb565, int w, int h);

} // namespace ui
