#include "telemetry_store.h"
#include "modbus_parser.h"
#include "logger.h"
#include <string.h>

#include "FreeRTOS.h"
#include "semphr.h"
#include <stdio.h>

#define TAG "MODBUS_PARSE"

modbus_config_t g_modbus_config;
static modbus_config_t s_staging_modbus_config;
bool g_modbus_config_changed = false;
static bool s_modbus_changed = false;
static SemaphoreHandle_t s_modbus_config_mutex = NULL;

const modbus_port_service_config_t *modbus_get_port_config(uint8_t port)
{
    return port < MODBUS_MAX_PORTS ? &g_modbus_config.ports[port] : NULL;
}

static modbus_port_mode_t mode_from_string(const char *mode)
{
    if (mode == NULL) return MODBUS_PORT_MASTER;
    if (strcmp(mode, "disabled") == 0) return MODBUS_PORT_DISABLED;
    if (strcmp(mode, "tcp_server") == 0) return MODBUS_PORT_TCP_SERVER;
    if (strcmp(mode, "tcp_client") == 0) return MODBUS_PORT_TCP_CLIENT;
    if (strcmp(mode, "modbus_gateway") == 0) return MODBUS_PORT_GATEWAY;
    return MODBUS_PORT_MASTER;
}

static const char *mode_to_string(modbus_port_mode_t mode)
{
    switch (mode) {
        case MODBUS_PORT_DISABLED: return "disabled";
        case MODBUS_PORT_TCP_SERVER: return "tcp_server";
        case MODBUS_PORT_TCP_CLIENT: return "tcp_client";
        case MODBUS_PORT_GATEWAY: return "modbus_gateway";
        default: return "modbus_master";
    }
}

void modbus_config_lock(void) {
    if (!s_modbus_config_mutex) {
        s_modbus_config_mutex = xSemaphoreCreateMutex();
    }
    xSemaphoreTake(s_modbus_config_mutex, portMAX_DELAY);
}

void modbus_config_unlock(void) {
    if (s_modbus_config_mutex) xSemaphoreGive(s_modbus_config_mutex);
}

void modbus_config_save_cb(cJSON *root, bool include_secrets) {
    (void)include_secrets;
    if (!root) return;

    cJSON *modbus = cJSON_CreateObject();
    
    // Lưu cấu hình các port phần cứng
    cJSON *ports = cJSON_CreateArray();
    modbus_config_lock();
    for (int i = 0; i < MODBUS_MAX_PORTS; i++) {
        cJSON *port_obj = cJSON_CreateObject();
        // Giả sử lấy từ config chung (chưa lưu chi tiết trong modbus_config_t)
        // Hiện tại g_modbus_config chỉ chứa device list. Ta có thể thêm port config sau.
        cJSON_AddNumberToObject(port_obj, "id", i);
        cJSON_AddBoolToObject(port_obj, "enabled", g_modbus_config.ports[i].enabled);
        cJSON_AddStringToObject(port_obj, "mode", mode_to_string(g_modbus_config.ports[i].mode));
        cJSON_AddNumberToObject(port_obj, "baudrate", g_modbus_config.ports[i].baudrate);
        cJSON_AddNumberToObject(port_obj, "data_bits", g_modbus_config.ports[i].data_bits);
        cJSON_AddNumberToObject(port_obj, "parity", g_modbus_config.ports[i].parity);
        cJSON_AddNumberToObject(port_obj, "stop_bits", g_modbus_config.ports[i].stop_bits);
        cJSON *tcp = cJSON_CreateObject();
        cJSON_AddNumberToObject(tcp, "listen_port", g_modbus_config.ports[i].tcp_port);
        cJSON_AddNumberToObject(tcp, "max_clients", g_modbus_config.ports[i].max_clients);
        cJSON_AddItemToObject(port_obj, "tcp", tcp);
        cJSON_AddItemToArray(ports, port_obj);
    }
    cJSON_AddItemToObject(modbus, "ports", ports);
    // Lưu danh sách thiết bị
    cJSON *devices = cJSON_CreateArray();
    for (int i = 0; i < g_modbus_config.device_count; i++) {
        cJSON *dev = cJSON_CreateObject();
        cJSON_AddStringToObject(dev, "name", g_modbus_config.devices[i].name);
        cJSON_AddNumberToObject(dev, "port", g_modbus_config.devices[i].port);
        cJSON_AddNumberToObject(dev, "slave_id", g_modbus_config.devices[i].slave_id);
        cJSON_AddNumberToObject(dev, "poll_interval_ms", g_modbus_config.devices[i].poll_interval_ms);
        
        cJSON *regs = cJSON_CreateArray();
        for (int r = 0; r < g_modbus_config.devices[i].reg_count; r++) {
            cJSON *reg = cJSON_CreateObject();
            cJSON_AddNumberToObject(reg, "addr", g_modbus_config.devices[i].regs[r].reg_addr);
            cJSON_AddNumberToObject(reg, "count", g_modbus_config.devices[i].regs[r].count);
            cJSON_AddNumberToObject(reg, "func", g_modbus_config.devices[i].regs[r].func_code);
            cJSON_AddStringToObject(reg, "type", g_modbus_config.devices[i].regs[r].data_type);
            cJSON_AddNumberToObject(reg, "byte_order", g_modbus_config.devices[i].regs[r].byte_order);
            cJSON_AddNumberToObject(reg, "multiplier", g_modbus_config.devices[i].regs[r].multiplier);
            cJSON_AddNumberToObject(reg, "offset", g_modbus_config.devices[i].regs[r].offset);
            
            cJSON *keys = cJSON_CreateArray();
            for (int k = 0; k < MAX_KEYS_PER_REG; k++) {
                if (strlen(g_modbus_config.devices[i].regs[r].keys[k]) > 0) {
                    cJSON_AddItemToArray(keys, cJSON_CreateString(g_modbus_config.devices[i].regs[r].keys[k]));
                }
            }
            cJSON_AddItemToObject(reg, "keys", keys);
            cJSON_AddItemToArray(regs, reg);
        }
        /* Dashboard uses the compact `regs` name; loader accepts both this
         * and the legacy `registers` name for backward compatibility. */
        cJSON_AddItemToObject(dev, "regs", regs);
        cJSON_AddItemToArray(devices, dev);
    }
    cJSON_AddItemToObject(modbus, "devices", devices);
    modbus_config_unlock();
    
    cJSON_AddItemToObject(root, "modbus", modbus);
}

