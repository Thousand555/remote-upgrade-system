#include "gateway_console.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_console.h"
#include "firmware_downloader.h"
#include "firmware_store.h"
#include "gateway_log.h"
#include "gateway_wifi.h"
#include "upgrade_manager.h"

#if CONFIG_GATEWAY_RELIABILITY_TEST
#include "reliability_fault.h"
#include "upgrade_commands.h"
#endif

static const char *TAG = "gateway_console";
static esp_console_repl_t *s_repl;

#if CONFIG_GATEWAY_RELIABILITY_TEST
static bool gateway_console_test_configuration_allowed(void)
{
    gateway_upgrade_state_t state = upgrade_manager_state();

    return (state == GW_UPG_IDLE) ||
           (state == GW_UPG_SUCCESS) ||
           (state == GW_UPG_FAILED);
}

static bool gateway_console_fault_kind(const char *name,
                                       reliability_fault_kind_t *kind)
{
    reliability_fault_kind_t candidate;

    if ((name == NULL) || (kind == NULL)) {
        return false;
    }
    for (candidate = REL_FAULT_DROP_DATA_ACK;
         candidate < REL_FAULT_COUNT;
         candidate++) {
        if (strcmp(name, reliability_fault_kind_name(candidate)) == 0) {
            *kind = candidate;
            return true;
        }
    }
    return false;
}

static bool gateway_console_upgrade_command_id(const char *name,
                                               uint8_t *command)
{
    static const struct
    {
        const char *name;
        uint8_t command;
    } commands[] = {
        {"hello", UPG_SUB_HELLO},
        {"get_info", UPG_SUB_GET_INFO},
        {"enter_boot", UPG_SUB_ENTER_BOOT},
        {"start", UPG_SUB_START},
        {"erase", UPG_SUB_ERASE},
        {"data", UPG_SUB_DATA},
        {"query", UPG_SUB_QUERY_PROGRESS},
        {"verify", UPG_SUB_VERIFY},
        {"activate", UPG_SUB_ACTIVATE},
        {"abort", UPG_SUB_ABORT},
    };
    size_t index;

    if ((name == NULL) || (command == NULL)) {
        return false;
    }
    for (index = 0U; index < (sizeof(commands) / sizeof(commands[0])); index++) {
        if (strcmp(name, commands[index].name) == 0) {
            *command = commands[index].command;
            return true;
        }
    }
    return false;
}

static void gateway_console_print_faults(void)
{
    reliability_fault_snapshot_t snapshot;
    reliability_fault_kind_t kind;

    reliability_fault_get_snapshot(&snapshot);
    printf("Reliability fault injection: ENABLED (test build only)\n");
    for (kind = REL_FAULT_DROP_DATA_ACK; kind < REL_FAULT_COUNT; kind++) {
        printf("  %-26s : %s\n",
               reliability_fault_kind_name(kind),
               snapshot.armed[kind] ? "armed" : "off");
    }
    if (snapshot.timeout_remaining > 0U) {
        printf("  timeout                    : command=0x%02X, remaining=%" PRIu32 "\n",
               snapshot.timeout_command,
               snapshot.timeout_remaining);
    } else {
        printf("  timeout                    : off\n");
    }
}

static int gateway_console_test_command(int argc, char **argv)
{
    reliability_fault_kind_t kind;
    uint8_t command;
    char *end;
    unsigned long count;

    if ((argc == 3) && (strcmp(argv[1], "fault") == 0) &&
        (strcmp(argv[2], "show") == 0)) {
        gateway_console_print_faults();
        return 0;
    }
    if (!gateway_console_test_configuration_allowed()) {
        printf("Fault configuration is locked while an upgrade task is active.\n");
        return 1;
    }
    if ((argc == 3) && (strcmp(argv[1], "fault") == 0) &&
        (strcmp(argv[2], "clear") == 0)) {
        reliability_fault_clear();
        printf("All reliability faults cleared.\n");
        return 0;
    }
    if ((argc == 3) && (strcmp(argv[1], "fault") == 0) &&
        gateway_console_fault_kind(argv[2], &kind)) {
        (void)reliability_fault_arm(kind);
        printf("Armed one-shot fault: %s\n", reliability_fault_kind_name(kind));
        return 0;
    }
    if ((argc == 5) && (strcmp(argv[1], "fault") == 0) &&
        (strcmp(argv[2], "timeout") == 0) &&
        gateway_console_upgrade_command_id(argv[3], &command)) {
        end = NULL;
        count = strtoul(argv[4], &end, 0);
        if ((end == argv[4]) || (*end != '\0') || (count == 0UL) ||
            (count > 100UL)) {
            printf("Timeout count must be in the range 1..100.\n");
            return 1;
        }
        (void)reliability_fault_set_timeout(command, (uint32_t)count);
        printf("Armed timeout: command=0x%02X, count=%lu\n",
               command,
               count);
        return 0;
    }

    printf("Usage:\n");
    printf("  test fault <show|clear>\n");
    printf("  test fault <drop_data_ack_once|duplicate_data_once|gap_offset_once>\n");
    printf("  test fault <bad_manifest_crc_once|drop_activate_ack_once>\n");
    printf("  test fault timeout <hello|get_info|enter_boot|start|erase|data|query|verify|activate|abort> <count>\n");
    return 1;
}
#endif

