# GatewayV2 Software Requirements Specification

**Document ID:** GWV2-SRS-001  
**Version:** 1.0 draft  
**Status:** Draft for product, firmware and test review  
**System:** GatewayV2 industrial IoT gateway  
**Baseline:** Ethernet/WiFi/LTE enabled; USB networking disabled; mDNS enabled  
**Hostname:** `synaptix-gateway.local`

## 1. Purpose

This document defines the externally observable behavior, functional
requirements, quality requirements, interfaces and verification criteria for
GatewayV2 firmware. It is the baseline for implementation, test planning,
release review and change control.

This SRS describes what the software shall do. Internal implementation details
such as source-file layout, task names and specific library calls are not
contractual unless explicitly stated as an architectural constraint.

## 2. Scope

GatewayV2 is an embedded gateway that collects data from three independent
RS485/Modbus RTU channels, exposes Modbus TCP, publishes telemetry through
MQTT, and provides local Web Config over Ethernet or WiFi. LTE provides a WAN
fallback. The firmware also provides persistence, time synchronization,
diagnostics and controlled network failover.

The current release includes:

- STM32H563-based firmware with FreeRTOS and lwIP.
- Ethernet DHCP/static configuration.
- WiFi station connectivity through the ST67W611 module.
- LTE PPPoS connectivity through the A7680C modem.
- Automatic route selection and failover.
- MQTT over TCP/IP with DNS resolution and retry backoff.
- HTTP Web Config on port 80.
- Fixed mDNS name `synaptix-gateway.local` and HTTP service advertisement.
- Three Modbus RTU channels and a Modbus TCP server.
- Persistent configuration, NTP synchronization and serial diagnostics.

USB networking, HTTPS, web authentication, secure OTA and fleet management
are deferred and shall not be represented as supported in this release.

## 3. Definitions and Acronyms

| Term | Meaning |
|---|---|
| Usable interface | Interface with stable link, administrative UP state and valid IPv4 address |
| Default route | Interface currently selected for outbound IP traffic |
| Route generation | Monotonic identifier for a successfully applied route transition |
| Attempt epoch | Identifier for one MQTT connection attempt |
| Stale callback | Asynchronous result belonging to an expired attempt or route |
| Web Config | Local HTTP dashboard and configuration API |
| NCP | Network co-processor used by the WiFi module |
| PPPoS | PPP over serial connection used by LTE |
| SRS | Software Requirements Specification |

## 4. Stakeholders and Use Cases

| Stakeholder | Required outcome |
|---|---|
| Operator | Commission and diagnose the gateway locally |
| System integrator | Connect field devices through RS485/Modbus |
| Cloud operator | Receive telemetry through a stable MQTT connection |
| Support engineer | Diagnose network, modem, DNS and resource failures |
| Manufacturing engineer | Build and flash a deterministic release image |
| Product owner | Verify release scope and acceptance criteria |

Primary use cases are commissioning, field data collection, cloud publishing,
network failover, local configuration, diagnostics and controlled reboot.

## 5. System Context and Constraints

The firmware shall coordinate the MCU, persistent storage, three RS485 ports,
Ethernet PHY, ST67W611 WiFi NCP, A7680C LTE modem and lwIP network stack.

The following constraints apply:

- Only one interface may own the default route at a time.
- USB networking is disabled in the current production baseline.
- HTTP is plain HTTP on port 80 and is intended for a trusted local network.
- MQTT QoS, offline queue semantics and broker configuration are unchanged by
  this SRS.
- lwIP asynchronous DNS cannot be force-cancelled directly; stale requests
  must therefore be invalidated logically and retained until terminal callback.
- All lwIP operations requiring TCP/IP context shall execute in that context.

## 6. Functional Requirements

### 6.1 Boot and configuration

| ID | Requirement | Verification |
|---|---|---|
| BOOT-001 | The system shall initialize storage, configuration, logger, network interfaces and application services in a deterministic order. | Boot integration test |
| BOOT-002 | The system shall capture reset flags and expose the reset reason in diagnostics. | Hardware reset test |
| BOOT-003 | The system shall load and validate persistent configuration before starting dependent services. | Configuration test |
| BOOT-004 | Invalid configuration shall not overwrite the last valid configuration. | Negative configuration test |
| BOOT-005 | Configuration changes shall be persisted before success is reported. | Power-loss/persistence test |
| BOOT-006 | Changes requiring reboot shall be reported as pending restart. | API/UI test |

### 6.2 Network management

