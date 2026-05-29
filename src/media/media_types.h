// Shared media data types passed between the camera, codecs, pipeline and UI.
#pragma once

#include <cstdint>
#include <cstddef>

namespace media {

enum class PixelFormat {
    RGB565,
    RGB888,
    YUV422,
};

enum class Codec {
    MJPEG,
    H264,
};

// A raw (uncompressed) video frame. `data` is owned by the producer (camera or
// decoder) and is only valid until the frame is released / the next call.
struct RawFrame {
    uint8_t *data;
    size_t size;
    uint16_t width;
    uint16_t height;
    PixelFormat format;
    int64_t ts_us;
};

// A compressed video frame. `data` is owned by the encoder (valid until the
// next encode) or by the caller for decode input.
struct EncodedFrame {
    uint8_t *data;
    size_t size;
    uint16_t width;
    uint16_t height;
    Codec codec;
    bool keyframe;
    int64_t ts_us;
};

} // namespace media
