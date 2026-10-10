/**
 * @file modbus_master.c
 * @brief Modbus RTU Master - Multi-Port Support
 */
#include "modbus_master.h"
#include "logger.h"
#include "FreeRTOS.h"
#include "task.h"
#include "mmb_serial.h"
#include "rs485_driver.h"
#include "queue.h"
#include "modbus_parser.h"

static const char *TAG = "ModbusMaster";

/* Arrays for multi-port support */
static nmbs_t nmbs_instance[MODBUS_MAX_PORTS];
static MMBCLient_t modbus_client[MODBUS_MAX_PORTS];

/* Configuration and Data Queue */
static modbus_port_config_t port_configs[MODBUS_MAX_PORTS];


#include <stdio.h>
#include "app_config.h"

static TaskHandle_t modbus_task_handles[MODBUS_MAX_PORTS] = {NULL};

extern bool g_modbus_config_changed;
int modbus_config_apply_cb(void) {
    if (!g_modbus_config_changed) {
        LOGI(TAG, "Modbus configuration unchanged, skipping apply");
        return 0;
    }
    LOGI(TAG, "Modbus configuration applying...");
    for (int i = 0; i < MODBUS_MAX_PORTS; i++) {
        const modbus_port_service_config_t *service = modbus_get_port_config((uint8_t)i);
        bool should_run = (service != NULL && service->enabled && service->mode == MODBUS_PORT_MASTER);
        
        if (should_run) {
            if (modbus_task_handles[i] == NULL) {
                char task_name[16];
                snprintf(task_name, sizeof(task_name), "MB_P%d", i);
                if (xTaskCreate(modbus_task, task_name, GW_TASK_STACK_MODBUS, (void *)(intptr_t)i, GW_TASK_PRIO_MODBUS, &modbus_task_handles[i]) != pdPASS) {
                    LOGE(TAG, "Failed to create Modbus task for Port %d", i);
                } else {
                    LOGI(TAG, "Created Modbus Master task for Port %d", i);
                }
            } else {
                xTaskNotifyGive(modbus_task_handles[i]); // Notify existing task to reload config
            }
        } else {
            if (modbus_task_handles[i] != NULL) {
                xTaskNotifyGive(modbus_task_handles[i]); // Notify to shutdown
            }
        }
    }
    
    // Also notify serial_tcp_server of the configuration changes
    extern void serial_tcp_server_apply_config(void);
    serial_tcp_server_apply_config();

    g_modbus_config_changed = false;
    return 0;
}

static void modbus_config_notify_cb(void) {
    LOGI(TAG, "Modbus configuration changed. Triggering reload...");
    for (int i = 0; i < MODBUS_MAX_PORTS; i++) {
        if (modbus_task_handles[i]) {
            xTaskNotifyGive(modbus_task_handles[i]);
        }
    }
    extern void serial_tcp_server_apply_config(void);
    serial_tcp_server_apply_config();
}

static void modbus_reload_port_config(int port) {
    if (port < 0 || port >= MODBUS_MAX_PORTS) return;
    
    // Clear current points
    port_configs[port].num_points = 0;
    
    modbus_config_lock();
    for (int i = 0; i < g_modbus_config.device_count; i++) {
        if (g_modbus_config.devices[i].port == port) {
            for (int r = 0; r < g_modbus_config.devices[i].reg_count; r++) {
                if (port_configs[port].num_points < MODBUS_MAX_POINTS_PER_PORT) {
                    int p_idx = port_configs[port].num_points;
                    port_configs[port].points[p_idx].slave_id = g_modbus_config.devices[i].slave_id;
                    port_configs[port].points[p_idx].func_code = g_modbus_config.devices[i].regs[r].func_code;
                    port_configs[port].points[p_idx].reg_addr = g_modbus_config.devices[i].regs[r].reg_addr;
                    port_configs[port].points[p_idx].quantity = g_modbus_config.devices[i].regs[r].count;
                    port_configs[port].points[p_idx].poll_interval_ms = g_modbus_config.devices[i].poll_interval_ms;
                    port_configs[port].points[p_idx].last_poll_tick = 0;
                    port_configs[port].num_points++;
                }
            }
        }
    }
    modbus_config_unlock();
    LOGI(TAG, "Port %d reloaded with %d polling points", port, port_configs[port].num_points);
}

/* NanoModbus Platform Callbacks - use arg to pass port number */
static int32_t nmbs_read_serial(uint8_t* buf, uint16_t count, int32_t byte_timeout_ms, void* arg) {
    int port = (int)(intptr_t)arg;
    if (port < 0 || port >= MODBUS_MAX_PORTS) port = 0;
    return rs485_recv_polling(port, buf, count, byte_timeout_ms);
}