| ID | Requirement | Verification |
|---|---|---|
| NET-001 | The network manager shall be the sole owner of interface selection, default netif, route generation and route transition logs. | Code review and route test |
| NET-002 | Ethernet, WiFi and LTE shall expose independent link, administrative, IP and usability states. | State/integration test |
| NET-003 | An interface shall be usable only with stable link, UP state and valid IPv4. | State test |
| NET-004 | Ethernet link transitions shall remain stable for one second before publication or DHCP start/stop. | Link-bounce test |
| NET-005 | The default route shall never select an interface without a valid usable state and IP. | Failover test |
| NET-006 | A successful route change shall increment route generation exactly once and emit its callback after default netif application. | Route unit test |
| NET-007 | ETH, WiFi and LTE shall fail over without reboot when the preferred route becomes unusable. | Hardware integration test |
| NET-008 | USB, while disabled, shall not be initialized, selected as default route or affect MQTT. | Configuration regression test |

### 6.3 MQTT and DNS

| ID | Requirement | Verification |
|---|---|---|
| MQTT-001 | MQTT shall implement the states IDLE, DNS_PENDING, CONNECTING, CONNECTED, DISCONNECTING and BACKOFF. | State-machine test |
| MQTT-002 | Only the MQTT task shall create attempts, change state, advance epoch or schedule retry. | Concurrency test/code review |
| MQTT-003 | At most one MQTT attempt and one active DNS request shall exist at any time. | Stress test |
| MQTT-004 | An attempt shall bind an epoch, route generation, interface, IP snapshot, deadline and cancellation/terminal state. | State test |
| MQTT-005 | DNS and socket callbacks shall validate epoch, route generation, request ID and cancellation state. | Callback race test |
| MQTT-006 | A stale callback shall not modify current connection state, socket ownership, IP, retry backoff or current attempt. | Callback race test |
| MQTT-007 | Each DNS request shall produce at most one terminal completion and shall not reuse context before callback completion. | DNS lifecycle test |
| MQTT-008 | A route change shall cancel the logical attempt, disconnect the old socket in TCP/IP context, debounce 500 ms and reconnect only after disconnect completion and a usable route. | Route failover test |
| MQTT-009 | Each connect attempt shall have an independent deadline and exactly one timeout event. | Broker timeout test |
| MQTT-010 | Retry delay shall use exponential backoff from 1 s to a maximum of 60 s and reset after successful connection. | Backoff test |
| MQTT-011 | MQTT diagnostics shall identify epoch, route, generation, request ID, reason and elapsed time. | Log/status test |

### 6.4 Web Config and mDNS

| ID | Requirement | Verification |
|---|---|---|
| WEB-001 | The HTTP server shall listen on port 80 and serve the embedded dashboard. | HTTP integration test |
| WEB-002 | Web Config shall be reachable through every usable Ethernet and WiFi IP address. | Interface test |
| WEB-003 | mDNS shall advertise `synaptix-gateway.local` only when a usable LAN interface and multicast support are available. | mDNS test |
| WEB-004 | mDNS shall advertise the HTTP service on port 80 and follow the usable LAN interface. | mDNS packet/integration test |
| WEB-005 | Loss of one LAN interface shall not stop HTTP or remove another usable interface. | Route/interface test |
| WEB-006 | Discovery and status responses shall not expose `0.0.0.0` as an access URL. | API test |

### 6.5 Web API

| ID | Requirement | Verification |
|---|---|---|
| API-001 | `GET /api/status` shall return device identity, firmware, uptime, reset reason, resource, network, route and MQTT status. | API contract test |
| API-002 | `GET /api/network/discovery` shall return the fixed hostname, mDNS URL and usable interface URLs. | API contract test |
| API-003 | `GET /api/config` shall return the current configuration subject to security policy. | API test |
| API-004 | `POST /api/config` shall validate and persist accepted configuration atomically. | API/persistence test |
| API-005 | `POST /api/reboot` shall return an acknowledgement before controlled reboot. | API/hardware test |
| API-006 | Errors shall use a consistent machine-readable code and human-readable message. | Contract test |

### 6.6 Field communication

| ID | Requirement | Verification |
|---|---|---|
| MODBUS-001 | The firmware shall operate three independent RS485/Modbus RTU channels. | Hardware integration test |
| MODBUS-002 | Each channel shall support configured baud rate, parity, stop bits, address and timeout. | Configuration test |
| MODBUS-003 | Register mappings shall support scaling and offset where configured. | Mapping test |
| MODBUS-004 | Modbus TCP shall expose configured field data on port 502. | Protocol test |
| MODBUS-005 | A fault on one RS485 channel shall not stop the other channels or network services. | Fault isolation test |

### 6.7 Logging and diagnostics

| ID | Requirement | Verification |
|---|---|---|
| LOG-001 | System, vendor/NCP, shell and diagnostic output shall use one serialized UART frame pipeline. | UART stress test/code review |
| LOG-002 | Vendor chunks shall be aggregated through newline, timeout or bounded-buffer rules and emitted as complete CRLF frames. | Aggregation unit test |
| LOG-003 | A full log queue shall increment a drop counter without blocking network or driver tasks. | Queue saturation test |
| LOG-004 | Route, interface, MQTT, DNS and Web Config events shall be logged once with actionable context. | Log review |
| LOG-005 | Runtime status shall expose stale callbacks, timeouts, DNS failures, route reconnects and dropped log frames. | Diagnostics test |

