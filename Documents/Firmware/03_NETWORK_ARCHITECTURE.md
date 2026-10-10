# Network Layer Architecture

**Document ID:** FW-NET-001  
**Version:** 1.0  
**Status:** Complete  
**Project:** GatewayV2

---

# 1. Overview

This document describes the complete Network Layer architecture for GatewayV2, including all network interfaces (Ethernet, WiFi, LTE), the Network Manager with Multi-WAN failover, and integration with lwIP.

---

# 2. Architecture Summary

```
┌─────────────────────────────────────────────────────────────────────┐
│                        Application Layer                             │
│              (MQTT, HTTP, OTA, Modbus TCP, etc.)                   │
└─────────────────────────────────────────────────────────────────────┘
                                  │
                                  ▼
┌─────────────────────────────────────────────────────────────────────┐
│                      Network Manager                                 │
│  • Multi-interface management                                       │
│  • Priority-based failover (ETH > WiFi > LTE)                     │
│  • Configuration (JSON from LittleFS)                              │
│  • Status monitoring & event handling                               │
└─────────────────────────────────────────────────────────────────────┘
                                  │
          ┌───────────────────────┼───────────────────────┐
          ▼                       ▼                       ▼
┌─────────────────┐   ┌─────────────────┐   ┌─────────────────┐
│   ETH Netif     │   │  WiFi Netif     │   │   LTE Netif     │
│  (RMII/DP83848)│   │ (ST67W611/SPI1) │   │ (A7680C/UART1)  │
└─────────────────┘   └─────────────────┘   └─────────────────┘
          │                       │                       │
          ▼                       ▼                       ▼
┌─────────────────┐   ┌─────────────────┐   ┌─────────────────┐
│   STM32 ETH     │   │    SPI1 +       │   │   UART1 +       │
│    MAC + DMA    │   │    GPDMA        │   │    GPDMA        │
└─────────────────┘   └─────────────────┘   └─────────────────┘
```

---

# 3. Network Interfaces

## 3.1 Ethernet (RMII)

| Property | Value |
|----------|-------|
| PHY | DP83848 |
| Interface | RMII (Reduced Media Independent Interface) |
| Driver | `eth_driver` (STM32 HAL ETH) |
| Location | `network/eth_netif/` |
| Data Rate | 10/100 Mbps |
| DMA | GPDMA (Half-duplex/Tx, Half-duplex/Rx) |

### Key Files
- `network/eth_netif/eth_driver.c`
- `ports/lwip_port/ethernetif.c`

## 3.2 WiFi (ST67W611)

| Property | Value |
|----------|-------|
| Module | ST67W611 |
| Interface | SPI1 (PB3-SCK, PB4-MISO, PB5-MOSI) |
| Driver | `wifi_driver` using W6X_* API |
| Location | `network/wifi_netif/` |
| Port | `ports/st67w611_port/` |
| Architecture | T02 - NCP with LwIP on Host |
| Data Rate | Up to 72 Mbps |
| DMA | GPDMA |

### GPIO Control
| Pin | Function | Notes |
|-----|----------|-------|
| PD7 | CS | Chip Select |
| PD0 | RDY | Ready/Interrupt |
| PD1 | EN | Enable |
| PD2 | BOOT | Boot mode |
| PD3 | Wakeup | Wake control |
| PD4 | 32K | 32kHz reference |

### Key Files
- `network/wifi_netif/wifi_driver.c`
- `network/wifi_netif/wifi_driver.h`
- `ports/st67w611_port/st67w611_port.c`

### API Usage
```c
// Initialize
struct netif* wifi_netif = wifi_driver_init();

// Connect to AP
wifi_driver_connect("MySSID", "MyPassword");

// Get status
uint8_t status = wifi_driver_status();
int8_t rssi = wifi_driver_get_rssi();
```

## 3.3 LTE (A7680C)

| Property | Value |
|----------|-------|
| Module | A7680C |
| Interface | UART1 (PB6-RX, PB7-TX) |
| Driver | `pppos_driver` (PPPoS - Point-to-Point Protocol over Serial) |
| Location | `network/pppos_netif/` |
| Data Mode | PPP over UART |
| DMA | GPDMA with Idle Detection |

### GPIO Control
| Pin | Function | Notes |
|-----|----------|-------|
| PB8 | PWR | Power Key (1200ms pulse) |
| PB9 | RST | Hardware Reset |

### State Machine
```
IDLE → POWER_ON → INIT → AT_READY → NETWORK_SEARCH → REGISTERED → CONNECTING → PPP_MODE → CONNECTED
```

### Features
- Automatic Hard Reset on timeout
- Auto-reconnect on disconnect
- GPDMA Idle Detection for efficient RX
- Ring buffer for AT command response

### Key Files
- `network/pppos_netif/pppos_driver.c`
- `network/pppos_netif/pppos_driver.h`

### API Usage
```c
// Initialize
struct netif* lte_netif = lte_driver_init();

// Set APN (optional, default: "internet")
pppos_driver_set_apn("internet");

// Start connection
lte_driver_start();

// Get status
uint8_t status = pppos_driver_status();
int csq = pppos_driver_get_csq();
```

