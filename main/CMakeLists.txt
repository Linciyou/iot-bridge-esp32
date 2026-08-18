idf_component_register(SRCS "iot_bridge.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_event esp_netif esp_wifi esp_http_client nvs_flash)
