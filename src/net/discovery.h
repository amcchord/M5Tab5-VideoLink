// mDNS / Bonjour service advertisement + peer discovery. We advertise a
// standard _rtsp._tcp service and browse for the same to find the other Tab5.
#pragma once

#include "esp_err.h"
#include "esp_netif_ip_addr.h"

namespace net {

struct Peer {
    char name[64];
    esp_ip4_addr_t ip;
    uint16_t port;
    bool valid;
};

typedef void (*peer_cb_t)(const Peer &peer);

// Advertises our RTSP service and starts a background browse task that calls
// `cb` whenever a peer is (re)discovered. `codec` is published in a TXT record.
esp_err_t discovery_start(const char *device_name, uint16_t rtsp_port,
                          const char *codec, peer_cb_t cb);

// Returns the most recently discovered peer, if any.
bool discovery_get_peer(Peer *out);

} // namespace net
