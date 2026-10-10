#include "modbus_tcp_server.h"
#include "app_config.h"
#include "FreeRTOS.h"
#include "task.h"
#include "logger.h"
#include "network_manager.h"
#include "lwip/sockets.h"
#include "nanomodbus.h"
#include "modbus_master.h"
#include "modbus_parser.h"

static const char *TAG = "MBS_TCP";

/* Client Task arguments */
typedef struct {
    int client_fd;
    struct sockaddr_in client_addr;
} mbs_client_args_t;

/* Find RS485 port based on Unit ID (Slave ID) */
static int get_port_by_unit_id(uint8_t unit_id) {
    int port = -1;
    modbus_config_lock();
    for (int i = 0; i < g_modbus_config.device_count; i++) {
        if (g_modbus_config.devices[i].slave_id == unit_id) {
            port = g_modbus_config.devices[i].port;
            break;
        }
    }
    modbus_config_unlock();
    return port;
}

/* nanoMODBUS Transport Read */
static int32_t nmbs_tcp_read(uint8_t* buf, uint16_t count, int32_t byte_timeout_ms, void* arg) {
    int client_fd = (int)arg;
    struct timeval tv;
    if (byte_timeout_ms >= 0) {
        tv.tv_sec = byte_timeout_ms / 1000;
        tv.tv_usec = (byte_timeout_ms % 1000) * 1000;
        setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    } else {
        tv.tv_sec = 0;
        tv.tv_usec = 0;
        setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)); // blocking
    }
    
    int res = recv(client_fd, buf, count, 0);
    return res;
}

/* nanoMODBUS Transport Write */
static int32_t nmbs_tcp_write(const uint8_t* buf, uint16_t count, int32_t byte_timeout_ms, void* arg) {
    int client_fd = (int)arg;
    int res = send(client_fd, buf, count, 0);
    return res;
}

/* Callback: Read Holding Registers */
static nmbs_error cb_read_holding_registers(uint16_t address, uint16_t quantity, uint16_t* registers_out, uint8_t unit_id, void* arg) {
    int port = get_port_by_unit_id(unit_id);
    if (port < 0) {
        LOGW(TAG, "Unit ID %d not found in config", unit_id);
        return NMBS_EXCEPTION_ILLEGAL_DATA_ADDRESS;
    }
    
    int ret = modbus_read_holding((uint8_t)port, unit_id, address, quantity, registers_out);
    if (ret != 0) {
        return NMBS_EXCEPTION_SERVER_DEVICE_FAILURE;
    }
    return NMBS_ERROR_NONE;
}

/* Callback: Read Input Registers */
static nmbs_error cb_read_input_registers(uint16_t address, uint16_t quantity, uint16_t* registers_out, uint8_t unit_id, void* arg) {
    int port = get_port_by_unit_id(unit_id);
    if (port < 0) {
        return NMBS_EXCEPTION_ILLEGAL_DATA_ADDRESS;
    }
    
    int ret = modbus_read_input((uint8_t)port, unit_id, address, quantity, registers_out);
    if (ret != 0) {
        return NMBS_EXCEPTION_SERVER_DEVICE_FAILURE;
    }
    return NMBS_ERROR_NONE;
}

/* Callback: Write Single Register */
static nmbs_error cb_write_single_register(uint16_t address, uint16_t value, uint8_t unit_id, void* arg) {
    int port = get_port_by_unit_id(unit_id);
    if (port < 0) {
        return NMBS_EXCEPTION_ILLEGAL_DATA_ADDRESS;
    }
    
    int ret = modbus_write_single((uint8_t)port, unit_id, address, value);
    if (ret != 0) {
        return NMBS_EXCEPTION_SERVER_DEVICE_FAILURE;
    }
    return NMBS_ERROR_NONE;
}

/* Callback: Write Multiple Registers */
static nmbs_error cb_write_multiple_registers(uint16_t address, uint16_t quantity, const uint16_t* registers, uint8_t unit_id, void* arg) {
    int port = get_port_by_unit_id(unit_id);
    if (port < 0) {
        return NMBS_EXCEPTION_ILLEGAL_DATA_ADDRESS;
    }
    
    int ret = modbus_write_multiple((uint8_t)port, unit_id, address, quantity, registers);
    if (ret != 0) {
        return NMBS_EXCEPTION_SERVER_DEVICE_FAILURE;
    }
    return NMBS_ERROR_NONE;
}

