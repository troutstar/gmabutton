#include "app_state.h"
#include "ili9341.h"
#include "draw.h"
#include "led.h"
#include "touch.h"
#include "screensaver.h"
#include "alert.h"
#include "nvs_config.h"
#include "wifi_manager.h"
#include "wireguard_mgr.h"
#include "http_server.h"
#include "peer_comms.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>

static const char *TAG = "main";

app_ctx_t g_ctx;

/* ── State helpers ─────────────────────────────────────────────────────── */
void app_set_state(app_state_t s)
{
    xSemaphoreTake(g_ctx.state_mutex, portMAX_DELAY);
    g_ctx.state = s;
    if (s == STATE_DISMISS_PENDING)
        g_ctx.dismiss_pending_since_ms = (uint32_t)(esp_timer_get_time() / 1000);
    xSemaphoreGive(g_ctx.state_mutex);
}

app_state_t app_get_state(void)
{
    return g_ctx.state;
}

/* ── Local gesture → action mapping ─────────────────────────────────────── */
static void handle_gesture(gesture_t g)
{
    /* Factory-reset hold works from any mode. */
    if (g == GESTURE_LONG_HOLD) {
        ESP_LOGW(TAG, "factory reset triggered");
        led_set(true, true, true);
        vTaskDelay(pdMS_TO_TICKS(300));
        nvs_config_factory_reset();
        return;  /* unreachable */
    }
    if (g_ctx.config_mode) return;  /* setup wizard owns the UI */

    app_state_t s = app_get_state();
    switch (s) {
    case STATE_SCREENSAVER:
        if (g == GESTURE_HOLD_CALL) {
            ESP_LOGI(TAG, "→ CALL");
            http_apply_alert_call(true);
            peer_send(COMMS_ALERT_CALL);
        } else if (g == GESTURE_HOLD_HELP) {
            ESP_LOGI(TAG, "→ HELP");
            http_apply_alert_help(true);
            peer_send(COMMS_ALERT_HELP);
        }
        break;

    case STATE_ALERT_CALL:
    case STATE_ALERT_HELP:
        if (!g_ctx.is_local_alert) {
            /* receiver: any hold acknowledges — go solid + LED on */
            g_ctx.is_local_alert = true;
            if (s == STATE_ALERT_CALL) led_blue(); else led_red();
            /* SHORT_HOLD simultaneously arms dismiss overlay */
            if (g == GESTURE_HOLD_SHORT) {
                g_ctx.pending_underlay = s;
                app_set_state(STATE_DISMISS_PENDING);
            }
        } else if (g == GESTURE_HOLD_SHORT) {
            /* initiator: ~0.5 s hold arms dismiss overlay */
            g_ctx.pending_underlay = s;
            app_set_state(STATE_DISMISS_PENDING);
        }
        break;

    case STATE_DISMISS_PENDING:
        /* quick TAP or SHORT_HOLD confirms dismiss */
        if (g == GESTURE_TAP || g == GESTURE_HOLD_SHORT) {
            ESP_LOGI(TAG, "→ DISMISS");
            http_apply_dismiss();
            peer_send(COMMS_DISMISS);
        }
        break;

    default:
        break;
    }
}

/* ── Render task ───────────────────────────────────────────────────────── */
static uint16_t       *s_fb;
static spi_device_handle_t s_spi;

