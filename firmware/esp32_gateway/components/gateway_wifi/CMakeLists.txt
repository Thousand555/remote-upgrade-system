idf_component_register(SRCS "gateway_wifi.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_event esp_netif esp_wifi freertos gateway_config gateway_log nvs_flash)
