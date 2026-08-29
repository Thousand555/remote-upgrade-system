#include "app_upgrade.h"

#include "boot_metadata.h"
#include "flash_layout.h"
#include "main.h"
#include "modbus_rtu.h"
#include "uart_rtu_transport.h"
#include "upgrade_config.h"
#include "upgrade_protocol.h"

#define APP_UPGRADE_MAX_REQUIRED_RECORDS  232UL

static bool s_reset_after_response;

/* Shared single-thread workspaces; keeping these off the 1 KiB startup stack
 * prevents the first complete RTU frame from corrupting the APP stack. */
static uint8_t s_request_adu[MODBUS_RTU_MAX_ADU_SIZE];
static uint8_t s_response_adu[MODBUS_RTU_MAX_ADU_SIZE];
static modbus_rtu_frame_t s_rtu_frame;
static upgrade_message_t s_request;
static upgrade_message_t s_response;

static bool app_upgrade_request_is_empty(const upgrade_message_t *request)
{
    return (request->payload_length == 0U) &&
           (request->offset_or_next_offset == 0U);
}

static upgrade_status_t app_upgrade_handle_hello(
    const upgrade_message_t *request,
    upgrade_message_t *response)
{
    upgrade_hello_response_t hello;

    if (!app_upgrade_request_is_empty(request))
    {
        return UPG_STATUS_BAD_FRAME;
    }

    hello.capabilities = UPGRADE_APP_CAPABILITIES;
    hello.max_payload_size = UPGRADE_MAX_PAYLOAD_SIZE;
    hello.service_version = UPGRADE_APPLICATION_VERSION;
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

static upgrade_status_t app_upgrade_handle_get_info(
    const upgrade_message_t *request,
    upgrade_message_t *response)
{
    boot_metadata_record_t latest;
    boot_metadata_status_t metadata_status;
    upgrade_device_info_t info;

    if (!app_upgrade_request_is_empty(request))
    {
        return UPG_STATUS_BAD_FRAME;
    }

    metadata_status = boot_metadata_load_latest(&latest);
    info.product_id = UPGRADE_PRODUCT_ID;
    info.hardware_id = UPGRADE_HARDWARE_ID;
    info.bootloader_version = UPGRADE_BOOTLOADER_VERSION;
    info.application_version = UPGRADE_APPLICATION_VERSION;
    info.application_base = FLASH_LAYOUT_APP_BASE_ADDR;
    info.application_max_size = FLASH_LAYOUT_APP_MAX_SIZE;
    info.boot_state = (metadata_status == BOOT_METADATA_OK) ?
                      latest.state : (uint16_t)BOOT_STATE_EMPTY;
    info.capabilities = UPGRADE_APP_CAPABILITIES;

    if (upgrade_device_info_encode(&info,
                                   response->payload,
                                   sizeof(response->payload)) != PROTOCOL_OK)
    {
        return UPG_STATUS_BAD_FRAME;
    }

    response->payload_length = UPGRADE_DEVICE_INFO_SIZE;
    return UPG_STATUS_OK;
}

static upgrade_status_t app_upgrade_handle_enter_boot(
    const upgrade_message_t *request)
{
    boot_metadata_record_t latest;
    boot_metadata_record_t desired;
    boot_metadata_status_t metadata_status;

    if (!app_upgrade_request_is_empty(request))
    {
        return UPG_STATUS_BAD_FRAME;
    }

    metadata_status = boot_metadata_load_latest(&latest);
    if ((metadata_status != BOOT_METADATA_OK) &&
        (metadata_status != BOOT_METADATA_EMPTY))
    {
        return UPG_STATUS_FLASH_ERROR;
    }

    metadata_status = boot_metadata_compact_if_needed(
        APP_UPGRADE_MAX_REQUIRED_RECORDS);
    if ((metadata_status != BOOT_METADATA_OK) &&
        (metadata_status != BOOT_METADATA_EMPTY))
    {
        return UPG_STATUS_FLASH_ERROR;
    }

    boot_metadata_record_init(&desired, BOOT_STATE_UPDATE_REQUESTED);
    desired.session_id = request->session_id;
    if (boot_metadata_append(&desired, NULL) != BOOT_METADATA_OK)
    {
        return UPG_STATUS_FLASH_ERROR;
    }

    s_reset_after_response = true;
    return UPG_STATUS_OK;
}

static upgrade_status_t app_upgrade_dispatch(
    const upgrade_message_t *request,
    upgrade_message_t *response)
{
    switch (request->subfunction)
    {
        case UPG_SUB_HELLO:
            return app_upgrade_handle_hello(request, response);

        case UPG_SUB_GET_INFO:
            return app_upgrade_handle_get_info(request, response);

        case UPG_SUB_ENTER_BOOT:
            return app_upgrade_handle_enter_boot(request);

        default:
            return UPG_STATUS_BUSY;
    }
}

bool app_upgrade_init(void)
{
    s_reset_after_response = false;
    return uart_rtu_transport_init();
}

void app_upgrade_poll(void)
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

    if ((modbus_rtu_decode(s_request_adu,
                           request_length,
                           &s_rtu_frame) != PROTOCOL_OK) ||
        (s_rtu_frame.address != UPGRADE_NODE_ADDRESS))
    {
        return;
    }

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
        return;
    }

    upgrade_message_init(&s_response, s_request.subfunction);
    s_response.session_id = s_request.session_id;
    s_response.sequence = s_request.sequence;
    status = app_upgrade_dispatch(&s_request, &s_response);
    s_response.flags_or_status = (uint16_t)status;

    if ((upgrade_encode_response(UPGRADE_NODE_ADDRESS,
                                 &s_response,
                                 s_response_adu,
                                 sizeof(s_response_adu),
                                 &response_length) == PROTOCOL_OK) &&
        uart_rtu_transport_send(s_response_adu, response_length) &&
        s_reset_after_response)
    {
        HAL_Delay(10U);
        NVIC_SystemReset();
    }
}
