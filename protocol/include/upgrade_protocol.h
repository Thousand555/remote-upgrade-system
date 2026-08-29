#ifndef UPGRADE_PROTOCOL_H
#define UPGRADE_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "protocol_status.h"
#include "upgrade_commands.h"

#define UPGRADE_FUNCTION_CODE              0x41U
#define UPGRADE_EXCEPTION_FUNCTION_CODE    0xC1U
#define UPGRADE_PROTOCOL_VERSION           1U
#define UPGRADE_SUPPORTED_REQUEST_FLAGS    0x0000U
#define UPGRADE_MESSAGE_HEADER_SIZE        18U
#define UPGRADE_MAX_PAYLOAD_SIZE           224U
#define UPGRADE_DATA_ACK_SIZE              12U
#define UPGRADE_HELLO_RESPONSE_SIZE         8U
#define UPGRADE_DEVICE_INFO_SIZE            24U
#define UPGRADE_START_MANIFEST_SIZE         48U
#define UPGRADE_PROGRESS_SIZE               16U
#define UPGRADE_SHA256_SIZE                 32U
#define UPGRADE_MAX_IMAGE_SIZE             0x000E0000UL
#define UPGRADE_PROGRAM_WORD_SIZE          4U

typedef struct
{
    upgrade_subfunction_t subfunction;
    uint8_t protocol_version;
    uint16_t flags_or_status;
    uint32_t session_id;
    uint32_t sequence;
    uint32_t offset_or_next_offset;
    uint16_t payload_length;
    uint8_t payload[UPGRADE_MAX_PAYLOAD_SIZE];
} upgrade_message_t;

typedef struct
{
    upgrade_status_t status;
    uint16_t reserved;
    uint32_t accepted_sequence;
    uint32_t next_expected_offset;
} upgrade_data_ack_t;

typedef struct
{
    uint16_t capabilities;
    uint16_t max_payload_size;
    uint32_t service_version;
} upgrade_hello_response_t;

typedef struct
{
    uint16_t product_id;
    uint16_t hardware_id;
    uint32_t bootloader_version;
    uint32_t application_version;
    uint32_t application_base;
    uint32_t application_max_size;
    uint16_t boot_state;
    uint16_t capabilities;
} upgrade_device_info_t;

typedef struct
{
    uint32_t firmware_version;
    uint32_t image_size;
    uint32_t image_crc32;
    uint8_t image_sha256[UPGRADE_SHA256_SIZE];
    uint16_t product_id;
    uint16_t hardware_id;
} upgrade_start_manifest_t;

typedef struct
{
    uint16_t boot_state;
    uint16_t reserved;
    uint32_t received_bytes;
    uint32_t image_size;
    uint32_t error_code;
} upgrade_progress_t;

void upgrade_message_init(upgrade_message_t *message,
                          upgrade_subfunction_t subfunction);

bool upgrade_subfunction_is_valid(upgrade_subfunction_t subfunction);
bool upgrade_status_is_valid(upgrade_status_t status);

protocol_status_t upgrade_encode_request(
    uint8_t address,
    const upgrade_message_t *message,
    uint8_t *output,
    size_t output_capacity,
    size_t *output_length);

protocol_status_t upgrade_decode_request(
    const uint8_t *input,
    size_t input_length,
    uint8_t *address,
    upgrade_message_t *message);

protocol_status_t upgrade_encode_response(
    uint8_t address,
    const upgrade_message_t *message,
    uint8_t *output,
    size_t output_capacity,
    size_t *output_length);

protocol_status_t upgrade_decode_response(
    const uint8_t *input,
    size_t input_length,
    uint8_t *address,
    upgrade_message_t *message);

protocol_status_t upgrade_data_ack_encode(
    const upgrade_data_ack_t *ack,
    uint8_t *output,
    size_t output_capacity);

protocol_status_t upgrade_data_ack_decode(
    const uint8_t *input,
    size_t input_length,
    upgrade_data_ack_t *ack);

protocol_status_t upgrade_hello_response_encode(
    const upgrade_hello_response_t *hello,
    uint8_t *output,
    size_t output_capacity);

protocol_status_t upgrade_hello_response_decode(
    const uint8_t *input,
    size_t input_length,
    upgrade_hello_response_t *hello);

protocol_status_t upgrade_device_info_encode(
    const upgrade_device_info_t *info,
    uint8_t *output,
    size_t output_capacity);

protocol_status_t upgrade_device_info_decode(
    const uint8_t *input,
    size_t input_length,
    upgrade_device_info_t *info);

protocol_status_t upgrade_start_manifest_encode(
    const upgrade_start_manifest_t *manifest,
    uint8_t *output,
    size_t output_capacity);

protocol_status_t upgrade_start_manifest_decode(
    const uint8_t *input,
    size_t input_length,
    upgrade_start_manifest_t *manifest);

protocol_status_t upgrade_progress_encode(
    const upgrade_progress_t *progress,
    uint8_t *output,
    size_t output_capacity);

protocol_status_t upgrade_progress_decode(
    const uint8_t *input,
    size_t input_length,
    upgrade_progress_t *progress);

upgrade_data_action_t upgrade_classify_data_offset(
    uint32_t received_offset,
    uint32_t next_expected_offset);

protocol_status_t upgrade_validate_data_chunk(
    uint32_t offset,
    uint16_t payload_length,
    uint32_t image_size,
    bool *is_final_chunk);

#endif /* UPGRADE_PROTOCOL_H */
