# Project Structure Guide

## Folder Overview (từ CMakeLists.txt)

```
GatewayV2/
├── Core/                    # CubeMX generated code
├── Drivers/                 # ST HAL/LL drivers (spiflash, uart, modem)
├── GatewayV2.ioc            # CubeMX configuration
│
├── components/              # Reusable components (cqueue, mqtt_client, file_io)
├── libs/                    # External Git Submodules (mbedtls, littlefs, cJSON, logger, tinyusb)
├── middleware/              # Third-party protocol stacks (lwip, nanomodbus, mongoose)
├── network/                 # Network interfaces (manager, eth, wifi, pppos, usb)
├── ports/                   # HAL bindings & adapters
├── bsp/                     # Board support package
├── utils/                   # Utility functions
├── apps/                    # Application layer (business logic)
│   ├── main/               # 👈 ENTRY POINT - gateway.c
│   ├── modbus_master/      # Modbus RTU Master (3 ports)
│   ├── modbus_tcp_server/  # Modbus TCP Server (SCADA/PLC bridge)
│   ├── data_broker/        # Cloud data broker (MQTT publisher)
│   ├── http_server/        # Web Dashboard (Mongoose + LittleFS)
│   ├── ota/                # Firmware OTA (Dual-Path: Network swap + USB DFU)
│   ├── ntp_client/        # NTP time synchronization
│   ├── config/             # Configuration management (distributed JSON)
│   └── shell/              # CLI Shell (UART shell)
└── docs/                    # Documentation
```

---

## 1. apps/ - Application Layer

High-level business logic. **Phụ thuộc components/middleware/libs, KHÔNG phụ thuộc hardware trực tiếp.**

```
apps/
├── main/                    # 👈 ENTRY POINT - ĐIỂM KHỞI ĐẦU
│   ├── main.c              # main(), SystemClock_Config()
│   ├── main.h
│   ├── gateway.c           # Gateway state machine & init
│   └── gateway.h
│
├── modbus_master/          # Modbus RTU Master (3 ports) + JSON config
│   ├── modbus_master.c     # Poll scheduler, register mapping
│   ├── modbus_master.h
│   ├── modbus_parser.c     # JSON config parser + scaling (multiplier/offset)
│   └── modbus_parser.h
│
├── modbus_tcp_server/     # Modbus TCP Server (SCADA/PLC transparent bridge)
│   ├── modbus_tcp_server.c  # nanoModbus TCP wrapper + smart routing
│   └── modbus_tcp_server.h
│
├── data_broker/            # Cloud data broker (MQTT publisher)
│   ├── data_broker.c      # Data collection & MQTT publish
│   └── data_broker.h
│
├── ntp_client/             # NTP time synchronization (lwIP SNTP)
│   ├── ntp_client.c        # Time sync, timezone support
│   └── ntp_client.h
│
├── http_server/            # Web Dashboard (Mongoose + LittleFS)
│   ├── http_server.c        # REST API: /api/status, /api/config, /api/ota
│   └── http_server.h
│
├── ota/                    # Firmware OTA (Dual-Path: Network swap + USB DFU)
│   ├── ota.c
│   └── ota.h
│
├── config/                 # Configuration management (distributed pattern)
│   ├── config_manager.c   # JSON loader/saver + callback registry
│   └── config_manager.h
│
└── shell/                 # CLI Shell (UART debug console)
    ├── shell_serial.c      # UART shell task
    ├── cli_shell_command.c # Commands: ping, reboot, config, ls, cat, rm
    └── cli_shell.c         # Core shell engine (character processing)
```

### Config Manager Callback Pattern

Config Manager sử dụng **Distributed Notification Pattern** để notify các module khi config thay đổi:

```c
// Đăng ký với Config Manager (trong module init)
void config_manager_register_node(
    const char *node_name,      // Tên node JSON (vd: "network", "modbus")
    config_load_cb_t load_cb,   // Callback parse JSON → RAM
    config_save_cb_t save_cb,    // Callback serialize RAM → JSON
    config_notify_cb_t notify_cb // Callback khi config thay đổi (Runtime)
);

// Các module đã đăng ký:
// - network_manager: Nhận notify → reload network interface
// - modbus_master: Nhận notify → reload Modbus polling config
```

---

## 2. components/ - Reusable Components

Business-agnostic modules có thể dùng chung cho nhiều dự án. **KHÔNG phụ thuộc hardware trực tiếp.**

```
components/
├── cqueue/                 # Lock-free circular queue
│   ├── cqueue.c
│   └── cqueue.h
│
├── mqtt_client/            # MQTT client (lwIP-based)
│   └── mqtt_client.c/h
│
├── file_io/                # File operations (LittleFS wrapper)
│   ├── file_io.c
│   └── file_io.h
```

