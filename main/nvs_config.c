#include "nvs_config.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_system.h"
#include <string.h>

static const char *TAG = "nvs_cfg";
static const char *NS  = "gmabutton";

esp_err_t nvs_config_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS needs erase, doing it");
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    return err;
}

static void load_str(nvs_handle_t h, const char *key, char *dst, size_t cap)
{
    size_t sz = cap;
    if (nvs_get_str(h, key, dst, &sz) != ESP_OK) dst[0] = '\0';
}

static void load_u16(nvs_handle_t h, const char *key, uint16_t *dst, uint16_t dflt)
{
    if (nvs_get_u16(h, key, dst) != ESP_OK) *dst = dflt;
}

esp_err_t nvs_config_load(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "no config yet — entering setup mode");
        g_ctx.wifi_ssid[0]   = '\0';
        g_ctx.peer_port      = DEFAULT_PEER_PORT;
        g_ctx.config_mode    = true;
        strncpy(g_ctx.device_name, "gmabutton", sizeof(g_ctx.device_name) - 1);
        return ESP_OK;
    }
    if (err != ESP_OK) return err;

    load_str(h, "wifi_ssid",     g_ctx.wifi_ssid,     sizeof(g_ctx.wifi_ssid));
    load_str(h, "wifi_pass",     g_ctx.wifi_pass,     sizeof(g_ctx.wifi_pass));
    load_str(h, "peer_ip",       g_ctx.peer_ip,       sizeof(g_ctx.peer_ip));
    load_u16(h, "peer_port",     &g_ctx.peer_port,    DEFAULT_PEER_PORT);
    load_str(h, "device_name",   g_ctx.device_name,   sizeof(g_ctx.device_name));
    load_str(h, "wg_privkey",    g_ctx.wg_privkey,    sizeof(g_ctx.wg_privkey));
    load_str(h, "wg_server_pub", g_ctx.wg_server_pub, sizeof(g_ctx.wg_server_pub));
    load_str(h, "wg_endpoint",   g_ctx.wg_endpoint,   sizeof(g_ctx.wg_endpoint));
    load_u16(h, "wg_ep_port",    &g_ctx.wg_endpoint_port, 51820);
    load_str(h, "wg_local_ip",   g_ctx.wg_local_ip,   sizeof(g_ctx.wg_local_ip));

    if (g_ctx.device_name[0] == '\0')
        strncpy(g_ctx.device_name, "gmabutton", sizeof(g_ctx.device_name) - 1);

    g_ctx.config_mode = (g_ctx.wifi_ssid[0] == '\0');
    nvs_close(h);
    ESP_LOGI(TAG, "config loaded: ssid='%s' peer=%s:%u wg=%s",
             g_ctx.wifi_ssid, g_ctx.peer_ip, g_ctx.peer_port,
             g_ctx.wg_endpoint[0] ? g_ctx.wg_endpoint : "(disabled)");
    return ESP_OK;
}

esp_err_t nvs_config_save(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;

    nvs_set_str(h, "wifi_ssid",     g_ctx.wifi_ssid);
    nvs_set_str(h, "wifi_pass",     g_ctx.wifi_pass);
    nvs_set_str(h, "peer_ip",       g_ctx.peer_ip);
    nvs_set_u16(h, "peer_port",     g_ctx.peer_port);
    nvs_set_str(h, "device_name",   g_ctx.device_name);
    nvs_set_str(h, "wg_privkey",    g_ctx.wg_privkey);
    nvs_set_str(h, "wg_server_pub", g_ctx.wg_server_pub);
    nvs_set_str(h, "wg_endpoint",   g_ctx.wg_endpoint);
    nvs_set_u16(h, "wg_ep_port",    g_ctx.wg_endpoint_port);
    nvs_set_str(h, "wg_local_ip",   g_ctx.wg_local_ip);

    err = nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "config saved (err=%d)", err);
    return err;
}

void nvs_config_factory_reset(void)
{
    ESP_LOGW(TAG, "FACTORY RESET — wiping NVS namespace");
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_all(h);
        nvs_commit(h);
        nvs_close(h);
    }
    esp_restart();
}

void nvs_config_reboot(void)
{
    ESP_LOGI(TAG, "reboot requested");
    esp_restart();
}

/* Increment this whenever the calibration format or point layout changes. */
#define CAL_VERSION 2

void nvs_config_cal_load(void)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) == ESP_OK) {
        uint8_t ver = 0;
        nvs_get_u8(h, "cal_ver", &ver);
        if (ver == CAL_VERSION) {
            size_t sz = sizeof(cal_data_t);
            if (nvs_get_blob(h, "cal", &g_ctx.cal, &sz) != ESP_OK)
                g_ctx.cal.valid = false;
        } else {
            g_ctx.cal.valid = false;   /* stale version — force recalibration */
        }
        nvs_close(h);
    }
}

void nvs_config_cal_save(void)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_blob(h, "cal", &g_ctx.cal, sizeof(cal_data_t));
        nvs_set_u8(h, "cal_ver", CAL_VERSION);
        nvs_commit(h);
        nvs_close(h);
        ESP_LOGI(TAG, "calibration saved (v%d)", CAL_VERSION);
    }
}
