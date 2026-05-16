#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include <stdbool.h>
#include <stdint.h>

#define DEFAULT_PEER_PORT       8080
#define DISMISS_TIMEOUT_MS      4000
#define FACTORY_RESET_HOLD_MS   10000

typedef enum {
    STATE_SCREENSAVER = 0,
    STATE_ALERT_CALL,        /* blue */
    STATE_ALERT_HELP,        /* red  */
    STATE_DISMISS_PENDING,   /* alert active + dismiss overlay shown */
    STATE_CONFIG_MODE,       /* AP active, setup wizard served */
} app_state_t;

typedef enum {
    GESTURE_TAP = 0,         /* < 300 ms — confirm dismiss */
    GESTURE_HOLD_SHORT,      /* 300 ms–1.5 s — initiate dismiss */
    GESTURE_HOLD_CALL,       /* 1.5 s–3.5 s — "call me" (blue) */
    GESTURE_HOLD_HELP,       /* 3.5 s–10 s  — "help me" (red) */
    GESTURE_LONG_HOLD,       /* >= 10 s — factory reset */
} gesture_t;

typedef enum {
    COMMS_ALERT_CALL = 0,
    COMMS_ALERT_HELP,
    COMMS_DISMISS,
} comms_kind_t;

typedef struct {
    /* Runtime state — guarded by state_mutex unless noted */
    volatile app_state_t state;
    SemaphoreHandle_t    state_mutex;

    /* Inter-task channels */
    QueueHandle_t        touch_q;   /* posts gesture_t */
    QueueHandle_t        comms_q;   /* posts comms_kind_t */

    /* Persistent config (loaded from NVS at boot) */
    char     wifi_ssid[33];
    char     wifi_pass[65];
    char     peer_ip[64];
    uint16_t peer_port;
    char     device_name[32];

    /* WireGuard client config (all optional) */
    char     wg_privkey[72];      /* base64 44 chars; URL-encoded form needs up to ~52 chars */
    char     wg_server_pub[72];  /* server pubkey can have 3+ slashes → 52+ URL-encoded chars */
    char     wg_endpoint[64];
    uint16_t wg_endpoint_port;
    char     wg_local_ip[20];     /* device's address inside the tunnel */

    /* Runtime status — read by render_task, written by network tasks */
    bool     config_mode;         /* set at boot if wifi_ssid is empty */
    bool     wifi_connected;
    bool     wg_connected;
    char     ip_str[20];
    char     ap_ssid[32];         /* "gmabutton-XXXX" in config mode */
    char     time_str[9];         /* "HH:MM:SS" */
    uint32_t dismiss_pending_since_ms;
    app_state_t pending_underlay; /* which alert dismiss-pending is over */
    bool     is_local_alert;      /* true = this device initiated; false = received from peer */
    bool     holding;             /* finger currently on screen (set by touch_task) */
    uint32_t hold_start_ms;       /* esp_timer ms when current hold began */
} app_ctx_t;

extern app_ctx_t g_ctx;

/* Helpers — thread-safe state transitions */
void app_set_state(app_state_t s);
app_state_t app_get_state(void);
