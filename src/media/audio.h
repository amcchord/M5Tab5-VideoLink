// Two-way audio: ES7210 microphone capture -> RTP/L16 -> peer, and peer
// RTP/L16 -> ES8388 speaker. Uses the BSP's esp_codec_dev handles. Audio runs
// on a dedicated UDP port (not RTSP-negotiated) for simplicity; the payload is
// standard 16-bit linear PCM (RFC 3551 L16) so it remains interoperable.
#pragma once

#include "esp_err.h"
#include "esp_netif_ip_addr.h"

namespace media {

esp_err_t audio_init(uint8_t volume, uint8_t mic_gain, bool muted);

void audio_set_volume(uint8_t volume);   // 0..100 (speaker)
void audio_set_mic_gain(uint8_t gain);   // 0..100 (microphone)
void audio_set_mic_muted(bool muted);

// Sets the destination peer for outgoing audio.
void audio_set_peer(esp_ip4_addr_t ip);

} // namespace media
