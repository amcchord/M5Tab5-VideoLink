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
#include "esp_heap_caps.h"
#include "driver/ppa.h"

static const char *TAG = "pipeline";

// We encode/stream at this size (downscaled from the sensor's native 1280x720
// with the PPA). Keeping it at 640x360 bounds the H.264 hardware encoder's
// internal-RAM reference buffer (~45KB vs ~90KB at 720p) and the software
// decode cost on the receiver, and cuts bandwidth.
#define ENC_W VIDEOLINK_VIDEO_WIDTH
#define ENC_H VIDEOLINK_VIDEO_HEIGHT

namespace {

media::VideoEncoder *s_enc = nullptr;
media::VideoDecoder *s_dec = nullptr;
media::encoded_cb_t s_on_encoded = nullptr;
media::Codec s_tx_codec = media::Codec::MJPEG;
QueueHandle_t s_rx_q = nullptr;

ppa_client_handle_t s_ppa = nullptr;
uint8_t *s_enc_buf = nullptr; // downscaled RGB565 frame fed to the encoder

struct RxItem {
    uint8_t *data;
    size_t size;
    media::Codec codec;
};

// Hardware-downscale a captured RGB565 frame into s_enc_buf (ENC_W x ENC_H).
bool scale_to_enc(const media::RawFrame &raw)
{
    if (s_ppa == nullptr || s_enc_buf == nullptr) {
        return false;
    }
    ppa_srm_oper_config_t op = {};
    op.in.buffer = raw.data;
    op.in.pic_w = raw.width;
    op.in.pic_h = raw.height;
    op.in.block_w = raw.width;
    op.in.block_h = raw.height;
    op.in.srm_cm = PPA_SRM_COLOR_MODE_RGB565;
    op.out.buffer = s_enc_buf;
    op.out.buffer_size = (uint32_t) (ENC_W * ENC_H * 2);
    op.out.pic_w = ENC_W;
    op.out.pic_h = ENC_H;
    op.out.srm_cm = PPA_SRM_COLOR_MODE_RGB565;
    op.rotation_angle = PPA_SRM_ROTATION_ANGLE_0;
    op.scale_x = (float) ENC_W / (float) raw.width;
    op.scale_y = (float) ENC_H / (float) raw.height;
    op.mode = PPA_TRANS_MODE_BLOCKING;
    return ppa_do_scale_rotate_mirror(s_ppa, &op) == ESP_OK;
}

// Core 1: capture from camera, downscale, show local preview, encode, hand off.
void capture_encode_task(void *arg)
{
    (void) arg;
    while (true) {
        media::RawFrame raw = {};
        if (media::camera_capture(&raw) != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }

        if (raw.format == media::PixelFormat::RGB565 && scale_to_enc(raw)) {
            media::RawFrame scaled = {};
            scaled.data = s_enc_buf;
            scaled.size = (size_t) ENC_W * ENC_H * 2;
            scaled.width = ENC_W;
            scaled.height = ENC_H;
            scaled.format = media::PixelFormat::RGB565;
            scaled.ts_us = raw.ts_us;

            ui::video_set_frame(scaled.data, scaled.width, scaled.height);

            if (s_enc != nullptr && s_on_encoded != nullptr) {
                media::EncodedFrame enc = {};
                if (s_enc->encode(scaled, enc) == ESP_OK) {
                    s_on_encoded(enc);
                }
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

    // PPA scaler + downscaled encode buffer (camera native -> ENC_W x ENC_H).
    ppa_client_config_t pc = {};
    pc.oper_type = PPA_OPERATION_SRM;
    ppa_register_client(&pc, &s_ppa);
    s_enc_buf = (uint8_t *) heap_caps_aligned_alloc(128, (size_t) ENC_W * ENC_H * 2, MALLOC_CAP_SPIRAM);

    ESP_LOGI(TAG, "encoding at %dx%d; internal heap free=%u largest=%u", ENC_W, ENC_H,
             (unsigned) heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned) heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));

    s_enc = create_encoder(tx_codec);
    if (s_enc != nullptr) {
        esp_err_t oerr = s_enc->open(ENC_W, ENC_H, PixelFormat::RGB565, quality);
        if (oerr != ESP_OK && s_enc->codec() != Codec::MJPEG) {
            ESP_LOGW(TAG, "%s encoder open failed (%s); falling back to MJPEG",
                     s_enc->codec() == Codec::H264 ? "H264" : "?", esp_err_to_name(oerr));
            s_enc->close();
            delete s_enc;
            s_enc = create_encoder(Codec::MJPEG);
            if (s_enc != nullptr) {
                s_enc->open(ENC_W, ENC_H, PixelFormat::RGB565, quality);
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
