idf_component_register(SRCS "firmware_downloader.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_http_client firmware_store freertos gateway_config gateway_log gateway_wifi json mbedtls nvs_flash)