static void render_task(void *arg)
{
    (void)arg;
    screensaver_init();
    uint32_t last_step = 0;

    for (;;) {
        app_state_t state;
        /* dismiss-pending auto-times-out back to the alert it interrupted */
        if (g_ctx.state == STATE_DISMISS_PENDING) {
            uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
            if (now - g_ctx.dismiss_pending_since_ms > DISMISS_TIMEOUT_MS) {
                app_set_state(g_ctx.pending_underlay == STATE_ALERT_HELP
                              ? STATE_ALERT_HELP : STATE_ALERT_CALL);
            }
        }
        state = g_ctx.state;

        if (state == STATE_SCREENSAVER) {
            uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
            if (now - last_step >= 50) {
                screensaver_step();
                last_step = now;
            }

            /* Hold-progress animation: expanding circle over screensaver */
            if (g_ctx.holding) {
                uint32_t elapsed = now - g_ctx.hold_start_ms;
                /* max radius covers full screen from centre (160,120): sqrt(160²+120²)=200 */
                const int MAX_R = 200;
                const int CX = SCREEN_W / 2, CY = SCREEN_H / 2;

                for (int strip = 0; strip < 2; ++strip) {
                    g_fb_y_offset = strip ? STRIP_H : 0;
                    screensaver_render_strip(s_fb);

                    if (elapsed < HOLD_CALL_MIN_MS) {
                        /* Phase 1: blue circle grows over screensaver */
                        int r = (int)((float)elapsed / HOLD_CALL_MIN_MS * MAX_R);
                        draw_filled_circle(s_fb, CX, CY, r, COL_BLUE);
                    } else if (elapsed < HOLD_HELP_MIN_MS) {
                        /* Phase 2: blue fill + expanding red circle */
                        draw_fill(s_fb, COL_BLUE);
                        uint32_t phase_elapsed = elapsed - HOLD_CALL_MIN_MS;
                        uint32_t phase_dur = HOLD_HELP_MIN_MS - HOLD_CALL_MIN_MS;
                        int r = (int)((float)phase_elapsed / phase_dur * MAX_R);
                        draw_filled_circle(s_fb, CX, CY, r, COL_RED);
                    } else {
                        /* Past HELP threshold: solid red */
                        draw_fill(s_fb, COL_RED);
                    }

                    uint32_t offset = strip ? STRIP_H : 0;
                    ili9341_blit_strip_async(s_spi, s_fb, offset);
                    ili9341_blit_wait(s_spi);
                }
            } else {
                g_fb_y_offset = 0;
                screensaver_render_strip(s_fb);
                ili9341_blit_strip_async(s_spi, s_fb, 0);
                ili9341_blit_wait(s_spi);

                g_fb_y_offset = STRIP_H;
                screensaver_render_strip(s_fb);
                ili9341_blit_strip_async(s_spi, s_fb, STRIP_H);
                ili9341_blit_wait(s_spi);
            }
        } else {
            /* Receiver devices flash: alternate screen-on/LED-on every 500 ms */
            if (!g_ctx.is_local_alert &&
                (state == STATE_ALERT_CALL || state == STATE_ALERT_HELP)) {
                uint32_t phase = ((uint32_t)(esp_timer_get_time() / 1000) / 500) & 1;
                if (phase == 0) {
                    /* screen flash — LED off */
                    led_off();
                    g_fb_y_offset = 0;
                    alert_render_strip(s_fb, state);
                    ili9341_blit_strip_async(s_spi, s_fb, 0);
                    ili9341_blit_wait(s_spi);
                    g_fb_y_offset = STRIP_H;
                    alert_render_strip(s_fb, state);
                    ili9341_blit_strip_async(s_spi, s_fb, STRIP_H);
                    ili9341_blit_wait(s_spi);
                } else {
                    /* LED flash — screen black. GPIO 17 (led_red) is the working
                       hardware blue LED on this board; used for both alert colours. */
                    led_red();
                    draw_fill(s_fb, COL_BLACK);
                    g_fb_y_offset = 0;
                    ili9341_blit_strip_async(s_spi, s_fb, 0);
                    ili9341_blit_wait(s_spi);
                    g_fb_y_offset = STRIP_H;
                    ili9341_blit_strip_async(s_spi, s_fb, STRIP_H);
                    ili9341_blit_wait(s_spi);
                }
            } else {
                g_fb_y_offset = 0;
                alert_render_strip(s_fb, state);
                ili9341_blit_strip_async(s_spi, s_fb, 0);
                ili9341_blit_wait(s_spi);

                g_fb_y_offset = STRIP_H;
                alert_render_strip(s_fb, state);
                ili9341_blit_strip_async(s_spi, s_fb, STRIP_H);
                ili9341_blit_wait(s_spi);
            }
            vTaskDelay(pdMS_TO_TICKS(80));
        }
    }
}

/* ── Touch consumer task ───────────────────────────────────────────────── */
static void gesture_consumer_task(void *arg)
{
    (void)arg;
    gesture_t g;
    for (;;) {
        if (xQueueReceive(g_ctx.touch_q, &g, portMAX_DELAY) == pdTRUE)
            handle_gesture(g);
    }
}

/* ── Boot ──────────────────────────────────────────────────────────────── */
void app_main(void)
{
    memset(&g_ctx, 0, sizeof(g_ctx));
    g_ctx.peer_port = DEFAULT_PEER_PORT;
    g_ctx.state_mutex = xSemaphoreCreateMutex();
    g_ctx.touch_q     = xQueueCreate(8,  sizeof(gesture_t));
    g_ctx.comms_q     = xQueueCreate(8,  sizeof(comms_kind_t));

    /* Half-height DMA framebuffer (full-screen + WiFi exceeds DMA DRAM). */
    s_fb = heap_caps_malloc(SCREEN_W * STRIP_H * sizeof(uint16_t),
                            MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!s_fb) {
        ESP_LOGE(TAG, "framebuffer alloc failed");
        return;
    }
    memset(s_fb, 0, SCREEN_W * STRIP_H * sizeof(uint16_t));

    led_init();
    ili9341_init(&s_spi);
    touch_init();

    nvs_config_init();
    nvs_config_load();

    /* config mode pulses white via the screensaver hint instead — keep the
       LED off; the user knows they're in setup from the screen. */
    if (g_ctx.config_mode) {
        app_set_state(STATE_CONFIG_MODE);
        led_off();
    } else {
        app_set_state(STATE_SCREENSAVER);
        led_off();
    }

    /* Tasks on Core 1: graphics + touch */
    xTaskCreatePinnedToCore(render_task,           "render",   8192, NULL, 5, NULL, 1);
    xTaskCreatePinnedToCore(touch_task,            "touch",    2048, NULL, 6, NULL, 1);
    xTaskCreatePinnedToCore(gesture_consumer_task, "gesture",  4096, NULL, 4, NULL, 0);

    /* Networking on Core 0 */
    wifi_manager_init();
    if (!g_ctx.config_mode) {
        wireguard_mgr_start();          /* no-op if not configured */
        peer_comms_start();
    }
    http_server_start();

    ESP_LOGI(TAG, "boot complete (%s mode, ip=%s)",
             g_ctx.config_mode ? "CONFIG" : "NORMAL",
             g_ctx.ip_str);
}
