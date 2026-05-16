#include "screensaver.h"
#include "draw.h"
#include "app_state.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ── Particles with trail history. We render history each strip — no need
   to persist pixel state across the half-buffer flush. */

#define N_PARTICLES   24
#define HISTORY_LEN    6
#define OVERLAY_H     18

typedef struct {
    float x, y;
    float vx, vy;
    float hue;            /* 0..1 */
    float hx[HISTORY_LEN];
    float hy[HISTORY_LEN];
} particle_t;

static particle_t s_p[N_PARTICLES];
static uint32_t   s_seed = 0xC0FFEEu;

static inline float rnd(void)
{
    s_seed = s_seed * 1664525u + 1013904223u;
    return (s_seed >> 8) * (1.0f / 16777216.0f);   /* [0,1) */
}

static inline float frnd(float lo, float hi) { return lo + (hi - lo) * rnd(); }

/* HSV (h in [0,1]) → RGB565BE.  Saturation = 1, value = v. */
static uint16_t hue_to_color(float h, float v)
{
    float r, g, b;
    float hh = h * 6.0f;
    int   i  = (int)hh;
    float f  = hh - i;
    float p  = 0.0f;
    float q  = v * (1.0f - f);
    float t  = v * f;
    switch (i % 6) {
        case 0: r = v; g = t; b = p; break;
        case 1: r = q; g = v; b = p; break;
        case 2: r = p; g = v; b = t; break;
        case 3: r = p; g = q; b = v; break;
        case 4: r = t; g = p; b = v; break;
        default:r = v; g = p; b = q; break;
    }
    return rgb565be(r, g, b);
}

void screensaver_init(void)
{
    for (int i = 0; i < N_PARTICLES; ++i) {
        s_p[i].x   = frnd(0, SCREEN_W);
        s_p[i].y   = frnd(OVERLAY_H + 4, SCREEN_H);
        s_p[i].vx  = frnd(-1.4f, 1.4f);
        s_p[i].vy  = frnd(-1.4f, 1.4f);
        s_p[i].hue = rnd();
        for (int k = 0; k < HISTORY_LEN; ++k) {
            s_p[i].hx[k] = s_p[i].x;
            s_p[i].hy[k] = s_p[i].y;
        }
    }
}

void screensaver_step(void)
{
    for (int i = 0; i < N_PARTICLES; ++i) {
        particle_t *p = &s_p[i];
        /* shift history */
        for (int k = HISTORY_LEN - 1; k > 0; --k) {
            p->hx[k] = p->hx[k - 1];
            p->hy[k] = p->hy[k - 1];
        }
        p->hx[0] = p->x;
        p->hy[0] = p->y;

        /* small random walk in acceleration for an organic feel */
        p->vx += frnd(-0.06f, 0.06f);
        p->vy += frnd(-0.06f, 0.06f);
        /* speed limit */
        if (p->vx >  1.6f) p->vx =  1.6f;
        if (p->vx < -1.6f) p->vx = -1.6f;
        if (p->vy >  1.6f) p->vy =  1.6f;
        if (p->vy < -1.6f) p->vy = -1.6f;

        p->x += p->vx;
        p->y += p->vy;

        /* wrap, leaving the overlay strip alone */
        if (p->x < 0)              p->x += SCREEN_W;
        if (p->x >= SCREEN_W)      p->x -= SCREEN_W;
        if (p->y < OVERLAY_H + 2)  p->y = SCREEN_H - 1;
        if (p->y >= SCREEN_H)      p->y = OVERLAY_H + 2;

        p->hue += 0.001f;
        if (p->hue >= 1.0f) p->hue -= 1.0f;
    }
}

static void draw_overlay(uint16_t *fb)
{
    /* dark band across the top */
    draw_fill_rect(fb, 0, 0, SCREEN_W, OVERLAY_H, COL_DKGRAY);

    /* device name on the left */
    draw_str(fb, 4, 5, g_ctx.device_name, COL_CYAN, 1);

    /* time centred */
    const char *t = g_ctx.time_str[0] ? g_ctx.time_str : "--:--:--";
    draw_str(fb, SCREEN_W / 2 - 24, 5, t, COL_WHITE, 1);

    /* status indicators on the right: WiFi + WG dots */
    uint16_t wifi_c = g_ctx.wifi_connected ? COL_GREEN : COL_RED;
    uint16_t wg_c   = g_ctx.wg_connected   ? COL_GREEN :
                      (g_ctx.wg_endpoint[0] ? COL_YELLOW : COL_GRAY);
    draw_str     (fb, SCREEN_W - 56, 5, "W",   wifi_c, 1);
    draw_fill_rect(fb, SCREEN_W - 46, 6, 6, 6, wifi_c);
    draw_str     (fb, SCREEN_W - 30, 5, "V",   wg_c,   1);
    draw_fill_rect(fb, SCREEN_W - 20, 6, 6, 6, wg_c);
}

void screensaver_render_strip(uint16_t *fb)
{
    draw_fill(fb, COL_BLACK);

    /* draw particle trails (oldest first so head is brightest) */
    for (int i = 0; i < N_PARTICLES; ++i) {
        particle_t *p = &s_p[i];
        for (int k = HISTORY_LEN - 1; k >= 0; --k) {
            float v = 1.0f - (k / (float)HISTORY_LEN);
            v = v * v;  /* steeper falloff */
            uint16_t c = hue_to_color(p->hue, v);
            int x = (int)p->hx[k];
            int y = (int)p->hy[k];
            /* 2×2 dot when on the head, single pixel for trail */
            if (k == 0) {
                draw_pixel(fb, x,     y,     c);
                draw_pixel(fb, x + 1, y,     c);
                draw_pixel(fb, x,     y + 1, c);
                draw_pixel(fb, x + 1, y + 1, c);
            } else {
                draw_pixel(fb, x, y, c);
            }
        }
    }

    /* overlay only paints into the top strip */
    if (g_fb_y_offset == 0) draw_overlay(fb);
}
