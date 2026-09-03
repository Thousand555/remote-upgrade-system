#ifndef GATEWAY_WIFI_H
#define GATEWAY_WIFI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "gateway_config.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GATEWAY_WIFI_SSID_MAX_LENGTH     32U
#define GATEWAY_WIFI_PASSWORD_MAX_LENGTH 64U

typedef struct
{
    bool configured;
    bool connected;
    char ssid[GATEWAY_WIFI_SSID_MAX_LENGTH + 1U];
    char server_url[GATEWAY_FIRMWARE_SERVER_URL_MAX_LENGTH];
} gateway_wifi_status_t;

/* Initializes the STA stack and applies a saved NVS profile when present. */
esp_err_t gateway_wifi_init(void);
esp_err_t gateway_wifi_configure(const char *ssid,
                                 const char *password,
                                 const char *server_url);
esp_err_t gateway_wifi_clear(void);
esp_err_t gateway_wifi_get_status(gateway_wifi_status_t *status);
esp_err_t gateway_wifi_get_server_url(char *buffer, size_t buffer_size);
bool gateway_wifi_is_configured(void);
bool gateway_wifi_is_connected(void);
esp_err_t gateway_wifi_wait_connected(uint32_t timeout_ms);
esp_err_t gateway_wifi_wait_time_synced(uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* GATEWAY_WIFI_H */
