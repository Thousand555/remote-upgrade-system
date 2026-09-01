#ifndef GATEWAY_LOG_H
#define GATEWAY_LOG_H

#include "esp_log.h"

#define GW_LOGE(tag, format, ...) ESP_LOGE(tag, format, ##__VA_ARGS__)
#define GW_LOGW(tag, format, ...) ESP_LOGW(tag, format, ##__VA_ARGS__)
#define GW_LOGI(tag, format, ...) ESP_LOGI(tag, format, ##__VA_ARGS__)
#define GW_LOGD(tag, format, ...) ESP_LOGD(tag, format, ##__VA_ARGS__)

#endif /* GATEWAY_LOG_H */