void modbus_config_load_cb(cJSON *node) {
    modbus_config_lock();
    memcpy(&s_staging_modbus_config, &g_modbus_config, sizeof(modbus_config_t));
    modbus_config_unlock();

    if (!node) {
        LOGW(TAG, "No modbus config node found");
        return;
    }

    cJSON *ports = cJSON_GetObjectItem(node, "ports");
    if (cJSON_IsArray(ports)) {
        for (int i = 0; i < MODBUS_MAX_PORTS; ++i) {
            modbus_port_service_config_t *pc = &s_staging_modbus_config.ports[i];
            if (i < cJSON_GetArraySize(ports)) {
                cJSON *p = cJSON_GetArrayItem(ports, i);
                cJSON *v = cJSON_GetObjectItem(p, "enabled");
                if (cJSON_IsBool(v)) pc->enabled = cJSON_IsTrue(v);
                v = cJSON_GetObjectItem(p, "mode");
                if (cJSON_IsString(v)) pc->mode = mode_from_string(v->valuestring);
                v = cJSON_GetObjectItem(p, "baudrate");
                if (cJSON_IsNumber(v)) pc->baudrate = (uint32_t)v->valueint;
                v = cJSON_GetObjectItem(p, "data_bits");
                if (cJSON_IsNumber(v)) pc->data_bits = (uint8_t)v->valueint;
                v = cJSON_GetObjectItem(p, "parity");
                if (cJSON_IsNumber(v)) pc->parity = (uint8_t)v->valueint;
                v = cJSON_GetObjectItem(p, "stop_bits");
                if (cJSON_IsNumber(v)) pc->stop_bits = (uint8_t)v->valueint;
                cJSON *tcp = cJSON_GetObjectItem(p, "tcp");
                if (cJSON_IsObject(tcp)) {
                    v = cJSON_GetObjectItem(tcp, "listen_port");
                    if (cJSON_IsNumber(v)) pc->tcp_port = (uint16_t)v->valueint;
                    v = cJSON_GetObjectItem(tcp, "max_clients");
                    if (cJSON_IsNumber(v)) pc->max_clients = (uint8_t)v->valueint;
                }
            }
        }
    }

    cJSON *devices = cJSON_GetObjectItem(node, "devices");
    if (cJSON_IsArray(devices)) {
        s_staging_modbus_config.device_count = 0;
        int dev_cnt = cJSON_GetArraySize(devices);
        if (dev_cnt > MAX_MODBUS_DEVICES) dev_cnt = MAX_MODBUS_DEVICES;
        
        s_staging_modbus_config.device_count = dev_cnt;
        
        for (int i = 0; i < dev_cnt; i++) {
            cJSON *dev = cJSON_GetArrayItem(devices, i);
            if (!dev) continue;
            
            cJSON *name = cJSON_GetObjectItem(dev, "name");
            if (cJSON_IsString(name)) {
                snprintf(s_staging_modbus_config.devices[i].name, sizeof(s_staging_modbus_config.devices[i].name), "%s", name->valuestring);
            }
            
            cJSON *port = cJSON_GetObjectItem(dev, "port");
            if (cJSON_IsNumber(port)) s_staging_modbus_config.devices[i].port = port->valueint;
            
            cJSON *slave = cJSON_GetObjectItem(dev, "slave_id");
            if (cJSON_IsNumber(slave)) s_staging_modbus_config.devices[i].slave_id = slave->valueint;
            
            cJSON *poll = cJSON_GetObjectItem(dev, "poll_interval_ms");
            if (cJSON_IsNumber(poll)) s_staging_modbus_config.devices[i].poll_interval_ms = poll->valueint;
            
            cJSON *regs = cJSON_GetObjectItem(dev, "regs");
            if (!cJSON_IsArray(regs)) regs = cJSON_GetObjectItem(dev, "registers");
            if (cJSON_IsArray(regs)) {
                int reg_cnt = cJSON_GetArraySize(regs);
                if (reg_cnt > MAX_REGISTERS_PER_DEVICE) reg_cnt = MAX_REGISTERS_PER_DEVICE;
                
                s_staging_modbus_config.devices[i].reg_count = reg_cnt;
                
                for (int r = 0; r < reg_cnt; r++) {
                    cJSON *reg = cJSON_GetArrayItem(regs, r);
                    if (!reg) continue;
                    
                    cJSON *addr = cJSON_GetObjectItem(reg, "addr");
                    if (cJSON_IsNumber(addr)) s_staging_modbus_config.devices[i].regs[r].reg_addr = addr->valueint;
                    
                    cJSON *count = cJSON_GetObjectItem(reg, "count");
                    if (cJSON_IsNumber(count)) s_staging_modbus_config.devices[i].regs[r].count = count->valueint;
                    
                    cJSON *func = cJSON_GetObjectItem(reg, "func");
                    if (cJSON_IsNumber(func)) s_staging_modbus_config.devices[i].regs[r].func_code = func->valueint;
                    
                    cJSON *type = cJSON_GetObjectItem(reg, "type");
                    if (cJSON_IsString(type)) {
                        snprintf(s_staging_modbus_config.devices[i].regs[r].data_type, sizeof(s_staging_modbus_config.devices[i].regs[r].data_type), "%s", type->valuestring);
                    }
                    
                    cJSON *byte_order = cJSON_GetObjectItem(reg, "byte_order");
                    if (cJSON_IsNumber(byte_order)) {
                        s_staging_modbus_config.devices[i].regs[r].byte_order = byte_order->valueint;
                    } else {
                        s_staging_modbus_config.devices[i].regs[r].byte_order = MODBUS_BYTE_ABCD;
                    }
                    
                    cJSON *multiplier = cJSON_GetObjectItem(reg, "multiplier");
                    if (cJSON_IsNumber(multiplier)) {
                        s_staging_modbus_config.devices[i].regs[r].multiplier = multiplier->valuedouble;
                    } else {
                        s_staging_modbus_config.devices[i].regs[r].multiplier = 1.0f;
                    }
                    
                    cJSON *offset = cJSON_GetObjectItem(reg, "offset");
                    if (cJSON_IsNumber(offset)) {
                        s_staging_modbus_config.devices[i].regs[r].offset = offset->valuedouble;
                    } else {
                        s_staging_modbus_config.devices[i].regs[r].offset = 0.0f;
                    }
                    
                    cJSON *keys = cJSON_GetObjectItem(reg, "keys");
                    if (cJSON_IsArray(keys)) {
                        memset(s_staging_modbus_config.devices[i].regs[r].keys, 0, sizeof(s_staging_modbus_config.devices[i].regs[r].keys));
                        int key_cnt = cJSON_GetArraySize(keys);
                        if (key_cnt > MAX_KEYS_PER_REG) key_cnt = MAX_KEYS_PER_REG;
                        for (int k = 0; k < key_cnt; k++) {
                            cJSON *key = cJSON_GetArrayItem(keys, k);
                            if (cJSON_IsString(key)) {
                                snprintf(s_staging_modbus_config.devices[i].regs[r].keys[k], sizeof(s_staging_modbus_config.devices[i].regs[r].keys[k]), "%s", key->valuestring);
                            }
                        }
                    }
                }
            }
        }
    }
    modbus_config_unlock();
    LOGI(TAG, "Loaded %d modbus devices", s_staging_modbus_config.device_count);
}