static bool gateway_console_upgrade_is_active(void)
{
    gateway_upgrade_state_t state = upgrade_manager_state();

    return (state != GW_UPG_IDLE) && (state != GW_UPG_SUCCESS) &&
           (state != GW_UPG_FAILED);
}

static void gateway_console_print_download_status(void)
{
    gateway_firmware_download_progress_t progress;
    esp_err_t status = firmware_downloader_get_progress(&progress);

    if (status != ESP_OK) {
        printf("Firmware downloader unavailable: %s\n", esp_err_to_name(status));
        return;
    }
    printf("State: %s\n", firmware_downloader_state_name(progress.state));
    printf("Firmware ID: %s\n",
           (progress.firmware_id[0] != '\0') ? progress.firmware_id : "(none)");
    printf("Progress: %" PRIu32 "/%" PRIu32 " bytes\n",
           progress.received_size, progress.package_size);
    printf("Resume checkpoint: %s\n", progress.can_resume ? "available" : "none");
    printf("Last result: %s\n", esp_err_to_name(progress.last_error));
}

static void gateway_console_print_wifi_status(void)
{
    gateway_wifi_status_t wifi_status;
    esp_err_t status = gateway_wifi_get_status(&wifi_status);

    if (status != ESP_OK) {
        printf("Wi-Fi manager unavailable: %s\n", esp_err_to_name(status));
        return;
    }
    printf("Configured: %s\n", wifi_status.configured ? "yes" : "no");
    printf("Connection: %s\n", wifi_status.connected ? "connected" : "disconnected");
    printf("SSID: %s\n", wifi_status.configured ? wifi_status.ssid : "(none)");
    printf("M8 server: %s\n",
           wifi_status.configured ? wifi_status.server_url : "(none)");
}

static int gateway_console_wifi_command(int argc, char **argv)
{
    const char *password;
    esp_err_t status;

    if ((argc == 2) && (strcmp(argv[1], "status") == 0)) {
        gateway_console_print_wifi_status();
        return 0;
    }
    if ((argc == 2) && (strcmp(argv[1], "clear") == 0)) {
        if (firmware_downloader_is_active()) {
            printf("Cannot clear network configuration while a download is active.\n");
            return 1;
        }
        status = gateway_wifi_clear();
        if (status != ESP_OK) {
            printf("Network configuration clear failed: %s\n",
                   esp_err_to_name(status));
            return 1;
        }
        printf("Network configuration cleared.\n");
        return 0;
    }
    if ((argc == 5) && (strcmp(argv[1], "configure") == 0)) {
        if (firmware_downloader_is_active()) {
            printf("Cannot reconfigure Wi-Fi while a download is active.\n");
            return 1;
        }
        password = (strcmp(argv[3], "-") == 0) ? "" : argv[3];
        status = gateway_wifi_configure(argv[2], password, argv[4]);
        if (status != ESP_OK) {
            printf("Network configuration failed: %s\n",
                   esp_err_to_name(status));
            return 1;
        }
        printf("Network profile saved; connection is starting. Use 'wifi status' to check it.\n");
        return 0;
    }

    printf("Usage:\n");
    printf("  wifi status\n");
    printf("  wifi configure <ssid> <password|-> <server_url>\n");
    printf("  wifi clear\n");
    return 1;
}

