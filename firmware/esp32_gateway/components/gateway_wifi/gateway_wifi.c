#include "gateway_wifi.h"

#include <ctype.h>
#include <string.h>
#include <time.h>

#include "esp_event.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "gateway_log.h"
#include "nvs.h"

#define GATEWAY_WIFI_CONNECTED_BIT  BIT0
#define GATEWAY_WIFI_NVS_NAMESPACE  "gw_network"
#define GATEWAY_WIFI_NVS_KEY        "profile"
#define GATEWAY_WIFI_PROFILE_MAGIC  0x31465747UL
#define GATEWAY_WIFI_PROFILE_SCHEMA 1U

typedef struct
{
    uint32_t magic;
    uint32_t schema_version;
    char ssid[GATEWAY_WIFI_SSID_MAX_LENGTH + 1U];
    char password[GATEWAY_WIFI_PASSWORD_MAX_LENGTH + 1U];
    char server_url[GATEWAY_FIRMWARE_SERVER_URL_MAX_LENGTH];
} gateway_wifi_profile_t;

static const char *TAG = "gateway_wifi";
static EventGroupHandle_t s_events;
static bool s_initialized;
static bool s_wifi_started;
static bool s_configured;
static gateway_wifi_profile_t s_profile;

static bool gateway_wifi_string_is_terminated(const char *text, size_t capacity)
{
    return memchr(text, '\0', capacity) != NULL;
}

static bool gateway_wifi_server_url_is_valid(const char *server_url)
{
    const char *authority;
    size_t length;
    size_t scheme_length;

    if (server_url == NULL) {
        return false;
    }
    length = strlen(server_url);
    if (strncmp(server_url, "https://", 8U) == 0) {
        scheme_length = 8U;
    } else if (GATEWAY_ALLOW_INSECURE_HTTP &&
               (strncmp(server_url, "http://", 7U) == 0)) {
        scheme_length = 7U;
    } else {
        return false;
    }
    if ((length <= scheme_length) ||
        (length >= GATEWAY_FIRMWARE_SERVER_URL_MAX_LENGTH)) {
        return false;
    }
    authority = server_url + scheme_length;
    if ((strncmp(authority, "127.", 4U) == 0) ||
        (strncmp(authority, "0.0.0.0", 7U) == 0) ||
        (strncmp(authority, "localhost", 9U) == 0)) {
        return false;
    }
    return true;
}

static bool gateway_wifi_password_is_valid(const char *password)
{
    size_t index;
    size_t length = strlen(password);

    if (length == 0U) {
        return true;
    }
    if ((length >= 8U) && (length <= 63U)) {
        return true;
    }
    if (length != 64U) {
        return false;
    }
    for (index = 0U; index < length; index++) {
        if (!isxdigit((unsigned char)password[index])) {
            return false;
        }
    }
    return true;
}

static bool gateway_wifi_profile_is_valid(const gateway_wifi_profile_t *profile)
{
    size_t ssid_length;
    size_t password_length;

    if ((profile == NULL) ||
        (profile->magic != GATEWAY_WIFI_PROFILE_MAGIC) ||
        (profile->schema_version != GATEWAY_WIFI_PROFILE_SCHEMA) ||
        !gateway_wifi_string_is_terminated(profile->ssid, sizeof(profile->ssid)) ||
        !gateway_wifi_string_is_terminated(profile->password, sizeof(profile->password)) ||
        !gateway_wifi_string_is_terminated(profile->server_url,
                                           sizeof(profile->server_url))) {
        return false;
    }
    ssid_length = strlen(profile->ssid);
    password_length = strlen(profile->password);
    return (ssid_length > 0U) && (ssid_length <= GATEWAY_WIFI_SSID_MAX_LENGTH) &&
           (password_length <= GATEWAY_WIFI_PASSWORD_MAX_LENGTH) &&
           gateway_wifi_password_is_valid(profile->password) &&
           gateway_wifi_server_url_is_valid(profile->server_url);
}

