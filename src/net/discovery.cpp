#include "net/discovery.h"

#include <cstring>

#include "app/app_config.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "mdns.h"

static const char *TAG = "mdns";

namespace {

char s_hostname[64] = {};
net::Peer s_peer = {};
net::peer_cb_t s_cb = nullptr;

void browse_task(void *arg)
{
    (void) arg;
    while (true) {
        mdns_result_t *results = nullptr;
        esp_err_t err = mdns_query_ptr(VIDEOLINK_SERVICE_TYPE, VIDEOLINK_SERVICE_PROTO,
                                       3000, 10, &results);
        if (err == ESP_OK && results != nullptr) {
            for (mdns_result_t *r = results; r != nullptr; r = r->next) {
                // Skip our own advertisement.
                if (r->hostname != nullptr && strncmp(r->hostname, s_hostname, sizeof(s_hostname)) == 0) {
                    continue;
                }
                for (mdns_ip_addr_t *a = r->addr; a != nullptr; a = a->next) {
                    if (a->addr.type != ESP_IPADDR_TYPE_V4) {
                        continue;
                    }
                    net::Peer p = {};
                    const char *nm = r->instance_name ? r->instance_name
                                     : (r->hostname ? r->hostname : "peer");
                    strncpy(p.name, nm, sizeof(p.name) - 1);
                    p.ip = a->addr.u_addr.ip4;
                    p.port = r->port;
                    p.valid = true;
                    s_peer = p;
                    ESP_LOGI(TAG, "Peer '%s' at " IPSTR ":%u", p.name, IP2STR(&p.ip), p.port);
                    if (s_cb) {
                        s_cb(p);
                    }
                    break;
                }
            }
            mdns_query_results_free(results);
        }
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}

} // namespace

namespace net {

esp_err_t discovery_start(const char *device_name, uint16_t rtsp_port,
                          const char *codec, peer_cb_t cb)
{
    s_cb = cb;
    strncpy(s_hostname, device_name, sizeof(s_hostname) - 1);

    esp_err_t err = mdns_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mdns_init -> %s", esp_err_to_name(err));
        return err;
    }
    mdns_hostname_set(device_name);
    mdns_instance_name_set(device_name);

    mdns_txt_item_t txt[] = {
        {"path", "/" VIDEOLINK_RTSP_PATH},
        {"codec", (char *) codec},
    };
    err = mdns_service_add(device_name, VIDEOLINK_SERVICE_TYPE, VIDEOLINK_SERVICE_PROTO,
                           rtsp_port, txt, sizeof(txt) / sizeof(txt[0]));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "mdns_service_add -> %s", esp_err_to_name(err));
    }

    xTaskCreatePinnedToCore(browse_task, "mdns_browse", 4096, nullptr, 4, nullptr, 0);
    ESP_LOGI(TAG, "Advertising %s.%s.%s:%u", device_name, VIDEOLINK_SERVICE_TYPE,
             VIDEOLINK_SERVICE_PROTO, rtsp_port);
    return ESP_OK;
}

bool discovery_get_peer(Peer *out)
{
    if (s_peer.valid && out != nullptr) {
        *out = s_peer;
        return true;
    }
    return false;
}

} // namespace net
