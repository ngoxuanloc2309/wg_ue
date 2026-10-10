# GatewayV2 Product Technical Specification

**Document ID:** GWV2-PTS-001  
**Version:** 1.0 draft  
**Status:** Draft for product and system-integration review  
**Product:** Synaptix GatewayV2  
**Document type:** Product Technical Specification  
**Audience:** Product, system integration, firmware and QA  
**Classification:** Internal Engineering  
**Primary hostname:** `synaptix-gateway.local`

## 1. Product Overview

GatewayV2 is an embedded industrial IoT gateway for collecting data from
RS485/Modbus field devices and bridging it to local Ethernet networks, WiFi,
LTE and cloud services.

The product provides:

- Three independent RS485/Modbus RTU channels.
- Modbus TCP access for local SCADA or supervisory systems.
- MQTT telemetry transport to a configured broker.
- Automatic Ethernet, WiFi and LTE route failover.
- Local HTTP Web Config and diagnostics.
- Optional mDNS-based Web Config discovery when multicast is available.
- Persistent configuration and NTP time synchronization.

## 2. Key Features

| Feature | Specification | Status | Notes |
|---|---|---|---|
| Field protocol | Modbus RTU over RS485 | Supported | Three independent channels |
| Local protocol | Modbus TCP | Supported | Default service port 502 |
| Cloud protocol | MQTT over TCP/IP | Supported | Broker and topic configurable |
| WAN connectivity | Ethernet, WiFi, LTE | Supported | Automatic route selection |
| Web Config | HTTP on port 80 | Supported | Local network access |
| Web discovery | mDNS `synaptix-gateway.local` | Supported when available | Requires usable LAN and multicast support |
| Time | NTP synchronization | Supported | Requires a usable IP route |
| Diagnostics | Serial log, shell and HTTP status | Supported | Runtime network and service status |
| USB networking | RNDIS/ECM Web Config | Deferred | Disabled in current baseline |

## 3. Hardware Platform

### 3.1 MCU and firmware platform

| Item | Specification |
|---|---|
| MCU family | STM32H563 |
| RTOS | FreeRTOS |
| IP stack | lwIP |
| Application architecture | Embedded multi-task gateway firmware |
| Primary build output | `GatewayV2.bin` |

### 3.2 Non-volatile storage

| Item | Specification | Notes |
|---|---|---|
| External flash | Winbond, JEDEC ID `0xEF4018` | Detected in current hardware |
| Capacity | 128 Mbit | Used for persistent application data and firmware support |
| Configuration | Persistent | Validated before runtime use |

### 3.3 Ethernet

| Item | Specification |
|---|---|
| Interface | 10/100 Ethernet |
| PHY | External Ethernet PHY |
| Addressing | DHCP or static IPv4 |
| Web Config | Available on usable Ethernet IP |
| Default route | Supported |

### 3.4 WiFi and Bluetooth module

| Item | Specification |
|---|---|
| Module | ST67W611 |
| Operating mode | WiFi station |
| Addressing | DHCP through configured access point |
| Web Config | Available on usable WiFi IP |
| Default route | Supported |
| Bluetooth | Module capability; not part of the current GatewayV2 application interface |

### 3.5 LTE modem

| Item | Specification |
|---|---|
| Modem | A7680C |
| Data interface | PPP over serial (PPPoS) |
| SIM | Required and must be ready |
| RF | Cellular antenna required for network registration |
| Default route | Supported as WAN fallback |
| Web Config | Not directly advertised as a LAN access path |

### 3.6 RS485

| Item | Specification |
|---|---|
| Number of channels | 3 independent channels |
| Protocol | Modbus RTU |
| Configuration | Baud rate, parity, stop bits, address and timeout |
| Isolation | Channel operation is logically isolated |
| Channel independence | Independent |
| Fault isolation | Failure on one channel does not stop other channels or network services |

### 3.7 USB

| Item | Current release |
|---|---|
| USB network interface | Disabled |
| USB Web Config URL | Not available |
| Internet failover participation | Not applicable |
| Planned use | Local maintenance/configuration network in a future release |

### 3.8 Electrical characteristics

| Item | Specification | Status |
|---|---|---|
| Input voltage range | TBD — pending hardware qualification | TBD |
| Typical power consumption | TBD — pending measurement | TBD |
| Maximum power consumption | TBD — pending measurement | TBD |
| Power-up sequence | TBD — pending hardware qualification | TBD |
| Reverse-polarity protection | TBD — pending schematic review | TBD |
| Over-voltage/over-current protection | TBD — pending schematic review | TBD |

### 3.9 Mechanical characteristics