/* Modbus TCP Client Task */
static void modbus_tcp_client_task(void *pvParameters) {
    mbs_client_args_t *args = (mbs_client_args_t *)pvParameters;
    int client_fd = args->client_fd;
    
    char client_ip[16];
    snprintf(client_ip, sizeof(client_ip), "%s", inet_ntoa(args->client_addr.sin_addr));
    LOGI(TAG, "Client %s connected", client_ip);
    
    vPortFree(args);
    
    nmbs_t nmbs;
    nmbs_platform_conf platform_conf;
    nmbs_platform_conf_create(&platform_conf);
    platform_conf.transport = NMBS_TRANSPORT_TCP;
    platform_conf.read = nmbs_tcp_read;
    platform_conf.write = nmbs_tcp_write;
    platform_conf.arg = (void*)client_fd;
    
    nmbs_callbacks callbacks;
    nmbs_callbacks_create(&callbacks);
    callbacks.read_holding_registers = cb_read_holding_registers;
    callbacks.read_input_registers = cb_read_input_registers;
    callbacks.write_single_register = cb_write_single_register;
    callbacks.write_multiple_registers = cb_write_multiple_registers;
    
    nmbs_error err = nmbs_server_create(&nmbs, 0, &platform_conf, &callbacks);
    if (err != NMBS_ERROR_NONE) {
        LOGE(TAG, "Failed to create nmbs server");
        goto cleanup;
    }
    
    nmbs_set_read_timeout(&nmbs, 5000); // 5s timeout heartbeat
    nmbs_set_byte_timeout(&nmbs, 500);  // 500ms inter-byte timeout
    
    while (1) {
        err = nmbs_server_poll(&nmbs);
        if (err != NMBS_ERROR_NONE) {
            if (err == NMBS_ERROR_TIMEOUT) {
                // Just a timeout on recv, continue waiting
                continue; 
            } else {
                LOGW(TAG, "Client %s disconnected (err: %d)", client_ip, err);
                break;
            }
        }
    }
    
cleanup:
    close(client_fd);
    vTaskDelete(NULL);
}

/* Modbus TCP Server Listener Task */
static void modbus_tcp_server_task(void *pvParameters) {
    network_wait_lwip_ready();
    
    // Wait for at least one interface to get an IP
    while (network_get_active_interface() == NET_IF_NONE) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        LOGE(TAG, "Failed to create socket");
        vTaskDelete(NULL);
        return;
    }
    
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(EDGE_DEFAULT_MODBUS_TCP_PORT);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    
    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        LOGE(TAG, "Failed to bind to port %d", EDGE_DEFAULT_MODBUS_TCP_PORT);
        close(server_fd);
        vTaskDelete(NULL);
        return;
    }
    
    if (listen(server_fd, MBS_TCP_MAX_CLIENTS) < 0) {
        LOGE(TAG, "Failed to listen");
        close(server_fd);
        vTaskDelete(NULL);
        return;
    }
    
    LOGI(TAG, "Started on port %d", EDGE_DEFAULT_MODBUS_TCP_PORT);
    
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        
        if (client_fd >= 0) {
            mbs_client_args_t *args = pvPortMalloc(sizeof(mbs_client_args_t));
            if (args != NULL) {
                args->client_fd = client_fd;
                args->client_addr = client_addr;
                
                BaseType_t ret = xTaskCreate(modbus_tcp_client_task, "MB_TCP_Cli", GW_TASK_STACK_MB_CLIENT, args, GW_TASK_PRIO_MODBUS - 1, NULL);
                if (ret != pdPASS) {
                    LOGE(TAG, "Failed to spawn client task");
                    vPortFree(args);
                    close(client_fd);
                }
            } else {
                LOGE(TAG, "OOM alloc client args");
                close(client_fd);
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}

void modbus_tcp_server_init(void) {
    BaseType_t ret = xTaskCreate(modbus_tcp_server_task, "MB_TCP_Srv", GW_TASK_STACK_MB_TCP, NULL, GW_TASK_PRIO_NETWORK, NULL);
    configASSERT(ret == pdPASS);
}
