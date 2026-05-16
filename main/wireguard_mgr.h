#pragma once

#include "esp_err.h"

/* Brings up a WireGuard client interface using the keys/endpoint in g_ctx.
   Returns ESP_OK on success, ESP_ERR_INVALID_STATE if WG is unconfigured
   (in which case the rest of the system continues with plain WiFi only).
   Also spawns wg_task — a watchdog that re-establishes the tunnel if the
   handshake goes stale. */
esp_err_t wireguard_mgr_start(void);
