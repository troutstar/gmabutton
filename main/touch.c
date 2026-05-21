#include "touch.h"
#include "draw.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TOUCH_IRQ_GPIO  36
#define POLL_MS         10

static const char *TAG = "touch";
static spi_device_handle_t s_xpt = NULL;

static inline bool is_touched(void)
{
    return gpio_get_level(TOUCH_IRQ_GPIO) == 0;
}

void xpt2046_spi_init(void)
{
    spi_device_interface_config_t cfg = {
        .clock_speed_hz = 2 * 1000 * 1000,
        .mode           = 0,
        .spics_io_num   = XPT_CS_GPIO,
        .queue_size     = 1,
        .pre_cb         = NULL,
    };
    esp_err_t err = spi_bus_add_device(SPI2_HOST, &cfg, &s_xpt);
    if (err != ESP_OK)
        ESP_LOGE(TAG, "xpt2046 spi_bus_add_device failed: %s", esp_err_to_name(err));
}

bool xpt2046_sample(uint16_t *x_raw, uint16_t *y_raw)
{
    if (!is_touched()) return false;
    if (!s_xpt) return false;

    uint32_t sx = 0, sy = 0;
    for (int i = 0; i < 8; i++) {
        uint8_t tx[3], rx[3];

        /* X axis: command 0xD0 */
        tx[0] = 0xD0; tx[1] = 0; tx[2] = 0;
        spi_transaction_t t = {
            .length    = 24,
            .tx_buffer = tx,
            .rx_buffer = rx,
        };
        spi_device_polling_transmit(s_xpt, &t);
        sx += (((uint16_t)rx[1] << 8 | rx[2]) >> 3) & 0xFFF;

        /* Y axis: command 0x90 */
        tx[0] = 0x90;
        spi_device_polling_transmit(s_xpt, &t);
        sy += (((uint16_t)rx[1] << 8 | rx[2]) >> 3) & 0xFFF;
    }
    *x_raw = (uint16_t)(sx / 8);
    *y_raw = (uint16_t)(sy / 8);
    return true;
}

void touch_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << TOUCH_IRQ_GPIO,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,    /* XPT2046 supplies its own pull-up */
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
}

static inline uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void post_gesture(gesture_t g)
{
    if (g_ctx.touch_q) xQueueSend(g_ctx.touch_q, &g, 0);
}

/*
 * Gesture state machine — duration-based, two states only:
 *   IDLE → DOWN on touch-down, record t_down
 *   DOWN → while held: fire LONG_HOLD at FACTORY_RESET_HOLD_MS
 *         → on release: classify by duration and post gesture
 *
 * Duration buckets (on release):
 *   < HOLD_SHORT_MIN_MS  → GESTURE_TAP        (confirm dismiss)
 *   < HOLD_CALL_MIN_MS   → GESTURE_HOLD_SHORT  (initiate dismiss ~0.5 s)
 *   < HOLD_HELP_MIN_MS   → GESTURE_HOLD_CALL   (call me — 2 s hold)
 *   < FACTORY_RESET      → GESTURE_HOLD_HELP   (help me — 3.5–10 s hold)
 *   (LONG_HOLD fires while still held at 10 s, release is then ignored)
 */
typedef enum { IDLE, DOWN } st_t;

void touch_task(void *arg)
{
    (void)arg;
    st_t s = IDLE;
    uint32_t t_down = 0;
    bool long_hold_fired = false;
    bool last_touched = false;

    ESP_LOGI(TAG, "touch task running");
    for (;;) {
        bool touched = is_touched();
        uint32_t t = now_ms();

        switch (s) {
        case IDLE:
            if (touched && !last_touched) {
                t_down = t;
                long_hold_fired = false;
                g_ctx.holding = true;
                g_ctx.hold_start_ms = t;
                s = DOWN;

                /* Sample and store calibrated touch position */
                uint16_t rx, ry;
                if (xpt2046_sample(&rx, &ry)) {
                    if (g_ctx.cal.valid) {
                        int sx = (int)(g_ctx.cal.ax * rx + g_ctx.cal.bx);
                        int sy = (int)(g_ctx.cal.ay * ry + g_ctx.cal.by);
                        if (sx < 0) sx = 0;
                        if (sx >= SCREEN_W) sx = SCREEN_W - 1;
                        if (sy < 0) sy = 0;
                        if (sy >= SCREEN_H) sy = SCREEN_H - 1;
                        g_ctx.last_touch_x = (uint16_t)sx;
                        g_ctx.last_touch_y = (uint16_t)sy;
                    } else {
                        g_ctx.last_touch_x = rx;
                        g_ctx.last_touch_y = ry;
                    }
                }
            }
            break;

        case DOWN:
            if (touched) {
                if (!long_hold_fired && (t - t_down) >= FACTORY_RESET_HOLD_MS) {
                    post_gesture(GESTURE_LONG_HOLD);
                    long_hold_fired = true;
                }
            } else {
                /* finger lifted — classify by hold duration */
                g_ctx.holding = false;
                if (!long_hold_fired) {
                    uint32_t dur = t - t_down;
                    if      (dur < HOLD_SHORT_MIN_MS) post_gesture(GESTURE_TAP);
                    else if (dur < HOLD_CALL_MIN_MS)  post_gesture(GESTURE_HOLD_SHORT);
                    else if (dur < HOLD_HELP_MIN_MS)  post_gesture(GESTURE_HOLD_CALL);
                    else                              post_gesture(GESTURE_HOLD_HELP);
                }
                s = IDLE;
            }
            break;
        }

        last_touched = touched;
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
    }
}