---

## 3. libs/ - External Dependencies (Git Submodules)

Đây là nơi chứa toàn bộ các thư viện được tích hợp dưới dạng Git Submodule để dễ dàng quản lý phiên bản và cập nhật từ repo gốc:

```
libs/
├── logger/                  # Thread-safe bounded logging submodule
├── littlefs/                # Flash file system (ARM Mbed)
├── cJSON/                   # JSON parsing library
├── mbedtls/                 # TLS/SSL cryptography library
├── tinyusb/                 # USB Device stack
└── stm32-dp83848/           # ETH PHY DP83848 Driver
```

---

## 4. middleware/ - Third-Party Protocol Stacks

Chứa các middleware được nhúng trực tiếp vào source code (không dùng submodule). **KHÔNG BAO GIỜ SỬA CODE TRONG ĐÂY.**

```
middleware/
├── lwip/                    # TCP/IP stack
│   ├── src/                # lwIP core (DHCP, TCP, UDP, PPPoS)
│   └── Target/            # Board-specific (ethernetif, PHY driver)
│
├── nanomodbus/             # Modbus RTU/TCP (nanoModbus library)
│   └── nanomodbus.h       # Single-file header library
│
└── mongoose/               # HTTP/HTTPS server
    ├── mongoose.c          # Amalgamated source
    ├── mongoose.h
    ├── mongoose_glue.c     # HAL time glue
    ├── mongoose_glue.h
    ├── mongoose_lfs.c      # LittleFS adapter for static files
    └── mongoose_lfs.h
```

---

## 5. ports/ - HAL Bindings & Adapters

Tách HAL-dependent code ra khỏi middleware và libs để dễ upgrade.

```
ports/
├── modbus_port/              # NanoModbus HAL binding (RTU over UART)
│   ├── mmb_serial.c        # Thread-safe wrapper với mutex
│   └── mmb_serial.h
│
├── lwip_port/               # lwIP network interface
│   └── ethernetif.c         # ETH MAC driver (DP83848)
│
├── littlefs_port/           # LittleFS block device
│   ├── block_device.c      # SPI Flash backend (W25Q128)
│   └── block_device.h
│
├── mongoose_port/            # Mongoose HTTP server
│   ├── mongoose_glue.c     # Glue functions (time)
│   ├── mongoose_lfs.c      # LittleFS adapter for static files
│   ├── mongoose_glue.h
│   └── mongoose_lfs.h
│
├── st67w611_port/           # ST67W611 WiFi/BLE SPI porting
│   ├── wifi_spi_port.c    # SPI interface (SPI1)
│   └── wifi_spi_port.h
│
├── spiflash_port/           # SPI Flash HAL (W25Q128)
│   └── spiflash.c
│
├── usb_port/                # USB / TinyUSB Porting
│   └── Target/
│       ├── tusb_config.h
│       └── usb_descriptors.c
│
└── octospi/                 # OctoSPI driver (MX25LM51245)
    └── octospi_driver.c
```

---

## 6. network/ - Network Interfaces

Network interface implementations cho GatewayV2.

```
network/
├── network_manager/          # Multi-interface manager with failover
│   ├── network_manager.c   # Init, failover, priority, config
│   └── network_manager.h
│
├── eth_netif/               # Ethernet (STM32 ETH MAC + DP83848)
│   ├── eth_driver.c
│   └── eth_driver.h
│
├── wifi_netif/              # WiFi (ST67W611 NCP mode)
│   ├── wifi_driver.c       # W6X_* API wrapper
│   └── wifi_driver.h
│
├── pppos_netif/            # LTE PPPoS (A7680C AT modem)
│   ├── pppos_driver.c      # PPPoS over UART1
│   └── pppos_driver.h
│
└── usb_netif/              # USB Network (CDC-RNDIS via TinyUSB)
    ├── usb_netif.c
    └── usb_netif.h
```

---

## 7. drivers/ - Hardware Drivers

Low-level drivers giao tiếp trực tiếp với HAL. **Phụ thuộc hardware.**

```
drivers/
├── uart/                    # UART/RS485 drivers
│   └── rs485_driver.c       # Direct dispatch ISR, DE control
├── modem/                   # LTE modem driver (A7680C)
│   └── a7680c.c            # UART RX DMA, ring buffer
├── spiflash/                # SPI Flash HAL (W25Q128)
│   └── spif.c
└── octospi/                 # OctoSPI driver
    └── octospi_driver.c
```

---

## 8. bsp/ - Board Support Package

Board-level abstractions. **Phụ thuộc hardware nhưng KHÔNG phụ thuộc middleware.**

```
bsp/
├── bsp.c                    # Board initialization
├── bsp.h
└── board.h                  # Board definitions
```

---

