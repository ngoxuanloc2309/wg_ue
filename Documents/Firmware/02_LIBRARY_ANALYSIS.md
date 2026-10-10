# Library Analysis for Gateway V2

**Document ID:** FW-002
**Version:** 2.1
**Date:** 2026-07-10
**Project:** GatewayV2 Industrial IoT Gateway

> Phân tích các thư viện từ SX-ETH-Gateway và RS485_RF_IO_V2 để xác định khả năng reuse cho GatewayV2.
> Dựa trên README.md requirements và SX-ETH-Gateway source code.

---

# GatewayV2 Requirements Summary

## Core Features (từ README)

| Feature | Status | Notes |
|---------|--------|-------|
| Multi Network Interface | ✅ Complete | Ethernet, WiFi, LTE, USB đồng thời |
| Modbus RTU Master (3 ports) | ✅ Complete | nanoModbus + 3 RS485 ports + JSON config |
| Modbus TCP Server | ✅ Complete | nanoModbus TCP + smart Unit ID routing |
| MQTT/MQTTS | ✅ Complete | lwIP MQTT, TLS via mbedtls |
| HTTP/HTTPS Server | ✅ Complete | Mongoose + LittleFS |
| Web Dashboard | ✅ Complete | HTML/JS + REST API (/api/status, /api/config) |
| BLE Gateway | ⚠️ Partial | ST67W611 WiFi driver done, BLE pending |
| LTE PPPoS | ✅ Complete | A7680C driver with PPPoS |
| NTP Client | ✅ Complete | lwIP SNTP + JSON config |
| Network Manager | ✅ Complete | Multi-WAN Failover |
| CLI Shell | ✅ Complete | UART shell với commands (ping, reboot, config, ls, cat, rm) |
| OTA Update | ⚠️ Stub | Chỉ stub, chưa implement HTTP download + dual-bank flash |
| Modbus RTU Slave | ⚠️ Future | Không trong scope hiện tại |

## Project Structure (từ README)

```
gateway/
├── apps/                    # Application layer
├── components/              # Custom components
├── middleware/             # MQTT, HTTP, Modbus, BLE, lwip, mbedtls
├── network/                # eth_netif, wifi_netif, pppos_netif, usb_netif, network_manager
├── drivers/                # Hardware drivers
├── bsp/                    # Board support package
├── configs/                # Configuration files
├── docs/                   # Documentation
└── tests/                  # Unit tests
```

---

# Executive Summary

## Reuse Summary by Feature (dựa trên README)

| README Feature | Components | Source Available | Reuse Status | Effort |
|----------------|------------|------------------|--------------|--------|
| **Multi Network Interface** | | | | |
| Ethernet | lwIP + DP83848 | ✅ | ✅ Complete | Done |
| WiFi | ST67W611 driver | ✅ | ✅ Complete (SPI1 + W6X API) | Done |
| LTE PPPoS | A7680C + PPP stack | ✅ | ✅ Complete (UART1 + PPP) | Done |
| USB | USB CDC/RNDIS | ✅ | ✅ Complete (TinyUSB) | Done |
| Network Manager | Multi-iface logic | ✅ | ✅ Complete (Failover) | Done |
| **Industrial Protocols** | | | | |
| Modbus RTU Master | nanoModbus | ✅ | ✅ Complete (3 RS485 + JSON config) | Done |
| Modbus TCP Server | nanoModbus TCP | ✅ | ✅ Complete (nanoModbus TCP + routing) | Done |
| Modbus RTU Slave | - | ❌ | ⚠️ Future | P3 |
| **Cloud Connectivity** | | | | |
| MQTT/MQTTS | lwIP MQTT + mbedTLS | ✅ | ✅ Complete | Done |
| HTTP/HTTPS Client | - | ❌ | ❌ Not needed | - |
| **Web Configuration** | | | | |
| HTTP Server | Mongoose + LittleFS | ✅ | ✅ Complete | Done |
| Web Dashboard | HTML/JS | ✅ | ✅ Complete | Done |
| **BLE Gateway** | | | | |
| BLE Scanner/GATT | ST67W611 driver | ✅ | ⚠️ WiFi done, BLE pending | P2 |
| **Time Sync** | | | | |
| NTP Client | lwIP SNTP | ✅ | ✅ Complete | Done |
| **CLI Shell** | | | | |
| UART Shell | CLI Shell component | ✅ | ✅ Complete | Done |
| **OTA** | | | | |
| Firmware Update | OTA stub | ✅ | ⚠️ Stub only | 2 weeks |

## Quick Reference

| Nguồn | Thư viện | Khả năng reuse | Ghi chú |
|--------|-----------|----------------|---------|
| SX-ETH-Gateway | lwIP + DP83848 Driver | ✅ **Có reuse hoàn toàn** | Cùng STM32H5 + DP83848 |
| SX-ETH-Gateway | FreeModbus (RTU/TCP) | ❌ **Không reuse** | GatewayV2 dùng nanoModbus |
| GatewayV2 | nanoModbus (RTU/TCP) | ✅ **Viết mới hoàn toàn** | TCP Server + Smart routing |
| SX-ETH-Gateway | MQTT (lwIP-based) | ✅ **Có reuse hoàn toàn** | ThingsBoard wrapper |
| SX-ETH-Gateway | mbedTLS | ✅ **Có reuse hoàn toàn** | HAL wrappers có sẵn |
| GatewayV2 | Mongoose HTTP | ✅ **Complete** | LittleFS static files + REST API |
| SX-ETH-Gateway | SPI Flash (W25Q128) | ✅ **Có reuse** | W25Q128 giống nhau |
| SX-ETH-Gateway | Logger | ✅ **Copy trực tiếp** | Không phụ thuộc hardware |
| SX-ETH-Gateway | cQueue | ✅ **Copy trực tiếp** | Không phụ thuộc hardware |
| SX-ETH-Gateway | LittleFS | ✅ **Có reuse** | Phụ thuộc SPI Flash driver |
| SX-ETH-Gateway | NTP/SNTP | ✅ **Copy trực tiếp** | lwIP contrib |

