#include "media/audio.h"

#include <cstring>
#include <cstdlib>

#include "app/app_config.h"

#include "bsp/m5stack_tab5.h"
#include "esp_codec_dev.h"
#include "lwip/sockets.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "audio";

#define AUDIO_PT       VIDEOLINK_RTP_PT_AUDIO
#define FRAME_SAMPLES  VIDEOLINK_AUDIO_FRAME
#define FRAME_BYTES    (FRAME_SAMPLES * 2)

namespace {

esp_codec_dev_handle_t s_spk = nullptr;
esp_codec_dev_handle_t s_mic = nullptr;
SemaphoreHandle_t s_lock = nullptr;

int s_tx_sock = -1;
struct sockaddr_in s_peer = {};
bool s_have_peer = false;
bool s_muted = false;
uint32_t s_ssrc = 0x51A0D102;

void swap16_block(const uint8_t *src, uint8_t *dst, size_t n_samples)
{
    for (size_t i = 0; i < n_samples; i++) {
        dst[2 * i] = src[2 * i + 1];
        dst[2 * i + 1] = src[2 * i];
    }
}

// Core 1: capture mic, packetize RTP/L16, send to peer.
void mic_task(void *arg)
{
    (void) arg;
    uint8_t pcm[FRAME_BYTES];
    uint8_t pkt[12 + FRAME_BYTES];
    uint16_t seq = 0;
    uint32_t ts = 0;

    while (true) {
        if (s_mic == nullptr || esp_codec_dev_read(s_mic, pcm, FRAME_BYTES) != 0) {
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }
        ts += FRAME_SAMPLES;

        xSemaphoreTake(s_lock, portMAX_DELAY);
        bool go = s_have_peer && !s_muted && s_tx_sock >= 0;
        struct sockaddr_in dest = s_peer;
        xSemaphoreGive(s_lock);
        if (!go) {
            continue;
        }

        pkt[0] = 0x80;
        pkt[1] = AUDIO_PT;
        pkt[2] = (seq >> 8) & 0xFF;
        pkt[3] = seq & 0xFF;
        pkt[4] = (ts >> 24) & 0xFF;
        pkt[5] = (ts >> 16) & 0xFF;
        pkt[6] = (ts >> 8) & 0xFF;
        pkt[7] = ts & 0xFF;
        pkt[8] = (s_ssrc >> 24) & 0xFF;
        pkt[9] = (s_ssrc >> 16) & 0xFF;
        pkt[10] = (s_ssrc >> 8) & 0xFF;
        pkt[11] = s_ssrc & 0xFF;
        swap16_block(pcm, pkt + 12, FRAME_SAMPLES); // L16 is big-endian
        seq++;
        sendto(s_tx_sock, pkt, sizeof(pkt), 0, (struct sockaddr *) &dest, sizeof(dest));
    }
}

// Core 0: receive RTP/L16, play to speaker.
void speaker_task(void *arg)
{
    (void) arg;
    int rx = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in la = {};
    la.sin_family = AF_INET;
    la.sin_addr.s_addr = htonl(INADDR_ANY);
    la.sin_port = htons(VIDEOLINK_AUDIO_PORT);
    bind(rx, (struct sockaddr *) &la, sizeof(la));

    uint8_t pkt[12 + FRAME_BYTES + 64];
    uint8_t pcm[FRAME_BYTES + 64];
    while (true) {
        int n = recvfrom(rx, pkt, sizeof(pkt), 0, nullptr, nullptr);
        if (n <= 12 || s_spk == nullptr) {
            continue;
        }
        size_t samples = (size_t) (n - 12) / 2;
        swap16_block(pkt + 12, pcm, samples); // BE -> host
        esp_codec_dev_write(s_spk, pcm, (int) (samples * 2));
    }
}

} // namespace

namespace media {

esp_err_t audio_init(uint8_t volume, uint8_t mic_gain, bool muted)
{
    s_lock = xSemaphoreCreateMutex();
    s_muted = muted;

    esp_err_t err = bsp_audio_init(nullptr);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "bsp_audio_init -> %s", esp_err_to_name(err));
        return err;
    }
    s_spk = bsp_audio_codec_speaker_init();
    s_mic = bsp_audio_codec_microphone_init();
    if (s_spk == nullptr || s_mic == nullptr) {
        ESP_LOGE(TAG, "codec init failed (spk=%p mic=%p)", s_spk, s_mic);
        return ESP_FAIL;
    }

    esp_codec_dev_sample_info_t fs = {};
    fs.bits_per_sample = 16;
    fs.channel = 1;
    fs.channel_mask = 0;
    fs.sample_rate = VIDEOLINK_AUDIO_RATE;
    fs.mclk_multiple = 0;
    esp_codec_dev_open(s_spk, &fs);
    esp_codec_dev_open(s_mic, &fs);

    audio_set_volume(volume);
    audio_set_mic_gain(mic_gain);
    audio_set_mic_muted(muted);

    s_tx_sock = socket(AF_INET, SOCK_DGRAM, 0);

    xTaskCreatePinnedToCore(mic_task, "mic_tx", 4096, nullptr, 5, nullptr, 1);
    xTaskCreatePinnedToCore(speaker_task, "spk_rx", 4096, nullptr, 5, nullptr, 0);
    ESP_LOGI(TAG, "audio up: %d Hz mono, RTP/L16 on UDP %d", VIDEOLINK_AUDIO_RATE, VIDEOLINK_AUDIO_PORT);
    return ESP_OK;
}

void audio_set_volume(uint8_t volume)
{
    if (s_spk) {
        esp_codec_dev_set_out_vol(s_spk, (int) (volume > 100 ? 100 : volume));
    }
}

void audio_set_mic_gain(uint8_t gain)
{
    if (s_mic) {
        float db = (gain > 100 ? 100 : gain) * 0.4f; // 0..40 dB
        esp_codec_dev_set_in_gain(s_mic, db);
    }
}

void audio_set_mic_muted(bool muted)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_muted = muted;
    xSemaphoreGive(s_lock);
    if (s_mic) {
        esp_codec_dev_set_in_mute(s_mic, muted);
    }
}

void audio_set_peer(esp_ip4_addr_t ip)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_peer.sin_family = AF_INET;
    s_peer.sin_addr.s_addr = ip.addr;
    s_peer.sin_port = htons(VIDEOLINK_AUDIO_PORT);
    s_have_peer = true;
    xSemaphoreGive(s_lock);
}

} // namespace media
