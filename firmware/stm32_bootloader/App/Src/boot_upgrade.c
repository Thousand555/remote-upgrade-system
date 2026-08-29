#include "boot_upgrade.h"

#include <stddef.h>

#include "boot_metadata.h"
#include "flash_if.h"
#include "flash_layout.h"
#include "main.h"
#include "modbus_rtu.h"
#include "uart_rtu_transport.h"
#include "upgrade_config.h"
#include "upgrade_protocol.h"

#define BOOT_UPGRADE_CRC32_POLYNOMIAL       0xEDB88320UL
#define BOOT_UPGRADE_STATE_RECORD_OVERHEAD  8UL

typedef enum
{
    BOOT_DEFER_NONE = 0,
    BOOT_DEFER_ERASE,
    BOOT_DEFER_RESET
} boot_deferred_action_t;

static boot_metadata_record_t s_record;
static bool s_has_record;
static bool s_metadata_corrupt;
static bool s_activity;
static boot_deferred_action_t s_deferred_action;

/*
 * Keep the protocol workspaces out of the 1 KiB Cortex-M startup stack.
 * boot_upgrade_poll() is single-threaded, so one static request/response set
 * is sufficient and avoids more than 1.2 KiB of automatic storage.
 */
static uint8_t s_request_adu[MODBUS_RTU_MAX_ADU_SIZE];
static uint8_t s_response_adu[MODBUS_RTU_MAX_ADU_SIZE];
static modbus_rtu_frame_t s_rtu_frame;
static upgrade_message_t s_request;
static upgrade_message_t s_response;

static bool boot_upgrade_request_is_empty(
    const upgrade_message_t *request)
{
    return (request->payload_length == 0U) &&
           (request->offset_or_next_offset == 0U);
}

static upgrade_status_t boot_upgrade_metadata_error(void)
{
    return UPG_STATUS_FLASH_ERROR;
}

static boot_state_t boot_upgrade_state(void)
{
    return s_has_record ? (boot_state_t)s_record.state : BOOT_STATE_EMPTY;
}

static upgrade_status_t boot_upgrade_append_state(boot_state_t state,
                                                   uint32_t error_code)
{
    boot_metadata_record_t desired;
    boot_metadata_record_t written;

    if (s_metadata_corrupt)
    {
        return boot_upgrade_metadata_error();
    }

    if (s_has_record)
    {
        desired = s_record;
    }
    else
    {
        boot_metadata_record_init(&desired, state);
    }

    desired.state = (uint16_t)state;
    desired.error_code = error_code;
    if (boot_metadata_append(&desired, &written) != BOOT_METADATA_OK)
    {
        return boot_upgrade_metadata_error();
    }

    s_record = written;
    s_has_record = true;
    return UPG_STATUS_OK;
}

static bool boot_upgrade_manifest_matches(
    const upgrade_start_manifest_t *manifest)
{
    uint32_t index;

    if ((!s_has_record) ||
        (s_record.firmware_version != manifest->firmware_version) ||
        (s_record.image_size != manifest->image_size) ||
        (s_record.image_crc32 != manifest->image_crc32))
    {
        return false;
    }

    for (index = 0U; index < BOOT_METADATA_SHA256_SIZE; index++)
    {
        if (s_record.image_sha256[index] != manifest->image_sha256[index])
        {
            return false;
        }
    }

    return true;
}

static uint32_t boot_upgrade_crc32_app(uint32_t image_size)
{
    volatile const uint8_t *data;
    uint32_t crc;
    uint32_t byte_index;
    uint32_t bit_index;

    data = (volatile const uint8_t *)FLASH_LAYOUT_APP_BASE_ADDR;
    crc = 0xFFFFFFFFUL;
    for (byte_index = 0U; byte_index < image_size; byte_index++)
    {
        crc ^= data[byte_index];
        for (bit_index = 0U; bit_index < 8U; bit_index++)
        {
            if ((crc & 1UL) != 0U)
            {
                crc = (crc >> 1U) ^ BOOT_UPGRADE_CRC32_POLYNOMIAL;
            }
            else
            {
                crc >>= 1U;
            }
        }
    }

    return ~crc;
}

