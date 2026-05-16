#include "http_server.h"
#include "app_state.h"
#include "nvs_config.h"
#include "led.h"
#include "peer_comms.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static const char *TAG = "httpd";
static httpd_handle_t s_srv = NULL;

/* ── State change helpers (also called from /api/alert handler) ─────────── */
void http_apply_alert_call(bool local)
{
    g_ctx.is_local_alert = local;
    app_set_state(STATE_ALERT_CALL);
    if (local) led_blue(); else led_off();
}

void http_apply_alert_help(bool local)
{
    g_ctx.is_local_alert = local;
    app_set_state(STATE_ALERT_HELP);
    if (local) led_red(); else led_off();
}

void http_apply_dismiss(void)
{
    app_set_state(STATE_SCREENSAVER);
    led_off();
}

/* ── Tiny urlencoded form parser ───────────────────────────────────────── */
static void url_decode(char *dst, const char *src, size_t cap)
{
    size_t o = 0;
    for (size_t i = 0; src[i] && o + 1 < cap; ++i) {
        if (src[i] == '+') {
            dst[o++] = ' ';
        } else if (src[i] == '%' && src[i+1] && src[i+2]) {
            char h[3] = { src[i+1], src[i+2], 0 };
            dst[o++] = (char)strtol(h, NULL, 16);
            i += 2;
        } else {
            dst[o++] = src[i];
        }
    }
    dst[o] = '\0';
}

/* Pull the value for `key` out of `body` ("a=1&b=hello&..."); writes raw,
   then url-decodes in place. Returns true on hit. */
static bool form_get(const char *body, const char *key, char *out, size_t cap)
{
    size_t klen = strlen(key);
    const char *p = body;
    while (p && *p) {
        if (!strncmp(p, key, klen) && p[klen] == '=') {
            const char *v = p + klen + 1;
            const char *e = strchr(v, '&');
            size_t vlen = e ? (size_t)(e - v) : strlen(v);
            if (vlen >= cap) vlen = cap - 1;
            char raw[256];
            if (vlen >= sizeof(raw)) vlen = sizeof(raw) - 1;
            memcpy(raw, v, vlen);
            raw[vlen] = '\0';
            url_decode(out, raw, cap);
            return true;
        }
        p = strchr(p, '&');
        if (p) p++;
    }
    out[0] = '\0';
    return false;
}

/* ── Setup wizard HTML (config mode) ────────────────────────────────────── */
static const char SETUP_HTML[] =
"<!doctype html><html><head><meta charset=utf-8>"
"<meta name=viewport content='width=device-width,initial-scale=1'>"
"<title>gmabutton setup</title>"
"<style>body{font-family:system-ui,sans-serif;max-width:480px;margin:1em auto;padding:0 1em;background:#111;color:#eee}"
"input,button{width:100%;padding:.6em;margin:.3em 0;box-sizing:border-box;font-size:1em;background:#222;color:#eee;border:1px solid #444;border-radius:4px}"
"button{background:#2a6;border-color:#2a6;color:#000;font-weight:bold;cursor:pointer}"
"h2{margin-top:1.5em;border-bottom:1px solid #444;padding-bottom:.2em}"
"label{display:block;margin-top:.6em;font-size:.85em;color:#aaa}"
"small{color:#888}"
"</style></head><body>"
"<h1>gmabutton setup</h1>"
"<p>Connect to your WiFi and configure your peer / VPN credentials.</p>"
"<form method=POST action=/api/setup>"
"<h2>WiFi</h2>"
"<label>SSID</label><input name=wifi_ssid required>"
"<label>Password</label><input name=wifi_pass type=password>"
"<h2>Device</h2>"
"<label>Device name</label><input name=device_name value=gmabutton>"
"<label>Peer IP (the other button)</label><input name=peer_ip placeholder='10.0.0.3 or 192.168.x.x'>"
"<label>Peer port</label><input name=peer_port value=8080>"
"<h2>WireGuard (optional)</h2>"
"<small>Leave blank to use plain WiFi only.</small>"
"<label>Device private key (base64)</label><input name=wg_privkey>"
"<label>Server public key (base64)</label><input name=wg_server_pub>"
"<label>Server endpoint (host or IP)</label><input name=wg_endpoint>"
"<label>Server port</label><input name=wg_ep_port value=51820>"
"<label>This device's tunnel IP</label><input name=wg_local_ip placeholder='10.0.0.2'>"
"<button type=submit>Save and reboot</button>"
"</form></body></html>";

