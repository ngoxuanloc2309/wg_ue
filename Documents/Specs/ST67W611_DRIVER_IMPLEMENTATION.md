# ST67W611 WiFi Driver Implementation Guide

## 1. Tổng quan

### 1.1 Module ST67W611

ST67W611 là WiFi/BLE module của STMicroelectronics, hoạt động như **NCP (Network Co-Processor)**. STM32H5 (host) giao tiếp với module qua SPI và chạy lwIP stack trực tiếp.

### 1.2 Kiến trúc T02 - NCP with LwIP on Host

```
┌─────────────────────────────────────────────────────────────┐
│                      STM32H5 Host                          │
│  ┌─────────────┐    ┌─────────────┐    ┌─────────────┐    │
│  │  Application │    │    lwIP     │    │ ST67W611    │    │
│  │             │◄──►│   Stack     │◄──►│   Driver    │    │
│  └─────────────┘    └─────────────┘    └──────┬──────┘    │
│                                                │            │
└────────────────────────────────────────────────┼────────────┘
                                                 │ SPI
                                        ┌────────▼────────┐
                                        │   ST67W611     │
                                        │   (NCP)        │
                                        │ - WiFi MAC/PHY │
                                        │ - BLE Core     │
                                        │ - RF Frontend  │
                                        └─────────────────┘
```

---

## 2. Kết nối phần cứng

### 2.1 Pinout chính thức (từ Hardware Doc)

| MCU Pin | GPIO | Schematic Net | Direction | Function | Firmware |
|--------:|------|---------------|-----------|----------|----------|
| 88 | PD7 | ST67_NCS | Output | SPI chip select | st67_driver |
| 89 | PB3 | ST67_SCK | Output | SPI clock | st67_driver |
| 90 | PB4 | ST67_MISO | Input | SPI MISO | st67_driver |
| 91 | PB5 | ST67_MOSI | Output | SPI MOSI | st67_driver |
| 81 | PD0 | ST67_RDY | Input | Module ready / interrupt | st67_driver |
| 82 | PD1 | ST67_EN | Output | Module enable | st67_driver |
| 83 | PD2 | ST67_BOOT | Output | Boot mode select | st67_driver |
| 84 | PD3 | ST67_Wakeup | Output | Wakeup control | st67_driver |
| 85 | PD4 | ST67_32K | Clock/GPIO | 32 kHz reference | st67_driver |
| 86 | PD5 | ST67_RX | Input | Auxiliary UART RX | st67_driver |
| 87 | PD6 | ST67_TX | Output | Auxiliary UART TX | st67_driver |

### 2.2 SPI Configuration

- **SPI Instance**: **SPI1** (NOT SPI2)
- **Pins**: PB3 (SCK), PB4 (MISO), PB5 (MOSI) - default SPI1 pins
- **Mode**: Full duplex
- **Clock**: up to 50 MHz
- **DMA**: Enable TX and RX DMA

### 2.3 GPIO Configuration

| GPIO | Mode | Pull | Notes |
|------|------|------|-------|
| PD7 (NCS) | Push-Pull Output | - | Chip select, active low |
| PB3 (SCK) | Alternate Function | - | SPI2 SCK |
| PB4 (MISO) | Alternate Function | - | SPI2 MISO |
| PB5 (MOSI) | Alternate Function | - | SPI2 MOSI |
| PD0 (RDY) | Input | Pull-down | Module ready interrupt |
| PD1 (EN) | Push-Pull Output | - | Module enable, active high |
| PD2 (BOOT) | Push-Pull Output | - | Boot mode (normal = high) |
| PD3 (Wakeup) | Push-Pull Output | - | Wakeup control |
| PD4 (32K) | Analog | - | 32.768 kHz reference input |

### 2.4 Power Sequence

```
ST67_EN (PD1) ─┐
                ├──► Module Power On/Off
ST67_BOOT (PD2) ─► Normal mode: HIGH, Bootloader: LOW

ST67_RDY (PD0) ──► Input to MCU
                          │
                    Module ready signal
                    (goes HIGH when ready)
```

---

## 3. Thư viện ST67W611 (x-cube-st67w61)

### 3.1 Cấu trúc thư viện