static int gateway_console_firmware_command(int argc, char **argv)
{
    const gateway_firmware_manifest_t *manifest;
    esp_err_t status;

    if ((argc == 3) && (strcmp(argv[1], "download") == 0) &&
        (strcmp(argv[2], "status") == 0)) {
        gateway_console_print_download_status();
        return 0;
    }
    if ((argc == 3) && (strcmp(argv[1], "download") == 0) &&
        (strcmp(argv[2], "cancel") == 0)) {
        status = firmware_downloader_cancel();
        if (status != ESP_OK) {
            printf("Download cancel failed: %s\n", esp_err_to_name(status));
            return 1;
        }
        printf("Download cancellation requested.\n");
        return 0;
    }
    if ((argc == 3) && (strcmp(argv[1], "download") == 0)) {
        if (gateway_console_upgrade_is_active()) {
            printf("Cannot replace stm_fw while a local upgrade task is active.\n");
            return 1;
        }
        status = firmware_downloader_start(argv[2]);
        if (status != ESP_OK) {
            printf("Download request rejected: %s. Configure Wi-Fi and M8 server URL first.\n",
                   esp_err_to_name(status));
            return 1;
        }
        printf("Download accepted. Use 'firmware download status' to inspect progress.\n");
        return 0;
    }
    if ((argc != 2) || ((strcmp(argv[1], "info") != 0) &&
                        (strcmp(argv[1], "validate") != 0))) {
        printf("Usage: firmware <info|validate|download <firmware_id|status|cancel>>\n");
        return 1;
    }

    if (strcmp(argv[1], "validate") == 0) {
        status = firmware_store_validate();
        if (status != ESP_OK) {
            printf("Firmware package invalid: %s\n", esp_err_to_name(status));
            return 1;
        }
    }

    manifest = firmware_store_manifest();
    if (manifest == NULL) {
        printf("No valid package in stm_fw; run 'firmware validate' after loading it.\n");
        return 1;
    }

    printf("Firmware version: %" PRIu32 "\n", manifest->firmware_version);
    printf("Image size: %" PRIu32 " bytes\n", manifest->image_size);
    printf("Image CRC32: 0x%08" PRIX32 "\n", manifest->image_crc32);
    printf("Product/Hardware: 0x%04X/0x%04X\n",
           manifest->product_id,
           manifest->hardware_id);
    return 0;
}

static void gateway_console_print_upgrade_status(void)
{
    gateway_upgrade_progress_t progress;
    esp_err_t status = upgrade_manager_get_progress(&progress);

    if (status != ESP_OK) {
        printf("Upgrade manager unavailable: %s\n", esp_err_to_name(status));
        return;
    }
    printf("State: %s\n", upgrade_manager_state_name(progress.state));
    printf("Session: 0x%08" PRIX32 "\n", progress.session_id);
    printf("Firmware version: %" PRIu32 "\n", progress.firmware_version);
    printf("Progress: %" PRIu32 "/%" PRIu32 " bytes\n",
           progress.transferred_bytes,
           progress.image_size);
    printf("Remote boot state: %u\n", (unsigned int)progress.remote_boot_state);
    printf("Last result: %s, device status=%u\n",
           esp_err_to_name(progress.last_error),
           (unsigned int)progress.last_device_status);
}

static int gateway_console_upgrade_command(int argc, char **argv)
{
    gateway_upgrade_probe_t probe;
    esp_err_t status;

    if ((argc != 2) ||
        ((strcmp(argv[1], "start") != 0) &&
         (strcmp(argv[1], "probe") != 0) &&
         (strcmp(argv[1], "status") != 0) &&
         (strcmp(argv[1], "abort") != 0))) {
        printf("Usage: upgrade <probe|start|status|abort>\n");
        return 1;
    }

    if (strcmp(argv[1], "status") == 0) {
        gateway_console_print_upgrade_status();
        return 0;
    }
    if (strcmp(argv[1], "probe") == 0) {
        status = upgrade_manager_probe(&probe);
        if (status != ESP_OK) {
            printf("Probe failed: %s\n", esp_err_to_name(status));
            return 1;
        }
        printf("Service: capabilities=0x%04X, version=0x%08" PRIX32 "\n",
               probe.capabilities,
               probe.service_version);
        printf("Device: product=0x%04X, hardware=0x%04X, boot_state=%u\n",
               probe.product_id,
               probe.hardware_id,
               (unsigned int)probe.boot_state);
        printf("Versions: bootloader=0x%08" PRIX32 ", application=%" PRIu32 "\n",
               probe.bootloader_version,
               probe.application_version);
        printf("APP: base=0x%08" PRIX32 ", max_size=0x%08" PRIX32 "\n",
               probe.application_base,
               probe.application_max_size);
        return 0;
    }
    if (strcmp(argv[1], "start") == 0) {
        if (firmware_downloader_is_active()) {
            printf("Cannot start STM32 upgrade while a package download is active.\n");
            return 1;
        }
        status = upgrade_manager_start();
    } else {
        status = upgrade_manager_abort();
    }

    if (status != ESP_OK) {
        printf("Command failed: %s\n", esp_err_to_name(status));
        return 1;
    }
    printf("Command accepted. Use 'upgrade status' to inspect progress.\n");
    return 0;
}

