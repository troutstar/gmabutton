#include "screensaver_geiss.h"
#include "draw.h"

/* Geiss — warp-feedback screensaver. Implementation coming soon. */

void geiss_init(void) {}
void geiss_step(void) {}

void geiss_render_strip(uint16_t *fb)
{
    draw_fill(fb, COL_BLACK);
    draw_str_centered(fb, 104, "Geiss", COL_CYAN, 2);
    draw_str_centered(fb, 124, "coming soon", COL_GRAY, 1);
}
