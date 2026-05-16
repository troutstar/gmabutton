#include "wireguard_mgr.h"
#include "app_state.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <time.h>

/* Component header from trombik/esp_wireguard. If the dependency is not yet
   fetched (first build), this whole module compiles as a no-op stub so the
   rest of the project still links. Set WG_ENABLED to 1 once the component is
   resolved by `idf.py reconfigure`. */
#if __has_include("esp_wireguard.h")
  #include "esp_wireguard.h"
  #define WG_ENABLED 1
#else
  #define WG_ENABLED 0
  #warning "esp_wireguard.h not found — WireGuard support stubbed out"
#endif

static const char *TAG = "wg";

#if WG_ENABLED
static wireguard_ctx_t s_ctx = { 0 };

static void wg_task(void *arg)
{
    (void)arg;

    /* Wait for WiFi */
    while (!g_ctx.wifi_connected)
        vTaskDelay(pdMS_TO_TICKS(500));

    /* Wait for SNTP time sync — WireGuard handshake requires valid system time.
       Poll until time is past year 2020 (epoch > 1577836800). */
    {
        time_t t = 0;
        while (t < 1577836800) {
            vTaskDelay(pdMS_TO_TICKS(500));
            t = time(NULL);
        }
        ESP_LOGI(TAG, "time synced: %lld", (long long)t);
    }

    wireguard_config_t cfg = ESP_WIREGUARD_CONFIG_DEFAULT();
    cfg.private_key          = g_ctx.wg_privkey;
    cfg.public_key           = g_ctx.wg_server_pub;
    cfg.endpoint             = g_ctx.wg_endpoint;
    cfg.port                 = g_ctx.wg_endpoint_port;
    cfg.allowed_ip           = g_ctx.wg_local_ip;
    cfg.allowed_ip_mask      = (char *)"255.255.255.0";
    cfg.persistent_keepalive = 25;

    esp_err_t err = esp_wireguard_init(&cfg, &s_ctx);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "wg init failed: %s", esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }

    err = esp_wireguard_connect(&s_ctx);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "wg connect failed: %s", esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }

    /* Assume the tunnel is up once connect succeeds. The handshake itself
       runs asynchronously inside the lwIP tcpip task — the timer (every
       400 ms) fires `wireguard_start_handshake`, and the UDP receive
       callback (`wireguardif_network_rx`) processes the response. Both
       paths perform x25519 + chacha20poly1305 in tcpip-thread context,
       so `CONFIG_LWIP_TCPIP_TASK_STACK_SIZE` must be generous (≥ 12 KB)
       and the lightweight refc x25519 must be selected — see
       sdkconfig.defaults. NaCL x25519 will overflow the stack the moment
       the server's handshake response arrives. */
    g_ctx.wg_connected = true;
    ESP_LOGI(TAG, "WireGuard started: %s:%u (local %s)",
             g_ctx.wg_endpoint, g_ctx.wg_endpoint_port, g_ctx.wg_local_ip);

    /* Watchdog: reconnect if WiFi drops and comes back. */
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(60 * 1000));
        if (!g_ctx.wifi_connected) {
            g_ctx.wg_connected = false;
            /* wait for WiFi to return */
            while (!g_ctx.wifi_connected)
                vTaskDelay(pdMS_TO_TICKS(1000));
            vTaskDelay(pdMS_TO_TICKS(2000));
            esp_wireguard_disconnect(&s_ctx);
            if (esp_wireguard_connect(&s_ctx) == ESP_OK) {
                g_ctx.wg_connected = true;
                ESP_LOGI(TAG, "WireGuard reconnected after WiFi restore");
            }
        }
    }
}
#endif

esp_err_t wireguard_mgr_start(void)
{
    if (g_ctx.wg_privkey[0] == '\0' || g_ctx.wg_endpoint[0] == '\0') {
        ESP_LOGI(TAG, "WireGuard not configured — skipping");
        return ESP_ERR_INVALID_STATE;
    }
#if !WG_ENABLED
    ESP_LOGE(TAG, "WireGuard configured but component is missing");
    return ESP_ERR_NOT_SUPPORTED;
#else
    /* Init and connect run inside the task so crypto has sufficient stack. */
    xTaskCreatePinnedToCore(wg_task, "wg", 4096, NULL, 3, NULL, 0);
    return ESP_OK;
#endif
}
