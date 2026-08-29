#include "upgrade_protocol.h"

#include "modbus_rtu.h"
#include "protocol_byte_order.h"

static protocol_status_t upgrade_validate_message(
    const upgrade_message_t *message,
    bool response)
{
    if (message == NULL)
    {
        return PROTOCOL_NULL_ARGUMENT;
    }

    if (message->protocol_version != UPGRADE_PROTOCOL_VERSION)
    {
        return PROTOCOL_VERSION_ERROR;
    }

    if (!upgrade_subfunction_is_valid(message->subfunction))
    {
        return PROTOCOL_SUBFUNCTION_ERROR;
    }

    if (message->payload_length > UPGRADE_MAX_PAYLOAD_SIZE)
    {
        return PROTOCOL_PAYLOAD_TOO_LARGE;
    }

    if (response)
    {
        /* Validate the raw wire width before any potentially narrow enum cast. */
        if (message->flags_or_status > (uint16_t)UPG_STATUS_TIMEOUT)
        {
            return PROTOCOL_STATUS_CODE_ERROR;
        }
    }
    else if ((message->flags_or_status &
              (uint16_t)(~UPGRADE_SUPPORTED_REQUEST_FLAGS)) != 0U)
    {
        return PROTOCOL_FLAGS_ERROR;
    }

    return PROTOCOL_OK;
}

static protocol_status_t upgrade_encode(
    uint8_t address,
    const upgrade_message_t *message,
    bool response,
    uint8_t *output,
    size_t output_capacity,
    size_t *output_length)
{
    modbus_rtu_frame_t frame;
    protocol_status_t status;
    size_t index;

    if ((output == NULL) || (output_length == NULL))
    {
        return PROTOCOL_NULL_ARGUMENT;
    }

    status = upgrade_validate_message(message, response);
    if (status != PROTOCOL_OK)
    {
        return status;
    }

    frame.address = address;
    frame.function = UPGRADE_FUNCTION_CODE;
    frame.data_length = UPGRADE_MESSAGE_HEADER_SIZE +
                        message->payload_length;
    frame.data[0] = (uint8_t)message->subfunction;
    frame.data[1] = message->protocol_version;
    protocol_write_le16(&frame.data[2], message->flags_or_status);
    protocol_write_le32(&frame.data[4], message->session_id);
    protocol_write_le32(&frame.data[8], message->sequence);
    protocol_write_le32(&frame.data[12],
                        message->offset_or_next_offset);
    protocol_write_le16(&frame.data[16], message->payload_length);

    for (index = 0U; index < message->payload_length; index++)
    {
        frame.data[UPGRADE_MESSAGE_HEADER_SIZE + index] =
            message->payload[index];
    }

    return modbus_rtu_encode(&frame,
                             output,
                             output_capacity,
                             output_length);
}

static protocol_status_t upgrade_decode(
    const uint8_t *input,
    size_t input_length,
    bool response,
    uint8_t *address,
    upgrade_message_t *message)
{
    modbus_rtu_frame_t frame;
    protocol_status_t status;
    uint16_t payload_length;
    size_t index;

    if ((input == NULL) || (address == NULL) || (message == NULL))
    {
        return PROTOCOL_NULL_ARGUMENT;
    }

    status = modbus_rtu_decode(input, input_length, &frame);
    if (status != PROTOCOL_OK)
    {
        return status;
    }

    if (frame.function != UPGRADE_FUNCTION_CODE)
    {
        return PROTOCOL_FUNCTION_ERROR;
    }

    if (frame.data_length < UPGRADE_MESSAGE_HEADER_SIZE)
    {
        return PROTOCOL_LENGTH_ERROR;
    }

    payload_length = protocol_read_le16(&frame.data[16]);
    if (payload_length > UPGRADE_MAX_PAYLOAD_SIZE)
    {
        return PROTOCOL_PAYLOAD_TOO_LARGE;
    }

    if (frame.data_length !=
        (UPGRADE_MESSAGE_HEADER_SIZE + (size_t)payload_length))
    {
        return PROTOCOL_PAYLOAD_LENGTH_MISMATCH;
    }

    message->subfunction = (upgrade_subfunction_t)frame.data[0];
    message->protocol_version = frame.data[1];
    message->flags_or_status = protocol_read_le16(&frame.data[2]);
    message->session_id = protocol_read_le32(&frame.data[4]);
    message->sequence = protocol_read_le32(&frame.data[8]);
    message->offset_or_next_offset = protocol_read_le32(&frame.data[12]);
    message->payload_length = payload_length;

    for (index = 0U; index < payload_length; index++)
    {
        message->payload[index] =
            frame.data[UPGRADE_MESSAGE_HEADER_SIZE + index];
    }

    status = upgrade_validate_message(message, response);
    if (status != PROTOCOL_OK)
    {
        return status;
    }

    *address = frame.address;
    return PROTOCOL_OK;
}