---

# 1. FreeModbus Library

## 1.1 Source Location
```
SX-ETH-Gateway/SynaptiX/services/modbus/
├── include/
│   ├── mb.h           # Main header
│   ├── mbport.h      # Porting layer interface
│   ├── mbconfig.h     # Configuration
│   ├── mbframe.h     # Frame definitions
│   ├── mbfunc.h      # Function handlers
│   ├── mbproto.h     # Protocol definitions
│   └── port.h        # Type definitions
└── functions/
    ├── mb.c
    ├── mbfunccoils.c
    ├── mbfuncdisc.c
    ├── mbfuncholding.c
    ├── mbfuncinput.c
    ├── mbfuncother.c
    └── mbutils.c
```

## 1.2 Features

| Feature | Status | Notes |
|---------|--------|-------|
| Modbus RTU Master (V1) | ⚠️ | SX-ETH-Gateway: Cần porting layer |
| Modbus RTU Master (V2) | ✅ | GatewayV2: nanoModbus + 3 RS485 + JSON config |
| Modbus TCP Server (V1) | ✅ | SX-ETH-Gateway: FreeModbus TCP |
| Modbus TCP Server (V2) | ✅ | GatewayV2: nanoModbus TCP + smart routing |
| Function 01 - Read Coils | ✅ | |
| Function 02 - Read Discrete Inputs | ✅ | |
| Function 03 - Read Holding Registers | ✅ | |
| Function 04 - Read Input Registers | ✅ | |
| Function 05 - Write Single Coil | ✅ | |
| Function 06 - Write Single Register | ✅ | |
| Function 15 - Write Multiple Coils | ✅ | |
| Function 16 - Write Multiple Registers | ✅ | |

## 1.3 Porting Layer Requirements

FreeModbus cần các hàm porting:

```c
// Serial Port
BOOL xMBPortSerialInit(UCHAR ucPort, ULONG ulBaudRate, 
                       UCHAR ucDataBits, eMBParity eParity);
void vMBPortSerialEnable(BOOL xRxEnable, BOOL xTxEnable);
BOOL xMBPortSerialGetByte(CHAR *pucByte);
BOOL xMBPortSerialPutByte(CHAR ucByte);

// Timer (for RTU timeout)
BOOL xMBPortTimersInit(USHORT usTimeOut50us);
void vMBPortTimersEnable(void);
void vMBPortTimersDisable(void);

// Events
BOOL xMBPortEventInit(void);
BOOL xMBPortEventPost(eMBEventType eEvent);
BOOL xMBPortEventGet(eMBEventType *eEvent);

// TCP Port (for Modbus TCP)
BOOL xMBTCPPortInit(USHORT usTCPPort);
BOOL xMBTCPPortSendResponse(const UCHAR *pucMBTCPFrame, USHORT usTCPLength);
```

## 1.4 Reuse Assessment

| Aspect | Score | Notes |
|--------|-------|-------|
| Core Protocol | 10/10 | Protocol logic không đổi |
| Porting Layer | 7/10 | Cần viết mới cho GatewayV2 UART |
| Integration | 8/10 | Cần tích hợp với RS485 driver |

**Kết luận:** ✅ **RECOMMENDED TO REUSE**
- Copy toàn bộ `modbus/` folder
- Viết porting layer mới cho UART/RS485

---

# 2. lwIP Stack

## 2.1 Source Location
```
SX-ETH-Gateway/lwip/
├── src/                    # lwIP core
├── contrib/               # Additional apps
├── App/
│   ├── lwip.c
│   └── lwip.h
└── Target/
    ├── ethernetif.c       # Ethernet interface ✅
    ├── ethernetif.h
    ├── dp83848.c          # DP83848 PHY driver ✅
    ├── dp83848.h
    ├── lan8742.c          # LAN8742 PHY driver
    └── lan8742.h
```

## 2.2 Key Files for GatewayV2

### ethernetif.c - Ethernet Interface Driver

Đã implement đầy đủ cho STM32H5:
- Zero-copy RX/TX buffers
- DMA descriptors management
- PHY auto-negotiation
- Link state monitoring
- FreeRTOS integration

### dp83848.c - PHY Driver

Đã support DP83848 (cùng PHY với GatewayV2):
- PHY initialization
- Link state detection
- Auto-negotiation
- Speed/Duplex configuration

## 2.3 lwIP Configuration (lwipopts.h)

Key settings cần review:

```c
#define LWIP_NETIF_STATUS_CALLBACK      1
#define LWIP_NETIF_LINK_CALLBACK        1
#define LWIP_TCP                        1
#define LWIP_UDP                        1
#define LWIP_ICMP                       1
#define LWIP_DHCP                       1
#define LWIP_SOCKET                     1
#define MEM_ALIGNMENT                   4
#define MEM_SIZE                        (16*1024*1024)  // 16MB
#define PBUF_POOL_SIZE                  32
#define MEMP_NUM_PBUF                   32
#define TCP_SND_BUF                     (32*1024)
#define TCP_WND                         (32*1024)
```

