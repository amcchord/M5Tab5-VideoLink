#include "rtsp/link.h"

#include "rtsp/rtsp_server.h"
#include "rtsp/rtsp_client.h"
#include "media/video_pipeline.h"
#include "esp_log.h"

static const char *TAG = "link";

namespace {

void on_client_jpeg(const uint8_t *jpeg, size_t len)
{
    media::pipeline_submit_remote(jpeg, len, media::Codec::MJPEG);
}

} // namespace

namespace rtsp {

void link_start(uint16_t rtsp_port)
{
    server_start(rtsp_port);
}

void link_on_encoded(const media::EncodedFrame &frame)
{
    if (frame.codec == media::Codec::MJPEG) {
        server_send_jpeg(frame.data, frame.size);
    }
    // H.264 (RFC 6184) packetization is added in the H.264 milestone.
}

void link_on_peer(const net::Peer &peer)
{
    if (!client_connected()) {
        ESP_LOGI(TAG, "connecting to peer %s", peer.name);
        client_connect(peer.ip, peer.port, on_client_jpeg);
    }
}

} // namespace rtsp
