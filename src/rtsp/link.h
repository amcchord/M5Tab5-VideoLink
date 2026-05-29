// Ties the RTSP server (publishing our camera) and RTSP client (pulling the
// peer's camera) to the media pipeline, for a symmetric two-way video link.
#pragma once

#include <cstdint>

#include "media/media_types.h"
#include "net/discovery.h"

namespace rtsp {

void link_start(uint16_t rtsp_port, media::Codec codec);

// Pass to media::pipeline_start as the encoded-frame sink (sends to the peer).
void link_on_encoded(const media::EncodedFrame &frame);

// Pass to net::discovery_start as the peer callback (connects to the peer).
void link_on_peer(const net::Peer &peer);

} // namespace rtsp
