#pragma once

#include <stdint.h>
#include "app_state.h"

/* Number of screensavers available. */
#define SS_COUNT            2
/* Number of items in the top-level system menu (Screensaver / Factory Reset). */
#define SS_SYS_MENU_COUNT   3
/* Number of items in the factory-reset confirm screen (Cancel / Confirm). */
#define SS_CONFIRM_COUNT    2
/* Inactivity timeout before any menu auto-dismisses (ms). */
#define SS_MENU_TIMEOUT_MS  20000

/* Display names shown in the picker — indexed by screensaver_id_t. */
extern const char *const ss_names[SS_COUNT];

/* Switch the active screensaver, save to NVS, and reinitialise it. */
void screensaver_switch(uint8_t id);

/* Standard three-function interface called by main.c. */
void screensaver_init(void);
void screensaver_step(void);

/* Renders one strip of the screensaver or the active menu overlay.
   Call once per strip with g_fb_y_offset set, then blit. */
void screensaver_render_strip(uint16_t *fb);
