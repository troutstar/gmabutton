#pragma once

#include "app_state.h"
#include "driver/spi_master.h"
#include <stdint.h>

/* Runs full-screen calibration UI — blocks until 3 targets are tapped.
   Renders directly (no render task running yet). Stores result in *out. */
void calibration_run(spi_device_handle_t disp_spi, uint16_t *fb, cal_data_t *out);