## 2.4 Integration with FreeRTOS

```c
// From app_freertos.c
void MX_LWIP_Init(void *app_settings);

// From ethernetif.c
osThreadId_t EthIfHandle;  // Ethernet interface task
osSemaphoreId_t RxPktSemaphore;
osSemaphoreId_t TxPktSemaphore;
```

## 2.5 Reuse Assessment

| Aspect | Score | Notes |
|--------|-------|-------|
| lwIP Core | 10/10 | Library chuẩn, không cần thay đổi |
| Ethernet Driver | 10/10 | Cùng STM32H5 + DP83848 |
| PHY Driver | 10/10 | Cùng DP83848 PHY |
| Integration | 9/10 | Cần verify DMA channels |

**Kết luận:** ✅ **RECOMMENDED TO REUSE**
- Copy `lwip/` folder
- Verify DMA channel assignments
- Test with actual hardware

---

# 3. Mongoose HTTP Server

## 3.1 Source Location
```
SX-ETH-Gateway/mongoose/
├── mongoose.c              # Mongoose core library
├── mongoose.h              # Mongoose API header
├── mongoose_config.h       # Configuration
├── mongoose_glue.c         # HAL glue implementation
├── mongoose_glue.h         # HAL interface
├── mongoose_fs.c          # File system integration
└── mongoose_impl.c        # Implementation
```

## 3.2 HTTP Server Implementation

HTTP Server dùng **Mongoose** (Cesanta Software):
```c
// http_server.c
void http_server_init(void) {
  mongoose_init();              // Khởi tạo Mongoose
  mongoose_add_ws_reporter(100, "state");    // WebSocket reporter
  mongoose_add_ws_reporter(2000, "ssids");    // SSID scanner
}
```

## 3.3 Mongoose Features Enabled

Từ `mongoose_glue.h`:
- ✅ HTTP Server
- ✅ HTTPS Server (WIZARD_ENABLE_HTTPS = 1)
- ✅ HTTP Web UI (WIZARD_ENABLE_HTTP_UI = 1)
- ✅ HTTP Login Authentication (WIZARD_ENABLE_HTTP_UI_LOGIN = 1)
- ✅ WebSocket support (WIZARD_ENABLE_WEBSOCKET = 1)
- ❌ MQTT (disabled in Mongoose - dùng MQTT client riêng)

## 3.4 Mongoose Glue Features

Mongoose cung cấp:
- HTTP handlers cho config, state, security
- WebSocket reporters cho real-time updates
- File upload/download handlers
- OTA firmware update integration
- Authentication system

## 3.5 License Warning

```
// SPDX-License-Identifier: GPL-2.0-only or commercial
```

**Important:** Mongoose sử dụng dual license:
- GPL v2 (open source)
- Commercial license (for proprietary use)

Cần xem xét license implications cho dự án GatewayV2.

## 3.6 Reuse Assessment

| Aspect | Score | Notes |
|--------|-------|-------|
| HTTP Core | 9/10 | Mongoose core reuse được |
| HTTPS | 9/10 | Cần tích hợp mbedTLS |
| Web UI | 8/10 | Cần port glue layer |
| Storage Backend | 6/10 | Cần adapter cho OctoSPI |
| License | ⚠️ | GPL/C Commercial |

**Kết luận:** ✅ **RECOMMENDED TO REUSE (with license consideration)**
- Copy `mongoose/` folder
- Viết glue layer mới cho GatewayV2
- Cần xem xét commercial license nếu không muốn public source

---

# 4. MQTT Client (ThingsBoard)

## 4.1 Source Location
```
SX-ETH-Gateway/SynaptiX/
├── services/
│   ├── mqtt/               # MQTT core (lwIP-based)
│   │   ├── mqtt.c
│   │   ├── mqtt.h
│   │   ├── mqtt_opts.h
│   │   └── mqtt_priv.h
│   └── thingsboard/        # ThingsBoard wrapper
│       ├── thingsboard-client.c
│       └── thingsboard-client.h
└── apps/
    └── mqtt/
        ├── mqtt_app.c      # Application layer
        └── mqtt_app.h
```

## 4.2 Architecture

```
┌─────────────────────────────────────────┐
│         Application Layer              │
│   (mqtt_app.c - Telemetry, RPC, OTA)  │
└────────────────────┬────────────────────┘
                     │
┌────────────────────▼────────────────────┐
│       ThingsBoard Client               │
│   (thingsboard-client.c/h)            │
│   - Topic management                   │
│   - Attribute updates                  │
│   - RPC handling                       │
└────────────────────┬────────────────────┘
                     │
┌────────────────────▼────────────────────┐
│         MQTT Client                    │
│     (mqtt.c - lwIP socket based)      │
│   - CONNECT, PUBLISH, SUBSCRIBE        │
│   - Keep-alive, Reconnect             │
└────────────────────┬────────────────────┘
                     │
┌────────────────────▼────────────────────┐
│          lwIP Stack                    │
│     (TCP socket abstraction)           │
└─────────────────────────────────────────┘
```

## 4.3 ThingsBoard API