void modbus_parser_process_data(uint8_t port, uint8_t slave_id, uint16_t reg_addr, const uint8_t *raw_data, uint16_t data_len) {
    if (!raw_data || data_len == 0) return;
    
    modbus_config_lock();
    modbus_device_t *dev = NULL;
    for (int i = 0; i < g_modbus_config.device_count; i++) {
        if (g_modbus_config.devices[i].port == port && g_modbus_config.devices[i].slave_id == slave_id) {
            dev = &g_modbus_config.devices[i];
            break;
        }
    }
    if (!dev) {
        modbus_config_unlock();
        return;
    }
    
    modbus_device_reg_t *reg = NULL;
    for (int i = 0; i < dev->reg_count; i++) {
        if (dev->regs[i].reg_addr == reg_addr) {
            reg = &dev->regs[i];
            break;
        }
    }
    
    if (!reg) {
        modbus_config_unlock();
        return;
    }

    modbus_device_reg_t local_reg = *reg;
    modbus_config_unlock();

    uint16_t offset = 0;
    uint8_t key_idx = 0;
    uint32_t ts = xTaskGetTickCount();

    if (local_reg.func_code == 1 || local_reg.func_code == 2) {
        for (uint16_t i = 0; i < local_reg.count && key_idx < MAX_KEYS_PER_REG; i++) {
            uint8_t byte_idx = i / 8;
            uint8_t bit_idx = i % 8;
            if (byte_idx >= data_len) break;
            
            bool val = (raw_data[byte_idx] & (1 << bit_idx)) ? true : false;
            telemetry_id_t tid = local_reg.telemetry_ids[key_idx];
            if (tid != TELEMETRY_INVALID_ID) {
                telemetry_value_t tv;
                tv.type = TELEMETRY_TYPE_BOOL;
                tv.value.boolean = val;
                telemetry_store_update(tid, &tv, TELEMETRY_VALUE_GOOD, ts);
            }
            key_idx++;
        }
    } else {
        uint8_t bytes_per_val = 2;
        if (strcmp(local_reg.data_type, "int32") == 0 || strcmp(local_reg.data_type, "uint32") == 0 || strcmp(local_reg.data_type, "float32") == 0) bytes_per_val = 4;
        
        while (offset + bytes_per_val <= data_len && key_idx < MAX_KEYS_PER_REG) {
            telemetry_id_t tid = local_reg.telemetry_ids[key_idx];
            if (tid != TELEMETRY_INVALID_ID) {
                float fval = 0.0f;
                int32_t ival = 0;
                uint32_t uval = 0;
                
                if (bytes_per_val == 2) {
                    uint16_t raw16 = (raw_data[offset] << 8) | raw_data[offset + 1];
                    if (strcmp(local_reg.data_type, "int16") == 0) {
                        ival = (int16_t)raw16;
                        fval = (float)ival;
                    } else {
                        uval = raw16;
                        fval = (float)uval;
                    }
                } else if (bytes_per_val == 4) {
                    uint32_t raw32 = 0;
                    if (local_reg.byte_order == MODBUS_BYTE_ABCD) {
                        raw32 = (raw_data[offset] << 24) | (raw_data[offset+1] << 16) | (raw_data[offset+2] << 8) | raw_data[offset+3];
                    } else if (local_reg.byte_order == MODBUS_BYTE_BADC) {
                        raw32 = (raw_data[offset+1] << 24) | (raw_data[offset] << 16) | (raw_data[offset+3] << 8) | raw_data[offset+2];
                    } else if (local_reg.byte_order == MODBUS_BYTE_CDAB) {
                        raw32 = (raw_data[offset+2] << 24) | (raw_data[offset+3] << 16) | (raw_data[offset] << 8) | raw_data[offset+1];
                    } else if (local_reg.byte_order == MODBUS_BYTE_DCBA) {
                        raw32 = (raw_data[offset+3] << 24) | (raw_data[offset+2] << 16) | (raw_data[offset+1] << 8) | raw_data[offset];
                    }
                    
                    if (strcmp(local_reg.data_type, "int32") == 0) {
                        ival = (int32_t)raw32;
                        fval = (float)ival;
                    } else if (strcmp(local_reg.data_type, "uint32") == 0) {
                        uval = raw32;
                        fval = (float)uval;
                    } else if (strcmp(local_reg.data_type, "float32") == 0) {
                        memcpy(&fval, &raw32, sizeof(float));
                    }
                }
                
                if (local_reg.multiplier != 0.0f) fval *= local_reg.multiplier;
                if (local_reg.offset != 0.0f) fval += local_reg.offset;
                
                telemetry_value_t tv;
                if (strcmp(local_reg.data_type, "float32") == 0) {
                    tv.type = TELEMETRY_TYPE_FLOAT;
                    tv.value.f32 = fval;
                } else if (strcmp(local_reg.data_type, "int32") == 0 || strcmp(local_reg.data_type, "int16") == 0) {
                    tv.type = TELEMETRY_TYPE_INT32;
                    tv.value.i32 = (int32_t)fval;
                } else {
                    tv.type = TELEMETRY_TYPE_UINT32;
                    tv.value.u32 = (uint32_t)fval;
                }
                
                telemetry_store_update(tid, &tv, TELEMETRY_VALUE_GOOD, ts);
            }
            offset += bytes_per_val;
            key_idx++;
        }
    }
}

