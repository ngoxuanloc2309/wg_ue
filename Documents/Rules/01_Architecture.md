# AI Firmware Architecture Rules

## Purpose

This document defines the architectural principles for the Gateway V2 firmware.

Every implementation must follow these principles to ensure the firmware remains modular, maintainable, scalable, and suitable for long-term product development.

This document defines **where responsibilities belong**, **how modules interact**, and **how new functionality should be integrated**.

---

# Architecture Philosophy

The firmware is built around **clear ownership** and **layered responsibilities**.

Every module should:

- Have one clear responsibility.
- Own its internal state.
- Expose a minimal public interface.
- Hide implementation details.
- Be independently testable.
- Minimize dependencies on other modules.

A module should have only one primary reason to change.

---

# Layered Architecture

The firmware is organized into four architectural layers.

```text
+------------------------------------------------------+
|                  Application Layer                   |
+------------------------------------------------------+
|                    Service Layer                     |
+------------------------------------------------------+
|                    Driver Layer                      |
+------------------------------------------------------+
|                    HAL / BSP Layer                   |
+------------------------------------------------------+
```

Each layer has a distinct responsibility.

No layer should violate the responsibility of another.

---

# Layer Responsibilities

## Application Layer

Responsible for product behavior.

Examples include:

- Gateway workflow
- Operating modes
- System state machine
- Feature orchestration

The Application layer defines **what the system should do**.

It must never directly access hardware.

---

## Service Layer

Responsible for reusable system functionality.

Typical services include:

- Network Manager
- Configuration Manager
- Storage Manager
- Modbus Manager
- MQTT Service
- HTTP Service
- OTA Service
- Logger

A Service coordinates multiple Drivers to implement a complete feature.

Services do not directly access MCU peripherals.

---

## Driver Layer

Responsible for individual hardware devices or communication modules.

Examples include:

- Ethernet Driver
- Wi-Fi Driver
- LTE Driver
- RS485 Driver
- SPI Flash Driver
- USB Driver

Drivers translate hardware operations into reusable software interfaces.

Drivers must never contain business logic.

---

## HAL / BSP Layer

Responsible for direct hardware access.

Typical responsibilities include:

- GPIO
- UART
- SPI
- Ethernet MAC
- DMA
- RTC
- Timer
- USB

Only this layer may directly call STM32 HAL or LL APIs.

---

# Dependency Rules

Dependencies always point downward.

Allowed:

Application
→ Service
→ Driver
→ HAL/BSP

Not allowed:

- HAL depending on Drivers
- Drivers depending on Services
- Services depending on Applications

Lower layers must never depend on higher layers.

---

# Module Ownership

Every module owns a single responsibility.

Examples:

Network Manager

- network interfaces
- interface selection
- connectivity state

Configuration Manager

- runtime configuration
- configuration validation

Storage Manager

- persistent storage
- configuration persistence

Logger

- system logging

Drivers

- hardware communication

Only the owning module may modify its internal state.

Other modules interact only through public APIs.

---

# Public Interface

Every module exposes a minimal public interface.

Public APIs should describe **what** a module provides, not **how** it works.

Implementation details remain private.

Internal data structures should not be accessed directly by other modules.

---

# Module Communication

Modules communicate through explicit interfaces.

Preferred communication mechanisms:

- Function calls
- Event Bus
- Callbacks
- Message Queues

Avoid:

- Global shared state
- Cross-module variable access
- Hidden dependencies

Communication should always be explicit and traceable.

---

# Data Flow

Information flows upward.

```text
Hardware
    ↓
HAL/BSP
    ↓
Drivers
    ↓
Services
    ↓
Application
```

Control flows downward.

```text
Application
    ↓
Services
    ↓
Drivers
    ↓
HAL/BSP
    ↓
Hardware
```

Business decisions belong to the Application and Service layers.

Hardware access belongs to Drivers and HAL.

---

# Separation of Concerns

Separate responsibilities whenever practical.

Do not mix:

- Hardware access
- Protocol implementation
- Business logic
- Configuration
- Storage
- Networking
- Logging
- User Interface

Each concern should evolve independently.

---

# Extensibility

New functionality should integrate into the existing architecture.

Prefer extending existing modules over modifying unrelated ones.

When introducing a new feature:

1. Determine its architectural layer.
2. Determine ownership.
3. Reuse existing modules when appropriate.
4. Create a new module only if no existing module has the correct responsibility.

---

# Architectural Consistency

Keep the architecture consistent throughout the project.

Avoid:

- Circular dependencies
- God objects
- Utility modules containing unrelated functionality
- Hardware access outside Drivers/HAL
- Business logic inside Drivers

Consistency is more important than personal coding style.

---

# AI Expectations

Before implementing any feature:

- Identify the correct architectural layer.
- Respect module ownership.
- Preserve dependency direction.
- Reuse existing architecture whenever possible.
- Keep responsibilities focused.

Do not redesign the architecture unless explicitly requested.

If ownership is unclear, ask for clarification instead of making architectural assumptions.