static int32_t nmbs_write_serial(const uint8_t* buf, uint16_t count, int32_t byte_timeout_ms, void* arg) {
    (void)byte_timeout_ms;
    int port = (int)(intptr_t)arg;
    if (port < 0 || port >= MODBUS_MAX_PORTS) port = 0;
    return rs485_send(port, buf, count);
}

void modbus_config_register(void) {
    extern void config_manager_register_node(const char*, void*, void*, void*, void*, void*);
int modbus_config_apply_cb(void);
    config_manager_register_node("modbus", modbus_config_load_cb, modbus_config_save_cb, modbus_config_notify_cb, modbus_config_commit_cb, modbus_config_apply_cb);
}

void modbus_master_init(void) {
    LOGI(TAG, "Initializing Modbus Master for %d ports...", MODBUS_MAX_PORTS);



    /* Dummy Config for Port 0 (To be replaced by JSON config loader) */
    port_configs[0].baudrate = MODBUS_DEFAULT_BAUD;
    port_configs[0].parity = MODBUS_DEFAULT_PARITY;
    port_configs[0].num_points = 0;
    
    port_configs[1].num_points = 0;
    port_configs[1].baudrate = MODBUS_DEFAULT_BAUD;
    port_configs[1].parity = MODBUS_DEFAULT_PARITY;
    
    port_configs[2].num_points = 0;
    port_configs[2].baudrate = MODBUS_DEFAULT_BAUD;
    port_configs[2].parity = MODBUS_DEFAULT_PARITY;

    for (int i = 0; i < MODBUS_MAX_PORTS; i++) {
        const modbus_port_service_config_t *service = modbus_get_port_config((uint8_t)i);
        rs485_runtime_config_t cfg;
        cfg.baudrate = service != NULL ? service->baudrate : MODBUS_DEFAULT_BAUD;
        cfg.parity = service != NULL ? service->parity : MODBUS_DEFAULT_PARITY;
        cfg.data_bits = service != NULL ? service->data_bits : 8;
        cfg.stop_bits = service != NULL ? service->stop_bits : 1;
        rs485_port_reconfigure((uint8_t)i, &cfg);

        if (service != NULL && service->enabled && service->mode == MODBUS_PORT_MASTER) {
            (void)rs485_port_acquire((uint8_t)i, RS485_OWNER_MODBUS_MASTER);
        }

        /* Setup NanoModbus Platform Config */
        nmbs_platform_conf platform_conf;
        nmbs_platform_conf_create(&platform_conf);
        platform_conf.transport = NMBS_TRANSPORT_RTU;
        platform_conf.read = nmbs_read_serial;
        platform_conf.write = nmbs_write_serial;
        platform_conf.arg = (void*)(intptr_t)i;  /* Pass port number */

        /* Initialize MMB_Serial Wrapper */
        mmb_init(&modbus_client[i], &nmbs_instance[i], &platform_conf, i);

        LOGI(TAG, "Port %d initialized", i);
    }
}

