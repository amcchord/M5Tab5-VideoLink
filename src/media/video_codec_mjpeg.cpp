// MJPEG backend using the ESP32-P4 hardware JPEG engine (esp_driver_jpeg).
//
// Note: the single JPEG hardware engine can only encode OR decode at a time, so
// a shared mutex serializes the encode (Core 1) and decode (Core 0) tasks.
// Also note: on pre-rev3.0 ESP32-P4 silicon the JPEG encoder cannot take YUV420
// input, so we feed RGB565 (the camera/ISP output) and sub-sample to YUV422.

#include "media/video_codec.h"

#include <cstring>

#include "driver/jpeg_encode.h"
#include "driver/jpeg_decode.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"

static const char *TAG = "mjpeg";

namespace {

SemaphoreHandle_t s_jpeg_lock = nullptr;

void ensure_lock()
{
    if (s_jpeg_lock == nullptr) {
        s_jpeg_lock = xSemaphoreCreateMutex();
    }
}

uint8_t map_quality(uint8_t q)
{
    if (q < 1) {
        return 1;
    }
    if (q > 100) {
        return 100;
    }
    return q;
}

class MjpegEncoder : public media::VideoEncoder {
public:
    esp_err_t open(uint16_t width, uint16_t height, media::PixelFormat in_fmt, uint8_t quality) override
    {
        ensure_lock();
        width_ = width;
        height_ = height;
        quality_ = map_quality(quality);
        if (in_fmt != media::PixelFormat::RGB565) {
            ESP_LOGW(TAG, "encoder expects RGB565 input");
        }

        jpeg_encode_engine_cfg_t eng = {.intr_priority = 0, .timeout_ms = 100};
        esp_err_t err = jpeg_new_encoder_engine(&eng, &engine_);
        if (err != ESP_OK) {
            return err;
        }

        size_t raw = (size_t) width_ * height_ * 2; // RGB565
        jpeg_encode_memory_alloc_cfg_t in_cfg = {.buffer_direction = JPEG_ENC_ALLOC_INPUT_BUFFER};
        in_buf_ = (uint8_t *) jpeg_alloc_encoder_mem(raw, &in_cfg, &in_cap_);
        jpeg_encode_memory_alloc_cfg_t out_cfg = {.buffer_direction = JPEG_ENC_ALLOC_OUTPUT_BUFFER};
        out_buf_ = (uint8_t *) jpeg_alloc_encoder_mem(raw, &out_cfg, &out_cap_);
        if (in_buf_ == nullptr || out_buf_ == nullptr) {
            ESP_LOGE(TAG, "encoder buffer alloc failed");
            return ESP_ERR_NO_MEM;
        }
        ESP_LOGI(TAG, "JPEG encoder %ux%u q=%u", width_, height_, quality_);
        return ESP_OK;
    }

    esp_err_t encode(const media::RawFrame &in, media::EncodedFrame &out) override
    {
        if (engine_ == nullptr) {
            return ESP_ERR_INVALID_STATE;
        }
        size_t need = (size_t) in.width * in.height * 2;
        if (need > in_cap_) {
            return ESP_ERR_INVALID_SIZE;
        }
        memcpy(in_buf_, in.data, need);

        jpeg_encode_cfg_t cfg = {
            .height = in.height,
            .width = in.width,
            .src_type = JPEG_ENCODE_IN_FORMAT_RGB565,
            .sub_sample = JPEG_DOWN_SAMPLING_YUV422,
            .image_quality = quality_,
        };
        uint32_t out_size = 0;
        xSemaphoreTake(s_jpeg_lock, portMAX_DELAY);
        esp_err_t err = jpeg_encoder_process(engine_, &cfg, in_buf_, need, out_buf_, out_cap_, &out_size);
        xSemaphoreGive(s_jpeg_lock);
        if (err != ESP_OK) {
            return err;
        }
        out.data = out_buf_;
        out.size = out_size;
        out.width = in.width;
        out.height = in.height;
        out.codec = media::Codec::MJPEG;
        out.keyframe = true; // every JPEG is self-contained
        out.ts_us = in.ts_us;
        return ESP_OK;
    }

    media::Codec codec() const override { return media::Codec::MJPEG; }

