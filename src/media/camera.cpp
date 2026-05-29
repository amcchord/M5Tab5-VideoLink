#include "media/camera.h"

#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <sys/ioctl.h>

#include "linux/videodev2.h"
#include "sys/mman.h"
#include "esp_video_device.h"
#include "bsp/m5stack_tab5.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "camera";

#define CAM_BUFFER_COUNT 2

namespace {

int s_fd = -1;
uint8_t *s_buf[CAM_BUFFER_COUNT] = {};
size_t s_buf_len[CAM_BUFFER_COUNT] = {};
int s_last_index = -1;
uint16_t s_w = 0;
uint16_t s_h = 0;

} // namespace

namespace media {

esp_err_t camera_init(uint16_t want_w, uint16_t want_h)
{
    bsp_camera_cfg_t cam_cfg = {};
    esp_err_t err = bsp_camera_start(&cam_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "bsp_camera_start -> %s", esp_err_to_name(err));
        return err;
    }

    s_fd = open(ESP_VIDEO_MIPI_CSI_DEVICE_NAME, O_RDONLY);
    if (s_fd < 0) {
        ESP_LOGE(TAG, "open(%s) failed", ESP_VIDEO_MIPI_CSI_DEVICE_NAME);
        return ESP_FAIL;
    }

    const int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    // Query the sensor's native resolution (the ISP does not freely downscale,
    // so we capture at the native size and convert RAW8 -> RGB565 in the ISP).
    struct v4l2_format fmt = {};
    fmt.type = type;
    uint16_t nw = want_w;
    uint16_t nh = want_h;
    if (ioctl(s_fd, VIDIOC_G_FMT, &fmt) == 0 && fmt.fmt.pix.width > 0) {
        nw = fmt.fmt.pix.width;
        nh = fmt.fmt.pix.height;
    }

    memset(&fmt, 0, sizeof(fmt));
    fmt.type = type;
    fmt.fmt.pix.width = nw;
    fmt.fmt.pix.height = nh;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB565;
    if (ioctl(s_fd, VIDIOC_S_FMT, &fmt) != 0) {
        ESP_LOGW(TAG, "S_FMT RGB565 %ux%u not accepted; using current format", nw, nh);
    }

    memset(&fmt, 0, sizeof(fmt));
    fmt.type = type;
    if (ioctl(s_fd, VIDIOC_G_FMT, &fmt) == 0) {
        s_w = fmt.fmt.pix.width;
        s_h = fmt.fmt.pix.height;
    } else {
        s_w = nw;
        s_h = nh;
    }
    uint32_t fourcc = fmt.fmt.pix.pixelformat;
    ESP_LOGI(TAG, "camera streaming at %ux%u fourcc=%c%c%c%c", s_w, s_h,
             (char) (fourcc & 0xFF), (char) ((fourcc >> 8) & 0xFF),
             (char) ((fourcc >> 16) & 0xFF), (char) ((fourcc >> 24) & 0xFF));

    struct v4l2_requestbuffers req = {};
    req.count = CAM_BUFFER_COUNT;
    req.type = type;
    req.memory = V4L2_MEMORY_MMAP;
    if (ioctl(s_fd, VIDIOC_REQBUFS, &req) != 0) {
        ESP_LOGE(TAG, "VIDIOC_REQBUFS failed");
        return ESP_FAIL;
    }

    for (int i = 0; i < CAM_BUFFER_COUNT; i++) {
        struct v4l2_buffer buf = {};
        buf.type = type;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        if (ioctl(s_fd, VIDIOC_QUERYBUF, &buf) != 0) {
            ESP_LOGE(TAG, "VIDIOC_QUERYBUF failed");
            return ESP_FAIL;
        }
        s_buf[i] = (uint8_t *) mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, s_fd, buf.m.offset);
        s_buf_len[i] = buf.length;
        if (s_buf[i] == MAP_FAILED) {
            ESP_LOGE(TAG, "mmap buffer %d failed", i);
            return ESP_FAIL;
        }
        if (ioctl(s_fd, VIDIOC_QBUF, &buf) != 0) {
            ESP_LOGE(TAG, "VIDIOC_QBUF failed");
            return ESP_FAIL;
        }
    }

    int t = type;
    if (ioctl(s_fd, VIDIOC_STREAMON, &t) != 0) {
        ESP_LOGE(TAG, "VIDIOC_STREAMON failed");
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t camera_capture(RawFrame *out)
{
    if (s_fd < 0 || out == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    struct v4l2_buffer buf = {};
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    if (ioctl(s_fd, VIDIOC_DQBUF, &buf) != 0) {
        return ESP_FAIL;
    }
    s_last_index = buf.index;
    out->data = s_buf[buf.index];
    out->size = buf.bytesused;
    out->width = s_w;
    out->height = s_h;
    out->format = PixelFormat::RGB565;
    out->ts_us = esp_timer_get_time();
    return ESP_OK;
}

void camera_release()
{
    if (s_fd < 0 || s_last_index < 0) {
        return;
    }
    struct v4l2_buffer buf = {};
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = s_last_index;
    ioctl(s_fd, VIDIOC_QBUF, &buf);
    s_last_index = -1;
}

uint16_t camera_width() { return s_w; }
uint16_t camera_height() { return s_h; }

} // namespace media