static esp_err_t setup_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, SETUP_HTML, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t setup_post_handler(httpd_req_t *req)
{
    char body[1024];
    int len = req->content_len;
    if (len <= 0 || len >= (int)sizeof(body)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "body too large");
        return ESP_FAIL;
    }
    int r = httpd_req_recv(req, body, len);
    if (r <= 0) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "recv failed");
        return ESP_FAIL;
    }
    body[r] = '\0';

    char tmp[16];
    form_get(body, "wifi_ssid",     g_ctx.wifi_ssid,     sizeof(g_ctx.wifi_ssid));
    form_get(body, "wifi_pass",     g_ctx.wifi_pass,     sizeof(g_ctx.wifi_pass));
    form_get(body, "device_name",   g_ctx.device_name,   sizeof(g_ctx.device_name));
    form_get(body, "peer_ip",       g_ctx.peer_ip,       sizeof(g_ctx.peer_ip));
    if (form_get(body, "peer_port", tmp, sizeof(tmp)))
        g_ctx.peer_port = (uint16_t)atoi(tmp);
    form_get(body, "wg_privkey",    g_ctx.wg_privkey,    sizeof(g_ctx.wg_privkey));
    form_get(body, "wg_server_pub", g_ctx.wg_server_pub, sizeof(g_ctx.wg_server_pub));
    form_get(body, "wg_endpoint",   g_ctx.wg_endpoint,   sizeof(g_ctx.wg_endpoint));
    if (form_get(body, "wg_ep_port", tmp, sizeof(tmp)))
        g_ctx.wg_endpoint_port = (uint16_t)atoi(tmp);
    form_get(body, "wg_local_ip",   g_ctx.wg_local_ip,   sizeof(g_ctx.wg_local_ip));

    if (g_ctx.peer_port == 0) g_ctx.peer_port = DEFAULT_PEER_PORT;

    nvs_config_save();
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req,
        "<h1>Saved.</h1><p>Rebooting — connect to your home WiFi.</p>",
        HTTPD_RESP_USE_STRLEN);

    /* Defer restart so the HTTP response actually flushes. */
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

/* ── Normal-mode status page ────────────────────────────────────────────── */
static const char STATUS_HTML[] =
"<!doctype html><html><head><meta charset=utf-8>"
"<meta name=viewport content='width=device-width,initial-scale=1'>"
"<title>gmabutton</title>"
"<style>body{font-family:system-ui,sans-serif;max-width:480px;margin:1em auto;padding:0 1em;background:#111;color:#eee}"
"pre{background:#222;padding:.5em;border-radius:4px;overflow:auto}"
"button{padding:.5em 1em;margin:.3em;background:#333;border:1px solid #555;color:#eee;cursor:pointer;border-radius:4px}"
".blue{background:#26f}.red{background:#c33}.gray{background:#444}"
"</style></head><body>"
"<h1>gmabutton</h1><pre id=s>loading…</pre>"
"<button class=blue onclick=fetch('/api/alert',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'type=call'})>CALL</button>"
"<button class=red  onclick=fetch('/api/alert',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'type=help'})>HELP</button>"
"<button class=gray onclick=fetch('/api/dismiss',{method:'POST'})>DISMISS</button>"
"<p><a href=/config style=color:#aaf>edit config</a></p>"
"<script>setInterval(async()=>{document.getElementById('s').textContent=await(await fetch('/api/status')).text()},1500)</script>"
"</body></html>";

