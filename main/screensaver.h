#pragma once

#include <stdint.h>

void screensaver_init(void);

/* Renders one strip of the screensaver (noof particles + status overlay).
   Call once per strip with g_fb_y_offset set, then blit. */
void screensaver_render_strip(uint16_t *fb);

/* Advance simulation by one frame. Call once per full screen update (after both
   strips have been drawn) so motion isn't doubled. */
void screensaver_step(void);