static bool boot_upgrade_flash_range_is_erased(uint32_t offset,
                                                uint32_t length)
{
    volatile const uint8_t *data;
    uint32_t index;

    data = (volatile const uint8_t *)(FLASH_LAYOUT_APP_BASE_ADDR + offset);
    for (index = 0U; index < length; index++)
    {
        if (data[index] != 0xFFU)
        {
            return false;
        }
    }

    return true;
}

static upgrade_status_t boot_upgrade_handle_hello(
    const upgrade_message_t *request,
    upgrade_message_t *response)
{
    upgrade_hello_response_t hello;

    if (!boot_upgrade_request_is_empty(request))
    {
        return UPG_STATUS_BAD_FRAME;
    }

    hello.capabilities = UPGRADE_BOOT_CAPABILITIES;
    hello.max_payload_size = UPGRADE_MAX_PAYLOAD_SIZE;
    hello.service_version = UPGRADE_BOOTLOADER_VERSION;
    if (upgrade_hello_response_encode(&hello,
                                      response->payload,
                                      sizeof(response->payload)) !=
        PROTOCOL_OK)
    {
        return UPG_STATUS_BAD_FRAME;
    }
    response->payload_length = UPGRADE_HELLO_RESPONSE_SIZE;
    return UPG_STATUS_OK;
}

static upgrade_status_t boot_upgrade_handle_get_info(
    const upgrade_message_t *request,
    upgrade_message_t *response)
{
    upgrade_device_info_t info;

    if (!boot_upgrade_request_is_empty(request))
    {
        return UPG_STATUS_BAD_FRAME;
    }

    info.product_id = UPGRADE_PRODUCT_ID;
    info.hardware_id = UPGRADE_HARDWARE_ID;
    info.bootloader_version = UPGRADE_BOOTLOADER_VERSION;
    info.application_version = s_has_record ? s_record.firmware_version : 0U;
    info.application_base = FLASH_LAYOUT_APP_BASE_ADDR;
    info.application_max_size = FLASH_LAYOUT_APP_MAX_SIZE;
    info.boot_state = (uint16_t)boot_upgrade_state();
    info.capabilities = UPGRADE_BOOT_CAPABILITIES;

    if (upgrade_device_info_encode(&info,
                                   response->payload,
                                   sizeof(response->payload)) != PROTOCOL_OK)
    {
        return UPG_STATUS_BAD_FRAME;
    }
    response->payload_length = UPGRADE_DEVICE_INFO_SIZE;
    return UPG_STATUS_OK;
}