void upgrade_message_init(upgrade_message_t *message,
                          upgrade_subfunction_t subfunction)
{
    size_t index;

    if (message == NULL)
    {
        return;
    }

    message->subfunction = subfunction;
    message->protocol_version = UPGRADE_PROTOCOL_VERSION;
    message->flags_or_status = UPGRADE_SUPPORTED_REQUEST_FLAGS;
    message->session_id = 0U;
    message->sequence = 0U;
    message->offset_or_next_offset = 0U;
    message->payload_length = 0U;

    for (index = 0U; index < UPGRADE_MAX_PAYLOAD_SIZE; index++)
    {
        message->payload[index] = 0U;
    }
}

bool upgrade_subfunction_is_valid(upgrade_subfunction_t subfunction)
{
    switch (subfunction)
    {
        case UPG_SUB_HELLO:
        case UPG_SUB_GET_INFO:
        case UPG_SUB_ENTER_BOOT:
        case UPG_SUB_START:
        case UPG_SUB_ERASE:
        case UPG_SUB_DATA:
        case UPG_SUB_QUERY_PROGRESS:
        case UPG_SUB_VERIFY:
        case UPG_SUB_ACTIVATE:
        case UPG_SUB_ABORT:
        case UPG_SUB_GET_LOG:
            return true;

        default:
            return false;
    }
}

bool upgrade_status_is_valid(upgrade_status_t status)
{
    return (uint32_t)status <= (uint32_t)UPG_STATUS_TIMEOUT;
}

protocol_status_t upgrade_encode_request(
    uint8_t address,
    const upgrade_message_t *message,
    uint8_t *output,
    size_t output_capacity,
    size_t *output_length)
{
    return upgrade_encode(address,
                          message,
                          false,
                          output,
                          output_capacity,
                          output_length);
}

protocol_status_t upgrade_decode_request(
    const uint8_t *input,
    size_t input_length,
    uint8_t *address,
    upgrade_message_t *message)
{
    return upgrade_decode(input,
                          input_length,
                          false,
                          address,
                          message);
}

protocol_status_t upgrade_encode_response(
    uint8_t address,
    const upgrade_message_t *message,
    uint8_t *output,
    size_t output_capacity,
    size_t *output_length)
{
    return upgrade_encode(address,
                          message,
                          true,
                          output,
                          output_capacity,
                          output_length);
}

protocol_status_t upgrade_decode_response(
    const uint8_t *input,
    size_t input_length,
    uint8_t *address,
    upgrade_message_t *message)
{
    return upgrade_decode(input,
                          input_length,
                          true,
                          address,
                          message);
}

protocol_status_t upgrade_data_ack_encode(
    const upgrade_data_ack_t *ack,
    uint8_t *output,
    size_t output_capacity)
{
    if ((ack == NULL) || (output == NULL))
    {
        return PROTOCOL_NULL_ARGUMENT;
    }

    if (!upgrade_status_is_valid(ack->status))
    {
        return PROTOCOL_STATUS_CODE_ERROR;
    }

    if (output_capacity < UPGRADE_DATA_ACK_SIZE)
    {
        return PROTOCOL_BUFFER_TOO_SMALL;
    }

    protocol_write_le16(&output[0], (uint16_t)ack->status);
    protocol_write_le16(&output[2], ack->reserved);
    protocol_write_le32(&output[4], ack->accepted_sequence);
    protocol_write_le32(&output[8], ack->next_expected_offset);
    return PROTOCOL_OK;
}

protocol_status_t upgrade_data_ack_decode(
    const uint8_t *input,
    size_t input_length,
    upgrade_data_ack_t *ack)
{
    uint16_t raw_status;

    if ((input == NULL) || (ack == NULL))
    {
        return PROTOCOL_NULL_ARGUMENT;
    }

    if (input_length != UPGRADE_DATA_ACK_SIZE)
    {
        return PROTOCOL_LENGTH_ERROR;
    }

    raw_status = protocol_read_le16(&input[0]);
    if (raw_status > (uint16_t)UPG_STATUS_TIMEOUT)
    {
        return PROTOCOL_STATUS_CODE_ERROR;
    }

    ack->status = (upgrade_status_t)raw_status;
    ack->reserved = protocol_read_le16(&input[2]);
    ack->accepted_sequence = protocol_read_le32(&input[4]);
    ack->next_expected_offset = protocol_read_le32(&input[8]);
    return PROTOCOL_OK;
}

