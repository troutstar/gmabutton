#include "alert.h"
#include "draw.h"
#include <string.h>

/* Track the previously rendered state so dismiss-pending knows which
   colour underlay to use. Updated only when entering a non-dismiss state. */
static app_state_t s_underlay = STATE_ALERT_CALL;

static uint16_t state_color(app_state_t s)
{
    if (s == STATE_ALERT_HELP) return COL_RED;
    return COL_BLUE;
}

static void render_dismiss_overlay(uint16_t *fb)
{
    /* Only the centre strip carries the prompt — keeps logic identical for
       both render passes (we just write the same widget twice). */
    int box_w = 240, box_h = 70;
    int box_x = (SCREEN_W - box_w) / 2;
    int box_y = (SCREEN_H - box_h) / 2;

    draw_fill_rect(fb, box_x, box_y, box_w, box_h, COL_BLACK);
    draw_rect     (fb, box_x, box_y, box_w, box_h, COL_WHITE);

    draw_str_centered(fb, box_y + 12, "TAP TO DISMISS", COL_WHITE, 2);
    draw_str_centered(fb, box_y + 44, "(auto-closes)",  COL_GRAY,  1);
}

static void render_config_hint(uint16_t *fb)
{
    if (g_fb_y_offset == 0) {
        draw_fill(fb, COL_BLACK);
        draw_str_centered(fb, 20,  "SETUP MODE",       COL_YELLOW, 2);
        draw_str_centered(fb, 50,  "Connect to WiFi:", COL_WHITE,  1);
        draw_str_centered(fb, 70,  g_ctx.ap_ssid,      COL_CYAN,   2);
        draw_str_centered(fb, 100, "Then visit:",      COL_WHITE,  1);
    } else {
        draw_fill(fb, COL_BLACK);
        draw_str_centered(fb, 130, "http://192.168.4.1", COL_CYAN, 2);
        draw_str_centered(fb, 200, "10s screen hold = factory reset", COL_GRAY, 1);
    }
}

void alert_render_strip(uint16_t *fb, app_state_t state)
{
    if (state == STATE_CONFIG_MODE) {
        render_config_hint(fb);
        return;
    }

    if (state == STATE_ALERT_CALL || state == STATE_ALERT_HELP) {
        s_underlay = state;
        draw_fill(fb, state_color(state));
        return;
    }

    /* STATE_DISMISS_PENDING */
    draw_fill(fb, state_color(s_underlay));
    render_dismiss_overlay(fb);
}
