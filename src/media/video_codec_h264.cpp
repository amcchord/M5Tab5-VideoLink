// H.264 backend: ESP32-P4 hardware encoder (esp_h264) + tinyh264 software
// decoder. EXPERIMENTAL / needs on-hardware validation (see plan): the HW
// encoder consumes a packed YUV420 layout (O_UYY_E_VYY) and the SW decoder
// emits I420, so we color-convert to/from the camera's RGB565 in software.
//
// Default codec remains MJPEG; this is selected via settings and is the
// preferred runtime codec once validated for bitrate/CPU on real hardware.

#include "media/video_codec.h"

#include <cstring>
#include <cstdlib>

#include "app/app_config.h"

#include "esp_h264_enc_single_hw.h"
#include "esp_h264_dec_sw.h"
#include "esp_h264_dec_param.h"
extern "C" {
#include "esp_h264_alloc.h" // header lacks its own extern "C" guard
}
#include "esp_heap_caps.h"
#include "esp_log.h"

static const char *TAG = "h264";

namespace {

inline uint8_t clamp8(int v)
{
    if (v < 0) {
        return 0;
    }
    if (v > 255) {
        return 255;
    }
    return (uint8_t) v;
}

// RGB565 frame (width=src_w stride) -> packed YUV420 "O_UYY_E_VYY" of enc_w x enc_h.
void rgb565_to_o_uyy_e_vyy(const uint8_t *rgb, int src_w, uint8_t *out, int enc_w, int enc_h)
{
    uint8_t *o = out;
    for (int r = 0; r < enc_h; r++) {
        const uint16_t *line = (const uint16_t *) (rgb + (size_t) r * src_w * 2);
        bool even_line = ((r & 1) == 0); // line 1,3,.. (index 0,2) carry U; else V
        for (int x = 0; x < enc_w; x += 2) {
            uint16_t p0 = line[x];
            uint16_t p1 = line[x + 1];
            int r0 = ((p0 >> 11) & 0x1F) << 3;
            int g0 = ((p0 >> 5) & 0x3F) << 2;
            int b0 = (p0 & 0x1F) << 3;
            int r1 = ((p1 >> 11) & 0x1F) << 3;
            int g1 = ((p1 >> 5) & 0x3F) << 2;
            int b1 = (p1 & 0x1F) << 3;
            int y0 = (77 * r0 + 150 * g0 + 29 * b0) >> 8;
            int y1 = (77 * r1 + 150 * g1 + 29 * b1) >> 8;
            if (even_line) {
                int u = (((-43 * r0 - 85 * g0 + 128 * b0) >> 8) + 128);
                *o++ = clamp8(u);
            } else {
                int v = (((128 * r0 - 107 * g0 - 21 * b0) >> 8) + 128);
                *o++ = clamp8(v);
            }
            *o++ = clamp8(y0);
            *o++ = clamp8(y1);
        }
    }
}

// I420 planar -> RGB565.
void i420_to_rgb565(const uint8_t *i420, int w, int h, uint8_t *rgb)
{
    const uint8_t *yp = i420;
    const uint8_t *up = yp + (size_t) w * h;
    const uint8_t *vp = up + (size_t) (w / 2) * (h / 2);
    uint16_t *out = (uint16_t *) rgb;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int c = yp[y * w + x] - 16;
            int d = up[(y / 2) * (w / 2) + (x / 2)] - 128;
            int e = vp[(y / 2) * (w / 2) + (x / 2)] - 128;
            int rr = clamp8((298 * c + 409 * e + 128) >> 8);
            int gg = clamp8((298 * c - 100 * d - 208 * e + 128) >> 8);
            int bb = clamp8((298 * c + 516 * d + 128) >> 8);
            out[y * w + x] = (uint16_t) (((rr & 0xF8) << 8) | ((gg & 0xFC) << 3) | (bb >> 3));
        }
    }
}

class H264Encoder : public media::VideoEncoder {
public:
    esp_err_t open(uint16_t width, uint16_t height, media::PixelFormat in_fmt, uint8_t quality) override
    {
        (void) in_fmt;
        enc_w_ = width & ~15;
        enc_h_ = height & ~15;
        src_w_ = width;
        if (enc_w_ < 80 || enc_h_ < 80) {
            return ESP_ERR_INVALID_SIZE;
        }
        uint32_t bitrate = (uint32_t) enc_w_ * enc_h_ * VIDEOLINK_VIDEO_FPS / 40;

        esp_h264_enc_cfg_hw_t cfg = {};
        cfg.pic_type = ESP_H264_RAW_FMT_O_UYY_E_VYY;
        cfg.gop = VIDEOLINK_VIDEO_FPS;
        cfg.fps = VIDEOLINK_VIDEO_FPS;
        cfg.res.width = enc_w_;
        cfg.res.height = enc_h_;
        cfg.rc.bitrate = bitrate;
        cfg.rc.qp_min = 25;
        cfg.rc.qp_max = (uint8_t) (46 - (quality / 10)); // higher quality -> lower max QP
        if (esp_h264_enc_hw_new(&cfg, &enc_) != ESP_H264_ERR_OK || enc_ == nullptr) {
            ESP_LOGE(TAG, "esp_h264_enc_hw_new failed");
            return ESP_FAIL;
        }
        if (esp_h264_enc_open(enc_) != ESP_H264_ERR_OK) {
            return ESP_FAIL;
        }
        in_len_ = (size_t) enc_w_ * enc_h_ * 3 / 2;
        uint32_t actual = 0;
        in_buf_ = (uint8_t *) esp_h264_aligned_calloc(64, 1, in_len_, &actual, MALLOC_CAP_SPIRAM);
        out_buf_ = (uint8_t *) esp_h264_aligned_calloc(64, 1, in_len_, &actual, MALLOC_CAP_SPIRAM);
        if (in_buf_ == nullptr || out_buf_ == nullptr) {
            return ESP_ERR_NO_MEM;
        }
        ESP_LOGI(TAG, "H.264 HW encoder %dx%d @%dfps ~%ukbps", enc_w_, enc_h_, VIDEOLINK_VIDEO_FPS,
                 (unsigned) (bitrate / 1000));
        return ESP_OK;
    }