```c
// ThingsBoard config structure
struct ThingsBoard_cfg {
    char username[128];
    char password[128];
    char clientID[128];
    char host[256];
    int port;
    size_t publish_time_S;
    bool tls;
    uint8_t ca_buff[2048];
    size_t ca_size;
    uint8_t client_crt_buff[2048];
    size_t client_crt_size;
    uint8_t client_key_buff[2048];
    size_t client_key_size;
};

// ThingsBoard API callbacks
struct ThingsboardAPI {
    get_last_telemetry_json get_telemetry;           // Telemetry data
    update_attribute_t update_attribute;              // Attribute update
    int (*rpc_handle)(...);                         // RPC handler
    int (*firmware_handle)(...);                     // OTA handler
};

// Core functions
void ThingsBoard_Init(ThingsBoard_t *client, cfg, api);
int tb_publish(ThingsBoard_t *client, topic, payload, len, qos, retain);
```

## 4.4 Topics Format (SynaptiX custom)

```c
#define TELEMETRY_API "synaptix/demo/telemetry/%s"
#define ATTRIBUTE_UPDATE_API "synaptix/demo/attributes/%s"
#define RPC_REQUEST_API "synaptix/demo/request/%s"
#define RPC_RESPONSE_API "synaptix/demo/response/%s"
#define ATTRIBUTE_FW_REQUEST_API "synaptix/fw/request/%s/chunk/"
#define ATTRIBUTE_FW_RESPONSE_API "synaptix/fw/response/%s/chunk/"
```

## 4.5 MQTT Features

| Feature | Status | Implementation |
|---------|--------|----------------|
| MQTT CONNECT | ✅ | Keep-alive, clean session |
| MQTT PUBLISH | ✅ | QoS 0/1, retained |
| MQTT SUBSCRIBE | ✅ | Wildcard topics |
| TLS/SSL | ✅ | mbedTLS certificates |
| Auto-reconnect | ✅ | Event-based |
| RPC (Remote Procedure Call) | ✅ | Async task-based |
| OTA Firmware Update | ⚠️ | SX-ETH-Gateway: ✅ Segmented download. **GatewayV2: Stub only** |
| Telemetry batching | ✅ | Configurable interval |
| Attribute updates | ✅ | On-demand |

## 4.6 OTA Firmware Update Flow

```
1. Server sends: fw/request/{name}/chunk/begin
   - Version, size, CRC
   - Client: mg_ota_begin()

2. Server sends: fw/request/{name}/chunk/{segment}
   - Hex-encoded firmware chunks
   - Client: mg_ota_write()

3. Server sends: fw/request/{name}/chunk/end
   - Client: mg_ota_end()
   - Validate CRC, trigger reboot
```

## 4.7 Reuse Assessment

| Aspect | Score | Notes |
|--------|-------|-------|
| MQTT Protocol | 10/10 | lwIP-based, không đổi |
| ThingsBoard API | 8/10 | Cần adapter cho GatewayV2 config |
| TLS/SSL | 9/10 | Cùng mbedTLS |
| Telemetry format | 7/10 | Cần viết mới cho GatewayV2 devices |
| RPC/RPC OTA | 8/10 | Architecture reuse được |

**Kết luận:** ✅ **RECOMMENDED TO REUSE**
- Copy `services/mqtt/` và `services/thingsboard/`
- Copy `apps/mqtt/mqtt_app.c`
- Viết adapter cho GatewayV2 telemetry format
- Giữ nguyên ThingsBoard API structure

---

# 5. SPI Flash Driver (W25Q128)

## 5.1 Source Location
```
SX-ETH-Gateway/SynaptiX/board/
├── spif/
│   ├── spif.c
│   └── spif.h
└── qspif/
    ├── w25q_mem.c
    └── w25q_mem.h
```

## 5.2 Features

- Read/Write/Erase sectors
- JEDEC ID reading
- 4-byte addressing support
- Write protection
- Status register handling

## 5.3 API

```c
bool SPIF_Init(bsp_spif_t *spi, SPI_HandleTypeDef *hspi, ...);
bool SPIF_ReadSector(bsp_spif_t *spi, uint32_t sector, ...);
bool SPIF_WriteSector(bsp_spif_t *spi, uint32_t sector, ...);
bool SPIF_EraseSector(bsp_spif_t *spi, uint32_t sector);
```

## 5.4 Reuse Assessment

| Aspect | Score | Notes |
|--------|-------|-------|
| W25Q128 Support | 10/10 | Cùng chip |
| SPI Interface | 8/10 | SPI1 trên GatewayV2 |
| Integration | 9/10 | Minor adapter needed |

**Kết luận:** ✅ **RECOMMENDED TO REUSE**
- Copy `spif/` folder
- Adapter SPI handle cho GatewayV2

---

# 6. Logger

## 6.1 Source Location
```
SX-ETH-Gateway/SynaptiX/services/logger/
├── logger.c
└── logger.h
```

## 6.2 Current GatewayV2 Features

- Multiple log levels (ERROR, WARN, INFO, DEBUG)
- Tag-based logging
- Thread-safe direct output callback
- Bounded `snprintf`/`vsnprintf` formatting
- Hex dump support with input and size validation
- Finite mutex wait on FreeRTOS builds
- No hardware or UART dependency
- Banner disabled by default

## 6.3 API