void modbus_parser_set_device_timeout(uint8_t port, uint8_t slave_id, uint16_t reg_addr) {
    modbus_config_lock();
    modbus_device_t *dev = NULL;
    for (int i = 0; i < g_modbus_config.device_count; i++) {
        if (g_modbus_config.devices[i].port == port && g_modbus_config.devices[i].slave_id == slave_id) {
            dev = &g_modbus_config.devices[i];
            break;
        }
    }
    if (!dev) {
        modbus_config_unlock();
        return;
    }
    
    modbus_device_reg_t *reg = NULL;
    for (int i = 0; i < dev->reg_count; i++) {
        if (dev->regs[i].reg_addr == reg_addr) {
            reg = &dev->regs[i];
            break;
        }
    }
    
    if (!reg) {
        modbus_config_unlock();
        return;
    }

    modbus_device_reg_t local_reg = *reg;
    modbus_config_unlock();

    uint32_t ts = xTaskGetTickCount();
    for (int k = 0; k < MAX_KEYS_PER_REG; k++) {
        telemetry_id_t tid = local_reg.telemetry_ids[k];
        if (tid != TELEMETRY_INVALID_ID) {
            telemetry_store_set_quality(tid, TELEMETRY_VALUE_TIMEOUT, ts);
        }
    }
}

