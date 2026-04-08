/**
 * @file egos_connection.c
 * @brief Connection manager state machine
 *
 * Orchestrates WiFi/Ethernet/MQTT lifecycle with automatic retry and fallback.
 */

#include "egos_internal.h"
#include <string.h>

static const char *TAG = "egos_conn";

#define ETHERNET_TIMEOUT_MS     5000
#define MQTT_FALLBACK_THRESHOLD 5
#define TASK_STACK_SIZE         4096
#define TASK_PRIORITY           5
#define TICK_MS                 100

/* --------------------------------------------------------------------------
 * State
 * -------------------------------------------------------------------------- */

static volatile egos_conn_state_t s_conn_state = EGOS_CONN_INIT;
static volatile bool s_wifi_connected = false;
static volatile bool s_mqtt_connected = false;
static volatile bool s_mqtt_switch_requested = false;
static volatile uint8_t s_mqtt_timeouts = 0;
static egos_cred_source_t s_cred_source = EGOS_CRED_DEFAULT;
static bool s_mqtt_initialized = false;
static TaskHandle_t s_task_handle = NULL;

#ifdef CONFIG_EGOS_ETHERNET_ENABLED
static volatile bool s_ethernet_connected = false;

typedef enum {
    EGOS_NET_ETHERNET,
    EGOS_NET_WIFI_DEFAULT,
    EGOS_NET_WIFI_STORED,
} egos_active_network_t;

static egos_active_network_t s_active_network = EGOS_NET_ETHERNET;
#endif

/* --------------------------------------------------------------------------
 * Internal Callbacks
 * -------------------------------------------------------------------------- */

static void wifi_status_cb(bool connected)
{
    s_wifi_connected = connected;
    if (!connected) {
        ESP_LOGW(TAG, "WiFi disconnected");
    }
}

static void mqtt_status_cb(bool connected)
{
    s_mqtt_connected = connected;
    if (connected) {
        s_mqtt_timeouts = 0;
        s_mqtt_switch_requested = false;
        ESP_LOGI(TAG, "MQTT connected");

        egos_led_state_t led = (s_cred_source == EGOS_CRED_STORED)
                               ? EGOS_LED_CONNECTED_STORED
                               : EGOS_LED_CONNECTED_DEFAULT;
        egos_led_update(led);

        if (egos_g_config.on_connected) {
            egos_g_config.on_connected(egos_g_config.user_data);
        }
    } else {
        ESP_LOGW(TAG, "MQTT disconnected");
        egos_led_update(EGOS_LED_MQTT_CONNECTING);

        if (egos_g_config.on_disconnected) {
            egos_g_config.on_disconnected(egos_g_config.user_data);
        }
    }
}

static void mqtt_timeout_cb(void)
{
    s_mqtt_timeouts++;
    ESP_LOGW(TAG, "MQTT timeout (%d/%d)", s_mqtt_timeouts, MQTT_FALLBACK_THRESHOLD);
    if (s_mqtt_timeouts >= MQTT_FALLBACK_THRESHOLD) {
        s_mqtt_switch_requested = true;
    }
}

#ifdef CONFIG_EGOS_ETHERNET_ENABLED
static void ethernet_status_cb(bool connected)
{
    s_ethernet_connected = connected;
    if (!connected) {
        ESP_LOGW(TAG, "Ethernet disconnected");
    }
}
#endif

/* --------------------------------------------------------------------------
 * MQTT Connect Helper
 * -------------------------------------------------------------------------- */

static esp_err_t try_mqtt_connect(void)
{
    char broker_uri[128];
    esp_err_t ret = egos_mqtt_resolve_broker(s_cred_source, broker_uri, sizeof(broker_uri));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to resolve broker");
        return ret;
    }

    if (s_mqtt_initialized) {
        egos_mqtt_stop();
        s_mqtt_initialized = false;
    }

    ret = egos_mqtt_init(broker_uri, mqtt_status_cb, mqtt_timeout_cb);
    if (ret != ESP_OK) {
        return ret;
    }

    s_mqtt_initialized = true;
    return egos_mqtt_connect();
}

/* --------------------------------------------------------------------------
 * Connection Manager Task
 * -------------------------------------------------------------------------- */

