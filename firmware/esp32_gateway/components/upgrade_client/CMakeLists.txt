idf_component_register(SRCS "upgrade_client.c"
                       INCLUDE_DIRS "include"
                       REQUIRES
                           gateway_config
                           gateway_log
                           transport_uart
                           upgrade_protocol
                       PRIV_REQUIRES reliability_test)