```c
// Initialization
void logger_init(LOGGING_LEVELS level, p_log_func p_func);

// Logging macros
#define log_debug(TAG, ...)  log_func(LOGGER_DEBUG, TAG, __VA_ARGS__)
#define log_info(TAG, ...)   log_func(LOGGER_INFO, TAG, __VA_ARGS__)
#define log_warn(TAG, ...)   log_func(LOGGER_WARNING, TAG, __VA_ARGS__)
#define log_error(TAG, ...) log_func(LOGGER_ERROR, TAG, __VA_ARGS__)

// Hex dump
void log_print_hex(LOGGING_LEVELS level, const char *TAG,
                   const uint8_t *hex_buff, uint16_t length);

// Configuration
void logger_set_level(LOGGING_LEVELS level);
```

## 6.4 Example Output

```
[INFO]APP : App init
[INFO]APP : Vendor : SynaptiX
[INFO]APP : Board name: SX-ZB-ETH-V2.0
[DEBUG]ETHIF : Set MAC address 00:80:E1:00:01:02
```

## 6.5 Reuse Assessment

| Aspect | Score | Notes |
|--------|-------|-------|
| Hardware Dependency | 10/10 | Không có |
| RTOS Dependency | 10/10 | Có macro cho FreeRTOS |
| Customization | 10/10 | Dễ dàng thay đổi |

**Kết luận:** ✅ **RECOMMENDED TO REUSE**
- Copy trực tiếp, không cần thay đổi
- Cần define `bsp_putchar()` hoặc serial output function

---

# 7. Circular Queue

## 7.1 Source Location
```
SX-ETH-Gateway/SynaptiX/services/cqueue/
├── cqueue.c
└── cqueue.h
```

## 7.2 Features

- Lock-free single producer/single consumer
- Static allocation
- ISR-safe (with some limitations)
- Multiple data type support

## 7.3 API

```c
void cqueue_init_static(CQueue_t *q, void *buf, size_t size, size_t type_size);
bool cqueue_send(CQueue_t *q, const void *data);
bool cqueue_receive(CQueue_t *q, void *data);
size_t cqueue_count(CQueue_t *q);
```

## 7.4 Reuse Assessment

| Aspect | Score | Notes |
|--------|-------|-------|
| Hardware Dependency | 10/10 | Không có |
| RTOS Dependency | 10/10 | Không có |
| Use Case | 10/10 | UART buffers, event queues |

**Kết luận:** ✅ **RECOMMENDED TO REUSE**
- Copy trực tiếp
- Sử dụng cho UART buffers và inter-task communication

---

# 8. LittleFS

## 8.1 Source Location
```
SX-ETH-Gateway/SynaptiX/services/littlefs/
├── lfs.c
├── lfs.h
├── lfs_util.c
└── lfs_util.h
```

## 8.2 Features

- Flash-friendly file system
- Power-loss resilience
- Small footprint
- Block wear-leveling

## 8.3 Block Device Implementation

```c
// From app.c
int fs_read(const struct lfs_config *c, lfs_size_t block, ...);
int fs_prog(const struct lfs_config *c, lfs_size_t block, ...);
int fs_erase(const struct lfs_config *c, lfs_size_t block);
```

## 8.4 Reuse Assessment

| Aspect | Score | Notes |
|--------|-------|-------|
| Core Library | 10/10 | Không đổi |
| Block Device | 8/10 | Phụ thuộc SPI Flash driver |
| Configuration | 9/10 | Cần điều chỉnh sector size |

**Kết luận:** ✅ **RECOMMENDED TO REUSE**
- Copy LittleFS core
- Implement block device interface cho GatewayV2 SPI Flash

---

# 9. OTA (Over-The-Air)

## 9.1 Source Location
```
SX-ETH-Gateway/SynaptiX/apps/ota/
├── ota.c
└── ota.h
```

## 9.2 Features (Từ app.c)

```c
// Bootloader info
typedef struct {
    uint8_t isNewFw;
    // Other fields...
} boot_info_t;

void boot_load(boot_info_t *info);
void boot_save(boot_info_t *info);

// OTA check timer
TimerHandle_t ota_check_timer;
```

## 9.3 Reuse Assessment

| Aspect | Score | Notes |
|--------|-------|-------|
| OTA Logic | 7/10 | Cần điều chỉnh cho dual-bank |
| Storage | 6/10 | OctoSPI vs SPI Flash |
| Bootloader | 5/10 | Cần viết mới cho dual-bank |

**Kết luận:** ⚠️ **PARTIAL REUSE**
- Có thể reuse design pattern
- Cần viết mới implementation cho dual-bank OTA

---

# 10. FATFS

## 10.1 Source Location
```
SX-ETH-Gateway/SynaptiX/services/fatfs/
├── App/
│   ├── fatfs.c
│   └── fatfs.h
├── src/
│   ├── ff.c, ff.h
│   ├── diskio.c
│   └── ...
└── Target/
    ├── user_diskio_spi.c  # SPI Flash backend
    └── ffconf.h
```

## 10.2 Reuse Assessment

| Aspect | Score | Notes |
|--------|-------|-------|
| Core FATFS | 10/10 | Standard library |
| SPI Backend | 8/10 | Cần adapter |
| OctoSPI | TBD | Không có, cần viết mới |

**Kết luận:** ⚠️ **CONSIDER LittleFS INSTEAD**
- LittleFS phù hợp hơn cho embedded flash
- FATFS có thể dùng cho USB MSC

---

# 11. Mongoose HTTP Server

## 11.1 Status

Từ code review, `http_server.c` có thể sử dụng Mongoose hoặc raw lwIP HTTP.

**Cần xác nhận:** Đọc `http_server.c` chi tiết để xác định library sử dụng.

## 11.2 Alternatives

