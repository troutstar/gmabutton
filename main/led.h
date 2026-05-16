#pragma once

#include <stdbool.h>

/* ESP32-2432S028 RGB LED on the back is common-anode (active-LOW).
   R=GPIO4, G=GPIO16, B=GPIO17. */
void led_init(void);
void led_set(bool r, bool g, bool b);     /* true = ON */
void led_red(void);
void led_blue(void);
void led_white(void);
void led_off(void);
