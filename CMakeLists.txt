set(srcs
    "src/egos_sdk.c"
    "src/egos_nvs.c"
    "src/egos_wifi.c"
    "src/egos_mqtt.c"
    "src/egos_connection.c"
)

set(requires
    nvs_flash
    esp_wifi
    esp_event
    esp_timer
    json
    mdns
    mqtt
    esp_netif
)

if(CONFIG_EGOS_ETHERNET_ENABLED)
    list(APPEND srcs "src/egos_ethernet.c")
    list(APPEND requires esp_eth)
endif()

if(CONFIG_EGOS_STATUS_LED_ENABLED)
    list(APPEND srcs "src/egos_led.c")
    list(APPEND requires esp_driver_ledc)
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src"
    REQUIRES ${requires}
)
