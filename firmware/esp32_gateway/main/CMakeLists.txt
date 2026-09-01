idf_component_register(SRCS "esp32_gateway.c"
                    INCLUDE_DIRS "."
                    REQUIRES
                        esp_psram
                        esp_system
                        firmware_store
                        gateway_console
                        gateway_log
                        heap
                        nvs_flash
                        spi_flash
                        transport_uart
                        upgrade_manager)
