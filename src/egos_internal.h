/**
 * @file egos_internal.h
 * @brief Internal shared declarations for EGOS SDK modules
 *
 * This header is NOT part of the public API. It is shared between
 * the internal .c files of the SDK component.
 */

#ifndef EGOS_INTERNAL_H
#define EGOS_INTERNAL_H

#include "egos_sdk.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --------------------------------------------------------------------------
 * Global State (defined in egos_sdk.c)
 * -------------------------------------------------------------------------- */

/** Stored copy of the user's configuration */
extern egos_config_t egos_g_config;

/** Auto-generated module ID (e.g., "my-buttons-AABBCCDDEEFF") */
extern char egos_g_module_id[64];

/* --------------------------------------------------------------------------
 * Enums
 * -------------------------------------------------------------------------- */

/** WiFi credential source */
typedef enum {
    EGOS_CRED_DEFAULT,   /**< Using default credentials from config */
    EGOS_CRED_STORED,    /**< Using credentials stored in NVS */
} egos_cred_source_t;

/** Connection manager states */
typedef enum {
    EGOS_CONN_INIT,
    EGOS_CONN_TRYING_ETHERNET,
    EGOS_CONN_TRYING_WIFI,
    EGOS_CONN_NETWORK_READY,
    EGOS_CONN_CONNECTED,
    EGOS_CONN_SWITCHING_NETWORK,
} egos_conn_state_t;

/** LED status states (used even when LED is disabled — connection manager references these) */
typedef enum {
    EGOS_LED_OFF,
    EGOS_LED_INIT,
    EGOS_LED_ETHERNET_CONNECTING,
    EGOS_LED_WIFI_CONNECTING_DEFAULT,
    EGOS_LED_WIFI_CONNECTING_STORED,
    EGOS_LED_MQTT_CONNECTING,
    EGOS_LED_CONNECTED_DEFAULT,
    EGOS_LED_CONNECTED_STORED,
    EGOS_LED_ERROR,
    EGOS_LED_CRED_PROVISIONING,
    EGOS_LED_NETWORK_SWITCHING,
    EGOS_LED_REBOOTING,
} egos_led_state_t;

/* --------------------------------------------------------------------------
 * Internal callback types
 * -------------------------------------------------------------------------- */

typedef void (*egos_internal_status_cb_t)(bool connected);
typedef void (*egos_internal_timeout_cb_t)(void);

/* --------------------------------------------------------------------------
 * NVS Module (egos_nvs.c)
 * -------------------------------------------------------------------------- */

esp_err_t egos_nvs_init(void);
esp_err_t egos_nvs_save_wifi_creds(const char *ssid, const char *password);
esp_err_t egos_nvs_load_wifi_creds(char *ssid_buf, size_t ssid_len,
                                    char *pass_buf, size_t pass_len);
esp_err_t egos_nvs_clear_wifi_creds(void);
bool egos_nvs_has_wifi_creds(void);

/* --------------------------------------------------------------------------
 * WiFi Module (egos_wifi.c)
 * -------------------------------------------------------------------------- */

esp_err_t egos_wifi_init(egos_internal_status_cb_t status_cb);
esp_err_t egos_wifi_connect(egos_cred_source_t source);
esp_err_t egos_wifi_disconnect(void);
bool egos_wifi_is_connected(void);
bool egos_wifi_get_ip(char *ip_str, size_t len);
bool egos_wifi_get_gateway_ip(char *ip_str, size_t len);

/* --------------------------------------------------------------------------
 * MQTT Module (egos_mqtt.c)
 * -------------------------------------------------------------------------- */

esp_err_t egos_mqtt_init(const char *broker_uri,
                          egos_internal_status_cb_t status_cb,
                          egos_internal_timeout_cb_t timeout_cb);
esp_err_t egos_mqtt_connect(void);
esp_err_t egos_mqtt_stop(void);
bool egos_mqtt_is_connected(void);
esp_err_t egos_mqtt_publish_input(const char *device_id, const char *command,
                                   const char *params_json);
esp_err_t egos_mqtt_publish_state(const char *device_id, const char *state_json);
esp_err_t egos_mqtt_resolve_broker(egos_cred_source_t cred_source,
                                    char *uri_buf, size_t uri_len);

/* --------------------------------------------------------------------------
 * Connection Manager (egos_connection.c)
 * -------------------------------------------------------------------------- */

esp_err_t egos_connection_start(void);
esp_err_t egos_connection_stop(void);

/* --------------------------------------------------------------------------
 * Ethernet Module (egos_ethernet.c) — conditional
 * -------------------------------------------------------------------------- */

#ifdef CONFIG_EGOS_ETHERNET_ENABLED
esp_err_t egos_ethernet_init(egos_internal_status_cb_t status_cb);
esp_err_t egos_ethernet_start(void);
esp_err_t egos_ethernet_stop(void);
bool egos_ethernet_is_connected(void);
bool egos_ethernet_get_ip(char *ip_str, size_t len);
void egos_ethernet_cleanup(void);
#endif

/* --------------------------------------------------------------------------
 * LED Module (egos_led.c) — conditional
 * -------------------------------------------------------------------------- */

#ifdef CONFIG_EGOS_STATUS_LED_ENABLED
esp_err_t egos_led_init(void);
void egos_led_set_state(egos_led_state_t state);
void egos_led_cleanup(void);
#endif

/**
 * Helper to set LED state safely (no-op when LED is disabled).
 * Use this in connection manager and other modules.
 */
static inline void egos_led_update(egos_led_state_t state)
{
#ifdef CONFIG_EGOS_STATUS_LED_ENABLED
    egos_led_set_state(state);
#else
    (void)state;
#endif
}

#ifdef __cplusplus
}
#endif

#endif /* EGOS_INTERNAL_H */