static void connection_manager_task(void *pvParameters)
{
    uint32_t state_timer = 0;

    ESP_LOGI(TAG, "Connection manager started");
    egos_led_update(EGOS_LED_INIT);

    while (1) {
        switch (s_conn_state) {

        case EGOS_CONN_INIT:
            state_timer = 0;

#ifdef CONFIG_EGOS_ETHERNET_ENABLED
            ESP_LOGI(TAG, "Trying Ethernet first...");
            egos_led_update(EGOS_LED_ETHERNET_CONNECTING);
            egos_ethernet_start();
            s_conn_state = EGOS_CONN_TRYING_ETHERNET;
#else
            /* Decide which WiFi credentials to try first */
            if (egos_nvs_has_wifi_creds()) {
                s_cred_source = EGOS_CRED_STORED;
                egos_led_update(EGOS_LED_WIFI_CONNECTING_STORED);
            } else {
                s_cred_source = EGOS_CRED_DEFAULT;
                egos_led_update(EGOS_LED_WIFI_CONNECTING_DEFAULT);
            }
            ESP_LOGI(TAG, "Connecting WiFi (%s credentials)...",
                     s_cred_source == EGOS_CRED_STORED ? "stored" : "default");
            egos_wifi_connect(s_cred_source);
            s_conn_state = EGOS_CONN_TRYING_WIFI;
#endif
            break;

#ifdef CONFIG_EGOS_ETHERNET_ENABLED
        case EGOS_CONN_TRYING_ETHERNET:
            state_timer += TICK_MS;

            if (s_ethernet_connected) {
                ESP_LOGI(TAG, "Ethernet connected");
                s_active_network = EGOS_NET_ETHERNET;
                s_conn_state = EGOS_CONN_NETWORK_READY;
                break;
            }

            if (state_timer >= ETHERNET_TIMEOUT_MS) {
                ESP_LOGW(TAG, "Ethernet timeout, falling back to WiFi");
                egos_ethernet_stop();

                if (egos_nvs_has_wifi_creds()) {
                    s_cred_source = EGOS_CRED_STORED;
                    s_active_network = EGOS_NET_WIFI_STORED;
                    egos_led_update(EGOS_LED_WIFI_CONNECTING_STORED);
                } else {
                    s_cred_source = EGOS_CRED_DEFAULT;
                    s_active_network = EGOS_NET_WIFI_DEFAULT;
                    egos_led_update(EGOS_LED_WIFI_CONNECTING_DEFAULT);
                }

                egos_wifi_connect(s_cred_source);
                s_conn_state = EGOS_CONN_TRYING_WIFI;
                state_timer = 0;
            }
            break;
#endif

        case EGOS_CONN_TRYING_WIFI:
            if (s_wifi_connected) {
                char ip[16];
                egos_wifi_get_ip(ip, sizeof(ip));
                ESP_LOGI(TAG, "WiFi connected (IP: %s)", ip);
                s_conn_state = EGOS_CONN_NETWORK_READY;
            }
            break;

        case EGOS_CONN_NETWORK_READY:
            ESP_LOGI(TAG, "Network ready, connecting MQTT...");
            egos_led_update(EGOS_LED_MQTT_CONNECTING);

            if (try_mqtt_connect() != ESP_OK) {
                ESP_LOGE(TAG, "Failed to start MQTT connection");
                egos_led_update(EGOS_LED_ERROR);
                vTaskDelay(pdMS_TO_TICKS(2000));
                /* Retry */
                break;
            }

            s_conn_state = EGOS_CONN_CONNECTED;
            break;

        case EGOS_CONN_CONNECTED:
            /* Check for MQTT network switch request */
            if (s_mqtt_switch_requested) {
                ESP_LOGW(TAG, "MQTT fallback threshold reached, switching network");
                s_mqtt_switch_requested = false;
                s_mqtt_timeouts = 0;
                s_conn_state = EGOS_CONN_SWITCHING_NETWORK;
                break;
            }

            /* If MQTT disconnected but network still up, reconnect */
            if (!s_mqtt_connected && (s_wifi_connected
#ifdef CONFIG_EGOS_ETHERNET_ENABLED
                || s_ethernet_connected
#endif
            )) {
                ESP_LOGI(TAG, "MQTT lost, reconnecting...");
                egos_led_update(EGOS_LED_MQTT_CONNECTING);
                try_mqtt_connect();
            }
            break;

        case EGOS_CONN_SWITCHING_NETWORK:
            egos_led_update(EGOS_LED_NETWORK_SWITCHING);

            /* Stop current MQTT */
            if (s_mqtt_initialized) {
                egos_mqtt_stop();
                s_mqtt_initialized = false;
            }

#ifdef CONFIG_EGOS_ETHERNET_ENABLED
            /* Cycle: Ethernet → WiFi(default) → WiFi(stored) → Ethernet */
            switch (s_active_network) {
                case EGOS_NET_ETHERNET:
                    egos_ethernet_stop();
                    s_cred_source = EGOS_CRED_DEFAULT;
                    s_active_network = EGOS_NET_WIFI_DEFAULT;
                    egos_led_update(EGOS_LED_WIFI_CONNECTING_DEFAULT);
                    egos_wifi_connect(s_cred_source);
                    s_conn_state = EGOS_CONN_TRYING_WIFI;
                    break;

                case EGOS_NET_WIFI_DEFAULT:
                    egos_wifi_disconnect();
                    if (egos_nvs_has_wifi_creds()) {
                        s_cred_source = EGOS_CRED_STORED;
                        s_active_network = EGOS_NET_WIFI_STORED;
                        egos_led_update(EGOS_LED_WIFI_CONNECTING_STORED);
                        egos_wifi_connect(s_cred_source);
                        s_conn_state = EGOS_CONN_TRYING_WIFI;
                    } else {
                        s_active_network = EGOS_NET_ETHERNET;
                        egos_led_update(EGOS_LED_ETHERNET_CONNECTING);
                        egos_ethernet_start();
                        s_conn_state = EGOS_CONN_TRYING_ETHERNET;
                        state_timer = 0;
                    }
                    break;

                case EGOS_NET_WIFI_STORED:
                    egos_wifi_disconnect();
                    s_active_network = EGOS_NET_ETHERNET;
                    egos_led_update(EGOS_LED_ETHERNET_CONNECTING);
                    egos_ethernet_start();
                    s_conn_state = EGOS_CONN_TRYING_ETHERNET;
                    state_timer = 0;
                    break;
            }
#else
            /* WiFi only: toggle between default and stored credentials */
            egos_wifi_disconnect();

            if (s_cred_source == EGOS_CRED_DEFAULT && egos_nvs_has_wifi_creds()) {
                s_cred_source = EGOS_CRED_STORED;
                egos_led_update(EGOS_LED_WIFI_CONNECTING_STORED);
            } else {
                s_cred_source = EGOS_CRED_DEFAULT;
                egos_led_update(EGOS_LED_WIFI_CONNECTING_DEFAULT);
            }

            ESP_LOGI(TAG, "Switching to %s WiFi credentials",
                     s_cred_source == EGOS_CRED_STORED ? "stored" : "default");
            egos_wifi_connect(s_cred_source);
            s_conn_state = EGOS_CONN_TRYING_WIFI;
#endif
            break;
        }

        vTaskDelay(pdMS_TO_TICKS(TICK_MS));
    }
}