static upgrade_status_t boot_upgrade_handle_start(
    const upgrade_message_t *request,
    upgrade_message_t *response)
{
    upgrade_start_manifest_t manifest;
    boot_metadata_record_t desired;
    boot_metadata_record_t written;
    boot_metadata_status_t metadata_status;
    boot_state_t state;
    uint32_t required_records;
    uint32_t index;

    if ((request->session_id == 0U) ||
        (request->offset_or_next_offset != 0U) ||
        (upgrade_start_manifest_decode(request->payload,
                                       request->payload_length,
                                       &manifest) != PROTOCOL_OK))
    {
        return UPG_STATUS_BAD_FRAME;
    }

    if ((manifest.image_size == 0U) ||
        (manifest.image_size > FLASH_LAYOUT_APP_MAX_SIZE))
    {
        return UPG_STATUS_BAD_IMAGE_SIZE;
    }
    if (manifest.product_id != UPGRADE_PRODUCT_ID)
    {
        return UPG_STATUS_BAD_PRODUCT;
    }
    if (manifest.hardware_id != UPGRADE_HARDWARE_ID)
    {
        return UPG_STATUS_BAD_HARDWARE;
    }
    if (manifest.firmware_version == 0U)
    {
        return UPG_STATUS_VERSION_REJECTED;
    }

    state = boot_upgrade_state();
    if ((state == BOOT_STATE_ERASING) ||
        (state == BOOT_STATE_RECEIVING) ||
        (state == BOOT_STATE_VERIFYING) ||
        (state == BOOT_STATE_PENDING_BOOT))
    {
        if ((s_record.session_id != request->session_id) ||
            (!boot_upgrade_manifest_matches(&manifest)))
        {
            return UPG_STATUS_BUSY;
        }

        response->offset_or_next_offset = s_record.received_bytes;
        return ((state == BOOT_STATE_ERASING) ||
                (state == BOOT_STATE_VERIFYING)) ? UPG_STATUS_BUSY :
                                                   UPG_STATUS_OK;
    }

    required_records =
        ((manifest.image_size + UPGRADE_METADATA_CHECKPOINT_SIZE - 1U) /
         UPGRADE_METADATA_CHECKPOINT_SIZE) +
        BOOT_UPGRADE_STATE_RECORD_OVERHEAD;
    metadata_status = boot_metadata_compact_if_needed(required_records);
    if ((metadata_status != BOOT_METADATA_OK) &&
        (metadata_status != BOOT_METADATA_EMPTY))
    {
        return boot_upgrade_metadata_error();
    }

    boot_metadata_record_init(&desired, BOOT_STATE_UPDATE_REQUESTED);
    desired.session_id = request->session_id;
    desired.firmware_version = manifest.firmware_version;
    desired.image_size = manifest.image_size;
    desired.image_crc32 = manifest.image_crc32;
    for (index = 0U; index < BOOT_METADATA_SHA256_SIZE; index++)
    {
        desired.image_sha256[index] = manifest.image_sha256[index];
    }

    if (boot_metadata_append(&desired, &written) != BOOT_METADATA_OK)
    {
        return boot_upgrade_metadata_error();
    }

    s_record = written;
    s_has_record = true;
    s_metadata_corrupt = false;
    response->offset_or_next_offset = 0U;
    return UPG_STATUS_OK;
}

static upgrade_status_t boot_upgrade_handle_erase(
    const upgrade_message_t *request)
{
    boot_state_t state;
    upgrade_status_t status;

    if (!boot_upgrade_request_is_empty(request))
    {
        return UPG_STATUS_BAD_FRAME;
    }
    if ((!s_has_record) || (request->session_id != s_record.session_id))
    {
        return UPG_STATUS_BAD_SESSION;
    }

    state = boot_upgrade_state();
    if (state == BOOT_STATE_RECEIVING)
    {
        return UPG_STATUS_OK;
    }
    if ((state != BOOT_STATE_UPDATE_REQUESTED) &&
        (state != BOOT_STATE_ERASING))
    {
        return UPG_STATUS_BUSY;
    }

    if (state == BOOT_STATE_UPDATE_REQUESTED)
    {
        status = boot_upgrade_append_state(BOOT_STATE_ERASING, 0U);
        if (status != UPG_STATUS_OK)
        {
            return status;
        }
    }

    s_deferred_action = BOOT_DEFER_ERASE;
    return UPG_STATUS_BUSY;
}

