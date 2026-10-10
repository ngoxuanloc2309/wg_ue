/**
 * @file http_server.h
 * @brief HTTP Server with Web Dashboard
 */
#ifndef __HTTP_SERVER_H__
#define __HTTP_SERVER_H__

#include <stdint.h>

#define HTTP_SERVER_PORT   80
#define HTTPS_SERVER_PORT 443

/**
 * @brief Initialize HTTP server
 */
void http_server_init(void);

/**
 * @brief HTTP Server task
 * @param params: unused
 */
void http_server_task(void *params);

/**
 * @brief Get device status as JSON
 * @return JSON string (caller must free)
 */
char* http_server_get_status_json(void);

/**
 * @brief Get current config as JSON
 * @return JSON string (caller must free)
 */
char* http_server_get_config_json(void);

/**
 * @brief Apply config from JSON
 * @param json: JSON configuration string
 * @return 0 on success, -1 on failure
 */
int http_server_set_config_json(const char *json);

/**
 * @brief Reboot device
 */
void http_server_reboot(void);
const char *http_server_get_hostname(void);

#endif /* __HTTP_SERVER_H__ */
