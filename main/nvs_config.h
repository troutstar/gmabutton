#pragma once

#include "app_state.h"
#include "esp_err.h"

/* Initialise NVS flash partition. Idempotent. */
esp_err_t nvs_config_init(void);

/* Load all persistent settings into g_ctx. Sets g_ctx.config_mode = true
   when wifi_ssid is empty (first boot or after factory reset). */
esp_err_t nvs_config_load(void);

/* Save all persistent settings from g_ctx. */
esp_err_t nvs_config_save(void);

/* Wipe the namespace and request a restart. Does not return. */
void nvs_config_factory_reset(void);
