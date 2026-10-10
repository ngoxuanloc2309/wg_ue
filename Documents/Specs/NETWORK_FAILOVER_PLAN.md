# Network Manager Multi-WAN Failover Plan

**Project:** GatewayV2 Industrial IoT Gateway  
**Date:** 2026-07-08  
**Status:** Draft

---

## 1. Problem Statement

Hiện tại, `network_manager.c` đang hardcoded chỉ hỗ trợ Ethernet và tự gọi `netif_set_default()`. Đồng thời `usb_netif.c` cũng tự ý gọi `netif_set_default()`, gây xung đột định tuyến.

GatewayV2 cần hỗ trợ nhiều cổng mạng (Ethernet, WiFi, LTE) với cơ chế Failover tự động để duy trì kết nối Internet liên tục.

---

## 2. Open Questions & Decisions

### Q1: Thứ tự ưu tiên (Priority)
| Priority | Interface | Notes |
|----------|-----------|-------|
| 1 | Ethernet | Primary - fastest & most stable |
| 2 | WiFi | Secondary |
| 3 | LTE | Tertiary - cellular backup |

> **Decision:** USB RNDIS không tham gia failover (chỉ dùng cho local config)

### Q2: Failover Delay
| Option | Pros | Cons |
|--------|------|------|
| Immediate | Nhanh nhất | Có thể switch không cần thiết nếu cáp lỏng tạm thời |
| 3-5s delay | Tránh flapping | Chờ lâu hơn |

> **Decision:** Sử dụng **2-stage approach**:
> - Stage 1: Debounce 3 giây trước khi đánh giá interface down
> - Stage 2: Sau khi down được xác nhận, chuyển sang interface dự phòng ngay lập tức

### Q3: Configuration Storage
> **Decision:** Lưu trong LittleFS với định dạng JSON:
> ```json
> {
>   "eth": { "enabled": true, "dhcp": true },
>   "wifi": { "enabled": true, "dhcp": true, "ssid": "xxx", "password": "xxx" },
>   "lte": { "enabled": true, "apn": "internet" }
> }
> ```

---

## 3. Architecture

### 3.1 Data Structures

```c
/* Network mode: DHCP or Static IP */
typedef enum {
    NET_MODE_DHCP = 0,
    NET_MODE_STATIC
} net_mode_t;

/* Network interface status */
typedef enum {
    NET_IF_STATUS_DOWN = 0,
    NET_IF_STATUS_DEGRADED,  /* Link up but no IP */
    NET_IF_STATUS_UP         /* Link up + IP obtained */
} net_if_status_t;

/* Static IP configuration */
typedef struct {
    uint32_t ip;        /* e.g., 192.168.1.100 */
    uint32_t netmask;   /* e.g., 255.255.255.0 */
    uint32_t gateway;   /* e.g., 192.168.1.1 */
    uint32_t dns_primary;
    uint32_t dns_secondary;
} network_ip_config_t;

/* Per-interface configuration */
typedef struct {
    bool enabled;                  /* Enable/disable interface */
    bool dhcp;                     /* TRUE = DHCP, FALSE = Static */
    network_ip_config_t static_ip; /* Used when dhcp = FALSE */
    uint8_t priority;              /* Lower = higher priority (0=highest) */
} network_interface_config_t;

/* Full network configuration */
typedef struct {
    network_interface_config_t eth;
    network_interface_config_t wifi;
    network_interface_config_t lte;
} network_config_t;
```

### 3.2 Module Responsibilities

| Module | Responsibility |
|--------|----------------|
| `network_manager.c` | Central controller - manages all interfaces, handles failover |
| `eth_driver.c` | Ethernet initialization, returns `struct netif*` |
| `wifi_driver.c` | WiFi initialization, returns `struct netif*` |
| `pppos_driver.c` | LTE PPPoS initialization, returns `struct netif*` |
| `usb_netif.c` | USB RNDIS - NO default gateway, NO failover |

### 3.3 API Design

```c
/* Initialization */
int network_manager_init(void);
int network_manager_load_config(void);
int network_manager_save_config(const network_config_t *config);

/* Interface control */
int network_interface_enable(net_if_type_t if_type);
int network_interface_disable(net_if_type_t if_type);
net_if_status_t network_interface_get_status(net_if_type_t if_type);
struct netif* network_interface_get_netif(net_if_type_t if_type);

/* Configuration */
int network_interface_set_config(net_if_type_t if_type, const network_interface_config_t *config);
int network_interface_get_config(net_if_type_t if_type, network_interface_config_t *config);

/* Failover control */
int network_set_primary_interface(net_if_type_t if_type);
net_if_type_t network_get_active_interface(void);

/* Status */
net_state_t network_get_state(net_if_type_t if_type);
const char* network_get_ip_address(net_if_type_t if_type);
```

---

## 4. Implementation Tasks

### Phase 1: Core Infrastructure

#### Task 1.1: Update network_manager.h
- Add new data structures (net_if_status_t, network_ip_config_t, network_interface_config_t, network_config_t)
- Add new API declarations
- Add event bits for interface status changes

