// Orchestrates the dual-core video pipeline:
//   Core 1: camera capture -> encode -> on_encoded() (handed to the network)
//   Core 0: submitted remote frames -> decode -> display
// This module is deliberately independent of the networking layer; the RTSP/RTP
// code wires itself in via the encoded callback and pipeline_submit_remote().
#pragma once

#include "esp_err.h"
#include "media/media_types.h"

namespace media {

typedef void (*encoded_cb_t)(const EncodedFrame &frame);

esp_err_t pipeline_start(Codec tx_codec, uint8_t quality, encoded_cb_t on_encoded);

// Hands a received compressed frame to the decode/display task (data is copied).
void pipeline_submit_remote(const uint8_t *data, size_t size, Codec codec);

// Currently active outgoing codec (may differ from requested if it fell back).
Codec pipeline_tx_codec();

} // namespace media
