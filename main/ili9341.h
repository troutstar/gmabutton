#pragma once

#include "driver/spi_master.h"
#include <stdint.h>

#define ILI_W       320
#define ILI_H       240
#define ILI_STRIP_H 120   /* half-height strip — two blits per frame */

void ili9341_init(spi_device_handle_t *out_spi);
/* blit a strip of ILI_STRIP_H rows starting at display row y_start */
void ili9341_blit_strip_async(spi_device_handle_t spi, const uint16_t *fb, int y_start);
void ili9341_blit_wait(spi_device_handle_t spi);
