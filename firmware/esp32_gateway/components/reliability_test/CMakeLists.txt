idf_component_register(SRCS "reliability_fault.c"
                       INCLUDE_DIRS "include"
                       REQUIRES
                           freertos
                           upgrade_protocol)