static upgrade_status_t boot_upgrade_handle_data(
    const upgrade_message_t *request,
    upgrade_message_t *response)
{
    upgrade_data_ack_t ack;
    upgrade_data_action_t action;
    upgrade_status_t status;
    protocol_status_t protocol_status;
    flash_if_status_t flash_status;
    boot_metadata_record_t previous_record;
    bool final_chunk;
    uint32_t old_offset;
    uint32_t new_offset;

    status = UPG_STATUS_OK;
    ack.status = UPG_STATUS_OK;
    ack.reserved = 0U;
    ack.accepted_sequence = request->sequence;
    ack.next_expected_offset = s_has_record ? s_record.received_bytes : 0U;

    if ((!s_has_record) || (request->session_id != s_record.session_id))
    {
        status = UPG_STATUS_BAD_SESSION;
    }
    else if (boot_upgrade_state() != BOOT_STATE_RECEIVING)
    {
        status = UPG_STATUS_BUSY;
    }
    else
    {
        protocol_status = upgrade_validate_data_chunk(
            request->offset_or_next_offset,
            request->payload_length,
            s_record.image_size,
            &final_chunk);
        if (protocol_status == PROTOCOL_ALIGNMENT_ERROR)
        {
            status = UPG_STATUS_BAD_OFFSET;
        }
        else if (protocol_status != PROTOCOL_OK)
        {
            status = UPG_STATUS_BAD_IMAGE_SIZE;
        }
        else
        {
            action = upgrade_classify_data_offset(
                request->offset_or_next_offset,
                s_record.received_bytes);
            if (action == UPG_DATA_REJECT_GAP)
            {
                status = UPG_STATUS_BAD_OFFSET;
            }
            else if (action == UPG_DATA_DUPLICATE)
            {
                if ((uint32_t)request->payload_length >
                    (s_record.received_bytes -
                     request->offset_or_next_offset))
                {
                    status = UPG_STATUS_BAD_OFFSET;
                }
                else if (flash_if_verify_app(request->offset_or_next_offset,
                                             request->payload,
                                             request->payload_length) !=
                         FLASH_IF_OK)
                {
                    status = UPG_STATUS_FLASH_ERROR;
                }
            }
            else
            {
                old_offset = s_record.received_bytes;
                if (!boot_upgrade_flash_range_is_erased(
                        request->offset_or_next_offset,
                        request->payload_length))
                {
                    flash_status = flash_if_verify_app(
                        request->offset_or_next_offset,
                        request->payload,
                        request->payload_length);
                }
                else
                {
                    flash_status = flash_if_write_app(
                        request->offset_or_next_offset,
                        request->payload,
                        request->payload_length,
                        final_chunk);
                }

                if (flash_status != FLASH_IF_OK)
                {
                    status = UPG_STATUS_FLASH_ERROR;
                }
                else
                {
                    new_offset = old_offset + request->payload_length;
                    previous_record = s_record;
                    s_record.received_bytes = new_offset;
                    if (((old_offset / UPGRADE_METADATA_CHECKPOINT_SIZE) !=
                         (new_offset / UPGRADE_METADATA_CHECKPOINT_SIZE)) ||
                        (new_offset == s_record.image_size))
                    {
                        status = boot_upgrade_append_state(
                            BOOT_STATE_RECEIVING,
                            0U);
                        if (status != UPG_STATUS_OK)
                        {
                            /*
                             * Keep RAM progress equal to the last durable
                             * checkpoint. A retry will compare the already
                             * programmed bytes and append Metadata again.
                             */
                            s_record = previous_record;
                        }
                    }
                    ack.next_expected_offset = s_record.received_bytes;
                }
            }
        }
    }

    ack.status = status;
    ack.next_expected_offset = s_has_record ? s_record.received_bytes : 0U;
    if (upgrade_data_ack_encode(&ack,
                                response->payload,
                                sizeof(response->payload)) != PROTOCOL_OK)
    {
        return UPG_STATUS_BAD_FRAME;
    }
    response->payload_length = UPGRADE_DATA_ACK_SIZE;
    response->offset_or_next_offset = ack.next_expected_offset;
    return status;
}

static upgrade_status_t boot_upgrade_handle_query(
    const upgrade_message_t *request,
    upgrade_message_t *response)
{
    upgrade_progress_t progress;

    if (!boot_upgrade_request_is_empty(request))
    {
        return UPG_STATUS_BAD_FRAME;
    }

    progress.boot_state = (uint16_t)boot_upgrade_state();
    progress.reserved = 0U;
    progress.received_bytes = s_has_record ? s_record.received_bytes : 0U;
    progress.image_size = s_has_record ? s_record.image_size : 0U;
    progress.error_code = s_has_record ? s_record.error_code : 0U;
    if (upgrade_progress_encode(&progress,
                                response->payload,
                                sizeof(response->payload)) != PROTOCOL_OK)
    {
        return UPG_STATUS_BAD_FRAME;
    }

    response->session_id = s_has_record ? s_record.session_id : 0U;
    response->offset_or_next_offset = progress.received_bytes;
    response->payload_length = UPGRADE_PROGRESS_SIZE;
    return (boot_upgrade_state() == BOOT_STATE_ERASING) ? UPG_STATUS_BUSY :
                                                          UPG_STATUS_OK;
}

