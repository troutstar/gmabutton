#pragma once

#include "esp_err.h"

/* In normal mode: STA, connect to g_ctx.wifi_ssid, start SNTP after IP.
   In config mode (g_ctx.config_mode == true): start a soft-AP named
   "gmabutton-XXXX" with the last 4 hex digits of the device MAC.
   Either way, esp_netif + event loop are initialised before returning. */
esp_err_t wifi_manager_init(void);