esp_err_t gateway_console_init(void)
{
    const esp_console_cmd_t firmware_command = {
        .command = "firmware",
        .help = "Inspect local package or control M10 HTTPS download",
        .hint = "<info|validate|download <firmware_id|status|cancel>>",
        .func = &gateway_console_firmware_command,
        .argtable = NULL,
    };
    const esp_console_cmd_t upgrade_command = {
        .command = "upgrade",
        .help = "Control the M7 local UART upgrade",
        .hint = "<probe|start|status|abort>",
        .func = &gateway_console_upgrade_command,
        .argtable = NULL,
    };
    const esp_console_cmd_t wifi_command = {
        .command = "wifi",
        .help = "Configure Wi-Fi and the M8 server at runtime",
        .hint = "<status|configure <ssid> <password|-> <server_url>|clear>",
        .func = &gateway_console_wifi_command,
        .argtable = NULL,
    };
#if CONFIG_GATEWAY_RELIABILITY_TEST
    const esp_console_cmd_t test_command = {
        .command = "test",
        .help = "Configure one-shot destructive reliability fault injection",
        .hint = "fault <show|clear|fault-name|timeout ...>",
        .func = &gateway_console_test_command,
        .argtable = NULL,
    };
#endif
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    esp_err_t status;

    repl_config.prompt = "gateway>";
    repl_config.max_cmdline_length = 320;

    status = esp_console_register_help_command();
    if (status != ESP_OK) {
        return status;
    }
    status = esp_console_cmd_register(&firmware_command);
    if (status != ESP_OK) {
        return status;
    }
    status = esp_console_cmd_register(&wifi_command);
    if (status != ESP_OK) {
        return status;
    }
    status = esp_console_cmd_register(&upgrade_command);
    if (status != ESP_OK) {
        return status;
    }
#if CONFIG_GATEWAY_RELIABILITY_TEST
    status = esp_console_cmd_register(&test_command);
    if (status != ESP_OK) {
        return status;
    }
    GW_LOGW(TAG,
            "Reliability fault injection is ENABLED; do not use this build in production");
#endif

#if defined(CONFIG_ESP_CONSOLE_UART_DEFAULT) || defined(CONFIG_ESP_CONSOLE_UART_CUSTOM)
    esp_console_dev_uart_config_t hardware_config =
        ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    status = esp_console_new_repl_uart(&hardware_config, &repl_config, &s_repl);
#elif defined(CONFIG_ESP_CONSOLE_USB_CDC)
    esp_console_dev_usb_cdc_config_t hardware_config =
        ESP_CONSOLE_DEV_CDC_CONFIG_DEFAULT();
    status = esp_console_new_repl_usb_cdc(&hardware_config, &repl_config, &s_repl);
#elif defined(CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG)
    esp_console_dev_usb_serial_jtag_config_t hardware_config =
        ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    status = esp_console_new_repl_usb_serial_jtag(&hardware_config,
                                                  &repl_config,
                                                  &s_repl);
#else
    status = ESP_ERR_NOT_SUPPORTED;
#endif
    if (status != ESP_OK) {
        return status;
    }

    status = esp_console_start_repl(s_repl);
    if (status == ESP_OK) {
        GW_LOGI(TAG,
                "Console ready: wifi <status|configure|clear>, firmware <info|validate|download>, upgrade <probe|start|status|abort>");
    }
    return status;
}