| Library | Pros | Cons |
|---------|------|------|
| Mongoose | Full-featured, async | License (GPL/Commercial) |
| lwIP HTTP | Lightweight, no extra deps | Limited features |
| libwebsockets | Modern, secure | Larger footprint |

---

# 12. RS485_RF_IO_V2 Analysis

## 12.1 Contents

Project này chủ yếu chứa:
- STM32H5 HAL drivers
- CMSIS DSP library
- Không có application code đáng chú ý

## 12.2 Reusable Components

| Component | Reuse | Notes |
|-----------|-------|-------|
| DSP Library | ⚠️ | Có thể cần cho signal processing |
| HAL Drivers | ✅ | STM32H5 HAL chuẩn |

## 12.3 Conclusion

**Không có thư viện đáng reuse** từ RS485_RF_IO_V2 cho GatewayV2.

---

# 13. DataloggerV2 (ESP-Hosted)

## 13.1 Source Location
```
DataloggerV2/esp-hosted-stm32-spi-prerelease/
├── host/
│   ├── drivers/
│   │   ├── spi/
│   │   ├── bt/
│   │   └── rpc/
│   └── api/
└── examples/
```

## 13.2 Reuse Assessment

| Component | Status | Reason |
|-----------|--------|--------|
| ESP-Hosted SPI | ❌ | Dùng ESP32-S3, không dùng ST67W611 |
| RPC Protocol | ❌ | Không tương thích |
| BT HCI | ❌ | ESP32 specific |

**Kết luận:** ❌ **NOT REUSABLE**
- ST67W611 và ESP32-S3 có protocol khác nhau
- Cần phát triển driver mới cho ST67W611

---

# 14. Reuse Assessment by README Feature

## 14.1 Multi Network Interface

```
Requirement: Ethernet, WiFi, LTE, USB đồng thời
Project Structure: network_manager/, eth_netif/, wifi_netif/, pppos_netif/, usb_netif/
```

| Component | Source | Reuse | Effort | Notes |
|-----------|--------|-------|--------|-------|
| eth_netif | SX-ETH-Gateway/lwip | ✅ Full | Done | Cùng DP83848 PHY |
| wifi_netif | ST67W611 lib | ✅ Complete | Done | ST67W611 WiFi driver |
| pppos_netif | A7680C AT | ✅ Complete | Done | A7680C PPPoS driver |
| usb_netif | None | ❌ New | 3 days | USB CDC/RNDIS |
| network_manager | Custom | ✅ Complete | Done | Multi-WAN Failover |

## 14.2 Modbus RTU (3 ports)

```
Requirement: Master mode, poll scheduler, register mapping
Porting: USART3, USART6, USART10 (RS485)
Status: ✅ Complete (GatewayV2 uses nanoModbus, not FreeModbus)
```

| Component | Source | Reuse | Effort | Notes |
|-----------|--------|-------|--------|-------|
| nanoModbus library | NanoModbus (single-header) | ✅ Complete | Done | RTU mode + thread-safe mutex |
| RS485 Porting Layer | ports/modbus_port | ✅ Complete | Done | UART + DMA + GPIO DE |
| Poll Scheduler | apps/modbus_master | ✅ Complete | Done | JSON config + multi-device |
| Modbus Master App | None | ❌ New | 1 week | Poll scheduler, register map |
| Register Database | None | ❌ New | 3 days | Holding/Input registers |

## 14.3 Modbus TCP

```
Requirement: TCP Client, TCP Server
Transport: Ethernet, WiFi, LTE
```

| Component | Source | Reuse | Effort | Notes |
|-----------|--------|-------|--------|-------|
| Modbus TCP Server | GatewayV2/apps/modbus_tcp_server | ✅ Complete | Done | nanoModbus TCP + smart routing |
| TCP Client | None | ❌ Future | P2 | Modbus master over TCP |

## 14.4 MQTT/MQTTS

```
Requirement: MQTT, MQTTS, TLS, Auto-reconnect, OTA ready
Protocol: ThingsBoard-compatible
```

| Component | Source | Reuse | Effort | Notes |
|-----------|--------|-------|--------|-------|
| MQTT Core | SX-ETH-Gateway/services/mqtt | ✅ Full | 1 day | lwIP-based MQTT |
| ThingsBoard Wrapper | SX-ETH-Gateway/services/thingsboard | ✅ Full | 1 day | Có thể adapter |
| mbedTLS | SX-ETH-Gateway/mbedtls | ✅ Full | 1 day | HAL wrappers có sẵn |
| MQTT App | GatewayV2/apps/data_broker | ✅ Complete | Done | MQTT publisher với JSON payload |

## 14.5 HTTP/HTTPS Server

```
Requirement: Web configuration, Firmware upgrade, Certificates
License: GPL/Commercial
Status: ✅ Complete (GatewayV2)
```

| Component | Source | Reuse | Effort | Notes |
|-----------|--------|-------|--------|-------|
| Mongoose Core | GatewayV2/middleware/mongoose | ✅ Complete | Done | Amalgamated source + LittleFS |
| HTTPS/TLS | mbedTLS | ✅ Complete | Done | |
| Web UI | GatewayV2/web_root | ✅ Complete | Done | HTML/JS dashboard |

## 14.6 BLE Gateway

```
Requirement: BLE Scanner, GATT Client, BLE to MQTT Bridge
Hardware: ST67W611 module
```