void modbus_config_commit_cb(void) {
    modbus_config_lock();
    if (memcmp(&g_modbus_config, &s_staging_modbus_config, sizeof(modbus_config_t)) != 0) {
        memcpy(&g_modbus_config, &s_staging_modbus_config, sizeof(modbus_config_t));
        g_modbus_config_changed = true;
        
        // --- FIX BUG 1 ---
        telemetry_store_clear();
        for (int d = 0; d < g_modbus_config.device_count; d++) {
            modbus_device_t *dev = &g_modbus_config.devices[d];
            for (int r = 0; r < dev->reg_count; r++) {
                modbus_device_reg_t *reg = &dev->regs[r];
                
                telemetry_value_type_t type = TELEMETRY_TYPE_INT32;
                if (strcmp(reg->data_type, "uint16") == 0) type = TELEMETRY_TYPE_UINT32;
                else if (strcmp(reg->data_type, "uint32") == 0) type = TELEMETRY_TYPE_UINT32;
                else if (strcmp(reg->data_type, "float32") == 0) type = TELEMETRY_TYPE_FLOAT;
                else if (strcmp(reg->data_type, "bool") == 0) type = TELEMETRY_TYPE_BOOL;

                for (int k = 0; k < MAX_KEYS_PER_REG; k++) {
                    reg->telemetry_ids[k] = TELEMETRY_INVALID_ID;
                    if (strlen(reg->keys[k]) > 0) {
                        telemetry_store_register_point(reg->keys[k], type, &reg->telemetry_ids[k]);
                    }
                }
            }
        }
        // -----------------
    } else {
        g_modbus_config_changed = false;
    }
    modbus_config_unlock();
}
