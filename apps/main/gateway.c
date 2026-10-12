#include "cJSON.h"
#include "gateway.h"
#include "config_manager.h"

#include "main.h"
#include "usart.h"
#include "logger.h"
#include "network_manager.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"
#include "spiflash.h"
#include "block_device.h"
#include "file_io.h"
#include "modbus_master.h"
#include "rs485_driver.h"
#include "data_broker.h"
#include "http_server.h"
#include "ota.h"
#include "usb_netif.h"
#include "ntp_client.h"
#include "shell_serial.h"
#include "modbus_tcp_server.h"
#include "serial_tcp_server.h"
#include <stdio.h>
#include "app_config.h"
#include "wifi_driver.h"
#include "mqtt_client.h"
#include "splc_port.h"

static gateway_state_t g_state = GW_STATE_INIT;
static SemaphoreHandle_t serial_tx_mutex;
static volatile bool usb_netif_start_requested;
static volatile bool usb_netif_start_pending;

static void usb_netif_start_task(void *arg)
{
    (void)arg;
    for (;;) {
        if (usb_netif_start_pending && !usb_netif_start_requested) {
            usb_netif_start_pending = false;
            usb_netif_start_requested = true;
            LOGI("SYS", "WiFi has IP; starting USB netif");
            usb_netif_init();
        }
        vTaskDelay(pdMS_TO_TICKS(100U));
    }
}

static void ota_health_task(void *arg)
{
    (void)arg;
    /* Give scheduler, storage, network and watchdog services time to start. */
    vTaskDelay(pdMS_TO_TICKS(15000U));
    ota_mark_healthy();
    vTaskDelete(NULL);
}

static void gateway_wifi_event(uint8_t event)
{
#if GW_USB_NETIF_ENABLE
    /* Start USB only after DHCP has supplied a host IP. */
    if (event == (uint8_t)WIFI_STATE_GOT_IP && !usb_netif_start_requested) {
        usb_netif_start_pending = true;
    }
#else
    (void)event;
#endif
}

#define UART_LOG_FRAME_SIZE 512U
#define UART_LOG_QUEUE_DEPTH 32U
typedef struct {
    uint16_t length;
    uint8_t data[UART_LOG_FRAME_SIZE];
} uart_log_frame_t;
static QueueHandle_t uart_log_queue;
static TaskHandle_t uart_log_task_handle;
volatile uint32_t uart_log_dropped_frames;

static void uart_hw_write(const uint8_t *data, uint16_t length, uint32_t timeout)
{
    if (data == NULL || length == 0U) return;
    HAL_UART_Transmit(&hlpuart1, (uint8_t *)data, length, timeout);
}

static void uart_log_task(void *arg)
{
    uart_log_frame_t frame;
    (void)arg;
    for (;;) {
        if (xQueueReceive(uart_log_queue, &frame, portMAX_DELAY) == pdTRUE) {
            uart_hw_write(frame.data, frame.length, 1000U);
        }
    }
}

static void uart_log_init(void)
{
    uart_log_queue = xQueueCreate(UART_LOG_QUEUE_DEPTH, sizeof(uart_log_frame_t));
    if (uart_log_queue != NULL) {
        (void)xTaskCreate(uart_log_task, "UART_LOG", 768U, NULL, 6U,
                          &uart_log_task_handle);
    }
}

void serial_tx_write(const uint8_t *data, uint16_t length, uint32_t timeout)
{
    uart_log_frame_t frame;
    (void)timeout;
    if (data == NULL || length == 0U) return;
    if (uart_log_queue == NULL) {
        uart_hw_write(data, length, timeout);
        return;
    }
    if (length > sizeof(frame.data)) length = sizeof(frame.data);
    frame.length = length;
    memcpy(frame.data, data, length);
    if (xQueueSend(uart_log_queue, &frame, 0U) != pdTRUE) {
        uart_log_dropped_frames++;
    }
}

void log_puts(const char *s) {
    size_t len = 0;
    while(s[len] != '\0') len++;
    serial_tx_write((const uint8_t *)s, (uint16_t)len, 1000U);
}

gateway_state_t gateway_get_state(void) {
    return g_state;
}

void gateway_set_state(gateway_state_t state) {
    if (state != g_state) {
        LOGI("SYS", "Gateway state: %d -> %d", g_state, state);
        g_state = state;
    }
}

const char* gateway_state_str(gateway_state_t state) {
    switch (state) {
        case GW_STATE_INIT:       return "INIT";
        case GW_STATE_NET_CONNECTING: return "NET_CONNECTING";
        case GW_STATE_NET_OK:     return "NET_OK";
        case GW_STATE_MODBUS_OK:  return "MODBUS_OK";
        case GW_STATE_TELEMETRY_OK: return "TELEMETRY_OK";
        case GW_STATE_RUNNING:    return "RUNNING";
        case GW_STATE_ERROR:      return "ERROR";
        default:                  return "UNKNOWN";
    }
}

