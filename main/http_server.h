#pragma once

#include "esp_err.h"
#include <stdbool.h>

/* Starts the HTTP server. Endpoints differ based on g_ctx.config_mode:
 *   config_mode == true  → setup wizard (/, POST /api/setup)
 *   config_mode == false → status/debug UI + alert/dismiss API
 */
esp_err_t http_server_start(void);

/* Helper used by main.c to translate received alerts into local state changes.
   local=true when triggered by a gesture, false when received from a peer. */
void http_apply_alert_call(bool local);
void http_apply_alert_help(bool local);
void http_apply_dismiss(void);