static upgrade_status_t boot_upgrade_handle_verify(
    const upgrade_message_t *request)
{
    upgrade_status_t status;
    uint32_t crc32;

    if (!boot_upgrade_request_is_empty(request))
    {
        return UPG_STATUS_BAD_FRAME;
    }
    if ((!s_has_record) || (request->session_id != s_record.session_id))
    {
        return UPG_STATUS_BAD_SESSION;
    }
    if ((boot_upgrade_state() != BOOT_STATE_RECEIVING) &&
        (boot_upgrade_state() != BOOT_STATE_VERIFYING))
    {
        return UPG_STATUS_BUSY;
    }
    if (s_record.received_bytes != s_record.image_size)
    {
        return UPG_STATUS_BAD_OFFSET;
    }

    if (boot_upgrade_state() == BOOT_STATE_RECEIVING)
    {
        status = boot_upgrade_append_state(BOOT_STATE_VERIFYING, 0U);
        if (status != UPG_STATUS_OK)
        {
            return status;
        }
    }

    crc32 = boot_upgrade_crc32_app(s_record.image_size);
    if (crc32 != s_record.image_crc32)
    {
        (void)boot_upgrade_append_state(BOOT_STATE_FAILED,
                                        UPG_STATUS_VERIFY_FAILED);
        return UPG_STATUS_VERIFY_FAILED;
    }

    return boot_upgrade_append_state(BOOT_STATE_PENDING_BOOT, 0U);
}

static upgrade_status_t boot_upgrade_handle_activate(
    const upgrade_message_t *request)
{
    if (!boot_upgrade_request_is_empty(request))
    {
        return UPG_STATUS_BAD_FRAME;
    }
    if ((!s_has_record) || (request->session_id != s_record.session_id))
    {
        return UPG_STATUS_BAD_SESSION;
    }
    if (boot_upgrade_state() != BOOT_STATE_PENDING_BOOT)
    {
        return UPG_STATUS_BUSY;
    }

    s_deferred_action = BOOT_DEFER_RESET;
    return UPG_STATUS_OK;
}

static upgrade_status_t boot_upgrade_handle_abort(
    const upgrade_message_t *request)
{
    if (!boot_upgrade_request_is_empty(request))
    {
        return UPG_STATUS_BAD_FRAME;
    }
    if ((!s_has_record) || (request->session_id != s_record.session_id))
    {
        return UPG_STATUS_BAD_SESSION;
    }

    return boot_upgrade_append_state(BOOT_STATE_FAILED, 0U);
}

static upgrade_status_t boot_upgrade_dispatch(
    const upgrade_message_t *request,
    upgrade_message_t *response)
{
    switch (request->subfunction)
    {
        case UPG_SUB_HELLO:
            return boot_upgrade_handle_hello(request, response);

        case UPG_SUB_GET_INFO:
            return boot_upgrade_handle_get_info(request, response);

        case UPG_SUB_ENTER_BOOT:
            return boot_upgrade_request_is_empty(request) ? UPG_STATUS_OK :
                                                            UPG_STATUS_BAD_FRAME;

        case UPG_SUB_START:
            return boot_upgrade_handle_start(request, response);

        case UPG_SUB_ERASE:
            return boot_upgrade_handle_erase(request);

        case UPG_SUB_DATA:
            return boot_upgrade_handle_data(request, response);

        case UPG_SUB_QUERY_PROGRESS:
            return boot_upgrade_handle_query(request, response);

        case UPG_SUB_VERIFY:
            return boot_upgrade_handle_verify(request);

        case UPG_SUB_ACTIVATE:
            return boot_upgrade_handle_activate(request);

        case UPG_SUB_ABORT:
            return boot_upgrade_handle_abort(request);

        case UPG_SUB_GET_LOG:
        default:
            return UPG_STATUS_BAD_FRAME;
    }
}