| Item | Specification | Status |
|---|---|---|
| Dimensions | TBD — pending mechanical design release | TBD |
| Weight | TBD — pending product measurement | TBD |
| Mounting | TBD — pending enclosure/mechanical definition | TBD |
| Connector types | TBD — pending hardware release | TBD |
| Enclosure | TBD — pending mechanical design release | TBD |

### 3.10 Environmental characteristics

| Item | Specification | Status |
|---|---|---|
| Operating temperature | TBD — pending environmental qualification | TBD |
| Storage temperature | TBD — pending environmental qualification | TBD |
| Relative humidity | TBD — pending environmental qualification | TBD |
| Vibration/shock | TBD — pending product qualification | TBD |
| Installation altitude | TBD — pending product qualification | TBD |

### 3.11 Protection and compliance

| Item | Specification | Status |
|---|---|---|
| ESD immunity | TBD — pending EMC qualification | TBD |
| Surge/transient immunity | TBD — pending EMC qualification | TBD |
| Galvanic isolation | TBD — pending schematic and safety review | TBD |
| EMC emissions/immunity | TBD — pending compliance testing | TBD |
| CE/FCC or regional marking | TBD — pending compliance plan | TBD |

The values above are intentionally marked TBD. They shall be populated from
released hardware documents and qualification reports, not inferred from
firmware behavior.

## 4. Network

### 4.1 Ethernet

Ethernet supports DHCP or static IPv4 configuration. When link is stable and
the interface has a valid IPv4 address, it may be selected as the default
route and may serve Web Config.

### 4.2 WiFi

WiFi operates in station mode and connects to the configured access point.
After association and DHCP completion, it may serve Web Config and become the
default route.

### 4.3 LTE

LTE uses the A7680C modem and PPPoS to provide a cellular IP connection. The
SIM must report ready, the modem must register with the operator and packet
attach must succeed before LTE is usable.

### 4.4 Network failover

| Property | Specification | Classification |
|---|---|---|
| Interface operation | Ethernet, WiFi and LTE may initialize independently | Product capability |
| Default route | One active route at a time | Product capability |
| Selection | Automatic based on interface usability and configured priority | Product capability |
| Usable state | Stable link, interface UP and valid IPv4 address | Product definition |
| Failover | Automatic; reboot not required | Product capability |
| Ethernet debounce | 1 second | Current baseline; verify before release |
| Route change debounce for cloud reconnect | 500 ms | Current baseline; verify before release |
| USB participation | Disabled and excluded from route selection | Release configuration |

When the default route changes, cloud services reconnect through the new
usable route. Existing IP sessions may be interrupted during transition.

## 5. Industrial Communication

### 5.1 Modbus RTU

GatewayV2 supports three concurrent RS485/Modbus RTU channels. Each channel
can be configured for the connected field devices and can expose mapped
register data to the application.

Supported configuration concepts include:

- Serial baud rate.
- Data parity and stop bits.
- Device/slave address.
- Request and response timeout.
- Register address and data type.
- Scaling and offset for application values.

### 5.2 Modbus TCP

| Item | Specification |
|---|---|
| Service | Modbus TCP server |
| Default port | 502 |
| Data source | Configured Modbus RTU devices and mapped gateway data |
| Availability | Any usable IP interface that permits local access |

## 6. Cloud Communication

### 6.1 MQTT

| Item | Specification |
|---|---|
| Protocol | MQTT |
| Transport | TCP/IP |
| Default port | 1883 |
| Broker | Configurable |
| Topic base | Configurable |
| Network interfaces | Ethernet, WiFi, LTE |
| DNS | Supported |
| Automatic reconnect | Supported |
| Route failover | Supported |
| Retry backoff | Supported |
| Maximum retry delay | 60 seconds | Current baseline; verify before release |
| QoS/offline queue | Preserved from current application configuration |

MQTT is an application-level service and does not require a specific physical
network interface. It follows the currently selected default route.

### 6.2 NTP

| Item | Specification |
|---|---|
| Service | Network Time Protocol |
| Purpose | Synchronize system time |
| Requirement | A usable IP route and reachable time server |
| Diagnostics | Synchronization result and Unix timestamp are logged |

## 7. Web Configuration

### 7.1 Access

| Access method | URL | Availability |
|---|---|---|
| mDNS | `http://synaptix-gateway.local/` | Usable LAN plus multicast support |
| Ethernet | `http://<ETH_IP>/` | Ethernet has valid IPv4 |
| WiFi | `http://<WIFI_IP>/` | WiFi has valid IPv4 |
| USB | `http://192.168.7.1/` | Deferred; USB disabled |

The IP address shown by the gateway is authoritative when mDNS is unavailable.
The product does not require a fixed Ethernet or WiFi IP address.

### 7.2 HTTP services

