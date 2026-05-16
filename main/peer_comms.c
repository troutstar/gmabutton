#include "peer_comms.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "comms";

void peer_send(comms_kind_t k)
{
    if (g_ctx.comms_q) xQueueSend(g_ctx.comms_q, &k, 0);
}

static void do_post(const char *path, const char *body)
{
    if (g_ctx.peer_ip[0] == '\0') {
        ESP_LOGW(TAG, "no peer configured, dropping %s", path);
        return;
    }
    char url[160];
    snprintf(url, sizeof(url), "http://%s:%u%s",
             g_ctx.peer_ip, g_ctx.peer_port, path);

    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 2000,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) {
        ESP_LOGE(TAG, "http init failed for %s", url);
        return;
    }
    esp_http_client_set_header(c, "Content-Type", "application/x-www-form-urlencoded");
    if (body) esp_http_client_set_post_field(c, body, strlen(body));

    for (int attempt = 0; attempt < 2; ++attempt) {
        esp_err_t err = esp_http_client_perform(c);
        if (err == ESP_OK) {
            int code = esp_http_client_get_status_code(c);
            ESP_LOGI(TAG, "POST %s → %d", url, code);
            break;
        }
        ESP_LOGW(TAG, "POST %s attempt %d failed: %s",
                 url, attempt + 1, esp_err_to_name(err));
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    esp_http_client_cleanup(c);
}

static void comms_task(void *arg)
{
    (void)arg;
    comms_kind_t k;
    for (;;) {
        if (xQueueReceive(g_ctx.comms_q, &k, portMAX_DELAY) != pdTRUE) continue;
        if (!g_ctx.wifi_connected) {
            ESP_LOGW(TAG, "skip send — wifi down");
            continue;
        }
        switch (k) {
        case COMMS_ALERT_CALL: do_post("/api/alert",   "type=call"); break;
        case COMMS_ALERT_HELP: do_post("/api/alert",   "type=help"); break;
        case COMMS_DISMISS:    do_post("/api/dismiss", "");          break;
        }
    }
}

void peer_comms_start(void)
{
    xTaskCreatePinnedToCore(comms_task, "comms", 6144, NULL, 4, NULL, 0);
}
