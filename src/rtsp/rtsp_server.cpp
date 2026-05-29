#include "rtsp/rtsp_server.h"

#include <cstring>
#include <cstdio>
#include <cstdint>

#include "rtsp/rtp.h"
#include "app/app_config.h"

#include "lwip/sockets.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "rtsp_srv";

#define SERVER_RTP_PORT 6970

namespace {

SemaphoreHandle_t s_lock = nullptr;
int s_udp = -1;
bool s_streaming = false;
struct sockaddr_in s_dest = {};
uint16_t s_seq = 0;
uint32_t s_ssrc = 0x32A1B3C4;

void fmt_ip(uint32_t s_addr_net, char *out, size_t n)
{
    uint32_t a = ntohl(s_addr_net);
    snprintf(out, n, "%u.%u.%u.%u", (unsigned) ((a >> 24) & 0xFF), (unsigned) ((a >> 16) & 0xFF),
             (unsigned) ((a >> 8) & 0xFF), (unsigned) (a & 0xFF));
}

int hdr_cseq(const char *req)
{
    const char *q = strstr(req, "CSeq:");
    if (q == nullptr) {
        q = strstr(req, "Cseq:");
    }
    int v = 0;
    if (q != nullptr) {
        sscanf(q, "%*[^:]: %d", &v);
    }
    return v;
}

void send_str(int c, const char *s)
{
    send(c, s, strlen(s), 0);
}

void handle_client(int c, const struct sockaddr_in *peer)
{
    char our_ip[16];
    struct sockaddr_in la;
    socklen_t ll = sizeof(la);
    if (getsockname(c, (struct sockaddr *) &la, &ll) == 0) {
        fmt_ip(la.sin_addr.s_addr, our_ip, sizeof(our_ip));
    } else {
        strcpy(our_ip, "0.0.0.0");
    }

    char buf[1024];
    char resp[512];
    while (true) {
        int n = recv(c, buf, sizeof(buf) - 1, 0);
        if (n <= 0) {
            break;
        }
        buf[n] = '\0';
        int cseq = hdr_cseq(buf);

        if (strncmp(buf, "OPTIONS", 7) == 0) {
            snprintf(resp, sizeof(resp),
                     "RTSP/1.0 200 OK\r\nCSeq: %d\r\n"
                     "Public: OPTIONS, DESCRIBE, SETUP, PLAY, TEARDOWN\r\n\r\n", cseq);
            send_str(c, resp);
        } else if (strncmp(buf, "DESCRIBE", 8) == 0) {
            char sdp[384];
            int sl = snprintf(sdp, sizeof(sdp),
                              "v=0\r\no=- 0 0 IN IP4 %s\r\ns=VideoLink\r\nc=IN IP4 %s\r\nt=0 0\r\n"
                              "m=video 0 RTP/AVP %d\r\na=rtpmap:%d JPEG/90000\r\na=control:*\r\n",
                              our_ip, our_ip, VIDEOLINK_RTP_PT_JPEG, VIDEOLINK_RTP_PT_JPEG);
            snprintf(resp, sizeof(resp),
                     "RTSP/1.0 200 OK\r\nCSeq: %d\r\nContent-Type: application/sdp\r\n"
                     "Content-Length: %d\r\n\r\n", cseq, sl);
            send_str(c, resp);
            send(c, sdp, sl, 0);
        } else if (strncmp(buf, "SETUP", 5) == 0) {
            int cp = 0, cp2 = 0;
            const char *t = strstr(buf, "client_port=");
            if (t != nullptr) {
                sscanf(t, "client_port=%d-%d", &cp, &cp2);
            }
            xSemaphoreTake(s_lock, portMAX_DELAY);
            s_dest.sin_family = AF_INET;
            s_dest.sin_addr = peer->sin_addr;
            s_dest.sin_port = htons(cp);
            xSemaphoreGive(s_lock);
            snprintf(resp, sizeof(resp),
                     "RTSP/1.0 200 OK\r\nCSeq: %d\r\n"
                     "Transport: RTP/AVP;unicast;client_port=%d-%d;server_port=%d-%d;ssrc=%08X\r\n"
                     "Session: 12345678\r\n\r\n",
                     cseq, cp, cp2, SERVER_RTP_PORT, SERVER_RTP_PORT + 1, (unsigned) s_ssrc);
            send_str(c, resp);
        } else if (strncmp(buf, "PLAY", 4) == 0) {
            xSemaphoreTake(s_lock, portMAX_DELAY);
            s_streaming = true;
            xSemaphoreGive(s_lock);
            snprintf(resp, sizeof(resp), "RTSP/1.0 200 OK\r\nCSeq: %d\r\nSession: 12345678\r\n\r\n", cseq);
            send_str(c, resp);
            ESP_LOGI(TAG, "PLAY: streaming to client RTP port %d", ntohs(s_dest.sin_port));
        } else if (strncmp(buf, "TEARDOWN", 8) == 0) {
            xSemaphoreTake(s_lock, portMAX_DELAY);
            s_streaming = false;
            xSemaphoreGive(s_lock);
            snprintf(resp, sizeof(resp), "RTSP/1.0 200 OK\r\nCSeq: %d\r\n\r\n", cseq);
            send_str(c, resp);
            break;
        } else {
            snprintf(resp, sizeof(resp), "RTSP/1.0 501 Not Implemented\r\nCSeq: %d\r\n\r\n", cseq);
            send_str(c, resp);
        }
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_streaming = false;
    xSemaphoreGive(s_lock);
    close(c);
}

void server_task(void *arg)
{
    uint16_t port = (uint16_t) (uintptr_t) arg;
    int ls = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in sa = {};
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_ANY);
    sa.sin_port = htons(port);
    if (bind(ls, (struct sockaddr *) &sa, sizeof(sa)) < 0) {
        ESP_LOGE(TAG, "bind :%u failed", port);
        close(ls);
        vTaskDelete(nullptr);
        return;
    }
    listen(ls, 1);
    ESP_LOGI(TAG, "RTSP server listening on :%u", port);

    while (true) {
        struct sockaddr_in ca;
        socklen_t cl = sizeof(ca);
        int c = accept(ls, (struct sockaddr *) &ca, &cl);
        if (c < 0) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        char ip[16];
        fmt_ip(ca.sin_addr.s_addr, ip, sizeof(ip));
        ESP_LOGI(TAG, "client %s connected", ip);
        handle_client(c, &ca);
    }
}

} // namespace

