#pragma once

#include "app_state.h"
#include <stdint.h>

/* Renders one strip of the current alert state.
 *   STATE_ALERT_CALL       → solid blue
 *   STATE_ALERT_HELP       → solid red
 *   STATE_DISMISS_PENDING  → alert colour + "TAP TO CONFIRM" overlay
 *   STATE_CONFIG_MODE      → setup hint screen (AP info)
 */
void alert_render_strip(uint16_t *fb, app_state_t state);