void modbus_task(void *params) {
    int port = (int)(intptr_t)params;
    if (port < 0 || port >= MODBUS_MAX_PORTS) {
        port = 0;
    }
    
    // We don't need to check service here because we only create task if it's enabled.
    // However, for safety:
    const modbus_port_service_config_t *service = modbus_get_port_config((uint8_t)port);
    if (service == NULL || !service->enabled || service->mode != MODBUS_PORT_MASTER) {
        LOGI(TAG, "Modbus task skipped on Port %d (service mode not modbus_master)", port);
        modbus_task_handles[port] = NULL;
        vTaskDelete(NULL);
        return;
    }

    LOGI(TAG, "Modbus Master task started on Port %d", port);
    modbus_reload_port_config(port);

    for (;;) {
        if (ulTaskNotifyTake(pdTRUE, 0)) {
            const modbus_port_service_config_t *srv = modbus_get_port_config((uint8_t)port);
            if (srv == NULL || !srv->enabled || srv->mode != MODBUS_PORT_MASTER) {
                LOGI(TAG, "Modbus task tearing down on Port %d", port);
                rs485_port_release(port, RS485_OWNER_MODBUS_MASTER);
                modbus_task_handles[port] = NULL;
                vTaskDelete(NULL);
                return; // unreachable
            }
            modbus_reload_port_config(port);
        }

        if (rs485_port_acquire(port, RS485_OWNER_MODBUS_MASTER) < 0) {
            vTaskDelay(pdMS_TO_TICKS(100)); // Port is owned by someone else, wait
            continue;
        }

        uint32_t current_tick = xTaskGetTickCount();
        modbus_port_config_t *cfg = &port_configs[port];
        
        for (int i = 0; i < cfg->num_points; i++) {
            modbus_point_t *pt = &cfg->points[i];
            
            if ((current_tick - pt->last_poll_tick) >= pdMS_TO_TICKS(pt->poll_interval_ms)) {
                if (pt->quantity > MODBUS_MAX_WORDS_PER_POLL || pt->quantity == 0) {
                    LOGE(TAG, "Port %d: Invalid quantity %d for Slave %d Reg 0x%04X", port, pt->quantity, pt->slave_id, pt->reg_addr);
                    pt->last_poll_tick = current_tick; /* Prevent spamming log */
                    continue;
                }

                modbus_telemetry_msg_t msg = {0};
                msg.port = port;
                msg.slave_id = pt->slave_id;
                msg.func_code = pt->func_code;
                msg.reg_addr = pt->reg_addr;
                msg.quantity = pt->quantity;
                msg.timestamp = current_tick;
                int ret = -1;
                for (int retry = 0; retry < 3; retry++) {
                    if (pt->func_code == MB_FUNC_READ_HOLDING_REGISTERS) {
                        ret = mmb_read_holding_registers(&modbus_client[port], pt->slave_id, pt->reg_addr, pt->quantity, (uint16_t*)msg.data);
                    } else if (pt->func_code == MB_FUNC_READ_INPUT_REGISTERS) {
                        ret = mmb_read_input_registers(&modbus_client[port], pt->slave_id, pt->reg_addr, pt->quantity, (uint16_t*)msg.data);
                    } else if (pt->func_code == MB_FUNC_READ_COILS) {
                        ret = mmb_read_coils(&modbus_client[port], pt->slave_id, pt->reg_addr, pt->quantity, (uint8_t*)msg.data);
                    } else if (pt->func_code == MB_FUNC_READ_DISCRETE_INPUTS) {
                        ret = mmb_read_discrete_inputs(&modbus_client[port], pt->slave_id, pt->reg_addr, pt->quantity, (uint8_t*)msg.data);
                    } else {
                        /* Write functions are typically event-driven, not scheduled. Skip polling them. */
                        ret = 0; 
                    }
                    if (ret == 0) break;
                    vTaskDelay(pdMS_TO_TICKS(50));
                }
                
                if (ret == 0) {
                    pt->last_poll_tick = current_tick;
                    LOGD(TAG, "Port %d: Read Slave %d, Reg 0x%04X OK", port, pt->slave_id, pt->reg_addr);
                    modbus_parser_process_data(port, pt->slave_id, pt->reg_addr, (const uint8_t*)msg.data, msg.quantity * 2);
                } else {
                    LOGW(TAG, "Port %d: Read Slave %d, Reg 0x%04X FAILED", port, pt->slave_id, pt->reg_addr);
                    modbus_parser_set_device_timeout(port, pt->slave_id, pt->reg_addr);
                }
                
                /* Small delay to yield CPU and prevent RS485 bus congestion */
                vTaskDelay(pdMS_TO_TICKS(20));
            }
        }
        
        /* Base delay before next round-robin check */
        vTaskDelay(pdMS_TO_TICKS(100)); 
    }
}

/* Public API Implementations */
int modbus_read_holding(uint8_t port, uint8_t addr, uint16_t start, uint16_t count, uint16_t *data) {
    if (port >= MODBUS_MAX_PORTS) return -1;
    return mmb_read_holding_registers(&modbus_client[port], addr, start, count, data);
}

int modbus_read_input(uint8_t port, uint8_t addr, uint16_t start, uint16_t count, uint16_t *data) {
    if (port >= MODBUS_MAX_PORTS) return -1;
    return mmb_read_input_registers(&modbus_client[port], addr, start, count, data);
}

int modbus_write_single(uint8_t port, uint8_t addr, uint16_t reg, uint16_t value) {
    if (port >= MODBUS_MAX_PORTS) return -1;
    return mmb_write_single_register(&modbus_client[port], addr, reg, value);
}

int modbus_write_multiple(uint8_t port, uint8_t addr, uint16_t start, uint16_t count, const uint16_t *data) {
    if (port >= MODBUS_MAX_PORTS) return -1;
    return mmb_write_multiple_registers(&modbus_client[port], addr, start, count, data);
}

int modbus_write_single_coil(uint8_t port, uint8_t addr, uint16_t coil, bool value) {
    if (port >= MODBUS_MAX_PORTS) return -1;
    return mmb_write_single_coil(&modbus_client[port], addr, coil, value);
}

int modbus_write_multiple_coils(uint8_t port, uint8_t addr, uint16_t start, uint16_t count, const uint8_t *data) {
    if (port >= MODBUS_MAX_PORTS) return -1;
    return mmb_write_multiple_coils(&modbus_client[port], addr, start, count, data);
}