namespace rtsp {

void server_start(uint16_t rtsp_port)
{
    s_lock = xSemaphoreCreateMutex();
    s_udp = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in la = {};
    la.sin_family = AF_INET;
    la.sin_addr.s_addr = htonl(INADDR_ANY);
    la.sin_port = htons(SERVER_RTP_PORT);
    bind(s_udp, (struct sockaddr *) &la, sizeof(la));
    xTaskCreatePinnedToCore(server_task, "rtsp_srv", 6144, (void *) (uintptr_t) rtsp_port, 5, nullptr, 0);
}

void server_send_jpeg(const uint8_t *jpeg, size_t len)
{
    if (s_lock == nullptr) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool go = s_streaming && s_udp >= 0;
    struct sockaddr_in dest = s_dest;
    uint16_t seq = s_seq;
    uint32_t ssrc = s_ssrc;
    xSemaphoreGive(s_lock);
    if (!go) {
        return;
    }
    uint32_t ts = (uint32_t) (esp_timer_get_time() * 9 / 100); // us -> 90 kHz
    rtp_send_jpeg(s_udp, &dest, jpeg, len, &seq, ssrc, ts);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_seq = seq;
    xSemaphoreGive(s_lock);
}

void server_send_h264(const uint8_t *annexb, size_t len)
{
    if (s_lock == nullptr) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool go = s_streaming && s_udp >= 0;
    struct sockaddr_in dest = s_dest;
    uint16_t seq = s_seq;
    uint32_t ssrc = s_ssrc;
    xSemaphoreGive(s_lock);
    if (!go) {
        return;
    }
    uint32_t ts = (uint32_t) (esp_timer_get_time() * 9 / 100);
    rtp_send_h264(s_udp, &dest, annexb, len, &seq, ssrc, ts);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_seq = seq;
    xSemaphoreGive(s_lock);
}

bool server_has_client()
{
    return s_streaming;
}

} // namespace rtsp
