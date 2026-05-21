#include "screensaver_noofcyd.h"
#include "draw.h"
#include "app_state.h"
#include <math.h>
#include <string.h>

/* ─────────────────────────────────────────────────────────────────────────
   noofcyd — rotating multi-blade diatom shapes with colour cycling.
   Petal geometry is fixed at birth (no morphing); shapes bounce and spin
   while their HSV colour drifts continuously.
   ───────────────────────────────────────────────────────────────────────── */

#define N_SHAPES   7
#define OVERLAY_H  24
#define SCL        ((float)SCREEN_H)

typedef struct {
    float px, py;           /* position (pixels) */
    float vx, vy;           /* velocity (pixels/frame) */
    float ang;              /* rotation angle (degrees) */
    float spn;              /* spin rate (degrees/frame) */
    int   blad;             /* blade count 2..18 */
    /* Fixed petal geometry in pixels (local, un-rotated, origin = centre) */
    float ax;               /* inner point on x-axis */
    float bx, by;           /* upper lobe */
    float dx;               /* tip */
    /* Colour */
    float hue, sat, val;
    float hr, sr, vr;       /* HSV drift rates */
    float cr, cg, cb;
} nshape_t;

static nshape_t s_sh[N_SHAPES];
static uint32_t s_seed = 0xC0FFEEu;

static inline float rnd(void)
{
    s_seed = s_seed * 1664525u + 1013904223u;
    return (s_seed >> 8) * (1.0f / 16777216.0f);
}

static const float bladeratio[] = {
    0.f,0.f,
    3.00000f,1.73205f,1.00000f,0.72654f,0.57735f,0.48157f,
    0.41421f,0.36397f,0.19076f,0.29363f,0.26795f,0.24648f,
    0.22824f,0.21256f,0.19891f,0.18693f,0.17633f,0.16687f,
};

static void hsv_rgb(float h, float s, float v, float *r, float *g, float *b)
{
    while (h <   0.f) h += 360.f;
    while (h >= 360.f) h -= 360.f;
    if (s <= 0.f) { *r = *g = *b = v; return; }
    float hh = h / 60.f;
    int   hi = (int)hh;
    float f  = hh - hi;
    float p  = v*(1-s), q = v*(1-s*f), t = v*(1-s*(1-f));
    switch (hi % 6) {
        case 0: *r=v; *g=t; *b=p; break;
        case 1: *r=q; *g=v; *b=p; break;
        case 2: *r=p; *g=v; *b=t; break;
        case 3: *r=p; *g=q; *b=v; break;
        case 4: *r=t; *g=p; *b=v; break;
        default:*r=v; *g=p; *b=q; break;
    }
}

static void shape_init(int i)
{
    nshape_t *s = &s_sh[i];

    s->px   = rnd() * SCREEN_W;
    s->py   = OVERLAY_H + rnd() * (SCREEN_H - OVERLAY_H);

    /* Wide size range for visual variety */
    float sca = (rnd() * 0.17f + 0.05f) * SCL;   /* 12..55 px */

    s->vx   = (rnd() - 0.5f) * 0.05f * sca * 2.f;
    s->vy   = (rnd() - 0.5f) * 0.05f * sca * 2.f;

    s->blad = 2 + (int)(rnd() * 17.f);
    s->ang  = rnd() * 360.f;
    s->spn  = (rnd() - 0.5f) * 40.f / (10 + s->blad);

    /* Fixed petal shape: random noof-style nx/ny at a stable geep snapshot,
       then scale to pixels.  wobble in [3..9], nx in [0.05..0.28]. */
    float wobble = 3.f + rnd() * 6.f;
    float nx     = 0.05f + rnd() * 0.23f;
    float ny     = nx * (rnd() * 0.6f + 0.1f);    /* y is 10-70% of x */
    float sn     = sca / SCL;
    float ws     = wobble * sca;
    /* Clamp ny per blade count */
    if (ny > nx * bladeratio[s->blad]) ny = nx * bladeratio[s->blad];

    s->ax = nx * sn * ws;
    s->bx = nx * ws;  s->by = ny * ws;
    s->dx = 0.3f * ws;

    s->hue  = rnd() * 360.f;
    s->sat  = rnd() * 0.5f + 0.5f;     /* 0.5..1.0 — always vivid */
    s->val  = rnd() * 0.5f + 0.5f;     /* 0.5..1.0 — always bright */
    s->hr   = (rnd() * 1.5f + 0.5f) * (rnd() > 0.5f ? 1.f : -1.f); /* ±0.5..2 deg/frame */
    s->sr   = rnd() * 0.02f;
    s->vr   = rnd() * 0.02f;

    hsv_rgb(s->hue, s->sat, s->val, &s->cr, &s->cg, &s->cb);
}