## 7. External Interfaces

### 7.1 Network interfaces

| Interface | Role | Release behavior |
|---|---|---|
| Ethernet | LAN/WAN and Web Config | DHCP or static IPv4; can own default route |
| WiFi | LAN/WAN and Web Config | Station mode; can own default route |
| LTE | WAN fallback | PPPoS; can own default route |
| USB | Local management network | Disabled in current baseline |

### 7.2 Cloud interface

MQTT shall use the configured broker hostname and port 1883 by default. DNS,
route identity and connection state shall be observable locally. TLS and
credential hardening are future security work unless separately approved.

### 7.3 HTTP interface

| Method | Endpoint | Purpose |
|---|---|---|
| GET | `/api/status` | Device and service status |
| GET | `/api/network/discovery` | Web Config access addresses |
| GET | `/api/config` | Read configuration |
| POST | `/api/config` | Validate and save configuration |
| POST | `/api/reboot` | Controlled reboot |

## 8. Non-Functional Requirements

| ID | Requirement | Verification |
|---|---|---|
| NFR-001 | MQTT reconnect shall begin within 3 s after a new route is stable and has a valid IP. | Failover timing test |
| NFR-002 | The system shall not create parallel MQTT sockets or DNS attempts. | Long-run stress test |
| NFR-003 | The firmware shall preserve at least 20% free heap under product workload. | Resource test |
| NFR-004 | System and vendor logs shall not be split or interleaved under concurrent output. | UART stress test |
| NFR-005 | Network callbacks shall not perform unsafe lwIP operations outside TCP/IP context. | Static review and stress test |
| NFR-006 | A single interface failure shall not crash or reboot the gateway. | Fault-injection test |
| NFR-007 | The release build shall be reproducible from the documented build configuration. | Build verification |
| NFR-008 | Configuration and diagnostics shall remain available over a usable local interface while WAN failover occurs. | Integration test |

## 9. Security Requirements and Limitations

| Capability | Current release |
|---|---|
| Local HTTP Web Config | Supported on trusted LAN |
| Web authentication | Not implemented; planned |
| HTTPS | Not implemented; planned |
| MQTT TLS | Not implemented/TBD |
| Signed OTA and anti-rollback | Not implemented; planned |
| Secure credential storage | Requires product security decision |

The current release shall document the trusted-LAN security assumption and
shall not claim production-grade remote-management security.

## 10. Verification and Acceptance

Verification shall test externally observable behavior at the highest useful
seam. Unit tests shall cover state transitions and idempotency; integration
tests shall cover real interface and protocol behavior; hardware tests shall
cover link changes, modem registration, NCP behavior and power/reset cases.

Release acceptance requires:

1. Firmware builds successfully with USB disabled and the approved mDNS
   configuration.
2. Ethernet, WiFi and LTE can become usable independently and fail over
   without reboot.
3. MQTT has no parallel attempt, DNS request or stale callback state change.
4. MQTT reconnects within 3 s after route stabilization and valid IP.
5. Web Config works through every usable IP and mDNS when multicast is
   available.
6. Modbus RTU channels remain isolated from one another and from network
   failures.
7. Logs remain complete and ordered during UART stress.
8. Heap, stack, timeout and reset diagnostics meet the documented thresholds.

## 11. Traceability

Every functional requirement shall map to at least one test case and one
release result. A requirement may be marked `Implemented`, `Verified`,
`Deferred` or `Rejected` during release review. Changes to route ownership,
MQTT lifecycle, Web Config reachability, security posture or protocol
behavior require an SRS change review.

## 12. Out of Scope

- USB RNDIS/ECM networking in the current release.
- HTTPS and Web Config authentication.
- MQTT TLS and fleet credential provisioning.
- Signed OTA, anti-rollback and secure boot changes.
- Multi-device mDNS collision management.
- Remote fleet management and cloud device registry.
- Changes to MQTT QoS or offline queue semantics.
- Hardware redesign, RF certification and enclosure changes.

## 13. Open Decisions

The following must be resolved before production security certification:

- Authentication and authorization model for Web Config.
- HTTPS certificate provisioning and renewal.
- MQTT TLS version, broker credentials and secret storage.
- Signed firmware update and anti-rollback policy.
- Device identity and mDNS strategy for multiple gateways on one LAN.
- Manufacturing test limits for RF, Ethernet, RS485 and environmental
  conditions.

## 14. Change Control

This SRS is the normative software baseline. Product-level presentation and
datasheet information belongs in `GatewayV2_Product_Spec.md`. Detailed REST
payload schemas, if needed, should be maintained as a separate API contract
document. Implementation changes shall reference the affected requirement IDs
and update verification evidence before release approval.
