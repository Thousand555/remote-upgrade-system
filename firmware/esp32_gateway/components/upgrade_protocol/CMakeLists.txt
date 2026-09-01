set(PROTOCOL_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../protocol")

idf_component_register(
    SRCS
        "${PROTOCOL_ROOT}/src/crc16_modbus.c"
        "${PROTOCOL_ROOT}/src/modbus_rtu.c"
        "${PROTOCOL_ROOT}/src/modbus_rtu_stream.c"
        "${PROTOCOL_ROOT}/src/protocol_byte_order.c"
        "${PROTOCOL_ROOT}/src/upgrade_protocol.c"
    INCLUDE_DIRS "${PROTOCOL_ROOT}/include")