| Component | Source | Reuse | Effort | Notes |
|-----------|--------|-------|--------|-------|
| ST67W611 Driver | None | ❌ New | TBD | Protocol unknown |
| BLE Stack | None | ❌ New | TBD | Phụ thuộc ST67W611 |
| BLE to MQTT | None | ❌ New | 1 week | Application logic |

## 14.7 LTE PPPoS

```
Requirement: 4G LTE via PPPoS
Hardware: A7680C module
```

| Component | Source | Reuse | Effort | Notes |
|-----------|--------|-------|--------|-------|
| A7680C Driver | None | ❌ New | 1 week | AT commands |
| PPPoS Stack | lwIP contrib | ✅ Full | 2 days | Có sẵn trong lwIP |
| Network Interface | None | ❌ New | 3 days | PPP netif integration |

## 14.8 NTP Client

```
Requirement: NTP Client, timezone, RTC sync
```

| Component | Source | Reuse | Effort | Notes |
|-----------|--------|-------|
| NTP Client | GatewayV2/apps/ntp_client | ✅ Complete | Done | lwIP SNTP + JSON config |
| SNTP Library | lwIP contrib | ✅ Full | - | Có sẵn |

## 14.9 OTA Update

```
Requirement: Firmware upgrade qua MQTT/HTTP
Future: Dual-bank OTA
```

| Component | Source | Reuse | Effort | Notes |
|-----------|--------|-------|--------|-------|
| OTA Protocol | apps/ota | ⚠️ Stub | To-do | GatewayV2: stub only |
| Storage | - | ❌ New | 1 week | Dual-bank OctoSPI flash |
| Bootloader | - | ❌ New | 1 week | Dual-bank boot logic |

---

# 16. Integration Architecture

## 16.1 Recommended Project Structure

```
GatewayV2/
├── Core/                     # CubeMX generated
│   ├── Src/
│   └── Inc/
├── apps/                     # Application layer
│   ├── modbus_master/
│   ├── telemetry/
│   ├── ble_gateway/
│   └── config_manager/
├── components/               # Reusable components
│   ├── logger/
│   ├── cqueue/
│   ├── spiflash/
│   ├── littlefs/
│   └── thingsboard/
├── middleware/               # Third-party stacks
│   ├── lwip/
│   ├── mbedtls/
│   ├── mqtt/
│   ├── modbus/
│   ├── mongoose/
│   └── tinyusb/
├── network/                  # Network interfaces
│   ├── eth_netif/
│   ├── wifi_netif/
│   ├── pppos_netif/
│   ├── usb_netif/
│   └── network_manager/
├── drivers/                  # Hardware drivers
├── bsp/                      # Board support
├── utils/                    # Utility functions
│   ├── str2hex/
│   └── cjson/
├── helpers/                  # Helper macros
├── configs/                  # Configurations
├── docs/                     # Documentation
└── tests/                    # Unit tests
```

## 16.2 Layer Architecture

```
┌─────────────────────────────────────────────────────────┐
│                    User Applications                     │
│  Modbus Master | MQTT | HTTP Server | BLE Gateway       │
├─────────────────────────────────────────────────────────┤
│                  Middleware Services                    │
│  Modbus RTU/TCP | MQTT Client | ThingsBoard | NTP     │
├─────────────────────────────────────────────────────────┤
│                   Network Stack                         │
│         lwIP + mbedTLS + Multi-Network Manager         │
├─────────────────────────────────────────────────────────┤
│                Network Interfaces                       │
│  Ethernet | WiFi (ST67W611) | LTE (A7680C) | USB      │
├─────────────────────────────────────────────────────────┤
│                   Drivers / BSP                        │
│  UART/RS485 | SPI Flash | Ethernet PHY | USB           │
├─────────────────────────────────────────────────────────┤
│                    FreeRTOS / HAL                       │
│                   STM32H563VGT6                         │
└─────────────────────────────────────────────────────────┘
```

---

# 17. Recommended Implementation Plan

## Phase 1: Foundation (Week 1-2) - P0

| Task | Source | Effort | Dependency |
|------|--------|--------|------------|
| Project structure + Build | New | 2 days | - |
| Logger (copy directly) | SX-ETH-Gateway | 1 hour | - |
| cQueue (copy directly) | SX-ETH-Gateway | 1 hour | - |
| SPI Flash Driver (W25Q128) | SX-ETH-Gateway | 2 hours | - |
| LittleFS integration | SX-ETH-Gateway | 2 days | SPI Flash |
| lwIP + DP83848 | SX-ETH-Gateway | 2 days | - |
| BSP/Board init | New | 1 day | - |

## Phase 2: Network Interfaces (Week 2-4) - P0

### 2a. Ethernet (Week 2)

| Task | Source | Effort | Dependency |
|------|--------|--------|------------|
| Ethernetif porting | SX-ETH-Gateway | 2 days | lwIP |
| PHY initialization | SX-ETH-Gateway | 1 day | - |
| IP configuration | New | 1 day | lwIP |

### 2b. Network Manager (Week 3)

| Task | Source | Effort | Dependency |
|------|--------|--------|------------|
| Multi-interface framework | New | 3 days | Ethernet, lwIP |
| Priority management | New | 2 days | - |
| Failover logic | New | 2 days | - |
| Route selection | New | 2 days | - |

### 2c. Other Interfaces (Week 4) - P1

