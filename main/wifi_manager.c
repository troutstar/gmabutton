#include "wifi_manager.h"
#include "app_state.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <time.h>
#include <stdio.h>

static const char *TAG = "wifi";

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "STA disconnected, retrying");
        g_ctx.wifi_connected = false;
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
        snprintf(g_ctx.ip_str, sizeof(g_ctx.ip_str),
                 IPSTR, IP2STR(&e->ip_info.ip));
        ESP_LOGI(TAG, "got IP: %s", g_ctx.ip_str);
        g_ctx.wifi_connected = true;

        esp_netif_set_hostname(esp_netif_get_handle_from_ifkey("WIFI_STA_DEF"), g_ctx.device_name);

        if (esp_sntp_enabled()) esp_sntp_stop();
        esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
        esp_sntp_setservername(0, "pool.ntp.org");
        esp_sntp_init();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STACONNECTED) {
        ESP_LOGI(TAG, "AP: client connected");
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STADISCONNECTED) {
        ESP_LOGI(TAG, "AP: client disconnected");
    }
}

static void time_task(void *arg)
{
    (void)arg;
    for (;;) {
        time_t now = time(NULL);
        struct tm tm;
        localtime_r(&now, &tm);
        snprintf(g_ctx.time_str, sizeof(g_ctx.time_str),
                 "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

static void build_ap_ssid(void)
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    snprintf(g_ctx.ap_ssid, sizeof(g_ctx.ap_ssid),
             "gmabutton-%02X%02X", mac[4], mac[5]);
}

static esp_err_t start_sta(void)
{
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                        on_wifi_event, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                        on_wifi_event, NULL, NULL);

    wifi_config_t wc = { 0 };
    strncpy((char *)wc.sta.ssid,     g_ctx.wifi_ssid, sizeof(wc.sta.ssid) - 1);
    strncpy((char *)wc.sta.password, g_ctx.wifi_pass, sizeof(wc.sta.password) - 1);
    wc.sta.threshold.authmode = WIFI_AUTH_OPEN;

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wc);
    esp_wifi_start();

    setenv("TZ", "UTC0", 1);
    tzset();
    xTaskCreatePinnedToCore(time_task, "time", 2048, NULL, 2, NULL, 0);
    return ESP_OK;
}

static esp_err_t start_ap(void)
{
    build_ap_ssid();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                        on_wifi_event, NULL, NULL);

    wifi_config_t wc = { 0 };
    strncpy((char *)wc.ap.ssid, g_ctx.ap_ssid, sizeof(wc.ap.ssid) - 1);
    wc.ap.ssid_len      = strlen(g_ctx.ap_ssid);
    wc.ap.channel       = 1;
    wc.ap.max_connection = 4;
    wc.ap.authmode      = WIFI_AUTH_OPEN;

    esp_wifi_set_mode(WIFI_MODE_AP);
    esp_wifi_set_config(WIFI_IF_AP, &wc);
    esp_wifi_start();

    /* Soft-AP default IP is 192.168.4.1 */
    strncpy(g_ctx.ip_str, "192.168.4.1", sizeof(g_ctx.ip_str) - 1);
    ESP_LOGI(TAG, "AP ready: SSID=%s IP=192.168.4.1", g_ctx.ap_ssid);
    return ESP_OK;
}

esp_err_t wifi_manager_init(void)
{
    esp_netif_init();
    esp_event_loop_create_default();
    return g_ctx.config_mode ? start_ap() : start_sta();
}
