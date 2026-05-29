// Minimal RTSP client that connects to the peer's RTSP server, performs the
// OPTIONS/DESCRIBE/SETUP/PLAY handshake, and receives the RTP/JPEG stream,
// delivering reassembled JPEG frames via a callback.
#pragma once

#include <cstdint>
#include <cstddef>

#include "esp_netif_ip_addr.h"

namespace rtsp {

typedef void (*client_jpeg_cb_t)(const uint8_t *jpeg, size_t len);

void client_connect(esp_ip4_addr_t ip, uint16_t rtsp_port, client_jpeg_cb_t cb);
void client_stop();
bool client_connected();

} // namespace rtsp
