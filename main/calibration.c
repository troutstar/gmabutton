#include "calibration.h"
#include "draw.h"
#include "ili9341.h"
#include "touch.h"
#include "app_state.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "cal";

/* Three calibration targets in screen coordinates */
static const int CAL_PTS = 4;
static const int TARGET_X[4] = {  40, 280, 280,  40 };
static const int TARGET_Y[4] = {  40,  40, 200, 200 };

static void draw_target(uint16_t *fb, int cx, int cy)
{
    /* Crosshair arms — long enough for adult finger reference */
    draw_line(fb, cx - 28, cy, cx + 28, cy, COL_WHITE);
    draw_line(fb, cx, cy - 28, cx, cy + 28, COL_WHITE);
    /* Large filled centre dot */
    draw_filled_circle(fb, cx, cy, 10, COL_YELLOW);
}

static void render_and_blit(spi_device_handle_t disp_spi, uint16_t *fb,
                             int cx, int cy, int step)
{
    char label[24];
    snprintf(label, sizeof(label), "TAP %d OF 4", step + 1);

    /* Top strip */
    g_fb_y_offset = 0;
    draw_fill(fb, COL_BLACK);
    if (cy < STRIP_H)        draw_target(fb, cx, cy);
    draw_str_centered(fb, 55, label,         COL_WHITE,  2);
    draw_str_centered(fb, 85, "TAP THE",     COL_DKGRAY, 1);
    draw_str_centered(fb, 95, "CROSSHAIR",   COL_DKGRAY, 1);
    ili9341_blit_strip_async(disp_spi, fb, 0);
    ili9341_blit_wait(disp_spi);

    /* Bottom strip */
    g_fb_y_offset = STRIP_H;
    draw_fill(fb, COL_BLACK);
    if (cy >= STRIP_H)       draw_target(fb, cx, cy);
    ili9341_blit_strip_async(disp_spi, fb, STRIP_H);
    ili9341_blit_wait(disp_spi);
}

static void wait_for_tap(uint16_t *x_out, uint16_t *y_out)
{
    /* Wait for finger down */
    while (gpio_get_level(36) != 0)
        vTaskDelay(pdMS_TO_TICKS(10));

    /* Collect 16 averaged samples while touched */
    uint32_t sx = 0, sy = 0;
    int n = 0;
    for (int i = 0; i < 16; i++) {
        uint16_t rx, ry;
        if (xpt2046_sample(&rx, &ry)) {
            sx += rx; sy += ry; n++;
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    *x_out = n ? (uint16_t)(sx / n) : 2048;
    *y_out = n ? (uint16_t)(sy / n) : 2048;

    /* Wait for release */
    while (gpio_get_level(36) == 0)
        vTaskDelay(pdMS_TO_TICKS(10));

    /* Debounce */
    vTaskDelay(pdMS_TO_TICKS(120));
}

void calibration_run(spi_device_handle_t disp_spi, uint16_t *fb, cal_data_t *out)
{
    uint16_t raw_x[3], raw_y[3];

    for (int i = 0; i < CAL_PTS; i++) {
        render_and_blit(disp_spi, fb, TARGET_X[i], TARGET_Y[i], i);
        wait_for_tap(&raw_x[i], &raw_y[i]);
        ESP_LOGI(TAG, "pt%d: screen=(%d,%d) raw=(%u,%u)",
                 i, TARGET_X[i], TARGET_Y[i], raw_x[i], raw_y[i]);
    }

    /* X calibration: points 0 (top-left) and 1 (top-right) span the full X range.
       Y calibration: points 0 (top-left) and 3 (bottom-left) span the full Y range. */
    float dx_raw = (float)raw_x[1] - (float)raw_x[0];
    float dy_raw = (float)raw_y[3] - (float)raw_y[0];

    if (dx_raw == 0.0f || dy_raw == 0.0f) {
        ESP_LOGW(TAG, "degenerate calibration, using identity");
        out->ax = 1.0f; out->bx = 0.0f;
        out->ay = 1.0f; out->by = 0.0f;
        out->valid = false;
        return;
    }

    out->ax = (TARGET_X[1] - TARGET_X[0]) / dx_raw;
    out->bx = TARGET_X[0] - out->ax * raw_x[0];
    out->ay = (TARGET_Y[3] - TARGET_Y[0]) / dy_raw;
    out->by = TARGET_Y[0] - out->ay * raw_y[0];
    out->valid = true;

    ESP_LOGI(TAG, "cal: ax=%.4f bx=%.1f ay=%.4f by=%.1f",
             out->ax, out->bx, out->ay, out->by);

    /* Brief confirmation flash */
    g_fb_y_offset = 0;
    draw_fill(fb, COL_BLACK);
    draw_str_centered(fb, 50, "CALIBRATED", COL_GREEN, 2);
    ili9341_blit_strip_async(disp_spi, fb, 0);
    ili9341_blit_wait(disp_spi);
    g_fb_y_offset = STRIP_H;
    draw_fill(fb, COL_BLACK);
    ili9341_blit_strip_async(disp_spi, fb, STRIP_H);
    ili9341_blit_wait(disp_spi);
    vTaskDelay(pdMS_TO_TICKS(600));
}
