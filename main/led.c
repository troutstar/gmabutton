#include "led.h"
#include "driver/gpio.h"

#define LED_R_GPIO 17
#define LED_G_GPIO 16
#define LED_B_GPIO  4

void led_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << LED_R_GPIO) |
                        (1ULL << LED_G_GPIO) |
                        (1ULL << LED_B_GPIO),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    led_off();
}

void led_set(bool r, bool g, bool b)
{
    /* active-low: ON = drive 0 */
    gpio_set_level(LED_R_GPIO, r ? 0 : 1);
    gpio_set_level(LED_G_GPIO, g ? 0 : 1);
    gpio_set_level(LED_B_GPIO, b ? 0 : 1);
}

void led_red(void)   { led_set(true,  false, false); }
void led_blue(void)  { led_set(false, false, true ); }
void led_white(void) { led_set(true,  true,  true ); }
void led_off(void)   { led_set(false, false, false); }
