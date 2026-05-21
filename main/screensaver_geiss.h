#pragma once
#include <stdint.h>

void geiss_init(void);
void geiss_step(void);
void geiss_render_strip(uint16_t *fb);