```
libs/x-cube-st67w61/
├── Middlewares/ST/ST67W6X_Network_Driver/
│   ├── Api/                    # Public API
│   │   ├── w6x_api.h          # Main API functions
│   │   ├── w6x_types.h        # Type definitions
│   │   └── w6x_legacy.h       # Legacy API
│   ├── Driver/
│   │   ├── W61_at/            # AT command driver
│   │   ├── W61_bus/           # SPI bus interface
│   │   │   ├── spi_iface.h    # SPI interface definitions
│   │   │   └── spi_port.h     # SPI porting layer
│   │   └── W61_driver.h      # Main driver
│   └── Conf/                  # Configuration templates
│       └── w6x_config_template.h
├── Projects/
│   └── NUCLEO-H563ZI/         # Compatible board (STM32H5)
│       └── ST67W6X_CLI_LWIP/  # Reference implementation
│           ├── ST67W6X/App/app_st67w6x.c
│           └── ST67W6X/Target/spi_port.c
└── NCP_Binary/               # Pre-built firmware
```

### 3.2 Các API chính

#### System APIs
```c
W6X_Status_t W6X_Init(void);                    // Initialize driver
void W6X_DeInit(void);                           // De-initialize
W6X_Status_t W6X_RegisterAppCb(W6X_App_Cb_t *app_cb); // Register callbacks
W6X_Status_t W6X_Reset(uint8_t restore);         // Reset module
```

#### WiFi APIs
```c
W6X_Status_t W6X_WiFi_Init(void);               // Initialize WiFi
void W6X_WiFi_DeInit(void);                      // De-initialize WiFi
W6X_Status_t W6X_WiFi_Connect(W6X_WiFi_Connect_Opts_t *opts); // Connect to AP
W6X_Status_t W6X_WiFi_Disconnect(uint32_t restore); // Disconnect
W6X_Status_t W6X_WiFi_Station_GetState(W6X_WiFi_StaStateType_e *state,
                                        W6X_WiFi_Connect_t *data);
W6X_Status_t W6X_WiFi_Station_GetMACAddress(uint8_t mac[6]);
```

#### Net APIs (for lwIP integration)
```c
W6X_Status_t W6X_Net_Init(void);                // Initialize network
W6X_Status_t W6X_Net_Station_GetIPAddress(uint8_t ip[4], uint8_t gw[4], uint8_t mask[4]);
W6X_Status_t W6X_Net_Station_SetIPAddress(uint8_t ip[4], uint8_t gw[4], uint8_t mask[4]);
W6X_Status_t W6X_Net_SetHostname(uint8_t hostname[33]);
```

### 3.3 SPI Interface

```c
// Initialize SPI transaction
int32_t spi_transaction_init(void);

// Read/Write data
int32_t spi_read(struct spi_msg *msg, int32_t timeout_ms);
int32_t spi_write(struct spi_msg *msg, int32_t timeout_ms);

// Register callback for received data
int32_t spi_rxd_callback_register(spi_msg_ctrl_t type, spi_rxd_notify_func_t cb, void *arg);
```

---

## 4. Triển khai Driver

### 4.1 Cấu trúc thư mục

```
network/wifi_netif/
├── CMakeLists.txt
├── wifi_driver.h
├── wifi_driver.c
├── wifi_spi_port.c      # SPI porting layer (từ thư viện)
└── wifi_spi_port.h
```

### 4.2 Checklist triển khai

#### Phase 1: Hardware Setup (CubeMX)
- [ ] **SPI1 Configuration**
  - [ ] Mode: Full Duplex Master
  - [ ] Clock: 50 MHz (max)
  - [ ] Data size: 8 bits
  - [ ] CPOL: Low
  - [ ] CPHA: 1 Edge
  - [ ] Enable TX DMA
  - [ ] Enable RX DMA

- [ ] **GPIO Configuration**
  - [ ] PB3 → SPI1_SCK (AF5)
  - [ ] PB4 → SPI1_MISO (AF5)
  - [ ] PB5 → SPI1_MOSI (AF5)
  - [ ] PD7 → GPIO_Output (ST67_NCS)
  - [ ] PD0 → GPIO_Input (ST67_RDY), Pull-down
  - [ ] PD1 → GPIO_Output (ST67_EN)
  - [ ] PD2 → GPIO_Output (ST67_BOOT)
  - [ ] PD3 → GPIO_Output (ST67_Wakeup)

- [ ] **Project Settings**
  - [ ] Add define: `ST67_ARCH=W6X_ARCH_T02`
  - [ ] Add include path: `Middlewares/ST/ST67W6X_Network_Driver/Api`
  - [ ] Add source files từ thư viện