---

# 4. Network Manager

## 4.1 Overview

The Network Manager (`network_manager`) is the central coordination point for all network interfaces. It provides:

- Interface registration and lifecycle management
- Status monitoring and failover
- Configuration management (JSON)
- Priority-based routing

## 4.2 Location

```
network/network_manager/
├── network_manager.c
└── network_manager.h
```

## 4.3 Key Features

| Feature | Description |
|---------|-------------|
| Multi-interface | Supports ETH, WiFi, LTE simultaneously |
| Priority-based | Default: ETH > WiFi > LTE |
| Failover | Automatic switching on link loss |
| Debounce | 3-second debounce to prevent flapping |
| Config Persistence | JSON stored in LittleFS |
| Thread-safe | Uses mutex for config access |

## 4.4 Configuration

Configuration is stored in `config/network.json`:

```json
{
    "eth": {
        "enabled": true,
        "dhcp": true,
        "priority": 0
    },
    "wifi": {
        "enabled": true,
        "dhcp": true,
        "priority": 1,
        "ssid": "MyNetwork",
        "password": "MyPassword"
    },
    "lte": {
        "enabled": true,
        "dhcp": true,
        "priority": 2,
        "apn": "internet"
    }
}
```

## 4.5 API Reference

### Initialization
```c
void network_manager_init(void);
int network_manager_load_config(void);
int network_manager_save_config(const network_config_t *config);
```

### Interface Management
```c
int network_interface_enable(net_if_type_t if_type);
int network_interface_disable(net_if_type_t if_type);
net_if_status_t network_interface_get_status(net_if_type_t if_type);
struct netif* network_interface_get_netif(net_if_type_t if_type);
```

### Failover Control
```c
int network_set_primary_interface(net_if_type_t if_type);
net_if_type_t network_get_active_interface(void);
int network_switch_to_interface(net_if_type_t if_type);
```

### Status Queries
```c
net_state_t network_get_state(net_if_type_t if_type);
const char* network_get_ip_address(net_if_type_t if_type);
```

## 4.6 Event Bits

| Bit | Event |
|-----|-------|
| LWIP_READY_BIT | lwIP initialization complete |
| ETH_STATUS_BIT | Ethernet status change |
| WIFI_STATUS_BIT | WiFi status change |
| LTE_STATUS_BIT | LTE status change |

---

# 5. lwIP Integration

## 5.1 PPP Support

The lwIP stack is configured to include PPP support for LTE:

```
middleware/lwip/src/netif/ppp/*.c
```

Configuration (lwipopts.h):
- `LWIP_PPP` enabled
- `LWIP_PPP_API` enabled
- PAP/CHAP disabled (no mbedtls dependency)

## 5.2 Netif Registration

Each driver registers its interface with lwIP:

```c
// Ethernet - uses default ethernetif
netif_add(&netif, &ipaddr, &netmask, &gw, NULL, ethernetif_init, tcpip_input);

// WiFi/LTE - uses custom input (tcpip_input directly)
netif_add(&netif, &ipaddr, &netmask, &gw, NULL, NULL, tcpip_input);
```

---

# 6. Failover Logic

## 6.1 Algorithm

```
1. Monitor all interfaces every 1 second
2. On status change event:
   a. Increment debounce counter
   b. After 3 consecutive events:
      - Find best available interface (by priority)
      - If different from current:
        * Set new default interface
        * Trigger reconnection callbacks
```

## 6.2 Priority Order

Default priority (configurable):
1. Ethernet (priority = 0)
2. WiFi (priority = 1)
3. LTE (priority = 2)

---

# 7. Tasks

| Task | Stack Size | Priority | Function |
|------|------------|----------|----------|
| network_task | 2048 | Normal | Status monitoring |
| network_failover_task | 1024 | Normal | Failover handling |

---

# 8. Build Configuration

## CMake Dependencies

```
network_wifi_netif → middleware_lwip, ports_st67w611_port
network_pppos_netif → middleware_lwip
network_network_manager → middleware_lwip, components_cjson, components_file_io
```

## Library Sources

| Library | Source |
|---------|--------|
| ST67W611 | `libs/x-cube-st67w61/Middlewares/ST/ST67W6X_Network_Driver/` |
| lwIP PPP | `middleware/lwip/src/netif/ppp/*.c` |
| cJSON | `libs/cJSON/` |

---

# 9. Testing Checklist

| Interface | Test | Expected Result |
|-----------|------|-----------------|
| ETH | Link up | LED blinks, DHCP obtains IP |
| ETH | Cable disconnect | Failover to WiFi |
| WiFi | Connect to AP | DHCP obtains IP |
| WiFi | AP unavailable | Failover to LTE |
| LTE | SIM card inserted | Network registered |
| LTE | Data connection | PPP obtains IP |
| All | Failover | Application sees no interruption |

---

# 10. Document History

| Version | Date | Changes |
|---------|------|---------|
| 1.0 | 2026-07-08 | Initial document |
