/**
 * @file main.c
 * @brief EGOS SDK Example — Basic Buttons
 *
 * Demonstrates a simple EGOS module with 4 buttons and 1 LED.
 * Buttons publish press/release events. The LED is controlled
 * via output commands from the EGOS controller.
 *
 * Hardware:
 *   - 4 buttons on GPIO 4, 5, 6, 7 (active low with internal pull-up)
 *   - 1 LED on GPIO 8 (active high)
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "egos_sdk.h"

static const char *TAG = "example";

/* --------------------------------------------------------------------------
 * Hardware Configuration
 * -------------------------------------------------------------------------- */

#define NUM_BUTTONS     4
#define LED_GPIO        8
#define DEBOUNCE_MS     50
#define SCAN_INTERVAL   20

static const int BUTTON_GPIOS[NUM_BUTTONS] = {4, 5, 6, 7};
static bool button_states[NUM_BUTTONS] = {false};
static uint32_t button_debounce[NUM_BUTTONS] = {0};

/* --------------------------------------------------------------------------
 * Device Definitions
 * -------------------------------------------------------------------------- */

static const egos_device_t devices[] = {
    EGOS_DEVICE("button1", "button", "Button 1", NULL),
    EGOS_DEVICE("button2", "button", "Button 2", NULL),
    EGOS_DEVICE("button3", "button", "Button 3", NULL),
    EGOS_DEVICE("button4", "button", "Button 4", NULL),
    EGOS_DEVICE("led1",    "led",    "Status LED", NULL),
};

#define DEVICE_COUNT (sizeof(devices) / sizeof(devices[0]))

/* --------------------------------------------------------------------------
 * Output Command Handler
 * -------------------------------------------------------------------------- */

/**
 * Handle output commands from the EGOS controller.
 * This is called when the controller sends a command to one of our devices.
 */
static void on_output_command(const char *device_id, const char *command,
                               const cJSON *params, void *user_data)
{
    ESP_LOGI(TAG, "Command: device=%s, command=%s", device_id, command);

    /* Handle LED setState command */
    if (strcmp(device_id, "led1") == 0 && strcmp(command, "setState") == 0) {
        if (params) {
            cJSON *state = cJSON_GetObjectItem(params, "state");
            if (cJSON_IsString(state)) {
                bool on = (strcmp(state->valuestring, "on") == 0);
                gpio_set_level(LED_GPIO, on ? 1 : 0);
                ESP_LOGI(TAG, "LED %s", on ? "ON" : "OFF");
            }
        }
    }
}

/**
 * Called when the module connects to the MQTT broker.
 */
static void on_connected(void *user_data)
{
    ESP_LOGI(TAG, "Connected to EGOS! Module ID: %s", egos_get_module_id());
}

/* --------------------------------------------------------------------------
 * Button Scanning Task
 * -------------------------------------------------------------------------- */

static void button_scan_task(void *pvParameters)
{
    char device_id[16];
    char params[32];

    while (1) {
        for (int i = 0; i < NUM_BUTTONS; i++) {
            bool pressed = (gpio_get_level(BUTTON_GPIOS[i]) == 0);  /* Active low */

            /* Debounce */
            if (pressed != button_states[i]) {
                if (button_debounce[i] == 0) {
                    button_debounce[i] = xTaskGetTickCount();
                }

                uint32_t elapsed = (xTaskGetTickCount() - button_debounce[i])
                                   * portTICK_PERIOD_MS;
                if (elapsed >= DEBOUNCE_MS) {
                    button_states[i] = pressed;
                    button_debounce[i] = 0;

                    /* Publish button event */
                    snprintf(device_id, sizeof(device_id), "button%d", i + 1);
                    snprintf(params, sizeof(params), "{\"pressed\":%s}",
                             pressed ? "true" : "false");
                    egos_publish_input(device_id, "changed", params);

                    ESP_LOGI(TAG, "%s %s", device_id, pressed ? "PRESSED" : "RELEASED");
                }
            } else {
                button_debounce[i] = 0;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(SCAN_INTERVAL));
    }
}

/* --------------------------------------------------------------------------
 * GPIO Initialization
 * -------------------------------------------------------------------------- */

static void init_gpio(void)
{
    /* Configure button GPIOs as input with pull-up */
    for (int i = 0; i < NUM_BUTTONS; i++) {
        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << BUTTON_GPIOS[i]),
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&io_conf);
    }

    /* Configure LED GPIO as output */
    gpio_config_t led_conf = {
        .pin_bit_mask = (1ULL << LED_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&led_conf);
    gpio_set_level(LED_GPIO, 0);
}

/* --------------------------------------------------------------------------
 * Main
 * -------------------------------------------------------------------------- */

void app_main(void)
{
    ESP_LOGI(TAG, "EGOS Basic Buttons Example");

    /* Initialize hardware */
    init_gpio();

    /* Configure EGOS SDK */
    egos_config_t config = EGOS_CONFIG_DEFAULT();
    config.module_prefix = "basic-buttons";
    config.devices = devices;
    config.device_count = DEVICE_COUNT;
    config.on_output_command = on_output_command;
    config.on_connected = on_connected;

    /* Initialize and start the SDK */
    ESP_ERROR_CHECK(egos_init(&config));
    ESP_ERROR_CHECK(egos_start());

    /* Start button scanning task */
    xTaskCreate(button_scan_task, "btn_scan", 2048, NULL, 5, NULL);

    ESP_LOGI(TAG, "Module started, scanning buttons...");
}