static void boot_upgrade_run_deferred_action(void)
{
    flash_if_status_t flash_status;
    boot_deferred_action_t action;

    action = s_deferred_action;
    s_deferred_action = BOOT_DEFER_NONE;

    if (action == BOOT_DEFER_ERASE)
    {
        flash_status = flash_if_erase_app(s_record.image_size);
        if (flash_status == FLASH_IF_OK)
        {
            s_record.received_bytes = 0U;
            (void)boot_upgrade_append_state(BOOT_STATE_RECEIVING, 0U);
        }
        else
        {
            (void)boot_upgrade_append_state(BOOT_STATE_FAILED,
                                            UPG_STATUS_FLASH_ERROR);
        }
    }
    else if (action == BOOT_DEFER_RESET)
    {
        HAL_Delay(10U);
        NVIC_SystemReset();
    }
}

bool boot_upgrade_init(void)
{
    boot_metadata_status_t metadata_status;

    s_has_record = false;
    s_metadata_corrupt = false;
    s_activity = false;
    s_deferred_action = BOOT_DEFER_NONE;

    metadata_status = boot_metadata_load_latest(&s_record);
    if (metadata_status == BOOT_METADATA_OK)
    {
        s_has_record = true;
    }
    else if (metadata_status == BOOT_METADATA_CORRUPT)
    {
        boot_metadata_record_init(&s_record, BOOT_STATE_FAILED);
        s_record.error_code = UPG_STATUS_FLASH_ERROR;
        s_metadata_corrupt = true;
    }
    else
    {
        boot_metadata_record_init(&s_record, BOOT_STATE_EMPTY);
    }

    return uart_rtu_transport_init();
}

void boot_upgrade_poll(void)
{
    size_t request_length;
    size_t response_length;
    uint8_t address;
    upgrade_status_t status;

    if (!uart_rtu_transport_receive(s_request_adu,
                                    sizeof(s_request_adu),
                                    &request_length))
    {
        return;
    }

    if (modbus_rtu_decode(s_request_adu,
                          request_length,
                          &s_rtu_frame) != PROTOCOL_OK)
    {
        return;
    }
    if (s_rtu_frame.address != UPGRADE_NODE_ADDRESS)
    {
        return;
    }

    s_activity = true;
    if (s_rtu_frame.function != UPGRADE_FUNCTION_CODE)
    {
        if (modbus_rtu_encode_exception(
                UPGRADE_NODE_ADDRESS,
                s_rtu_frame.function,
                MODBUS_EXCEPTION_ILLEGAL_FUNCTION,
                s_response_adu,
                sizeof(s_response_adu),
                &response_length) == PROTOCOL_OK)
        {
            (void)uart_rtu_transport_send(s_response_adu, response_length);
        }
        return;
    }

    if (upgrade_decode_request(s_request_adu,
                               request_length,
                               &address,
                               &s_request) != PROTOCOL_OK)
    {
        if (modbus_rtu_encode_exception(
                UPGRADE_NODE_ADDRESS,
                UPGRADE_FUNCTION_CODE,
                MODBUS_EXCEPTION_ILLEGAL_DATA_VALUE,
                s_response_adu,
                sizeof(s_response_adu),
                &response_length) == PROTOCOL_OK)
        {
            (void)uart_rtu_transport_send(s_response_adu, response_length);
        }
        return;
    }

    upgrade_message_init(&s_response, s_request.subfunction);
    s_response.session_id = s_request.session_id;
    s_response.sequence = s_request.sequence;
    s_response.offset_or_next_offset = s_has_record ?
                                       s_record.received_bytes : 0U;
    status = boot_upgrade_dispatch(&s_request, &s_response);
    s_response.flags_or_status = (uint16_t)status;

    if ((upgrade_encode_response(UPGRADE_NODE_ADDRESS,
                                 &s_response,
                                 s_response_adu,
                                 sizeof(s_response_adu),
                                 &response_length) == PROTOCOL_OK) &&
        uart_rtu_transport_send(s_response_adu, response_length))
    {
        boot_upgrade_run_deferred_action();
    }
    else
    {
        s_deferred_action = BOOT_DEFER_NONE;
    }
}

bool boot_upgrade_has_activity(void)
{
    return s_activity;
}
