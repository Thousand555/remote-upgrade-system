#include "test_support.h"

#include "crc16_modbus.h"
#include "modbus_rtu.h"
#include "upgrade_protocol.h"

static void test_recalculate_crc(uint8_t *adu, size_t adu_length)
{
    uint16_t crc;

    crc = crc16_modbus_calculate(adu, adu_length - 2U);
    adu[adu_length - 2U] = (uint8_t)(crc & 0xFFU);
    adu[adu_length - 1U] = (uint8_t)((crc >> 8U) & 0xFFU);
}

bool test_upgrade_protocol_suite(void)
{
    static const upgrade_subfunction_t subfunctions[] =
    {
        UPG_SUB_HELLO,
        UPG_SUB_GET_INFO,
        UPG_SUB_ENTER_BOOT,
        UPG_SUB_START,
        UPG_SUB_ERASE,
        UPG_SUB_DATA,
        UPG_SUB_QUERY_PROGRESS,
        UPG_SUB_VERIFY,
        UPG_SUB_ACTIVATE,
        UPG_SUB_ABORT,
        UPG_SUB_GET_LOG
    };
    static const uint8_t expected_hello_request[] =
    {
        0x01U, 0x41U, 0x01U, 0x01U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x31U, 0x95U
    };
    upgrade_data_ack_t ack;
    upgrade_data_ack_t decoded_ack;
    upgrade_hello_response_t hello;
    upgrade_hello_response_t decoded_hello;
    upgrade_device_info_t info;
    upgrade_device_info_t decoded_info;
    upgrade_start_manifest_t manifest;
    upgrade_start_manifest_t decoded_manifest;
    upgrade_progress_t progress;
    upgrade_progress_t decoded_progress;
    upgrade_message_t decoded;
    upgrade_message_t message;
    protocol_status_t status;
    uint8_t ack_data[UPGRADE_DATA_ACK_SIZE];
    uint8_t payload_data[UPGRADE_START_MANIFEST_SIZE];
    uint8_t address;
    uint8_t adu[MODBUS_RTU_MAX_ADU_SIZE];
    size_t adu_length;
    size_t index;
    bool is_final;

    upgrade_message_init(&message, UPG_SUB_DATA);
    message.session_id = 0x12345678UL;
    message.sequence = 0x01020304UL;
    message.offset_or_next_offset = 0x00001000UL;
    message.payload_length = UPGRADE_MAX_PAYLOAD_SIZE;
    for (index = 0U; index < message.payload_length; index++)
    {
        message.payload[index] = (uint8_t)(index + 3U);
    }

    status = upgrade_encode_request(7U,
                                    &message,
                                    adu,
                                    sizeof(adu),
                                    &adu_length);
    TEST_CHECK_EQ(PROTOCOL_OK, status);
    TEST_CHECK_EQ(246U, adu_length);
    TEST_CHECK_EQ(7U, adu[0]);
    TEST_CHECK_EQ(UPGRADE_FUNCTION_CODE, adu[1]);
    TEST_CHECK_EQ(0x78U, adu[6]);
    TEST_CHECK_EQ(0x56U, adu[7]);
    TEST_CHECK_EQ(0x34U, adu[8]);
    TEST_CHECK_EQ(0x12U, adu[9]);
    TEST_CHECK_EQ(0x04U, adu[10]);
    TEST_CHECK_EQ(0x03U, adu[11]);
    TEST_CHECK_EQ(0x02U, adu[12]);
    TEST_CHECK_EQ(0x01U, adu[13]);
    TEST_CHECK_EQ(0x00U, adu[14]);
    TEST_CHECK_EQ(0x10U, adu[15]);
    TEST_CHECK_EQ(0xE0U, adu[18]);
    TEST_CHECK_EQ(0x00U, adu[19]);

    TEST_CHECK_EQ(PROTOCOL_OK,
                  upgrade_decode_request(adu,
                                         adu_length,
                                         &address,
                                         &decoded));
    TEST_CHECK_EQ(7U, address);
    TEST_CHECK_EQ(message.subfunction, decoded.subfunction);
    TEST_CHECK_EQ(message.session_id, decoded.session_id);
    TEST_CHECK_EQ(message.sequence, decoded.sequence);
    TEST_CHECK_EQ(message.offset_or_next_offset,
                  decoded.offset_or_next_offset);
    TEST_CHECK_EQ(message.payload_length, decoded.payload_length);
    TEST_CHECK(test_memory_equal(message.payload,
                                 decoded.payload,
                                 message.payload_length));

    for (index = 0U;
         index < (sizeof(subfunctions) / sizeof(subfunctions[0]));
         index++)
    {
        upgrade_message_init(&message, subfunctions[index]);
        TEST_CHECK_EQ(PROTOCOL_OK,
                      upgrade_encode_request(1U,
                                             &message,
                                             adu,
                                             sizeof(adu),
                                             &adu_length));
        TEST_CHECK_EQ(PROTOCOL_OK,
                      upgrade_decode_request(adu,
                                             adu_length,
                                             &address,
                                             &decoded));
        TEST_CHECK_EQ(subfunctions[index], decoded.subfunction);
    }

    /* Exercise every legal upgrade Payload length from 0 through 224. */
    for (index = 0U; index <= UPGRADE_MAX_PAYLOAD_SIZE; index++)
    {
        size_t payload_index;

        upgrade_message_init(&message, UPG_SUB_DATA);
        message.payload_length = (uint16_t)index;
        for (payload_index = 0U; payload_index < index; payload_index++)
        {
            message.payload[payload_index] =
                (uint8_t)(payload_index ^ index);
        }

        TEST_CHECK_EQ(PROTOCOL_OK,
                      upgrade_encode_request(1U,
                                             &message,
                                             adu,
                                             sizeof(adu),
                                             &adu_length));
        TEST_CHECK_EQ(index + UPGRADE_MESSAGE_HEADER_SIZE + 4U,
                      adu_length);
        TEST_CHECK_EQ(PROTOCOL_OK,
                      upgrade_decode_request(adu,
                                             adu_length,
                                             &address,
                                             &decoded));
        TEST_CHECK_EQ(index, decoded.payload_length);
        TEST_CHECK(test_memory_equal(message.payload,
                                     decoded.payload,
                                     index));
    }

    upgrade_message_init(&message, UPG_SUB_DATA);
    message.payload_length = UPGRADE_MAX_PAYLOAD_SIZE + 1U;
    TEST_CHECK_EQ(PROTOCOL_PAYLOAD_TOO_LARGE,
                  upgrade_encode_request(1U,
                                         &message,
                                         adu,
                                         sizeof(adu),
                                         &adu_length));
    message.payload_length = 0U;
    message.flags_or_status = 1U;
    TEST_CHECK_EQ(PROTOCOL_FLAGS_ERROR,
                  upgrade_encode_request(1U,
                                         &message,
                                         adu,
                                         sizeof(adu),
                                         &adu_length));
    message.flags_or_status = 0U;
    message.subfunction = (upgrade_subfunction_t)0x7FU;
    TEST_CHECK_EQ(PROTOCOL_SUBFUNCTION_ERROR,
                  upgrade_encode_request(1U,
                                         &message,
                                         adu,
                                         sizeof(adu),
                                         &adu_length));

    upgrade_message_init(&message, UPG_SUB_HELLO);
    TEST_CHECK_EQ(PROTOCOL_BUFFER_TOO_SMALL,
                  upgrade_encode_request(1U,
                                         &message,
                                         adu,
                                         21U,
                                         &adu_length));
    TEST_CHECK_EQ(PROTOCOL_OK,
                  upgrade_encode_request(1U,
                                         &message,
                                         adu,
                                         sizeof(adu),
                                         &adu_length));
    TEST_CHECK_EQ(sizeof(expected_hello_request), adu_length);
    TEST_CHECK(test_memory_equal(expected_hello_request,
                                 adu,
                                 adu_length));
    adu[3] = 2U;
    test_recalculate_crc(adu, adu_length);
    TEST_CHECK_EQ(PROTOCOL_VERSION_ERROR,
                  upgrade_decode_request(adu,
                                         adu_length,
                                         &address,
                                         &decoded));
    adu[3] = UPGRADE_PROTOCOL_VERSION;
    adu[18] = 1U;
    test_recalculate_crc(adu, adu_length);
    TEST_CHECK_EQ(PROTOCOL_PAYLOAD_LENGTH_MISMATCH,
                  upgrade_decode_request(adu,
                                         adu_length,
                                         &address,
                                         &decoded));
    adu[18] = 0U;
    adu[1] = 0x42U;
    test_recalculate_crc(adu, adu_length);
    TEST_CHECK_EQ(PROTOCOL_FUNCTION_ERROR,
                  upgrade_decode_request(adu,
                                         adu_length,
                                         &address,
                                         &decoded));

    upgrade_message_init(&message, UPG_SUB_QUERY_PROGRESS);
    message.flags_or_status = (uint16_t)UPG_STATUS_BUSY;
    TEST_CHECK_EQ(PROTOCOL_OK,
                  upgrade_encode_response(2U,
                                          &message,
                                          adu,
                                          sizeof(adu),
                                          &adu_length));
    TEST_CHECK_EQ(PROTOCOL_OK,
                  upgrade_decode_response(adu,
                                          adu_length,
                                          &address,
                                          &decoded));
    TEST_CHECK_EQ(UPG_STATUS_BUSY, decoded.flags_or_status);
    adu[4] = 0x00U;
    adu[5] = 0x01U;
    test_recalculate_crc(adu, adu_length);
    TEST_CHECK_EQ(PROTOCOL_STATUS_CODE_ERROR,
                  upgrade_decode_response(adu,
                                          adu_length,
                                          &address,
                                          &decoded));
    message.flags_or_status = 0xFFFFU;
    TEST_CHECK_EQ(PROTOCOL_STATUS_CODE_ERROR,
                  upgrade_encode_response(2U,
                                          &message,
                                          adu,
                                          sizeof(adu),
                                          &adu_length));

    ack.status = UPG_STATUS_OK;
    ack.reserved = 0U;
    ack.accepted_sequence = 0x11223344UL;
    ack.next_expected_offset = 0x00002000UL;
    TEST_CHECK_EQ(PROTOCOL_OK,
                  upgrade_data_ack_encode(&ack,
                                          ack_data,
                                          sizeof(ack_data)));
    TEST_CHECK_EQ(0x44U, ack_data[4]);
    TEST_CHECK_EQ(0x33U, ack_data[5]);
    TEST_CHECK_EQ(PROTOCOL_OK,
                  upgrade_data_ack_decode(ack_data,
                                          sizeof(ack_data),
                                          &decoded_ack));
    TEST_CHECK_EQ(ack.status, decoded_ack.status);
    TEST_CHECK_EQ(ack.accepted_sequence,
                  decoded_ack.accepted_sequence);
    TEST_CHECK_EQ(ack.next_expected_offset,
                  decoded_ack.next_expected_offset);
    TEST_CHECK_EQ(PROTOCOL_BUFFER_TOO_SMALL,
                  upgrade_data_ack_encode(&ack, ack_data, 11U));
    TEST_CHECK_EQ(PROTOCOL_LENGTH_ERROR,
                  upgrade_data_ack_decode(ack_data, 11U, &decoded_ack));
    ack_data[0] = 0x00U;
    ack_data[1] = 0x01U;
    TEST_CHECK_EQ(PROTOCOL_STATUS_CODE_ERROR,
                  upgrade_data_ack_decode(ack_data,
                                          sizeof(ack_data),
                                          &decoded_ack));
    ack.status = (upgrade_status_t)0xFFFFU;
    TEST_CHECK_EQ(PROTOCOL_STATUS_CODE_ERROR,
                  upgrade_data_ack_encode(&ack,
                                          ack_data,
                                          sizeof(ack_data)));

    hello.capabilities = 0x000D;
    hello.max_payload_size = UPGRADE_MAX_PAYLOAD_SIZE;
    hello.service_version = 0x00010000UL;
    TEST_CHECK_EQ(PROTOCOL_OK,
                  upgrade_hello_response_encode(&hello,
                                                payload_data,
                                                sizeof(payload_data)));
    TEST_CHECK_EQ(PROTOCOL_OK,
                  upgrade_hello_response_decode(payload_data,
                                                UPGRADE_HELLO_RESPONSE_SIZE,
                                                &decoded_hello));
    TEST_CHECK_EQ(hello.capabilities, decoded_hello.capabilities);
    TEST_CHECK_EQ(hello.max_payload_size, decoded_hello.max_payload_size);
    TEST_CHECK_EQ(hello.service_version, decoded_hello.service_version);

    info.product_id = 0x1122U;
    info.hardware_id = 0x3344U;
    info.bootloader_version = 0x01020304UL;
    info.application_version = 7U;
    info.application_base = 0x08020000UL;
    info.application_max_size = UPGRADE_MAX_IMAGE_SIZE;
    info.boot_state = 4U;
    info.capabilities = 0x000DU;
    TEST_CHECK_EQ(PROTOCOL_OK,
                  upgrade_device_info_encode(&info,
                                             payload_data,
                                             sizeof(payload_data)));
    TEST_CHECK_EQ(PROTOCOL_OK,
                  upgrade_device_info_decode(payload_data,
                                             UPGRADE_DEVICE_INFO_SIZE,
                                             &decoded_info));
    TEST_CHECK_EQ(info.product_id, decoded_info.product_id);
    TEST_CHECK_EQ(info.hardware_id, decoded_info.hardware_id);
    TEST_CHECK_EQ(info.application_base, decoded_info.application_base);
    TEST_CHECK_EQ(info.application_max_size,
                  decoded_info.application_max_size);
    TEST_CHECK_EQ(info.boot_state, decoded_info.boot_state);

    manifest.firmware_version = 9U;
    manifest.image_size = 12345U;
    manifest.image_crc32 = 0x89ABCDEFUL;
    manifest.product_id = 0x1122U;
    manifest.hardware_id = 0x3344U;
    for (index = 0U; index < UPGRADE_SHA256_SIZE; index++)
    {
        manifest.image_sha256[index] = (uint8_t)(index ^ 0x5AU);
    }
    TEST_CHECK_EQ(PROTOCOL_OK,
                  upgrade_start_manifest_encode(&manifest,
                                                payload_data,
                                                sizeof(payload_data)));
    TEST_CHECK_EQ(PROTOCOL_OK,
                  upgrade_start_manifest_decode(payload_data,
                                                UPGRADE_START_MANIFEST_SIZE,
                                                &decoded_manifest));
    TEST_CHECK_EQ(manifest.firmware_version,
                  decoded_manifest.firmware_version);
    TEST_CHECK_EQ(manifest.image_size, decoded_manifest.image_size);
    TEST_CHECK_EQ(manifest.image_crc32, decoded_manifest.image_crc32);
    TEST_CHECK(test_memory_equal(manifest.image_sha256,
                                 decoded_manifest.image_sha256,
                                 UPGRADE_SHA256_SIZE));
    TEST_CHECK_EQ(manifest.product_id, decoded_manifest.product_id);
    TEST_CHECK_EQ(manifest.hardware_id, decoded_manifest.hardware_id);

    progress.boot_state = 5U;
    progress.reserved = 0U;
    progress.received_bytes = 4096U;
    progress.image_size = 8192U;
    progress.error_code = 0x1234U;
    TEST_CHECK_EQ(PROTOCOL_OK,
                  upgrade_progress_encode(&progress,
                                          payload_data,
                                          sizeof(payload_data)));
    TEST_CHECK_EQ(PROTOCOL_OK,
                  upgrade_progress_decode(payload_data,
                                          UPGRADE_PROGRESS_SIZE,
                                          &decoded_progress));
    TEST_CHECK_EQ(progress.boot_state, decoded_progress.boot_state);
    TEST_CHECK_EQ(progress.received_bytes,
                  decoded_progress.received_bytes);
    TEST_CHECK_EQ(progress.image_size, decoded_progress.image_size);
    TEST_CHECK_EQ(progress.error_code, decoded_progress.error_code);

    TEST_CHECK_EQ(PROTOCOL_BUFFER_TOO_SMALL,
                  upgrade_start_manifest_encode(&manifest,
                                                payload_data,
                                                UPGRADE_START_MANIFEST_SIZE - 1U));
    TEST_CHECK_EQ(PROTOCOL_LENGTH_ERROR,
                  upgrade_progress_decode(payload_data,
                                          UPGRADE_PROGRESS_SIZE - 1U,
                                          &decoded_progress));

    TEST_CHECK_EQ(UPG_DATA_ACCEPT_NEW,
                  upgrade_classify_data_offset(4096U, 4096U));
    TEST_CHECK_EQ(UPG_DATA_DUPLICATE,
                  upgrade_classify_data_offset(0U, 4096U));
    TEST_CHECK_EQ(UPG_DATA_REJECT_GAP,
                  upgrade_classify_data_offset(8192U, 4096U));

    TEST_CHECK_EQ(PROTOCOL_OK,
                  upgrade_validate_data_chunk(0U,
                                              224U,
                                              448U,
                                              &is_final));
    TEST_CHECK(!is_final);
    TEST_CHECK_EQ(PROTOCOL_OK,
                  upgrade_validate_data_chunk(448U,
                                              3U,
                                              451U,
                                              &is_final));
    TEST_CHECK(is_final);
    TEST_CHECK_EQ(PROTOCOL_ALIGNMENT_ERROR,
                  upgrade_validate_data_chunk(1U,
                                              4U,
                                              8U,
                                              &is_final));
    TEST_CHECK_EQ(PROTOCOL_ALIGNMENT_ERROR,
                  upgrade_validate_data_chunk(0U,
                                              3U,
                                              8U,
                                              &is_final));
    TEST_CHECK_EQ(PROTOCOL_PAYLOAD_TOO_LARGE,
                  upgrade_validate_data_chunk(0U,
                                              225U,
                                              1024U,
                                              &is_final));
    TEST_CHECK_EQ(PROTOCOL_PAYLOAD_TOO_LARGE,
                  upgrade_validate_data_chunk(0U,
                                              0U,
                                              1024U,
                                              &is_final));
    TEST_CHECK_EQ(PROTOCOL_RANGE_ERROR,
                  upgrade_validate_data_chunk(0U,
                                              4U,
                                              UPGRADE_MAX_IMAGE_SIZE + 1U,
                                              &is_final));
    TEST_CHECK_EQ(PROTOCOL_RANGE_ERROR,
                  upgrade_validate_data_chunk(0xFFFFFFFCUL,
                                              8U,
                                              UPGRADE_MAX_IMAGE_SIZE,
                                              &is_final));
    return true;
}
