// SC2356 camera capture via esp_video's V4L2 interface (the BSP brings up the
// sensor + ISP; we open /dev/video0 and stream RGB565 frames).
#pragma once

#include "esp_err.h"
#include "media/media_types.h"

namespace media {

// Initializes the camera, requesting RGB565 at the given size (the driver may
// fall back to a supported size; query with camera_width/height afterwards).
esp_err_t camera_init(uint16_t want_w, uint16_t want_h);

// Dequeues the next frame (blocking). Pair every successful capture with
// camera_release() once the frame has been consumed.
esp_err_t camera_capture(RawFrame *out);
void camera_release();

uint16_t camera_width();
uint16_t camera_height();

} // namespace media
