// WiFi bring-up over the ESP32-C6 co-processor (esp_hosted + esp_wifi_remote).
// Supports both creating a network (SoftAP) and joining one (STA), selected
// from persisted settings.
#pragma once

#include "esp_err.h"
#include "esp_netif_ip_addr.h"

namespace net {

// connected: link is usable (STA got IP, or AP started). ip: our address.
typedef void (*wifi_status_cb_t)(bool connected, esp_ip4_addr_t ip, const char *msg);

// Powers the C6, initializes netif/event/wifi. Does not start an interface;
// call wifi_apply() afterwards. Returns an error (without aborting) on failure
// so the UI can stay up even if WiFi is unavailable.
esp_err_t wifi_init(wifi_status_cb_t cb);

// (Re)starts WiFi in the mode/credentials from current settings.
esp_err_t wifi_apply();

bool wifi_is_connected();
esp_ip4_addr_t wifi_ip();

} // namespace net
