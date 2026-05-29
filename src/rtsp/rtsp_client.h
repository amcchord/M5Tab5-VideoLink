// Minimal RTSP client that connects to the peer's RTSP server, performs the
// OPTIONS/DESCRIBE/SETUP/PLAY handshake, and receives the RTP/JPEG stream,
// delivering reassembled JPEG frames via a callback.
#pragma once

#include <cstdint>
#include <cstddef>

#include "esp_netif_ip_addr.h"
#include "media/media_types.h"

namespace rtsp {

// Callback receives a reassembled elementary frame (JPEG or H.264 Annex-B).
typedef void (*client_frame_cb_t)(const uint8_t *frame, size_t len);

void client_connect(esp_ip4_addr_t ip, uint16_t rtsp_port, media::Codec codec, client_frame_cb_t cb);
void client_stop();
bool client_connected();

} // namespace rtsp