static esp_err_t status_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, STATUS_HTML, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t api_status_handler(httpd_req_t *req)
{
    const char *state_str =
        g_ctx.state == STATE_SCREENSAVER ? "SCREENSAVER" :
        g_ctx.state == STATE_ALERT_CALL  ? "CALL"        :
        g_ctx.state == STATE_ALERT_HELP  ? "HELP"        :
        g_ctx.state == STATE_DISMISS_PENDING ? "DISMISS?" :
        g_ctx.state == STATE_CONFIG_MODE ? "CONFIG" : "?";

    char body[512];
    int n = snprintf(body, sizeof(body),
        "{\n"
        "  \"state\": \"%s\",\n"
        "  \"name\": \"%s\",\n"
        "  \"ip\": \"%s\",\n"
        "  \"peer\": \"%s:%u\",\n"
        "  \"wifi\": %s,\n"
        "  \"wg\": %s,\n"
        "  \"time\": \"%s\"\n"
        "}\n",
        state_str, g_ctx.device_name, g_ctx.ip_str,
        g_ctx.peer_ip, g_ctx.peer_port,
        g_ctx.wifi_connected ? "true" : "false",
        g_ctx.wg_connected   ? "true" : "false",
        g_ctx.time_str[0] ? g_ctx.time_str : "--:--:--");
    (void)n;
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t api_alert_handler(httpd_req_t *req)
{
    char body[64];
    int len = req->content_len < (int)sizeof(body) - 1 ? req->content_len : (int)sizeof(body) - 1;
    if (len > 0) {
        int r = httpd_req_recv(req, body, len);
        if (r > 0) body[r] = '\0'; else body[0] = '\0';
    } else {
        body[0] = '\0';
    }

    char type[16] = { 0 };
    form_get(body, "type", type, sizeof(type));

    if (!strcmp(type, "call")) {
        http_apply_alert_call(false);
    } else if (!strcmp(type, "help")) {
        http_apply_alert_help(false);
    } else {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "type must be call|help");
        return ESP_FAIL;
    }
    httpd_resp_sendstr(req, "ok");
    return ESP_OK;
}

static esp_err_t api_dismiss_handler(httpd_req_t *req)
{
    http_apply_dismiss();
    httpd_resp_sendstr(req, "ok");
    return ESP_OK;
}

static esp_err_t config_get_handler(httpd_req_t *req)
{
    /* Reuse the setup wizard form in normal mode too. */
    return setup_get_handler(req);
}

esp_err_t http_server_start(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port = g_ctx.config_mode ? 80 : g_ctx.peer_port;
    cfg.max_uri_handlers = 12;
    cfg.stack_size = 8192;
    cfg.lru_purge_enable = true;

    esp_err_t err = httpd_start(&s_srv, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(err));
        return err;
    }

    if (g_ctx.config_mode) {
        httpd_uri_t get  = { .uri = "/",          .method = HTTP_GET,  .handler = setup_get_handler  };
        httpd_uri_t post = { .uri = "/api/setup", .method = HTTP_POST, .handler = setup_post_handler };
        httpd_register_uri_handler(s_srv, &get);
        httpd_register_uri_handler(s_srv, &post);
    } else {
        httpd_uri_t root     = { .uri = "/",            .method = HTTP_GET,  .handler = status_get_handler };
        httpd_uri_t cfg_get  = { .uri = "/config",      .method = HTTP_GET,  .handler = config_get_handler };
        httpd_uri_t cfg_post = { .uri = "/api/setup",   .method = HTTP_POST, .handler = setup_post_handler };
        httpd_uri_t status   = { .uri = "/api/status",  .method = HTTP_GET,  .handler = api_status_handler };
        httpd_uri_t alert    = { .uri = "/api/alert",   .method = HTTP_POST, .handler = api_alert_handler };
        httpd_uri_t dismiss  = { .uri = "/api/dismiss", .method = HTTP_POST, .handler = api_dismiss_handler };
        httpd_register_uri_handler(s_srv, &root);
        httpd_register_uri_handler(s_srv, &cfg_get);
        httpd_register_uri_handler(s_srv, &cfg_post);
        httpd_register_uri_handler(s_srv, &status);
        httpd_register_uri_handler(s_srv, &alert);
        httpd_register_uri_handler(s_srv, &dismiss);
    }
    ESP_LOGI(TAG, "httpd started (%s mode)",
             g_ctx.config_mode ? "config" : "normal");
    return ESP_OK;
}
