#pragma once

#include <stdint.h>

#define SCREEN_W    320
#define SCREEN_H    240
#define STRIP_H     120   /* height of one render strip / framebuffer */

/* Set before each strip render: 0 for top half, 120 for bottom half.
   All draw functions subtract this from Y and clip to [0, STRIP_H). */
extern int g_fb_y_offset;

/* Big-endian RGB565 (DMA byte order on this panel) */
static inline uint16_t rgb565be(float r, float g, float b)
{
    uint16_t ri = (uint16_t)(r * 31.0f) & 0x1F;
    uint16_t gi = (uint16_t)(g * 63.0f) & 0x3F;
    uint16_t bi = (uint16_t)(b * 31.0f) & 0x1F;
    uint16_t rgb = (ri << 11) | (gi << 5) | bi;
    return (rgb >> 8) | (rgb << 8);
}

#define COL_BLACK     rgb565be(0,    0,    0   )
#define COL_WHITE     rgb565be(1,    1,    1   )
#define COL_RED       rgb565be(1,    0,    0   )
#define COL_GREEN     rgb565be(0,    1,    0   )
#define COL_BLUE      rgb565be(0,    0,    1   )
#define COL_CYAN      rgb565be(0,    1,    1   )
#define COL_YELLOW    rgb565be(1,    1,    0   )
#define COL_MAGENTA   rgb565be(1,    0,    1   )
#define COL_GRAY      rgb565be(0.4f, 0.4f, 0.4f)
#define COL_DKGRAY    rgb565be(0.15f,0.15f,0.15f)

void draw_fill(uint16_t *fb, uint16_t color);
void draw_fade(uint16_t *fb);
void draw_pixel(uint16_t *fb, int x, int y, uint16_t color);
void draw_line(uint16_t *fb, int x0, int y0, int x1, int y1, uint16_t color);
void draw_rect(uint16_t *fb, int x, int y, int w, int h, uint16_t color);
void draw_fill_rect(uint16_t *fb, int x, int y, int w, int h, uint16_t color);
void draw_char(uint16_t *fb, int x, int y, char c, uint16_t color, int scale);
void draw_str(uint16_t *fb, int x, int y, const char *s, uint16_t color, int scale);
void draw_str_centered(uint16_t *fb, int y, const char *s, uint16_t color, int scale);
void draw_hbar(uint16_t *fb, int x, int y, int w, int h, float fill, uint16_t color);
/* Filled circle clipped to current strip. cx/cy are screen coordinates. */
void draw_filled_circle(uint16_t *fb, int cx, int cy, int r, uint16_t color);
