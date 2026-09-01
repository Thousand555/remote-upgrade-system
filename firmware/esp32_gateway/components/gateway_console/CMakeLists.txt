idf_component_register(SRCS "gateway_console.c"
                       INCLUDE_DIRS "include"
                       REQUIRES
                           console
                           firmware_downloader
                           firmware_store
                           gateway_log
                           gateway_wifi
                           upgrade_manager
                       PRIV_REQUIRES
                           reliability_test
                           upgrade_protocol)