## 9. utils/ - Utility Functions

Utility functions và macros. **KHÔNG phụ thuộc hardware hay framework.**

```
utils/
└── cqueue/                  # Lock-free circular queue
    ├── cqueue.c
    └── cqueue.h
```

---

## Category Definitions

| Category | Definition | Rule |
|----------|------------|------|
| **apps/** | Business logic, orchestration | Phụ thuộc components/middleware/libs |
| **components/** | Business-agnostic, reusable | KHÔNG phụ thuộc hardware |
| **libs/** | External Git Submodules | Mã nguồn của bên thứ 3 |
| **middleware/** | Third-party protocol stacks | KHÔNG SỬA code |
| **ports/** | HAL bindings | Chỉ chứa adapter code |
| **network/** | Network interfaces | Hardware-specific |
| **drivers/** | Hardware/HAL drivers | Phụ thuộc hardware |
| **bsp/** | Board initialization | Phụ thuộc board |
| **utils/** | Pure utilities | KHÔNG phụ thuộc gì |

---

## Initialization Order (từ gateway.c)

```c
// 1. Core (CubeMX)
HAL_Init();
SystemClock_Config();

// 2. Logger
logger_init(LOGGER_INFO, output_callback);

// 3. Drivers & Storage
spiflash_init();           // W25Q128 SPI Flash
lfs_mount_fs();            // LittleFS mount
file_io_mkdir("/web_root"); // Create web root for HTTP

// 4. Register Config Nodes (distributed pattern)
network_config_register();   // → register("network", load, save, notify)
ntp_config_register();      // → register("ntp", load, save, notify)
modbus_config_register();    // → register("modbus", load, save, notify)

// 5. Load Config from LittleFS
config_load();              // Parse JSON → RAM + call all load callbacks

// 6. Initialize Modules
network_manager_init();     // Multi-WAN manager
usb_netif_init();           // USB CDC RNDIS (disabled by default during bring-up)
ntp_client_init();          // NTP time sync
modbus_master_init();       // Modbus RTU Master (3 ports)

// 7. Modbus TCP Server init (chờ network lên mới bind)
modbus_tcp_server_init();   // Task chờ IP xong mới accept

// 8. Init cloud & web
data_broker_init();         // MQTT publisher
http_server_init();         // Web Dashboard (Mongoose)
ota_init();                 // OTA init (bootloader lib + partition table)

// 9. Create FreeRTOS Tasks
xTaskCreate(network_task, "NET", ...);      // GW_TASK_PRIO_NETWORK (12)
xTaskCreate(network_failover_task, "FAIL", ...); // application failover priority
xTaskCreate(modbus_task, "MB_P0", ..., 0); // GW_TASK_PRIO_MODBUS (11)
xTaskCreate(modbus_task, "MB_P1", ..., 1); // GW_TASK_PRIO_MODBUS (11)
xTaskCreate(modbus_task, "MB_P2", ..., 2); // GW_TASK_PRIO_MODBUS (11)
xTaskCreate(data_broker_task, "DB_TASK", ...); // GW_TASK_PRIO_BROKER (11)
xTaskCreate(shell_serial_task, "SHELL", ...);  // GW_TASK_PRIO_SHELL (11)

// 10. Start Scheduler
vTaskStartScheduler();
```

The system monitor is a separate low-priority task and reports runtime health every
`GW_MONITOR_PERIOD_MS` (currently 30 seconds). The default task must remain a
cooperative application loop and must not be used as a busy monitoring loop.

### HTTP Server REST API

```
GET  /api/status          → Device status (uptime, network, memory)
GET  /api/config         → Current configuration JSON
POST /api/config         → Save config → triggers notify callbacks
POST /api/reboot         → Reboot device
POST /api/firmware_update → Upload firmware binary (admin auth required)
GET  /                   → Web Dashboard (index.html from LittleFS)
```

---

## Library Sources Summary

| Module | Source | Status |
|--------|---------|--------|
| **libs/logger** | Git Submodule | ✅ Ready |
| **libs/littlefs** | Git Submodule | ✅ Ready |
| **libs/cJSON** | Git Submodule | ✅ Ready |
| **libs/mbedtls** | Git Submodule | ✅ Ready |
| **libs/tinyusb** | Git Submodule | ✅ Ready |
| **libs/stm32-dp83848** | Git Submodule | ✅ Ready |
| **middleware/lwip** | ST Cube | ✅ Ready |
| **middleware/nanomodbus** | NanoModbus library | ✅ Ready |
| **middleware/mongoose** | Mongoose amalgamation | ✅ Ready |
| **components/cqueue** | Custom | ✅ Ready |
| **components/mqtt_client** | MQTT-C library | ✅ Ready |
| **components/cli_shell** | Custom | ✅ Ready |

---
