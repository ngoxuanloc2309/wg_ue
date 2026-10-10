/**
 * @file app_config.h
 * @brief GatewayV2 - Compile-time Configuration
 *
 * Feature flags and default parameters.
 * STM32H563VGT6 Industrial IoT Gateway
 */
#ifndef APP_CONFIG_H
#define APP_CONFIG_H

/* ================== DEVICE IDENTIFICATION ================== */
#define EDGE_DEFAULT_NAME       "STM32H5_GatewayV2"
#define GW_FIRMWARE_VERSION     "1.0.0"
#define GW_HARDWARE_REVISION    "REV1.0"

/* ================== FEATURE FLAGS ================== */
#define USE_MODBUS_MASTER       1
#define USE_MODBUS_RTU          1
#define USE_MODBUS_TCP          1
#define USE_MQTT_CLIENT         1
#define USE_ETHERNET            1
#define USE_LTE_4G              1
#define USE_WIFI                1
#define USE_BLE                 1
#define USE_USB                 1
#define USE_OTA                 1

/* ================== MODBUS CONFIG ================== */
#define EDGE_DEFAULT_MODBUS_TCP_PORT   502
#define GW_MODBUS_TIMEOUT_MS            1000
#define GW_MODBUS_RETRY                3
#define GW_MODBUS_RTU_PORTS            3
#define MBS_TCP_MAX_CLIENTS            3

/* ================== MQTT CONFIG ================== */
#define MQTT_DEFAULT_BROKER_URL        "mqtt://broker.hivemq.com:1883"
#define MQTT_DEFAULT_BROKER_PORT       1883
#define MQTT_DEFAULT_BROKER_USERNAME   "gatewayv2_user"
#define MQTT_DEFAULT_BROKER_PASSWORD   "gatewayv2_pass"
#define MQTT_DEFAULT_CLIENT_ID         EDGE_DEFAULT_NAME
#define GW_MQTT_BUFFER_SIZE            2048
#define MQTT_OFFLINE_QUEUE_SIZE        8U
#define MQTT_OFFLINE_TOPIC_SIZE        128U
#define MQTT_OFFLINE_PAYLOAD_SIZE      512U
#define GW_TELEMETRY_INTERVAL_MS       5000

/* ================== NETWORK CONFIG ================== */
#define APP_NETIF_PROBE_HOST            "8.8.8.8"
#define APP_NETIF_PROBE_PORT            53
#define APP_NETIF_PROBE_TIMEOUT_MS      5000U
#define APP_NETIF_FALLBACK_DNS          "1.1.1.1"
#define GW_LWIP_THREAD_STACKSIZE        2048

/* LTE recovery policy */
#define LTE_REGISTRATION_POLL_MS        2000U
#define LTE_PPP_BACKOFF_INITIAL_MS      5000U
#define LTE_PPP_BACKOFF_MAX_MS          60000U
#define LTE_HARD_RESET_LIMIT            3U
#define LTE_HARD_RESET_WINDOW_MS        900000U
#define LTE_RECOVERY_COOLDOWN_MS        900000U

/* ================== STORAGE CONFIG ================== */
#define GW_SPIFLASH_SIZE                 (16 * 1024 * 1024)  /* 16MB W25Q128 */
#define GW_CONFIG_FILE                   "/cfg/gateway.json"

/* ================== HARDWARE RESOURCES ================== */
#define GW_UART_RS485_COUNT             3
#define GW_LED_COUNT                    4
#define GW_BUTTON_COUNT                 2

/* ================== DEBUG ================== */
#define GW_DEBUG_ENABLE                  1

/* Temporary isolation switch for USB_DRD_FS/TinyUSB bring-up. */
#ifndef GW_USB_NETIF_ENABLE
#define GW_USB_NETIF_ENABLE               0
#endif

/* Keep mDNS independently switchable for network regression isolation. */
#ifndef GW_MDNS_ENABLE
#define GW_MDNS_ENABLE                    0
#endif

/* ================== OS TASK CONFIG ================== */
#define GW_TASK_PRIO_NETWORK      12
#define GW_TASK_PRIO_ETH_LINK     10
#define GW_TASK_PRIO_LOGGER       11
#define GW_TASK_PRIO_MONITOR      10
#define GW_MONITOR_PERIOD_MS      30000U
#define GW_TASK_PRIO_MODBUS       11
#define GW_TASK_PRIO_BROKER       11
#define GW_TASK_PRIO_SHELL        11

#define GW_TASK_STACK_NETWORK     2048
#define GW_TASK_STACK_MODBUS      1024
#define GW_TASK_STACK_BROKER      1024
#define GW_TASK_STACK_SHELL       2048
#define GW_TASK_STACK_HTTP        2048
#define GW_TASK_STACK_MB_TCP      2048
#define GW_TASK_STACK_MB_CLIENT   1536

/* ================== LOG LEVEL MAPPING ================== */
/* Map logger LOG_LEVEL_* to LOGGER_* values from logger.h */
#ifndef LOG_LEVEL_OFF
#define LOG_LEVEL_OFF     LOGGER_OFF
#endif
#ifndef LOG_LEVEL_ERROR
#define LOG_LEVEL_ERROR   LOGGER_ERROR
#endif
#ifndef LOG_LEVEL_WARNING
#define LOG_LEVEL_WARNING LOGGER_WARNING
#endif
#ifndef LOG_LEVEL_INFO
#define LOG_LEVEL_INFO    LOGGER_INFO
#endif
#ifndef LOG_LEVEL_DEBUG
#define LOG_LEVEL_DEBUG   LOGGER_DEBUG
#endif

#endif /* APP_CONFIG_H */