| Method | Endpoint | Description |
|---|---|---|
| GET | `/api/status` | Device, network and service status |
| GET | `/api/network/discovery` | Available Web Config URLs |
| GET | `/api/config` | Current configuration |
| POST | `/api/config` | Validate and save configuration |
| POST | `/api/reboot` | Controlled device reboot |

### 7.3 Dashboard information

The dashboard displays device identity, firmware version, uptime, active route,
interface addresses, MQTT state, resource usage, field-device status and
available Web Config access URLs.

## 8. Firmware and Storage

### 8.1 Configuration

| Configuration property | Specification |
|---|---|
| Persistence | Retained across reboot |
| Validation | Performed before activation |
| Invalid update | Rejected |
| Last valid configuration | Preserved |

### 8.2 Firmware update

| Feature | Current release |
|---|---|
| Local firmware image build | Supported |
| Secure remote OTA | Not implemented |
| Image signing | Not implemented |
| Anti-rollback | Not implemented |

### 8.3 Factory reset and reboot

Controlled reboot is available through the supported management interface.
Factory-reset behavior and production recovery policy require a product-level
decision before certification.

## 9. Diagnostics

| Diagnostic area | Information provided |
|---|---|
| Boot | Reset flags, initialization status and firmware startup |
| Network | Link, administrative state, IP, gateway, active route and failover |
| MQTT | Connected state, broker, route, retry and error status |
| DNS | Resolution result and failure status |
| LTE | SIM, signal, operator, registration, packet attach and PPP status |
| WiFi | Module startup, association, link and IP status |
| Modbus | Channel and device communication status |
| Resources | CPU, heap, lowest free heap and task stack usage |
| Serial output | Serialized system, vendor/NCP, shell and diagnostic frames |

## 10. Security

| Security feature | Current release | Product direction |
|---|---|---|
| Local HTTP Web Config | Supported on trusted LAN | Retain for commissioning |
| Web authentication | Not supported | Planned |
| HTTPS | Not supported | Planned |
| MQTT TLS | Not supported/TBD | Security review required |
| Secure credential storage | Not finalized | Security review required |
| Signed OTA | Not supported | Planned |
| Anti-rollback | Not supported | Planned |

The current release must be deployed on a trusted management network. It must
not be marketed as providing secure remote administration until the planned
security features are implemented and verified.

## 11. Performance and Reliability

| Property | Specification | Classification |
|---|---|---|
| MQTT reconnect | Starts within 3 s after route is stable and has valid IP | Engineering target; verify before release |
| Parallel MQTT attempts | Maximum 1 | Product behavior |
| Active DNS requests for MQTT | Maximum 1 | Product behavior |
| Route transition | No reboot required | Product capability |
| Interface failure | Unrelated interfaces and services remain operational | Product capability |
| UART logging | Complete, ordered frames under concurrent output | Product capability |
| Free heap | At least 20% under defined product workload | Engineering target; verify on release hardware |
| Modbus isolation | One channel failure does not stop other channels | Product capability |

The timing, retry and resource values in this section are release targets or
observed engineering behavior only until product management and qualification
tests formally approve them. Detailed state-machine and timeout behavior is
defined in the Software Requirements Specification.

## 12. Build and Firmware Baseline

This section is included because this document is classified as Internal
Engineering. It is not part of an external customer datasheet.

| Item | Baseline |
|---|---|
| Product image | `GatewayV2.bin` |
| Standard output directory | `build` |
| Build system | CMake with approved ARM toolchain |
| USB networking | OFF |
| mDNS | ON when multicast support is enabled |
| HTTP port | 80 |
| Modbus TCP port | 502 |
| MQTT default port | 1883 |

Production builds shall use the documented build and flash scripts and shall
not be confused with temporary USB-on, USB-off or clean-build test variants.

## 13. Current Limitations

- mDNS depends on host operating-system support, multicast delivery and local
  network policy; the interface IP URL remains the fallback.
- A fixed hostname can conflict if multiple gateways use the same hostname on
  one LAN; fleet identity and conflict management are not yet defined.
- Web Config is HTTP-only and has no authentication in the current release.
- USB networking is not available in the current baseline.
- LTE registration and signal quality depend on antenna, SIM, operator coverage
  and modem power conditions.
- MQTT TLS, signed OTA and secure credential storage are not yet part of the
  released security model.

## 14. Future Features

- USB RNDIS/ECM local configuration network.
- HTTPS and authenticated Web Config.
- MQTT TLS and protected credential provisioning.
- Signed OTA with anti-rollback.
- Unique per-device discovery identity and multi-gateway provisioning.
- Fleet monitoring and cloud device registry.
- Expanded production, environmental and EMC qualification data.

## 15. Related Documents

- `GatewayV2_Software_Requirements_Specification.md` — normative firmware
  behavior, requirements and verification criteria.
- `GatewayV2_Product_Spec.md` — earlier product-level draft retained for
  comparison and migration.
