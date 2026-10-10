#include "serial_tcp_server.h"
#include "modbus_parser.h"
#include "rs485_driver.h"
#include "network_manager.h"
#include "app_config.h"
#include "FreeRTOS.h"
#include "task.h"
#include "logger.h"
#include "lwip/sockets.h"
#include <errno.h>
#include <string.h>

static const char *TAG = "SERIAL_TCP";

static TaskHandle_t serial_tcp_handles[MODBUS_MAX_PORTS] = {NULL};

static void serial_tcp_task(void *arg)
{
    int port = (int)(intptr_t)arg;
    network_wait_lwip_ready();

    for (;;) {
        if (ulTaskNotifyTake(pdTRUE, 0)) {
            const modbus_port_service_config_t *srv = modbus_get_port_config((uint8_t)port);
            if (srv == NULL || !srv->enabled || srv->mode != MODBUS_PORT_TCP_SERVER) {
                LOGI(TAG, "Serial TCP task tearing down on Port %d", port);
                rs485_port_release((uint8_t)port, RS485_OWNER_SERIAL_TCP_SERVER);
                serial_tcp_handles[port] = NULL;
                vTaskDelete(NULL);
                return;
            }
        }

        const modbus_port_service_config_t *cfg = modbus_get_port_config((uint8_t)port);
        uint16_t listen_port = cfg != NULL && cfg->tcp_port != 0U ? cfg->tcp_port : 4001U;

        int server_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd < 0) {
            LOGE(TAG, "Failed to create listener");
            vTaskDelay(pdMS_TO_TICKS(1000U));
            continue;
        }

        int opt = 1;
        setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        struct sockaddr_in addr = {0};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(listen_port);
        addr.sin_addr.s_addr = htonl(INADDR_ANY);

        if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0 || listen(server_fd, 1) < 0) {
            LOGE(TAG, "Failed to listen on port %u", (unsigned)listen_port);
            close(server_fd);
            vTaskDelay(pdMS_TO_TICKS(1000U));
            continue;
        }

        LOGI(TAG, "Serial TCP server: RS485 port %d listening on %u", port, (unsigned)listen_port);

        for (;;) {
            fd_set readset;
            FD_ZERO(&readset);
            FD_SET(server_fd, &readset);
            struct timeval tv = {1, 0};
            
            int rc = select(server_fd + 1, &readset, NULL, NULL, &tv);
            
            if (ulTaskNotifyTake(pdTRUE, 0)) {
                break;
            }

            if (rc < 0) {
                vTaskDelay(pdMS_TO_TICKS(1000));
                continue;
            } else if (rc == 0) {
                continue;
            }

            struct sockaddr_in client_addr = {0};
            socklen_t client_len = sizeof(client_addr);
            int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
            if (client_fd < 0) continue;

            if (rs485_port_acquire((uint8_t)port, RS485_OWNER_SERIAL_TCP_SERVER) != 0) {
                LOGW(TAG, "RS485 port %d is busy; rejecting TCP client", port);
                close(client_fd);
                continue;
            }

            LOGI(TAG, "Serial TCP client connected on RS485 port %d", port);
            
            struct timeval timeout = {0, 50000};
            setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
            
            uint8_t rx[RS485_RX_BUF_SIZE];
            uint8_t serial_rx[RS485_RX_BUF_SIZE];
            
            for (;;) {
                if (ulTaskNotifyTake(pdTRUE, 0)) {
                    LOGI(TAG, "Config change requested, disconnecting client on Port %d", port);
                    break;
                }
                
                int n = recv(client_fd, rx, sizeof(rx), 0);
                if (n > 0) {
                    if (rs485_send((uint8_t)port, rx, (uint16_t)n) < 0) break;
                } else if (n == 0) {
                    break;
                } else if (errno != EWOULDBLOCK && errno != EAGAIN) {
                    break;
                }
                
                int serial_len = rs485_recv_polling((uint8_t)port, serial_rx, sizeof(serial_rx), 0U);
                if (serial_len > 0) {
                    if (send(client_fd, serial_rx, (size_t)serial_len, 0) < 0) break;
                }
            }
            
            close(client_fd);
            rs485_flush((uint8_t)port);
            rs485_port_release((uint8_t)port, RS485_OWNER_SERIAL_TCP_SERVER);
            LOGI(TAG, "Serial TCP client disconnected on RS485 port %d", port);
        }
        
        close(server_fd);
    }
}

void serial_tcp_server_apply_config(void)
{
    for (int i = 0; i < MODBUS_MAX_PORTS; i++) {
        const modbus_port_service_config_t *service = modbus_get_port_config((uint8_t)i);
        bool should_run = (service != NULL && service->enabled && service->mode == MODBUS_PORT_TCP_SERVER);
        
        if (should_run) {
            if (serial_tcp_handles[i] == NULL) {
                char task_name[16];
                snprintf(task_name, sizeof(task_name), "STCP_P%d", i);
                xTaskCreate(serial_tcp_task, task_name, 1536U, (void *)(intptr_t)i, GW_TASK_PRIO_NETWORK - 1U, &serial_tcp_handles[i]);
                LOGI(TAG, "Created Serial TCP task for Port %d", i);
            } else {
                xTaskNotifyGive(serial_tcp_handles[i]);
            }
        } else {
            if (serial_tcp_handles[i] != NULL) {
                xTaskNotifyGive(serial_tcp_handles[i]);
            }
        }
    }
}

void serial_tcp_server_init(void)
{
    serial_tcp_server_apply_config();
}