protocol_status_t upgrade_hello_response_encode(
    const upgrade_hello_response_t *hello,
    uint8_t *output,
    size_t output_capacity)
{
    if ((hello == NULL) || (output == NULL))
    {
        return PROTOCOL_NULL_ARGUMENT;
    }

    if (output_capacity < UPGRADE_HELLO_RESPONSE_SIZE)
    {
        return PROTOCOL_BUFFER_TOO_SMALL;
    }

    protocol_write_le16(&output[0], hello->capabilities);
    protocol_write_le16(&output[2], hello->max_payload_size);
    protocol_write_le32(&output[4], hello->service_version);
    return PROTOCOL_OK;
}

protocol_status_t upgrade_hello_response_decode(
    const uint8_t *input,
    size_t input_length,
    upgrade_hello_response_t *hello)
{
    if ((input == NULL) || (hello == NULL))
    {
        return PROTOCOL_NULL_ARGUMENT;
    }

    if (input_length != UPGRADE_HELLO_RESPONSE_SIZE)
    {
        return PROTOCOL_LENGTH_ERROR;
    }

    hello->capabilities = protocol_read_le16(&input[0]);
    hello->max_payload_size = protocol_read_le16(&input[2]);
    hello->service_version = protocol_read_le32(&input[4]);
    return PROTOCOL_OK;
}

protocol_status_t upgrade_device_info_encode(
    const upgrade_device_info_t *info,
    uint8_t *output,
    size_t output_capacity)
{
    if ((info == NULL) || (output == NULL))
    {
        return PROTOCOL_NULL_ARGUMENT;
    }

    if (output_capacity < UPGRADE_DEVICE_INFO_SIZE)
    {
        return PROTOCOL_BUFFER_TOO_SMALL;
    }

    protocol_write_le16(&output[0], info->product_id);
    protocol_write_le16(&output[2], info->hardware_id);
    protocol_write_le32(&output[4], info->bootloader_version);
    protocol_write_le32(&output[8], info->application_version);
    protocol_write_le32(&output[12], info->application_base);
    protocol_write_le32(&output[16], info->application_max_size);
    protocol_write_le16(&output[20], info->boot_state);
    protocol_write_le16(&output[22], info->capabilities);
    return PROTOCOL_OK;
}

protocol_status_t upgrade_device_info_decode(
    const uint8_t *input,
    size_t input_length,
    upgrade_device_info_t *info)
{
    if ((input == NULL) || (info == NULL))
    {
        return PROTOCOL_NULL_ARGUMENT;
    }

    if (input_length != UPGRADE_DEVICE_INFO_SIZE)
    {
        return PROTOCOL_LENGTH_ERROR;
    }

    info->product_id = protocol_read_le16(&input[0]);
    info->hardware_id = protocol_read_le16(&input[2]);
    info->bootloader_version = protocol_read_le32(&input[4]);
    info->application_version = protocol_read_le32(&input[8]);
    info->application_base = protocol_read_le32(&input[12]);
    info->application_max_size = protocol_read_le32(&input[16]);
    info->boot_state = protocol_read_le16(&input[20]);
    info->capabilities = protocol_read_le16(&input[22]);
    return PROTOCOL_OK;
}

protocol_status_t upgrade_start_manifest_encode(
    const upgrade_start_manifest_t *manifest,
    uint8_t *output,
    size_t output_capacity)
{
    size_t index;

    if ((manifest == NULL) || (output == NULL))
    {
        return PROTOCOL_NULL_ARGUMENT;
    }

    if (output_capacity < UPGRADE_START_MANIFEST_SIZE)
    {
        return PROTOCOL_BUFFER_TOO_SMALL;
    }

    protocol_write_le32(&output[0], manifest->firmware_version);
    protocol_write_le32(&output[4], manifest->image_size);
    protocol_write_le32(&output[8], manifest->image_crc32);
    for (index = 0U; index < UPGRADE_SHA256_SIZE; index++)
    {
        output[12U + index] = manifest->image_sha256[index];
    }
    protocol_write_le16(&output[44], manifest->product_id);
    protocol_write_le16(&output[46], manifest->hardware_id);
    return PROTOCOL_OK;
}

