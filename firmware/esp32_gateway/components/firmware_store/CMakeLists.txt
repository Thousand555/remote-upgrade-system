idf_component_register(SRCS "firmware_store.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_partition gateway_config gateway_log)
