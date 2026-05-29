// Minimal RTSP server (RFC 2326 subset) that publishes our camera as an
// RTP/JPEG stream. Handles OPTIONS/DESCRIBE/SETUP/PLAY/TEARDOWN for a single
// client (the peer Tab5). After PLAY, server_send_jpeg() streams frames.
#pragma once

#include <cstdint>
#include <cstddef>

namespace rtsp {

void server_start(uint16_t rtsp_port);

// Packetize + send one frame to the connected client (no-op if none).
void server_send_jpeg(const uint8_t *jpeg, size_t len);
void server_send_h264(const uint8_t *annexb, size_t len);

bool server_has_client();

} // namespace rtsp