static esp_err_t gateway_wifi_load_profile(gateway_wifi_profile_t *profile)
{
    nvs_handle_t handle;
    size_t length = sizeof(*profile);
    esp_err_t status;

    status = nvs_open(GATEWAY_WIFI_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (status != ESP_OK) {
        return status;
    }
    status = nvs_get_blob(handle, GATEWAY_WIFI_NVS_KEY, profile, &length);
    nvs_close(handle);
    if (status != ESP_OK) {
        return status;
    }
    if ((length != sizeof(*profile)) || !gateway_wifi_profile_is_valid(profile)) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;
}

static esp_err_t gateway_wifi_save_profile(const gateway_wifi_profile_t *profile)
{
    nvs_handle_t handle;
    esp_err_t status;

    status = nvs_open(GATEWAY_WIFI_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (status != ESP_OK) {
        return status;
    }
    status = nvs_set_blob(handle, GATEWAY_WIFI_NVS_KEY, profile, sizeof(*profile));
    if (status == ESP_OK) {
        status = nvs_commit(handle);
    }
    nvs_close(handle);
    return status;
}

static void gateway_wifi_event_handler(void *argument,
                                       esp_event_base_t event_base,
                                       int32_t event_id,
                                       void *event_data)
{
    (void)argument;

    if ((event_base == WIFI_EVENT) && (event_id == WIFI_EVENT_STA_START)) {
        if (s_configured) {
            (void)esp_wifi_connect();
        }
        return;
    }
    if ((event_base == WIFI_EVENT) && (event_id == WIFI_EVENT_STA_DISCONNECTED)) {
        if (s_events != NULL) {
            xEventGroupClearBits(s_events, GATEWAY_WIFI_CONNECTED_BIT);
        }
        if (s_configured) {
            (void)esp_wifi_connect();
            GW_LOGW(TAG, "Wi-Fi disconnected; reconnecting");
        }
        return;
    }
    if ((event_base == IP_EVENT) && (event_id == IP_EVENT_STA_GOT_IP)) {
        const ip_event_got_ip_t *got_ip = (const ip_event_got_ip_t *)event_data;

        if (s_events != NULL) {
            xEventGroupSetBits(s_events, GATEWAY_WIFI_CONNECTED_BIT);
        }
        if (got_ip != NULL) {
            GW_LOGI(TAG, "Wi-Fi connected; IPv4=" IPSTR,
                    IP2STR(&got_ip->ip_info.ip));
        }
    }
}

static esp_err_t gateway_wifi_initialize_stack(void)
{
    esp_sntp_config_t sntp_config =
        ESP_NETIF_SNTP_DEFAULT_CONFIG(CONFIG_GATEWAY_SNTP_SERVER);
    wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t status;

    s_events = xEventGroupCreate();
    if (s_events == NULL) {
        return ESP_ERR_NO_MEM;
    }
    status = esp_netif_init();
    if ((status != ESP_OK) && (status != ESP_ERR_INVALID_STATE)) {
        return status;
    }
    status = esp_event_loop_create_default();
    if ((status != ESP_OK) && (status != ESP_ERR_INVALID_STATE)) {
        return status;
    }
    if (esp_netif_create_default_wifi_sta() == NULL) {
        return ESP_ERR_NO_MEM;
    }
    status = esp_wifi_init(&wifi_init_config);
    if (status != ESP_OK) {
        return status;
    }
    status = esp_event_handler_register(WIFI_EVENT,
                                        ESP_EVENT_ANY_ID,
                                        &gateway_wifi_event_handler,
                                        NULL);
    if (status != ESP_OK) {
        return status;
    }
    status = esp_event_handler_register(IP_EVENT,
                                        IP_EVENT_STA_GOT_IP,
                                        &gateway_wifi_event_handler,
                                        NULL);
    if (status != ESP_OK) {
        return status;
    }
    return esp_netif_sntp_init(&sntp_config);
}

static esp_err_t gateway_wifi_apply_profile(void)
{
    wifi_config_t wifi_config;
    esp_err_t status;

    memset(&wifi_config, 0, sizeof(wifi_config));
    memcpy(wifi_config.sta.ssid, s_profile.ssid, strlen(s_profile.ssid));
    memcpy(wifi_config.sta.password,
           s_profile.password,
           strlen(s_profile.password));
    wifi_config.sta.threshold.authmode =
        (s_profile.password[0] == '\0') ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;

    if (s_wifi_started) {
        s_configured = false;
        status = esp_wifi_stop();
        if (status != ESP_OK) {
            return status;
        }
        s_wifi_started = false;
        xEventGroupClearBits(s_events, GATEWAY_WIFI_CONNECTED_BIT);
    }
    status = esp_wifi_set_mode(WIFI_MODE_STA);
    if (status != ESP_OK) {
        return status;
    }
    status = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (status != ESP_OK) {
        return status;
    }
    s_configured = true;
    status = esp_wifi_start();
    if (status != ESP_OK) {
        s_configured = false;
        return status;
    }
    s_wifi_started = true;
    GW_LOGI(TAG,
            "Runtime network profile applied: SSID='%s', server='%s'",
            s_profile.ssid,
            s_profile.server_url);
    return ESP_OK;
}

esp_err_t gateway_wifi_init(void)
{
    gateway_wifi_profile_t profile;
    esp_err_t status;

    if (s_initialized) {
        return ESP_OK;
    }
    status = gateway_wifi_initialize_stack();
    if (status != ESP_OK) {
        return status;
    }
    s_initialized = true;

    status = gateway_wifi_load_profile(&profile);
    if (status != ESP_OK) {
        memset(&profile, 0, sizeof(profile));
        profile.magic = GATEWAY_WIFI_PROFILE_MAGIC;
        profile.schema_version = GATEWAY_WIFI_PROFILE_SCHEMA;
        if ((CONFIG_GATEWAY_WIFI_SSID[0] != '\0') &&
            (strlen(CONFIG_GATEWAY_WIFI_SSID) <= GATEWAY_WIFI_SSID_MAX_LENGTH) &&
            (strlen(CONFIG_GATEWAY_WIFI_PASSWORD) <=
             GATEWAY_WIFI_PASSWORD_MAX_LENGTH) &&
            gateway_wifi_server_url_is_valid(CONFIG_GATEWAY_FIRMWARE_SERVER_URL)) {
            strcpy(profile.ssid, CONFIG_GATEWAY_WIFI_SSID);
            strcpy(profile.password, CONFIG_GATEWAY_WIFI_PASSWORD);
            strcpy(profile.server_url, CONFIG_GATEWAY_FIRMWARE_SERVER_URL);
        }
    }
    if (!gateway_wifi_profile_is_valid(&profile)) {
        GW_LOGW(TAG,
                "Network is not configured; use 'wifi configure <ssid> <password|-> <server_url>'");
        return ESP_OK;
    }
    s_profile = profile;
    return gateway_wifi_apply_profile();
}

esp_err_t gateway_wifi_configure(const char *ssid,
                                 const char *password,
                                 const char *server_url)
{
    gateway_wifi_profile_t profile;
    size_t ssid_length;
    size_t password_length;
    esp_err_t status;

    if (!s_initialized || (ssid == NULL) || (password == NULL) ||
        (server_url == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }
    ssid_length = strlen(ssid);
    password_length = strlen(password);
    if ((ssid_length == 0U) || (ssid_length > GATEWAY_WIFI_SSID_MAX_LENGTH) ||
        (password_length > GATEWAY_WIFI_PASSWORD_MAX_LENGTH) ||
        !gateway_wifi_password_is_valid(password) ||
        !gateway_wifi_server_url_is_valid(server_url)) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(&profile, 0, sizeof(profile));
    profile.magic = GATEWAY_WIFI_PROFILE_MAGIC;
    profile.schema_version = GATEWAY_WIFI_PROFILE_SCHEMA;
    strcpy(profile.ssid, ssid);
    strcpy(profile.password, password);
    strcpy(profile.server_url, server_url);

    status = gateway_wifi_save_profile(&profile);
    if (status != ESP_OK) {
        return status;
    }
    s_profile = profile;
    return gateway_wifi_apply_profile();
}

esp_err_t gateway_wifi_clear(void)
{
    nvs_handle_t handle;
    esp_err_t status;

    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    status = nvs_open(GATEWAY_WIFI_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (status != ESP_OK) {
        return status;
    }
    status = nvs_erase_key(handle, GATEWAY_WIFI_NVS_KEY);
    if (status == ESP_ERR_NVS_NOT_FOUND) {
        status = ESP_OK;
    }
    if (status == ESP_OK) {
        status = nvs_commit(handle);
    }
    nvs_close(handle);
    if (status != ESP_OK) {
        return status;
    }

    s_configured = false;
    memset(&s_profile, 0, sizeof(s_profile));
    if (s_events != NULL) {
        xEventGroupClearBits(s_events, GATEWAY_WIFI_CONNECTED_BIT);
    }
    if (s_wifi_started) {
        (void)esp_wifi_disconnect();
        status = esp_wifi_stop();
        if (status == ESP_OK) {
            s_wifi_started = false;
        }
    }
    GW_LOGI(TAG, "Runtime network profile cleared");
    return status;
}

esp_err_t gateway_wifi_get_status(gateway_wifi_status_t *status)
{
    if ((status == NULL) || !s_initialized) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(status, 0, sizeof(*status));
    status->configured = s_configured;
    status->connected = gateway_wifi_is_connected();
    if (s_configured) {
        strcpy(status->ssid, s_profile.ssid);
        strcpy(status->server_url, s_profile.server_url);
    }
    return ESP_OK;
}

esp_err_t gateway_wifi_get_server_url(char *buffer, size_t buffer_size)
{
    size_t length;

    if ((buffer == NULL) || !s_configured) {
        return ESP_ERR_INVALID_STATE;
    }
    length = strlen(s_profile.server_url);
    if (length >= buffer_size) {
        return ESP_ERR_INVALID_SIZE;
    }
    memcpy(buffer, s_profile.server_url, length + 1U);
    return ESP_OK;
}

bool gateway_wifi_is_configured(void)
{
    return s_configured;
}

bool gateway_wifi_is_connected(void)
{
    EventBits_t bits;

    if (s_events == NULL) {
        return false;
    }
    bits = xEventGroupGetBits(s_events);
    return ((bits & GATEWAY_WIFI_CONNECTED_BIT) != 0U);
}

esp_err_t gateway_wifi_wait_connected(uint32_t timeout_ms)
{
    EventBits_t bits;

    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!s_configured) {
        return ESP_ERR_NOT_FOUND;
    }
    bits = xEventGroupWaitBits(s_events,
                               GATEWAY_WIFI_CONNECTED_BIT,
                               pdFALSE,
                               pdTRUE,
                               pdMS_TO_TICKS(timeout_ms));
    return ((bits & GATEWAY_WIFI_CONNECTED_BIT) != 0U) ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t gateway_wifi_wait_time_synced(uint32_t timeout_ms)
{
    time_t now;
    esp_err_t status;

    if (!s_initialized || (timeout_ms == 0U)) {
        return ESP_ERR_INVALID_ARG;
    }
    time(&now);
    if ((int64_t)now >= GATEWAY_MIN_VALID_UNIX_TIME) {
        return ESP_OK;
    }
    status = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(timeout_ms));
    if (status != ESP_OK) {
        return status;
    }
    time(&now);
    if ((int64_t)now < GATEWAY_MIN_VALID_UNIX_TIME) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    GW_LOGI(TAG, "System clock synchronized for HTTPS certificate validation");
    return ESP_OK;
}
