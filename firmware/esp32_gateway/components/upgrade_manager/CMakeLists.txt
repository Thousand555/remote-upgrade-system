idf_component_register(SRCS "upgrade_manager.c"
                       INCLUDE_DIRS "include"
                       REQUIRES
                           esp_system
                           esp_timer
                           firmware_store
                           freertos
                           gateway_config
                           gateway_log
                           upgrade_client
                       PRIV_REQUIRES reliability_test)
