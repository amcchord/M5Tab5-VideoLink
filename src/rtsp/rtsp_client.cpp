#include "rtsp/rtsp_client.h"

#include <cstring>
#include <cstdio>

#include "rtsp/rtp.h"
#include "app/app_config.h"

#include "lwip/sockets.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "rtsp_cli";

#define CLIENT_RTP_PORT 5004

namespace {

volatile bool s_running = false;
uint32_t s_peer_addr = 0;
rtsp::client_jpeg_cb_t s_cb = nullptr;
rtsp::RtpJpegReassembler s_reasm;

void on_reasm_frame(const uint8_t *jpeg, size_t len, void *user)
{
    (void) user;
    if (s_cb) {
        s_cb(jpeg, len);
    }
}

// Send an RTSP request line + headers and read the response into `resp`.
bool rtsp_txn(int tcp, const char *req, char *resp, size_t resp_cap)
{
    if (send(tcp, req, strlen(req), 0) < 0) {
        return false;
    }
    int n = recv(tcp, resp, resp_cap - 1, 0);
    if (n <= 0) {
        return false;
    }
    resp[n] = '\0';
    return strncmp(resp, "RTSP/1.0 200", 12) == 0;
}

void client_task(void *arg)
{
    (void) arg;
    char url_host[24];
    uint32_t a = ntohl(s_peer_addr);
    snprintf(url_host, sizeof(url_host), "%u.%u.%u.%u", (unsigned) ((a >> 24) & 0xFF),
             (unsigned) ((a >> 16) & 0xFF), (unsigned) ((a >> 8) & 0xFF), (unsigned) (a & 0xFF));

    int tcp = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in sa = {};
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = s_peer_addr;
    sa.sin_port = htons(VIDEOLINK_RTSP_PORT);
    if (connect(tcp, (struct sockaddr *) &sa, sizeof(sa)) < 0) {
        ESP_LOGW(TAG, "connect to %s failed", url_host);
        close(tcp);
        s_running = false;
        vTaskDelete(nullptr);
        return;
    }

    char req[256];
    char resp[1024];
    int cseq = 1;
    char base[64];
    snprintf(base, sizeof(base), "rtsp://%s:%d/%s", url_host, VIDEOLINK_RTSP_PORT, VIDEOLINK_RTSP_PATH);

    snprintf(req, sizeof(req), "OPTIONS %s RTSP/1.0\r\nCSeq: %d\r\n\r\n", base, cseq++);
    rtsp_txn(tcp, req, resp, sizeof(resp));

    snprintf(req, sizeof(req), "DESCRIBE %s RTSP/1.0\r\nCSeq: %d\r\nAccept: application/sdp\r\n\r\n",
             base, cseq++);
    rtsp_txn(tcp, req, resp, sizeof(resp));

    snprintf(req, sizeof(req),
             "SETUP %s RTSP/1.0\r\nCSeq: %d\r\n"
             "Transport: RTP/AVP;unicast;client_port=%d-%d\r\n\r\n",
             base, cseq++, CLIENT_RTP_PORT, CLIENT_RTP_PORT + 1);
    if (!rtsp_txn(tcp, req, resp, sizeof(resp))) {
        ESP_LOGW(TAG, "SETUP failed");
        close(tcp);
        s_running = false;
        vTaskDelete(nullptr);
        return;
    }

    snprintf(req, sizeof(req), "PLAY %s RTSP/1.0\r\nCSeq: %d\r\nSession: 12345678\r\n\r\n", base, cseq++);
    rtsp_txn(tcp, req, resp, sizeof(resp));
    ESP_LOGI(TAG, "playing %s", base);

    // Receive RTP/JPEG on our client port.
    int udp = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in la = {};
    la.sin_family = AF_INET;
    la.sin_addr.s_addr = htonl(INADDR_ANY);
    la.sin_port = htons(CLIENT_RTP_PORT);
    bind(udp, (struct sockaddr *) &la, sizeof(la));
    struct timeval tv = {.tv_sec = 1, .tv_usec = 0};
    setsockopt(udp, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    s_reasm.init(on_reasm_frame, nullptr);
    uint8_t *pkt = (uint8_t *) malloc(1600);
    while (s_running) {
        int n = recvfrom(udp, pkt, 1600, 0, nullptr, nullptr);
        if (n > 0) {
            s_reasm.feed(pkt, (size_t) n);
        } else {
            // timeout: send an RTSP keepalive (GET_PARAMETER-ish via OPTIONS)
            snprintf(req, sizeof(req), "OPTIONS %s RTSP/1.0\r\nCSeq: %d\r\n\r\n", base, cseq++);
            send(tcp, req, strlen(req), 0);
        }
    }
    free(pkt);
    close(udp);
    close(tcp);
    s_running = false;
    vTaskDelete(nullptr);
}

} // namespace

namespace rtsp {

void client_connect(esp_ip4_addr_t ip, uint16_t rtsp_port, client_jpeg_cb_t cb)
{
    (void) rtsp_port; // we use VIDEOLINK_RTSP_PORT
    if (s_running) {
        return; // already connected to a peer
    }
    s_cb = cb;
    s_peer_addr = ip.addr;
    s_running = true;
    xTaskCreatePinnedToCore(client_task, "rtsp_cli", 6144, nullptr, 5, nullptr, 0);
}

void client_stop()
{
    s_running = false;
}

bool client_connected()
{
    return s_running;
}

} // namespace rtsp
