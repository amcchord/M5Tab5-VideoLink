// Detects an outdated ESP32-C6 ESP-Hosted slave firmware and updates it from
// the embedded image over the SDIO link. The stock Tab5 C6 image reports
// version 0.0.0, which associates to WiFi but can't pass data (no DHCP/IP)
// because it mismatches the host ESP-Hosted version.
#pragma once

namespace net {

// Must be called after esp_hosted_init()/esp_hosted_connect_to_slave().
// If the C6 firmware is older than the embedded image, flashes it over SDIO,
// activates it, and reboots the P4 (does not return in that case). Returns
// false if no update was needed or the update could not be performed.
bool coproc_update_if_needed();

} // namespace net
