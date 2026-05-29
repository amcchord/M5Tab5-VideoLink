#include "rtsp/link.h"

#include "rtsp/rtsp_server.h"
#include "rtsp/rtsp_client.h"
#include "media/video_pipeline.h"
#include "esp_log.h"

static const char *TAG = "link";

namespace {

media::Codec s_codec = media::Codec::MJPEG;

void on_client_frame(const uint8_t *frame, size_t len)
{
    media::pipeline_submit_remote(frame, len, s_codec);
}

} // namespace

namespace rtsp {

void link_start(uint16_t rtsp_port, media::Codec codec)
{
    s_codec = codec;
    server_start(rtsp_port);
}

void link_on_encoded(const media::EncodedFrame &frame)
{
    if (frame.codec == media::Codec::H264) {
        server_send_h264(frame.data, frame.size);
    } else {
        server_send_jpeg(frame.data, frame.size);
    }
}

void link_on_peer(const net::Peer &peer)
{
    if (!client_connected()) {
        ESP_LOGI(TAG, "connecting to peer %s", peer.name);
        client_connect(peer.ip, peer.port, s_codec, on_client_frame);
    }
}

} // namespace rtsp
