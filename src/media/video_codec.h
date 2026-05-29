// Codec abstraction (the "hybrid" layer): a common interface with MJPEG and
// H.264 backends so the runtime codec can be chosen/negotiated and swapped.
// MJPEG uses the ESP32-P4 hardware JPEG engine for both encode and decode;
// H.264 uses the hardware encoder + esp_h264 software decoder (added later).
#pragma once

#include "esp_err.h"
#include "media/media_types.h"

namespace media {

class VideoEncoder {
public:
    virtual ~VideoEncoder() = default;
    virtual esp_err_t open(uint16_t width, uint16_t height, PixelFormat in_fmt, uint8_t quality) = 0;
    // Encodes `in` into `out`. `out.data` points to encoder-owned memory valid
    // until the next encode() call.
    virtual esp_err_t encode(const RawFrame &in, EncodedFrame &out) = 0;
    virtual Codec codec() const = 0;
    virtual void close() = 0;
};

class VideoDecoder {
public:
    virtual ~VideoDecoder() = default;
    virtual esp_err_t open() = 0;
    // Decodes `in` into an RGB565 `out`. `out.data` points to decoder-owned
    // memory valid until the next decode() call.
    virtual esp_err_t decode(const EncodedFrame &in, RawFrame &out) = 0;
    virtual Codec codec() const = 0;
    virtual void close() = 0;
};

// Factories. Return nullptr for an unsupported codec on this build.
VideoEncoder *create_encoder(Codec c);
VideoDecoder *create_decoder(Codec c);

} // namespace media