    void close() override
    {
        if (engine_) {
            jpeg_del_encoder_engine(engine_);
            engine_ = nullptr;
        }
        if (in_buf_) {
            free(in_buf_);
            in_buf_ = nullptr;
        }
        if (out_buf_) {
            free(out_buf_);
            out_buf_ = nullptr;
        }
    }

private:
    jpeg_encoder_handle_t engine_ = nullptr;
    uint8_t *in_buf_ = nullptr;
    uint8_t *out_buf_ = nullptr;
    size_t in_cap_ = 0;
    size_t out_cap_ = 0;
    uint16_t width_ = 0;
    uint16_t height_ = 0;
    uint8_t quality_ = 60;
};

class MjpegDecoder : public media::VideoDecoder {
public:
    esp_err_t open() override
    {
        ensure_lock();
        jpeg_decode_engine_cfg_t eng = {.intr_priority = 0, .timeout_ms = 100};
        esp_err_t err = jpeg_new_decoder_engine(&eng, &engine_);
        if (err != ESP_OK) {
            return err;
        }
        // Output buffer sized for up to 1280x720 RGB565.
        out_cap_wanted_ = 1280u * 720u * 2u;
        jpeg_decode_memory_alloc_cfg_t cfg = {.buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER};
        out_buf_ = (uint8_t *) jpeg_alloc_decoder_mem(out_cap_wanted_, &cfg, &out_cap_);
        if (out_buf_ == nullptr) {
            return ESP_ERR_NO_MEM;
        }
        ESP_LOGI(TAG, "JPEG decoder ready (%u byte out buffer)", (unsigned) out_cap_);
        return ESP_OK;
    }

    esp_err_t decode(const media::EncodedFrame &in, media::RawFrame &out) override
    {
        if (engine_ == nullptr) {
            return ESP_ERR_INVALID_STATE;
        }
        jpeg_decode_picture_info_t info = {};
        esp_err_t err = jpeg_decoder_get_info(in.data, in.size, &info);
        if (err != ESP_OK) {
            return err;
        }
        if ((size_t) info.width * info.height * 2 > out_cap_) {
            ESP_LOGW(TAG, "frame %ux%u exceeds decode buffer", (unsigned) info.width, (unsigned) info.height);
            return ESP_ERR_INVALID_SIZE;
        }

        jpeg_decode_cfg_t cfg = {
            .output_format = JPEG_DECODE_OUT_FORMAT_RGB565,
            .rgb_order = JPEG_DEC_RGB_ELEMENT_ORDER_RGB,
            .conv_std = JPEG_YUV_RGB_CONV_STD_BT601,
        };
        uint32_t out_size = 0;
        xSemaphoreTake(s_jpeg_lock, portMAX_DELAY);
        err = jpeg_decoder_process(engine_, &cfg, in.data, in.size, out_buf_, out_cap_, &out_size);
        xSemaphoreGive(s_jpeg_lock);
        if (err != ESP_OK) {
            return err;
        }
        out.data = out_buf_;
        out.size = out_size;
        out.width = info.width;
        out.height = info.height;
        out.format = media::PixelFormat::RGB565;
        out.ts_us = in.ts_us;
        return ESP_OK;
    }

    media::Codec codec() const override { return media::Codec::MJPEG; }

    void close() override
    {
        if (engine_) {
            jpeg_del_decoder_engine(engine_);
            engine_ = nullptr;
        }
        if (out_buf_) {
            free(out_buf_);
            out_buf_ = nullptr;
        }
    }

private:
    jpeg_decoder_handle_t engine_ = nullptr;
    uint8_t *out_buf_ = nullptr;
    size_t out_cap_ = 0;
    size_t out_cap_wanted_ = 0;
};

} // namespace

// H.264 backends live in video_codec_h264.cpp; declared here as weak factories
// so the MJPEG-only build links. They are defined for real in that file.
namespace media {

__attribute__((weak)) VideoEncoder *create_h264_encoder() { return nullptr; }
__attribute__((weak)) VideoDecoder *create_h264_decoder() { return nullptr; }

VideoEncoder *create_encoder(Codec c)
{
    if (c == Codec::H264) {
        VideoEncoder *e = create_h264_encoder();
        if (e) {
            return e;
        }
        ESP_LOGW(TAG, "H.264 encoder unavailable, falling back to MJPEG");
    }
    return new MjpegEncoder();
}

VideoDecoder *create_decoder(Codec c)
{
    if (c == Codec::H264) {
        VideoDecoder *d = create_h264_decoder();
        if (d) {
            return d;
        }
        ESP_LOGW(TAG, "H.264 decoder unavailable, falling back to MJPEG");
    }
    return new MjpegDecoder();
}

} // namespace media