#### Phase 2: SPI Porting Layer
- [ ] Copy/adapt `spi_port.c` từ reference project
- [ ] Map với STM32H5 pins:
  - `NCP_SPI_HANDLE` → **hspi1** (NOT hspi2!)
  - `CHIP_EN_GPIO_Port` → GPIOD
  - `CHIP_EN_Pin` → GPIO_PIN_1 (PD1 = ST67_EN)
  - `SPI_RDY_GPIO_Port` → GPIOD
  - `SPI_RDY_Pin` → GPIO_PIN_0 (PD0 = ST67_RDY)
  - `SPI_CS_GPIO_Port` → GPIOD
  - `SPI_CS_Pin` → GPIO_PIN_7 (PD7 = ST67_NCS)

#### Phase 3: Driver Implementation
- [ ] Implement `wifi_driver_init()`
  - [ ] Initialize SPI via `spi_transaction_init()`
  - [ ] Call `W6X_Init()`
  - [ ] Register WiFi event callback
  - [ ] Call `W6X_WiFi_Init()`
  - [ ] Create lwIP netif via `netif_add()`

- [ ] Implement `wifi_driver_connect(ssid, password)`
  - [ ] Build `W6X_WiFi_Connect_Opts_t`
  - [ ] Call `W6X_WiFi_Connect()`

- [ ] Implement WiFi event handler
  - [ ] Handle `W6X_WIFI_EVT_CONNECTED_ID`
  - [ ] Handle `W6X_WIFI_EVT_DISCONNECTED_ID`
  - [ ] Handle `W6X_WIFI_EVT_GOT_IP_ID`

#### Phase 4: Network Manager Integration
- [ ] Test WiFi standalone
- [ ] Test failover: ETH → WiFi
- [ ] Test failback: WiFi → ETH

---

## 5. Event Flow

```
Host Startup
    │
    ▼
┌─────────────────────────────┐
│ wifi_driver_init()           │
│   ├── spi_transaction_init() │ ──► Initialize SPI
│   ├── W6X_Init()            │ ──► Initialize NCP driver
│   ├── W6X_RegisterAppCb()   │ ──► Register callbacks
│   ├── W6X_WiFi_Init()       │ ──► Initialize WiFi
│   └── netif_add()           │ ──► Add to lwIP
└─────────────────────────────┘
    │
    ▼
┌─────────────────────────────┐
│ wifi_driver_connect()       │
│   └── W6X_WiFi_Connect()   │ ──► Send connect command
└─────────────────────────────┘
    │
    ▼ (Async event)
┌─────────────────────────────┐
│ wifi_event_handler()        │
│   ├── CONNECTED ───────────►│ wifi_connected = 1
│   ├── GOT_IP ─────────────►│ netif_set_addr()
│   └── DISCONNECTED ───────►│ wifi_connected = 0
└─────────────────────────────┘
```

---

## 6. Tham khảo

1. **Pinout**: `Documents/Hardware/02_PINOUT_AND_INTERFACES.md` - Section 3.2
2. **Power**: `Documents/Hardware/03_POWER_CLOCK_MEMORY.md` - Section về ST67W611
3. **Library**: `libs/x-cube-st67w61/`
4. **Reference**: `libs/x-cube-st67w61/Projects/NUCLEO-H563ZI/Applications/ST67W6X/ST67W6X_CLI_LWIP/`
5. **SPI Port**: `libs/x-cube-st67w61/Projects/NUCLEO-H563ZI/Applications/ST67W6X/ST67W6X_CLI_LWIP/ST67W6X/Target/spi_port.c`
6. **API**: `libs/x-cube-st67w61/Middlewares/ST/ST67W6X_Network_Driver/Api/w6x_api.h`

---

## 7. Lưu ý quan trọng

1. **SPI Timing**: Module sử dụng SPI ready pin (PD0) để báo hiệu data sẵn sàng. Host phải poll hoặc sử dụng interrupt.

2. **Firmware**: Module có thể cần NCP firmware binary. Kiểm tra trong `libs/x-cube-st67w61/NCP_Binary/`.

3. **Power Sequence**:
   - Bật ST67_EN (PD1) trước
   - Chờ ST67_RDY (PD0) lên HIGH
   - Sau đó mới giao tiếp SPI

4. **Boot Mode**: ST67_BOOT (PD2) = HIGH cho normal operation

---

*Document version: 1.1*
*Last updated: 2026-07-08*
*Verified against: Documents/Hardware/02_PINOUT_AND_INTERFACES.md*