void gateway_app_start(void) {
    /* Wire cJSON to FreeRTOS heap. Newlib malloc is not thread-safe by default,
     * which caused heap corruption when parsing/saving concurrently. */
    cJSON_Hooks hooks;
    hooks.malloc_fn = pvPortMalloc;
    hooks.free_fn = vPortFree;
    cJSON_InitHooks(&hooks);

    /* 1. Logger init */
    serial_tx_mutex = xSemaphoreCreateMutex();
    uart_log_init();
    logger_init(LOGGER_INFO, log_puts);
    LOGI("SYS", "GatewayV2 starting...");
    LOGI("SYS", "Reset flags: PIN=%u BOR=%u SW=%u IWDG=%u WWDG=%u LPWR=%u",
         (unsigned)__HAL_RCC_GET_FLAG(RCC_FLAG_PINRST),
         (unsigned)__HAL_RCC_GET_FLAG(RCC_FLAG_BORRST),
         (unsigned)__HAL_RCC_GET_FLAG(RCC_FLAG_SFTRST),
         (unsigned)__HAL_RCC_GET_FLAG(RCC_FLAG_IWDGRST),
         (unsigned)__HAL_RCC_GET_FLAG(RCC_FLAG_WWDGRST),
         (unsigned)__HAL_RCC_GET_FLAG(RCC_FLAG_LPWRRST));
    __HAL_RCC_CLEAR_RESET_FLAGS();

    /* 2. Storage init */
    spiflash_init();
    int fs_status = lfs_mount_fs();
    if (fs_status != 0) {
        LOGE("SYS", "LittleFS unavailable, status=%d; continuing degraded", fs_status);
    } else {
        /* Ensure web_root directory exists */
        int web_root_status = file_io_mkdir("/web_root");
        if (web_root_status != 0) {
            LOGW("SYS", "web_root unavailable, status=%d; HTTP may be degraded", web_root_status);
        }
        int config_dir_status = file_io_mkdir("configs");
        if (config_dir_status != 0) {
            LOGW("SYS", "config dir unavailable, status=%d; config save may be degraded", config_dir_status);
        }
    }

    /* 3. Register Config Nodes */
    extern void network_config_register(void);
    extern void ntp_config_register(void);
    extern void modbus_config_register(void);
    
    network_config_register();
    ntp_config_register();
    modbus_config_register();

    /* 4. Load Config */
    extern int config_load(void);
    config_load();

    extern void config_apply_manager_init(void);
    config_apply_manager_init();

    /* 5. Initialize Modules */
    network_manager_init();
    wifi_driver_set_event_cb(gateway_wifi_event);
#if GW_USB_NETIF_ENABLE
    if (xTaskCreate(usb_netif_start_task, "USB_START", 768U, NULL, 2U, NULL) != pdPASS) {
        LOGE("SYS", "Failed to create USB netif start task");
    }
#endif

    /* Match the official NUCLEO-H563ZI T02 application: create the ST67
     * application task before the rest of the application tasks. */
#if defined(ST67_SPI_LOOPBACK) && ST67_SPI_LOOPBACK
    LOGW("SYS", "ST67 SPI loopback mode: startup task disabled; keep ST67_EN LOW");
#else
    if (wifi_driver_startup_task_create() != 0) {
        LOGE("SYS", "Failed to create ST67W6X startup task");
    }
#endif

    /* 4. NTP Init */
    ntp_client_init();

    /* 5. RS485 and Modbus init */
    rs485_port_init();
    modbus_master_init();
    (void)splc_host_start();
    
    /* Initialize Modbus TCP Server */
    modbus_tcp_server_init();
    serial_tcp_server_init();

    /* Initialize OTA Manager */
    data_broker_init();
    http_server_init();
    mqtt_client_init();
    if (ota_init() != 0) {
        LOGE("SYS", "OTA manager initialization failed");
    } else {
        (void)xTaskCreate(ota_health_task, "OTA_HEALTH", 512U, NULL, 2U, NULL);
    }

    /* 6. Create tasks */
    (void)xTaskCreate(network_task, "NET", GW_TASK_STACK_NETWORK, NULL, GW_TASK_PRIO_NETWORK, NULL);
    (void)xTaskCreate(network_failover_task, "FAILOVER", GW_TASK_STACK_NETWORK, NULL, GW_TASK_PRIO_NETWORK, NULL);

    /* Tasks will be dynamically created by their respective modules based on configuration */

    /* 8. Start Data Broker Task */
    (void)xTaskCreate(data_broker_task, "DB_TASK", GW_TASK_STACK_BROKER, NULL, GW_TASK_PRIO_BROKER, NULL);
    
    /* 9. Start CLI Shell */
    shell_serial_init();
    
    /* --- FIX BUG 2 --- */
    extern void config_apply_manager_request(uint32_t generation);
    config_apply_manager_request(config_get_generation());
}