    esp_err_t encode(const media::RawFrame &in, media::EncodedFrame &out) override
    {
        if (enc_ == nullptr) {
            return ESP_ERR_INVALID_STATE;
        }
        rgb565_to_o_uyy_e_vyy(in.data, src_w_, in_buf_, enc_w_, enc_h_);

        esp_h264_enc_in_frame_t inf = {};
        inf.raw_data.buffer = in_buf_;
        inf.raw_data.len = in_len_;
        inf.pts = (uint32_t) (in.ts_us / 1000);
        esp_h264_enc_out_frame_t outf = {};
        outf.raw_data.buffer = out_buf_;
        outf.raw_data.len = in_len_;
        if (esp_h264_enc_process(enc_, &inf, &outf) != ESP_H264_ERR_OK) {
            return ESP_FAIL;
        }
        out.data = outf.raw_data.buffer;
        out.size = outf.length;
        out.width = enc_w_;
        out.height = enc_h_;
        out.codec = media::Codec::H264;
        out.keyframe = (outf.frame_type != ESP_H264_FRAME_TYPE_P);
        out.ts_us = in.ts_us;
        return ESP_OK;
    }

    media::Codec codec() const override { return media::Codec::H264; }

    void close() override
    {
        if (enc_) {
            esp_h264_enc_close(enc_);
            esp_h264_enc_del(enc_);
            enc_ = nullptr;
        }
        free(in_buf_);
        free(out_buf_);
        in_buf_ = out_buf_ = nullptr;
    }

private:
    esp_h264_enc_handle_t enc_ = nullptr;
    uint8_t *in_buf_ = nullptr;
    uint8_t *out_buf_ = nullptr;
    size_t in_len_ = 0;
    int enc_w_ = 0, enc_h_ = 0, src_w_ = 0;
};

class H264Decoder : public media::VideoDecoder {
public:
    esp_err_t open() override
    {
        esp_h264_dec_cfg_sw_t cfg = {};
        cfg.pic_type = ESP_H264_RAW_FMT_I420;
        if (esp_h264_dec_sw_new(&cfg, &dec_) != ESP_H264_ERR_OK || dec_ == nullptr) {
            return ESP_FAIL;
        }
        if (esp_h264_dec_open(dec_) != ESP_H264_ERR_OK) {
            return ESP_FAIL;
        }
        esp_h264_dec_sw_get_param_hd(dec_, &param_);
        rgb_ = (uint8_t *) heap_caps_malloc(1280u * 720u * 2u, MALLOC_CAP_SPIRAM);
        return rgb_ ? ESP_OK : ESP_ERR_NO_MEM;
    }

    esp_err_t decode(const media::EncodedFrame &in, media::RawFrame &out) override
    {
        if (dec_ == nullptr) {
            return ESP_ERR_INVALID_STATE;
        }
        esp_h264_dec_in_frame_t inf = {};
        inf.raw_data.buffer = (uint8_t *) in.data;
        inf.raw_data.len = in.size;
        esp_h264_dec_out_frame_t outf = {};
        uint8_t *yuv = nullptr;

        // tinyh264 consumes one NAL at a time; loop until a picture is produced.
        while (inf.raw_data.len > 0) {
            if (esp_h264_dec_process(dec_, &inf, &outf) != ESP_H264_ERR_OK) {
                break;
            }
            if (outf.out_size > 0) {
                yuv = outf.outbuf;
            }
            if (inf.consume == 0) {
                break;
            }
            inf.raw_data.buffer += inf.consume;
            inf.raw_data.len -= inf.consume;
        }
        if (yuv == nullptr) {
            return ESP_FAIL; // no full frame yet (e.g. SPS/PPS only)
        }

        esp_h264_resolution_t res = {};
        if (esp_h264_dec_get_resolution(param_, &res) != ESP_H264_ERR_OK || res.width == 0) {
            return ESP_FAIL;
        }
        if ((size_t) res.width * res.height * 2 > 1280u * 720u * 2u) {
            return ESP_ERR_INVALID_SIZE;
        }
        i420_to_rgb565(yuv, res.width, res.height, rgb_);
        out.data = rgb_;
        out.size = (size_t) res.width * res.height * 2;
        out.width = res.width;
        out.height = res.height;
        out.format = media::PixelFormat::RGB565;
        out.ts_us = in.ts_us;
        return ESP_OK;
    }

    media::Codec codec() const override { return media::Codec::H264; }

    void close() override
    {
        if (dec_) {
            esp_h264_dec_close(dec_);
            esp_h264_dec_del(dec_);
            dec_ = nullptr;
        }
        if (rgb_) {
            heap_caps_free(rgb_);
            rgb_ = nullptr;
        }
    }

private:
    esp_h264_dec_handle_t dec_ = nullptr;
    esp_h264_dec_param_sw_handle_t param_ = nullptr;
    uint8_t *rgb_ = nullptr;
};

} // namespace

// Strong definitions overriding the weak fallbacks in video_codec_mjpeg.cpp.
namespace media {

VideoEncoder *create_h264_encoder() { return new H264Encoder(); }
VideoDecoder *create_h264_decoder() { return new H264Decoder(); }

} // namespace media
