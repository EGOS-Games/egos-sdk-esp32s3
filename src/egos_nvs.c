/**
 * @file egos_nvs.c
 * @brief NVS-based WiFi credential storage
 */

#include "egos_internal.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>

static const char *TAG = "egos_nvs";

#define NVS_NAMESPACE   "wifi_creds"
#define NVS_KEY_SSID    "ssid"
#define NVS_KEY_PASS    "password"

esp_err_t egos_nvs_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition needs erase, reinitializing");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "NVS initialized");
    } else {
        ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t egos_nvs_save_wifi_creds(const char *ssid, const char *password)
{
    if (!ssid || !password) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = nvs_set_str(handle, NVS_KEY_SSID, ssid);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save SSID: %s", esp_err_to_name(ret));
        nvs_close(handle);
        return ret;
    }

    ret = nvs_set_str(handle, NVS_KEY_PASS, password);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save password: %s", esp_err_to_name(ret));
        nvs_close(handle);
        return ret;
    }

    ret = nvs_commit(handle);
    nvs_close(handle);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "WiFi credentials saved (SSID: %s)", ssid);
    }
    return ret;
}

esp_err_t egos_nvs_load_wifi_creds(char *ssid_buf, size_t ssid_len,
                                    char *pass_buf, size_t pass_len)
{
    if (!ssid_buf || !pass_buf) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ret != ESP_OK) {
        return ESP_ERR_NOT_FOUND;
    }

    size_t required_len = ssid_len;
    ret = nvs_get_str(handle, NVS_KEY_SSID, ssid_buf, &required_len);
    if (ret != ESP_OK) {
        nvs_close(handle);
        return ESP_ERR_NOT_FOUND;
    }

    required_len = pass_len;
    ret = nvs_get_str(handle, NVS_KEY_PASS, pass_buf, &required_len);
    if (ret != ESP_OK) {
        nvs_close(handle);
        return ESP_ERR_NOT_FOUND;
    }

    nvs_close(handle);
    ESP_LOGI(TAG, "WiFi credentials loaded (SSID: %s)", ssid_buf);
    return ESP_OK;
}

esp_err_t egos_nvs_clear_wifi_creds(void)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = nvs_erase_all(handle);
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }

    nvs_close(handle);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "WiFi credentials cleared");
    }
    return ret;
}

bool egos_nvs_has_wifi_creds(void)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ret != ESP_OK) {
        return false;
    }

    size_t required_len = 0;
    ret = nvs_get_str(handle, NVS_KEY_SSID, NULL, &required_len);
    nvs_close(handle);

    return (ret == ESP_OK && required_len > 0);
}