/* --------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */

esp_err_t egos_connection_start(void)
{
    /* Initialize NVS */
    esp_err_t ret = egos_nvs_init();
    if (ret != ESP_OK) {
        return ret;
    }

    /* Initialize status LED (optional) */
#ifdef CONFIG_EGOS_STATUS_LED_ENABLED
    egos_led_init();
#endif

    /* Initialize WiFi */
    ret = egos_wifi_init(wifi_status_cb);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi init failed");
        return ret;
    }

    /* Initialize Ethernet (optional) */
#ifdef CONFIG_EGOS_ETHERNET_ENABLED
    ret = egos_ethernet_init(ethernet_status_cb);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Ethernet init failed, will use WiFi only");
    }
#endif

    /* Create connection manager task */
    BaseType_t task_ret = xTaskCreate(connection_manager_task, "egos_conn",
                                       TASK_STACK_SIZE, NULL, TASK_PRIORITY,
                                       &s_task_handle);
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create connection manager task");
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t egos_connection_stop(void)
{
    /* Delete task */
    if (s_task_handle) {
        vTaskDelete(s_task_handle);
        s_task_handle = NULL;
    }

    /* Stop MQTT */
    if (s_mqtt_initialized) {
        egos_mqtt_stop();
        s_mqtt_initialized = false;
    }

    /* Disconnect WiFi */
    egos_wifi_disconnect();

    /* Stop Ethernet */
#ifdef CONFIG_EGOS_ETHERNET_ENABLED
    egos_ethernet_stop();
    egos_ethernet_cleanup();
#endif

    /* Cleanup LED */
#ifdef CONFIG_EGOS_STATUS_LED_ENABLED
    egos_led_cleanup();
#endif

    s_conn_state = EGOS_CONN_INIT;
    ESP_LOGI(TAG, "Connection manager stopped");
    return ESP_OK;
}