#### Task 1.2: Refactor network_manager.c
- Replace single `gnetif` with array of `struct netif*` pointers
- Add configuration loading from LittleFS
- Add interface registration API
- Implement `network_interface_get_netif()`

#### Task 1.3: Remove netif_set_default() from usb_netif.c
- Remove line 201: `netif_set_default(netif);`
- Add comment explaining USB is for local config only

### Phase 2: Per-Interface Drivers

#### Task 2.1: eth_driver.c
- Create `ports/lwip_port/eth_driver.c`
- Return `struct netif*` from initialization
- Support both DHCP and Static IP
- Register with network_manager

#### Task 2.2: wifi_driver.c
- Create `network/wifi_netif/wifi_driver.c` (placeholder if not exist)
- Return `struct netif*` from initialization
- Support both DHCP and Static IP

#### Task 2.3: pppos_driver.c
- Create `network/pppos_netif/pppos_driver.c` (placeholder if not exist)
- Return `struct netif*` from initialization

### Phase 3: Failover Logic

#### Task 3.1: Implement failover_task
```c
void network_failover_task(void *params) {
    while (1) {
        // Check all interfaces in priority order
        // Find first interface with NET_IF_STATUS_UP
        // If different from current default, switch

        // Handle debouncing for flapping detection
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
```

#### Task 3.2: Implement link status callback
- Register with each interface for link change notifications
- Update interface status in real-time
- Trigger failover when appropriate

#### Task 3.3: Add DHCP renewal handling
- Monitor DHCP lease time
- Renew before expiration
- Handle DHCP failure gracefully

### Phase 4: Configuration Management

#### Task 4.1: Create default config
- Generate default JSON config
- Save to LittleFS on first boot

#### Task 4.2: Load/Save config APIs
- `network_manager_load_config()`
- `network_manager_save_config()`

#### Task 4.3: Web UI integration (future)
- HTTP endpoints for config get/set

### Phase 5: Testing & Verification

#### Task 5.1: Build verification
- Compile without errors

#### Task 5.2: Manual testing
- Test failover sequence (described below)

---

## 5. Failover Test Procedure

### Test 1: Ethernet Primary
1. Power on with Ethernet connected
2. Expected: ETH is default gateway, IP obtained via DHCP
3. Log: `Failover: ETH UP, set as default gateway`

### Test 2: WiFi Secondary
1. With ETH as default, enable WiFi
2. Expected: WiFi connects but NOT default gateway (ETH still primary)
3. Log: `WiFi UP, priority=2, default still ETH`

### Test 3: ETH Failover to WiFi
1. While ETH is default, disconnect Ethernet cable
2. Wait 3 seconds (debounce)
3. Expected: Failover to WiFi
4. Log: `Failover: ETH DOWN, switching to WiFi (priority=2)`

### Test 4: ETH Recovery
1. With WiFi as default, reconnect Ethernet cable
2. Expected: Failover back to ETH after link confirmed stable
3. Log: `Failover: ETH UP, switching back to ETH (priority=1)`

### Test 5: LTE Backup
1. Disconnect both ETH and WiFi
2. Expected: Failover to LTE
3. Log: `Failover: WiFi DOWN, switching to LTE (priority=3)`

---

## 6. Dependencies

| Dependency | Status | Notes |
|------------|--------|-------|
| lwIP | ✅ Exists | Already integrated |
| LittleFS | ✅ Exists | For config storage |
| Ethernet Driver | ⚠️ Partial | Needs refactor to return netif* |
| WiFi Driver | ❌ Not exist | Need to create |
| LTE Driver | ❌ Not exist | Need to create |
| USB Netif | ⚠️ Partial | Needs remove netif_set_default() |

---

## 7. Files to Modify/Create

### Modify:
- `network/network_manager/network_manager.h`
- `network/network_manager/network_manager.c`
- `network/usb_netif/usb_netif.c`
- `ports/lwip_port/ethernetif.c`

### Create:
- `network/eth_netif/eth_driver.c` (or refactor ethernetif.c)
- `network/wifi_netif/wifi_driver.c`
- `network/pppos_netif/pppos_driver.c`
- `configs/network_config.h` (defaults)

---

## 8. Timeline Estimate

| Phase | Tasks | Estimate |
|-------|-------|----------|
| Phase 1 | Core infrastructure | 2 days |
| Phase 2 | Per-interface drivers | 2 days |
| Phase 3 | Failover logic | 2 days |
| Phase 4 | Configuration | 1 day |
| Phase 5 | Testing | 1 day |
| **Total** | | **8 days** |

---

## 9. Open Items

- [ ] Confirm WiFi driver implementation approach (ST67W611 driver status)
- [ ] Confirm LTE driver implementation approach (A7680C AT commands)
- [ ] Define MAC address strategy (hardcoded vs generated)
- [ ] Define DNS server handling in static IP mode

---

## 10. Approval

> **Q1 - Priority:** Ethernet (1) > WiFi (2) > LTE (3) ✅ Agree?  
> **Q2 - Failover Delay:** 3-second debounce before failover ✅ Agree?  
> **Q3 - Config Storage:** LittleFS JSON ✅ Agree?

---
