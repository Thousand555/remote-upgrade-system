idf_component_register(SRCS "transport_uart.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_driver_uart esp_timer freertos gateway_config gateway_log)
