idf_component_register(SRCS "gateway_console.c"
                       INCLUDE_DIRS "include"
                       REQUIRES
                           console
                           firmware_store
                           gateway_log
                           upgrade_manager
                       PRIV_REQUIRES
                           reliability_test
                           upgrade_protocol)
