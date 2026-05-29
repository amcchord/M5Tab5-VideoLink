#include "media/video_pipeline.h"

#include <cstring>
#include <cstdlib>

#include "media/camera.h"
#include "media/video_codec.h"
#include "app/app_config.h"
#include "ui/ui.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"

static const char *TAG = "pipeline";

namespace {

media::VideoEncoder *s_enc = nullptr;
media::VideoDecoder *s_dec = nullptr;
media::encoded_cb_t s_on_encoded = nullptr;
media::Codec s_tx_codec = media::Codec::MJPEG;
QueueHandle_t s_rx_q = nullptr;

struct RxItem {
    uint8_t *data;
    size_t size;
    media::Codec codec;
};

// Core 1: capture from camera, show local preview, encode, hand off to network.
void capture_encode_task(void *arg)
{
    (void) arg;
    while (true) {
        media::RawFrame raw = {};
        if (media::camera_capture(&raw) != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }

        // Local self-view preview (overwritten by remote video once connected).
        if (raw.format == media::PixelFormat::RGB565) {
            ui::video_set_frame(raw.data, raw.width, raw.height);
        }

        if (s_enc != nullptr && s_on_encoded != nullptr) {
            media::EncodedFrame enc = {};
            if (s_enc->encode(raw, enc) == ESP_OK) {
                s_on_encoded(enc);
            }
        }
        media::camera_release();
    }
}

// Core 0: decode received frames and push them to the display.
void decode_task(void *arg)
{
    (void) arg;
    while (true) {
        RxItem item = {};
        if (xQueueReceive(s_rx_q, &item, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        media::VideoDecoder *dec = s_dec;
        if (dec != nullptr && item.codec == dec->codec()) {
            media::EncodedFrame ef = {};
            ef.data = item.data;
            ef.size = item.size;
            ef.codec = item.codec;
            media::RawFrame raw = {};
            if (dec->decode(ef, raw) == ESP_OK) {
                ui::video_set_frame(raw.data, raw.width, raw.height);
            }
        }
        free(item.data);
    }
}

} // namespace

namespace media {

esp_err_t pipeline_start(Codec tx_codec, uint8_t quality, encoded_cb_t on_encoded)
{
    s_on_encoded = on_encoded;

    esp_err_t err = camera_init(VIDEOLINK_VIDEO_WIDTH, VIDEOLINK_VIDEO_HEIGHT);
    if (err != ESP_OK) {
        return err;
    }

    s_enc = create_encoder(tx_codec);
    if (s_enc != nullptr) {
        esp_err_t oerr = s_enc->open(camera_width(), camera_height(), PixelFormat::RGB565, quality);
        if (oerr != ESP_OK && s_enc->codec() != Codec::MJPEG) {
            ESP_LOGW(TAG, "%s encoder open failed (%s); falling back to MJPEG",
                     s_enc->codec() == Codec::H264 ? "H264" : "?", esp_err_to_name(oerr));
            s_enc->close();
            delete s_enc;
            s_enc = create_encoder(Codec::MJPEG);
            if (s_enc != nullptr) {
                s_enc->open(camera_width(), camera_height(), PixelFormat::RGB565, quality);
            }
        }
        if (s_enc != nullptr) {
            s_tx_codec = s_enc->codec();
        }
    }

    // The decoder is created for our own tx codec for now (symmetric link). The
    // H.264 round generalizes this to pick a decoder per received codec.
    s_dec = create_decoder(s_tx_codec);
    if (s_dec != nullptr) {
        s_dec->open();
    }

    s_rx_q = xQueueCreate(3, sizeof(RxItem));

    xTaskCreatePinnedToCore(capture_encode_task, "cam_enc", 6144, nullptr, 6, nullptr, 1);
    xTaskCreatePinnedToCore(decode_task, "vdec", 6144, nullptr, 5, nullptr, 0);
    ESP_LOGI(TAG, "pipeline started (tx codec=%s)", s_tx_codec == Codec::H264 ? "H264" : "MJPEG");
    return ESP_OK;
}

void pipeline_submit_remote(const uint8_t *data, size_t size, Codec codec)
{
    if (s_rx_q == nullptr || data == nullptr || size == 0) {
        return;
    }
    RxItem item = {};
    item.size = size;
    item.codec = codec;
    item.data = (uint8_t *) malloc(size);
    if (item.data == nullptr) {
        return;
    }
    memcpy(item.data, data, size);
    // Latest-frame-wins: if the queue is full, drop this frame to bound latency.
    if (xQueueSend(s_rx_q, &item, 0) != pdTRUE) {
        free(item.data);
    }
}

Codec pipeline_tx_codec() { return s_tx_codec; }

} // namespace media
