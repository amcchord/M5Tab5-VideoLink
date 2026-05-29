#include "net/wifi_manager.h"

#include <cstring>

#include "app/settings.h"
#include "board/board.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_hosted.h"

static const char *TAG = "wifi";

#define WCHECK(x)                                                            \
    do {                                                                     \
        esp_err_t _e = (x);                                                  \
        if (_e != ESP_OK) {                                                  \
            ESP_LOGE(TAG, "%s -> %s", #x, esp_err_to_name(_e));              \
            return _e;                                                       \
        }                                                                    \
    } while (0)

namespace {

esp_netif_t *s_sta_netif = nullptr;
esp_netif_t *s_ap_netif = nullptr;
volatile bool s_connected = false;
esp_ip4_addr_t s_ip = {};
net::wifi_status_cb_t s_cb = nullptr;
bool s_started = false;

void notify(const char *msg)
{
    if (s_cb) {
        s_cb(s_connected, s_ip, msg);
    }
}

void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void) arg;
    if (base == WIFI_EVENT) {
        switch (id) {
        case WIFI_EVENT_STA_START:
            esp_wifi_connect();
            break;
        case WIFI_EVENT_STA_DISCONNECTED:
            s_connected = false;
            notify("disconnected, retrying");
            vTaskDelay(pdMS_TO_TICKS(1500));
            esp_wifi_connect();
            break;
        case WIFI_EVENT_AP_START:
            s_connected = true;
            notify("network created");
            break;
        case WIFI_EVENT_AP_STACONNECTED:
            notify("peer joined");
            break;
        case WIFI_EVENT_AP_STADISCONNECTED:
            notify("peer left");
            break;
        default:
            break;
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        auto *e = static_cast<ip_event_got_ip_t *>(data);
        s_ip = e->ip_info.ip;
        s_connected = true;
        notify("connected");
    }
}

} // namespace

namespace net {

esp_err_t wifi_init(wifi_status_cb_t cb)
{
    s_cb = cb;

    WCHECK(board::wifi_power_enable(true));
    vTaskDelay(pdMS_TO_TICKS(500)); // give the C6 time to boot

    // ESP-Hosted must bring up the SDIO transport to the C6 and connect to the
    // slave BEFORE the WiFi stack is initialized, otherwise esp_wifi_init fails
    // with "Transport not initialized".
    int herr = esp_hosted_init();
    if (herr != 0) {
        ESP_LOGE(TAG, "esp_hosted_init -> %d (check C6 slave firmware / SDIO pins)", herr);
        return ESP_FAIL;
    }
    herr = esp_hosted_connect_to_slave();
    if (herr != 0) {
        ESP_LOGW(TAG, "esp_hosted_connect_to_slave -> %d", herr);
    }

    WCHECK(esp_netif_init());

    esp_err_t err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "event loop -> %s", esp_err_to_name(err));
        return err;
    }

    s_sta_netif = esp_netif_create_default_wifi_sta();
    s_ap_netif = esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    WCHECK(esp_wifi_init(&cfg));

    WCHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               &on_event, nullptr, nullptr));
    WCHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                               &on_event, nullptr, nullptr));
    WCHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_LOGI(TAG, "WiFi initialized");
    return ESP_OK;
}

esp_err_t wifi_apply()
{
    const settings::Config &c = settings::get();

    if (s_started) {
        esp_wifi_stop();
        s_started = false;
        s_connected = false;
    }

    if (c.wifi_mode == settings::WifiMode::Create) {
        wifi_config_t wc = {};
        size_t n = strlen(c.ap_ssid);
        memcpy(wc.ap.ssid, c.ap_ssid, n < sizeof(wc.ap.ssid) ? n : sizeof(wc.ap.ssid));
        wc.ap.ssid_len = (uint8_t) n;
        strncpy((char *) wc.ap.password, c.ap_password, sizeof(wc.ap.password) - 1);
        wc.ap.channel = 6;
        wc.ap.max_connection = 4;
        wc.ap.authmode = strlen(c.ap_password) >= 8 ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;

        WCHECK(esp_wifi_set_mode(WIFI_MODE_AP));
        WCHECK(esp_wifi_set_config(WIFI_IF_AP, &wc));
        WCHECK(esp_wifi_start());

        esp_netif_ip_info_t ip = {};
        if (s_ap_netif && esp_netif_get_ip_info(s_ap_netif, &ip) == ESP_OK) {
            s_ip = ip.ip;
        }
        ESP_LOGI(TAG, "SoftAP '%s' (auth=%s)", c.ap_ssid,
                 wc.ap.authmode == WIFI_AUTH_OPEN ? "open" : "wpa2");
    } else {
        wifi_config_t wc = {};
        strncpy((char *) wc.sta.ssid, c.ssid, sizeof(wc.sta.ssid) - 1);
        strncpy((char *) wc.sta.password, c.password, sizeof(wc.sta.password) - 1);

        WCHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        WCHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
        WCHECK(esp_wifi_start());
        ESP_LOGI(TAG, "Joining '%s'", c.ssid);
    }

    s_started = true;
    return ESP_OK;
}

bool wifi_is_connected()
{
    return s_connected;
}

esp_ip4_addr_t wifi_ip()
{
    return s_ip;
}

} // namespace net