static void color_update(nshape_t *s)
{
    if (s->sat <= 0.5f && s->sr < 0.f) s->sr = -s->sr;
    if (s->sat >= 1.0f && s->sr > 0.f) s->sr = -s->sr;
    if (s->val <= 0.5f && s->vr < 0.f) s->vr = -s->vr;
    if (s->val >= 1.0f && s->vr > 0.f) s->vr = -s->vr;
    s->hue += s->hr;
    s->sat += s->sr;
    s->val += s->vr;
    if (s->sat < 0.f) s->sat = 0.f;
    if (s->sat > 1.f) s->sat = 1.f;
    if (s->val < 0.f) s->val = 0.f;
    if (s->val > 1.f) s->val = 1.f;
    hsv_rgb(s->hue, s->sat, s->val, &s->cr, &s->cg, &s->cb);
}

static void gravity(void)
{
    for (int a = 0; a < N_SHAPES; a++) {
        for (int b = 0; b < a; b++) {
            float dx = s_sh[b].px - s_sh[a].px;
            float dy = s_sh[b].py - s_sh[a].py;
            float d2 = dx*dx + dy*dy;
            if (d2 < 1.f) d2 = 1.f;
            if (d2 < 3600.f) {
                float z = 0.0015f / d2;
                s_sh[a].vx += dx * z * s_sh[b].dx;
                s_sh[b].vx -= dx * z * s_sh[a].dx;
                s_sh[a].vy += dy * z * s_sh[b].dx;
                s_sh[b].vy -= dy * z * s_sh[a].dx;
            }
        }
    }
}

static void draw_leaf(uint16_t *fb, nshape_t *s)
{
    float dpb = 360.f / (float)s->blad;
    uint16_t c = rgb565be(s->cr, s->cg, s->cb);

    for (int b = 0; b < s->blad; b++) {
        float theta = (s->ang + b * dpb) * (float)(M_PI / 180.0);
        float cosT  = cosf(theta);
        float sinT  = sinf(theta);

        int ax  = (int)(s->px + s->ax * cosT);
        int ay  = (int)(s->py + s->ax * sinT);
        int bx  = (int)(s->px + s->bx*cosT - s->by*sinT);
        int by_ = (int)(s->py + s->bx*sinT + s->by*cosT);
        int cx  = (int)(s->px + s->bx*cosT + s->by*sinT);
        int cy_ = (int)(s->py + s->bx*sinT - s->by*cosT);
        int dx_ = (int)(s->px + s->dx * cosT);
        int dy_ = (int)(s->py + s->dx * sinT);

        draw_line(fb, ax, ay,  bx, by_, c);
        draw_line(fb, bx, by_, dx_, dy_, c);
        draw_line(fb, dx_, dy_, cx, cy_, c);
        draw_line(fb, cx, cy_, ax, ay,  c);
    }
}

static void draw_overlay(uint16_t *fb)
{
    draw_str(fb, 4, 5, g_ctx.device_name, COL_CYAN, 2);

    const char *t = g_ctx.time_str[0] ? g_ctx.time_str : "--:--";
    char t5[6] = { t[0], t[1], t[2], t[3], t[4], '\0' };
    int name_len = 0;
    for (const char *p = g_ctx.device_name; *p; ++p) name_len++;
    int name_end     = 4 + name_len * 6 * 2;
    int status_start = SCREEN_W - 52;
    int time_w       = 5 * 6 * 2;
    int time_x       = name_end + (status_start - name_end - time_w) / 2;
    draw_str(fb, time_x, 5, t5, COL_WHITE, 2);

    uint16_t wifi_c = g_ctx.wifi_connected ? COL_GREEN : COL_RED;
    uint16_t wg_c   = g_ctx.wg_connected   ? COL_GREEN :
                      (g_ctx.wg_endpoint[0] ? COL_YELLOW : COL_GRAY);
    draw_str     (fb, SCREEN_W - 52, 5, "W",   wifi_c, 2);
    draw_fill_rect(fb, SCREEN_W - 38, 8, 8, 8, wifi_c);
    draw_str     (fb, SCREEN_W - 26, 5, "V",   wg_c,   2);
    draw_fill_rect(fb, SCREEN_W - 12, 8, 8, 8, wg_c);
}

void noofcyd_init(void)
{
    for (int i = 0; i < N_SHAPES; i++) shape_init(i);
}

void noofcyd_step(void)
{
    gravity();
    for (int i = 0; i < N_SHAPES; i++) {
        nshape_t *s = &s_sh[i];

        float margin = s->dx;
        if (s->px < -margin           && s->vx < 0.f) s->vx = -s->vx;
        if (s->px > SCREEN_W + margin && s->vx > 0.f) s->vx = -s->vx;
        if (s->py < OVERLAY_H - margin && s->vy < 0.f) s->vy = -s->vy;
        if (s->py > SCREEN_H + margin  && s->vy > 0.f) s->vy = -s->vy;

        s->px  += s->vx;
        s->py  += s->vy;
        s->ang += s->spn;
        if (s->ang <   0.f) s->ang += 360.f;
        if (s->ang > 360.f) s->ang -= 360.f;

        color_update(s);
    }
}

void noofcyd_render_strip(uint16_t *fb)
{
    draw_fill(fb, COL_BLACK);
    for (int i = 0; i < N_SHAPES; i++) draw_leaf(fb, &s_sh[i]);
    if (g_fb_y_offset == 0) draw_overlay(fb);
}