| Task | Source | Effort | Dependency |
|------|--------|--------|------------|
| ST67W611 WiFi driver | ✅ Complete | Done | ST67W611 lib + SPI1 |
| A7680C LTE driver | ✅ Complete | Done | AT commands + UART1 |
| PPPoS integration | ✅ Complete | Done | lwIP PPP |
| USB CDC/RNDIS | ❌ Pending | 3 days | - |

## Phase 3: Modbus (Week 3-4) - P0

| Task | Source | Effort | Dependency |
|------|--------|--------|------------|
| FreeModbus core | SX-ETH-Gateway | 2 hours | - |
| RS485 Porting Layer (3 ports) | New | 1 week | UART DMA |
| Modbus Master App | New | 1 week | FreeModbus |
| Modbus TCP | SX-ETH-Gateway | 3 days | lwIP |
| Register Database | New | 3 days | - |

## Phase 4: Cloud Services (Week 4-5) - P0

| Task | Source | Effort | Dependency |
|------|--------|--------|------------|
| mbedTLS | SX-ETH-Gateway | 1 day | - |
| MQTT Client | SX-ETH-Gateway | 2 days | lwIP, mbedTLS |
| ThingsBoard adapter | SX-ETH-Gateway | 2 days | MQTT |
| NTP/SNTP | SX-ETH-Gateway | 1 day | lwIP |
| Telemetry publishing | New | 3 days | Modbus, MQTT |

## Phase 5: Web Configuration (Week 5-6) - P1

| Task | Source | Effort | Dependency |
|------|--------|--------|------------|
| HTTP Server decision | - | 1 day | License decision |
| Mongoose porting | SX-ETH-Gateway | 1 week | mbedTLS |
| OR Custom HTTP server | New | 2 weeks | - |
| Web UI | New | 1 week | HTTP Server |
| Firmware upgrade page | New | 3 days | OTA |

## Phase 6: BLE Gateway (Week 6-8) - P2

| Task | Source | Effort | Dependency |
|------|--------|--------|------------|
| ST67W611 WiFi driver | ✅ Complete | Done | See network/wifi_netif |
| BLE stack integration | ❌ Pending | TBD | ST67W611 |
| BLE to MQTT bridge | ❌ Pending | 1 week | BLE, MQTT |

## Phase 7: OTA (Week 7-8) - P2

| Task | Source | Effort | Dependency |
|------|--------|--------|------------|
| OTA protocol | SX-ETH-Gateway | 2 days | MQTT, HTTP |
| Dual-bank storage | New | 1 week | LittleFS |
| Bootloader | New | 1 week | Flash layout | |

---

# 18. Key Findings Summary

## 18.1 HTTP Server - Mongoose Confirmed

HTTP Server sử dụng **Mongoose** (Cesanta Software):
- HTTPS với mbedTLS
- Web UI với login authentication
- WebSocket cho real-time updates
- File upload/download
- **License: GPL v2 hoặc Commercial**

## 18.2 MQTT - ThingsBoard Custom Implementation

MQTT client là implementation tự viết dựa trên lwIP sockets:
- ThingsBoard protocol (custom topic format)
- RPC handlers
- OTA firmware update
- TLS support với mbedTLS

## 18.3 License Considerations

| Component | License | Notes |
|-----------|---------|-------|
| Mongoose | GPL v2 / Commercial | Cần commercial license nếu không public source |
| lwIP | BSD | Không vấn đề |
| FreeModbus | BSD | Không vấn đề |
| mbedTLS | Apache 2.0 / GPL | Không vấn đề |
| ThingsBoard client | Custom? | Cần kiểm tra |
| GatewayV2 | MIT | README specifies MIT license |

## 18.4 HTTP Server License Decision

**Options:**

| Option | Pros | Cons | Recommendation |
|--------|------|------|----------------|
| Keep Mongoose (GPL) | Đầy đủ features, đã có | Phải public source code | ⚠️ Nếu đồng ý public |
| Buy Commercial | Đầy đủ features | Chi phí license | 💰 Tùy budget |
| Replace with lwIP httpd | Nhẹ, BSD | Ít features, basic | Basic web config only |
| Custom HTTP + Mongoose FS | Kiểm soát + reuse | Phức tạp hơn | Nếu cần hybrid |

**Recommendation:** Nếu GatewayV2 muốn MIT license, cần:
1. Mua Mongoose commercial license, hoặc
2. Thay Mongoose bằng lwIP httpd cho basic web config, hoặc
3. Implement lightweight HTTP server mới

## 18.5 Unknown Components - Cần Research

| Component | Issue | Action |
|-----------|-------|--------|
| ST67W611 | Protocol/API chưa xác định | Cần ST67W611 datasheet/API docs |
| A7680C | AT command set cần xác định | Cần A7680C AT command manual |
| BLE Protocol | Chưa rõ BLE gateway protocol | Cần spec từ ST67W611 |

---

# 19. Next Steps

1. ✅ ~~Đọc chi tiết http_server.c~~ - CONFIRMED: Mongoose
2. ✅ ~~Đọc mqtt_app.c~~ - CONFIRMED: ThingsBoard custom impl
3. ✅ ~~Đọc README.md~~ - CONFIRMED: Requirements
4. **Quyết định HTTP Server license** - Mongoose GPL vs alternative
5. **Check ST67W611 datasheet** - Develop driver mới
6. **Check A7680C AT commands** - Develop LTE driver
7. **Design Network Manager** - Multi-interface architecture
8. **Create project structure** - Bắt đầu Phase 1

---

**Document Status:** Complete - Đã xác minh tất cả components

**Last Updated:** 2026-07-07
