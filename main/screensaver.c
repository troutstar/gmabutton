#include "screensaver.h"
#include "screensaver_noofcyd.h"
#include "screensaver_geiss.h"
#include "app_state.h"
#include "draw.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_timer.h"
#include "led.h"

/* ── Screensaver registry ─────────────────────────────────────────────── */

const char *const ss_names[SS_COUNT] = { "Noof CYD", "Geiss" };

static void (*const s_init  [SS_COUNT])(void)          = { noofcyd_init,         geiss_init         };
static void (*const s_step  [SS_COUNT])(void)          = { noofcyd_step,         geiss_step         };
static void (*const s_render[SS_COUNT])(uint16_t *)    = { noofcyd_render_strip, geiss_render_strip };

#define SS_NVS_NS  "gmabutton"
#define SS_NVS_KEY "ss_id"

/* ── NVS helpers ──────────────────────────────────────────────────────── */

static void nv_load(void)
{
    nvs_handle_t h;
    if (nvs_open(SS_NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        uint8_t id = 0;
        nvs_get_u8(h, SS_NVS_KEY, &id);
        nvs_close(h);
        g_ctx.active_ss = (id < SS_COUNT) ? id : 0;
    }
}

static void nv_save(uint8_t id)
{
    nvs_handle_t h;
    if (nvs_open(SS_NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, SS_NVS_KEY, id);
        nvs_commit(h);
        nvs_close(h);
    }
}

/* ── Public — switch active screensaver ──────────────────────────────── */

void screensaver_switch(uint8_t id)
{
    if (id >= SS_COUNT) id = 0;
    g_ctx.active_ss = id;
    nv_save(id);
    s_init[id]();
}

/* ── Menu rendering helpers ───────────────────────────────────────────── */

/* Draw a full-screen menu.  title at top, items listed below.
   selected item is highlighted; active_mark (≥0) gets a dim marker. */
/* All menu overlays draw on top of the live screensaver — no background fill.
   Tap the zone containing the option you want.  No cycling or hold required. */

static void timeout_bar(uint16_t *fb, uint32_t entered_ms, uint16_t col)
{
    uint32_t elapsed = (uint32_t)(esp_timer_get_time() / 1000) - entered_ms;
    float fill = 1.0f - ((float)elapsed / (float)SS_MENU_TIMEOUT_MS);
    if (fill < 0.f) fill = 0.f;
    draw_hbar(fb, 0, 234, SCREEN_W, 4, fill, col);
}

/* Divide screen into N equal tap zones.  Visual zones match gesture zones exactly
   so that tapping anywhere in a labelled region selects that option. */
static void draw_menu(uint16_t *fb,
                      const char *title,
                      const char *const *items, int count,
                      int active_mark,
                      uint32_t entered_ms)
{
    int zone_h = SCREEN_H / count;   /* e.g. 80px for 3 items, 120px for 2 */

    for (int i = 0; i < count; i++) {
        int top = i * zone_h;
        int ty  = top + (zone_h - 32) / 2;   /* vertically centre 32px text */
        if (i > 0) draw_line(fb, 0, top, SCREEN_W, top, COL_GRAY);
        draw_str_centered(fb, ty, items[i], COL_WHITE, 4);
        if (i == active_mark)
            draw_fill_rect(fb, 8, ty + 13, 5, 5, COL_CYAN);
    }

    /* Title in top-left corner, small so it doesn't crowd the tap zone */
    draw_str(fb, 4, 4, title, COL_CYAN, 1);

    timeout_bar(fb, entered_ms, COL_CYAN);
}

static void draw_confirm(uint16_t *fb, uint32_t entered_ms)
{
    /* Top half (y 0-119): Cancel */
    draw_str_centered(fb, 44, "Cancel", COL_WHITE, 4);

    /* Bottom half (y 120-239): Confirm */
    draw_line(fb, 0, 120, SCREEN_W, 120, COL_GRAY);
    draw_str_centered(fb, 164, "Confirm Reset", COL_RED, 4);

    draw_str(fb, 4, 4, "Factory Reset?", COL_RED, 1);

    timeout_bar(fb, entered_ms, COL_RED);
}

/* ── Public API ───────────────────────────────────────────────────────── */

void screensaver_init(void)
{
    nv_load();
    s_init[g_ctx.active_ss]();
}

void screensaver_step(void)
{
    app_state_t s = g_ctx.state;

    /* Step the active screensaver only when it's actually visible */
    if (s == STATE_SCREENSAVER)
        s_step[g_ctx.active_ss]();

    /* Auto-dismiss menus after timeout */
    if (s == STATE_SYSTEM_MENU || s == STATE_SS_PICKER || s == STATE_FACTORY_RESET_CONFIRM) {
        uint32_t now     = (uint32_t)(esp_timer_get_time() / 1000);
        uint32_t elapsed = now - g_ctx.menu_entered_ms;
        if (elapsed >= SS_MENU_TIMEOUT_MS) {
            led_off();
            app_set_state(STATE_SCREENSAVER);
        }
    }
}

void screensaver_render_strip(uint16_t *fb)
{
    /* Always render the live screensaver first — menus overlay on top. */
    s_render[g_ctx.active_ss](fb);

    switch (g_ctx.state) {
    case STATE_SCREENSAVER:
        break;

    case STATE_SYSTEM_MENU: {
        static const char *const items[] = { "Screensaver", "Reboot", "Factory Reset" };
        draw_menu(fb, "Options", items, SS_SYS_MENU_COUNT, -1,
                  g_ctx.menu_entered_ms);
        break;
    }

    case STATE_SS_PICKER:
        draw_menu(fb, "Screensaver", ss_names, SS_COUNT,
                  (int)g_ctx.active_ss, g_ctx.menu_entered_ms);
        break;

    case STATE_FACTORY_RESET_CONFIRM:
        draw_confirm(fb, g_ctx.menu_entered_ms);
        break;

    default:
        break;
    }
}