protocol_status_t upgrade_start_manifest_decode(
    const uint8_t *input,
    size_t input_length,
    upgrade_start_manifest_t *manifest)
{
    size_t index;

    if ((input == NULL) || (manifest == NULL))
    {
        return PROTOCOL_NULL_ARGUMENT;
    }

    if (input_length != UPGRADE_START_MANIFEST_SIZE)
    {
        return PROTOCOL_LENGTH_ERROR;
    }

    manifest->firmware_version = protocol_read_le32(&input[0]);
    manifest->image_size = protocol_read_le32(&input[4]);
    manifest->image_crc32 = protocol_read_le32(&input[8]);
    for (index = 0U; index < UPGRADE_SHA256_SIZE; index++)
    {
        manifest->image_sha256[index] = input[12U + index];
    }
    manifest->product_id = protocol_read_le16(&input[44]);
    manifest->hardware_id = protocol_read_le16(&input[46]);
    return PROTOCOL_OK;
}

protocol_status_t upgrade_progress_encode(
    const upgrade_progress_t *progress,
    uint8_t *output,
    size_t output_capacity)
{
    if ((progress == NULL) || (output == NULL))
    {
        return PROTOCOL_NULL_ARGUMENT;
    }

    if (output_capacity < UPGRADE_PROGRESS_SIZE)
    {
        return PROTOCOL_BUFFER_TOO_SMALL;
    }

    protocol_write_le16(&output[0], progress->boot_state);
    protocol_write_le16(&output[2], progress->reserved);
    protocol_write_le32(&output[4], progress->received_bytes);
    protocol_write_le32(&output[8], progress->image_size);
    protocol_write_le32(&output[12], progress->error_code);
    return PROTOCOL_OK;
}

protocol_status_t upgrade_progress_decode(
    const uint8_t *input,
    size_t input_length,
    upgrade_progress_t *progress)
{
    if ((input == NULL) || (progress == NULL))
    {
        return PROTOCOL_NULL_ARGUMENT;
    }

    if (input_length != UPGRADE_PROGRESS_SIZE)
    {
        return PROTOCOL_LENGTH_ERROR;
    }

    progress->boot_state = protocol_read_le16(&input[0]);
    progress->reserved = protocol_read_le16(&input[2]);
    progress->received_bytes = protocol_read_le32(&input[4]);
    progress->image_size = protocol_read_le32(&input[8]);
    progress->error_code = protocol_read_le32(&input[12]);
    return PROTOCOL_OK;
}

upgrade_data_action_t upgrade_classify_data_offset(
    uint32_t received_offset,
    uint32_t next_expected_offset)
{
    if (received_offset == next_expected_offset)
    {
        return UPG_DATA_ACCEPT_NEW;
    }

    if (received_offset < next_expected_offset)
    {
        return UPG_DATA_DUPLICATE;
    }

    return UPG_DATA_REJECT_GAP;
}

protocol_status_t upgrade_validate_data_chunk(
    uint32_t offset,
    uint16_t payload_length,
    uint32_t image_size,
    bool *is_final_chunk)
{
    bool final_chunk;

    if (is_final_chunk == NULL)
    {
        return PROTOCOL_NULL_ARGUMENT;
    }

    if ((image_size == 0U) || (image_size > UPGRADE_MAX_IMAGE_SIZE))
    {
        return PROTOCOL_RANGE_ERROR;
    }

    if ((payload_length == 0U) ||
        (payload_length > UPGRADE_MAX_PAYLOAD_SIZE))
    {
        return PROTOCOL_PAYLOAD_TOO_LARGE;
    }

    if ((offset % UPGRADE_PROGRAM_WORD_SIZE) != 0U)
    {
        return PROTOCOL_ALIGNMENT_ERROR;
    }

    if ((offset >= image_size) ||
        ((uint32_t)payload_length > (image_size - offset)))
    {
        return PROTOCOL_RANGE_ERROR;
    }

    final_chunk = ((uint32_t)payload_length ==
                   (image_size - offset));
    if ((!final_chunk) &&
        ((payload_length % UPGRADE_PROGRAM_WORD_SIZE) != 0U))
    {
        return PROTOCOL_ALIGNMENT_ERROR;
    }

    *is_final_chunk = final_chunk;
    return PROTOCOL_OK;
}
