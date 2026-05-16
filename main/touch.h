#pragma once

#include "app_state.h"
#include "driver/spi_master.h"
#include <stdint.h>
#include <stdbool.h>

/* Initialise the XPT2046 IRQ pin and spawn touch_task. */
void touch_init(void);

/* Add XPT2046 as a second device on SPI2. Call after ili9341_init(). */
#define XPT_CS_GPIO  33
void xpt2046_spi_init(void);

/* Sample raw XPT2046 coordinates. Returns false if not currently touched.
   Averages 8 reads for stability. */
bool xpt2046_sample(uint16_t *x_raw, uint16_t *y_raw);

/* The task posts gesture_t values onto g_ctx.touch_q.
   All gestures are duration-based on release:
     < HOLD_SHORT_MIN_MS          → GESTURE_TAP        (confirm dismiss)
     HOLD_SHORT_MIN..HOLD_CALL_MIN → GESTURE_HOLD_SHORT (initiate dismiss)
     HOLD_CALL_MIN..HOLD_HELP_MIN  → GESTURE_HOLD_CALL  (call me — blue)
     HOLD_HELP_MIN..FACTORY_RESET  → GESTURE_HOLD_HELP  (help me — red)
     >= FACTORY_RESET_HOLD_MS      → GESTURE_LONG_HOLD  (factory reset) */
void touch_task(void *arg);

#define HOLD_SHORT_MIN_MS    300   /* min for dismiss-initiate hold */
#define HOLD_CALL_MIN_MS    1500   /* 1.5 s → "call me" */
#define HOLD_HELP_MIN_MS    3500   /* 3.5 s → "help me" */
