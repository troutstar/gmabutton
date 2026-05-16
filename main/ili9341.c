#include "ili9341.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

#define PIN_CS   15
#define PIN_DC    2
#define PIN_MOSI 13
#define PIN_MISO 12
#define PIN_CLK  14
#define PIN_BL   21

static spi_device_handle_t s_spi;

static void IRAM_ATTR dc_pre_transfer(spi_transaction_t *t)
{
    int dc = (int)t->user;
    gpio_set_level(PIN_DC, dc);
}

static void spi_send_cmd(uint8_t cmd)
{
    spi_transaction_t t = {
        .length = 8,
        .tx_buffer = &cmd,
        .user = (void *)0,
    };
    spi_device_polling_transmit(s_spi, &t);
}

static void spi_send_data(const uint8_t *data, size_t len)
{
    if (!len) return;
    spi_transaction_t t = {
        .length = len * 8,
        .tx_buffer = data,
        .user = (void *)1,
    };
    spi_device_polling_transmit(s_spi, &t);
}

static void spi_send_byte(uint8_t byte)
{
    spi_send_data(&byte, 1);
}

void ili9341_init(spi_device_handle_t *out_spi)
{
    gpio_set_direction(PIN_BL, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_BL, 0);
    gpio_set_direction(PIN_DC, GPIO_MODE_OUTPUT);

    spi_bus_config_t buscfg = {
        .mosi_io_num   = PIN_MOSI,
        .miso_io_num   = PIN_MISO,
        .sclk_io_num   = PIN_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = ILI_W * ILI_H * 2,
    };
    spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 40 * 1000 * 1000,
        .mode           = 0,
        .spics_io_num   = PIN_CS,
        .queue_size     = 7,
        .pre_cb         = dc_pre_transfer,
    };
    spi_bus_add_device(SPI2_HOST, &devcfg, &s_spi);
    if (out_spi) *out_spi = s_spi;

    spi_send_cmd(0x01); vTaskDelay(pdMS_TO_TICKS(150));
    spi_send_cmd(0x11); vTaskDelay(pdMS_TO_TICKS(120));

    spi_send_cmd(0xCB);
    spi_send_byte(0x39); spi_send_byte(0x2C); spi_send_byte(0x00);
    spi_send_byte(0x34); spi_send_byte(0x02);

    spi_send_cmd(0xCF);
    spi_send_byte(0x00); spi_send_byte(0xC1); spi_send_byte(0x30);

    spi_send_cmd(0xE8);
    spi_send_byte(0x85); spi_send_byte(0x00); spi_send_byte(0x78);

    spi_send_cmd(0xEA);
    spi_send_byte(0x00); spi_send_byte(0x00);

    spi_send_cmd(0xED);
    spi_send_byte(0x64); spi_send_byte(0x03);
    spi_send_byte(0x12); spi_send_byte(0x81);

    spi_send_cmd(0xF7); spi_send_byte(0x20);
    spi_send_cmd(0xC0); spi_send_byte(0x23);
    spi_send_cmd(0xC1); spi_send_byte(0x10);
    spi_send_cmd(0xC5); spi_send_byte(0x3E); spi_send_byte(0x28);
    spi_send_cmd(0xC7); spi_send_byte(0x86);
    spi_send_cmd(0x36); spi_send_byte(0x28);   /* MADCTL: landscape, BGR */
    spi_send_cmd(0x3A); spi_send_byte(0x55);   /* RGB565 */
    spi_send_cmd(0xB1); spi_send_byte(0x00); spi_send_byte(0x18);

    spi_send_cmd(0xB6);
    spi_send_byte(0x08); spi_send_byte(0x82); spi_send_byte(0x27);

    spi_send_cmd(0xF2); spi_send_byte(0x00);
    spi_send_cmd(0x26); spi_send_byte(0x01);

    spi_send_cmd(0xE0);
    {
        const uint8_t d[] = {0x0F,0x31,0x2B,0x0C,0x0E,0x08,0x4E,0xF1,
                             0x37,0x07,0x10,0x03,0x0E,0x09,0x00};
        spi_send_data(d, sizeof(d));
    }

    spi_send_cmd(0xE1);
    {
        const uint8_t d[] = {0x00,0x0E,0x14,0x03,0x11,0x07,0x31,0xC1,
                             0x48,0x08,0x0F,0x0C,0x31,0x36,0x0F};
        spi_send_data(d, sizeof(d));
    }

    spi_send_cmd(0x29);
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(PIN_BL, 1);
}

static void set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    uint8_t col[] = { x0 >> 8, x0 & 0xFF, x1 >> 8, x1 & 0xFF };
    uint8_t row[] = { y0 >> 8, y0 & 0xFF, y1 >> 8, y1 & 0xFF };
    spi_send_cmd(0x2A); spi_send_data(col, 4);
    spi_send_cmd(0x2B); spi_send_data(row, 4);
    spi_send_cmd(0x2C);
}

static spi_transaction_t s_blit_tx;

void ili9341_blit_strip_async(spi_device_handle_t spi, const uint16_t *fb, int y_start)
{
    set_window(0, y_start, ILI_W - 1, y_start + ILI_STRIP_H - 1);
    memset(&s_blit_tx, 0, sizeof(s_blit_tx));
    s_blit_tx.length    = ILI_W * ILI_STRIP_H * 16;
    s_blit_tx.tx_buffer = fb;
    s_blit_tx.user      = (void *)1;
    spi_device_queue_trans(spi, &s_blit_tx, portMAX_DELAY);
}

void ili9341_blit_wait(spi_device_handle_t spi)
{
    spi_transaction_t *ret;
    spi_device_get_trans_result(spi, &ret, portMAX_DELAY);
}
