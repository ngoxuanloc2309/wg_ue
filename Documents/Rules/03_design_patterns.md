# Firmware Design Patterns

## Purpose

This document defines the preferred firmware design patterns for the Gateway V2 project.

These patterns help AI assistants and developers create modules that are consistent, testable, maintainable, and suitable for embedded product firmware.

This document does not define coding syntax. Coding syntax is defined in `02_embedded_c_guidelines.md`.

---

## 1. General Pattern Rules

Use patterns to keep module structure predictable.

Do not force every pattern into every module.

Use the simplest pattern that fits the responsibility.

A module should expose only the APIs it actually needs.

Avoid creating abstractions without a clear reason.

---

## 2. Module Lifecycle Pattern

Most firmware modules should follow a clear lifecycle.

Preferred API shape:

```c
module_init();
module_deinit();
module_start();
module_stop();
module_process();
module_get_status();
```

Typical meaning:

- `init()` prepares internal state and dependencies.
- `deinit()` releases or resets module resources.
- `start()` activates operation.
- `stop()` stops operation safely.
- `process()` runs periodic or event-driven work.
- `get_status()` exposes current module status.

Not every module needs every function.

---

## 3. Manager Pattern

Use a Manager when a module coordinates multiple lower-level modules or owns system-level state.

Examples:

- `network_manager`
- `storage_manager`
- `config_manager`
- `modbus_manager`

Preferred API shape:

```c
network_manager_init();
network_manager_start();
network_manager_stop();
network_manager_process();
network_manager_get_status();
```

Manager modules may coordinate Drivers and Services.

Manager modules must not directly access STM32 HAL unless explicitly part of BSP or driver ownership.

---

## 4. Driver Pattern

Use a Driver when a module controls one hardware device, peripheral-facing protocol, or board-level interface.

Gateway V2 driver examples:

- Ethernet PHY / ETH driver
- ST67W611 Wi-Fi/BLE driver
- A7680C LTE driver
- RS485 driver
- W25Q128 SPI Flash driver
- MX25LM51245 OctoSPI Flash driver
- USB driver

Preferred API shape:

```c
driver_init();
driver_deinit();
driver_read();
driver_write();
driver_get_status();
driver_reset();
```

Drivers should:

- Hide hardware details.
- Own hardware communication.
- Expose clear status.
- Avoid business logic.
- Avoid direct knowledge of application behavior.

---

## 5. Service Pattern

Use a Service when a module implements reusable firmware functionality above Drivers.

Examples:

- MQTT service
- HTTP service
- OTA service
- Logger
- Event Bus

Preferred API shape:

```c
service_init();
service_start();
service_stop();
service_process();
service_handle_event();
```

Services may depend on Managers or Drivers according to the architecture rules.

Services should not directly access MCU peripherals.

---

## 6. State Machine Pattern

Use a state machine for modules with clear operating states.

Examples:

- Network connection handling
- OTA update flow
- Modbus polling
- LTE connection flow
- Gateway operating mode

Preferred structure:

```c
typedef enum
{
    MODULE_STATE_INIT = 0,
    MODULE_STATE_IDLE,
    MODULE_STATE_STARTING,
    MODULE_STATE_RUNNING,
    MODULE_STATE_STOPPING,
    MODULE_STATE_ERROR,
    MODULE_STATE_RECOVERY
} module_state_t;
```

Rules:

- Define states explicitly.
- Keep state transitions centralized.
- Avoid replacing state machines with many unrelated boolean flags.
- Handle invalid states safely.
- Add timeout handling where a state can block progress.
- Keep transition ownership inside the module.

---

## 7. Event-Driven Pattern

Use events when modules need loose coupling.

Preferred flow:

```text
ISR / Hardware Event
        ↓
Driver Event
        ↓
Event Bus / Queue
        ↓
Service / Manager
        ↓
Application
```

Rules:

- ISR must not execute business logic.
- ISR should only signal, push event, or release synchronization objects.
- Events should be explicit and typed.
- Event payloads should be small and bounded.
- Event producers should not depend on event consumers.

---

## 8. Configuration Pattern

Use this pattern for runtime and persistent configuration.

Preferred API shape:

```c
config_manager_load();
config_manager_validate();
config_manager_apply();
config_manager_save();
config_manager_reset_default();
```

Rules:

- Validate configuration before applying it.
- Validate configuration before saving it.
- Use safe defaults if configuration is missing or corrupted.
- Application code must not directly access persistent storage.
- Configuration changes must go through `config_manager`.

---

## 9. Storage Pattern

Use this pattern for persistent storage access.

Preferred API shape:

```c
storage_manager_init();
storage_manager_read();
storage_manager_write();
storage_manager_erase();
storage_manager_sync();
storage_manager_get_status();
```

Rules:

- Storage Manager owns persistent storage access.
- Drivers only implement device-level operations.
- Higher layers should access storage through Storage Manager.
- Do not scatter flash address constants across modules.
- Storage operations should include bounds checking and error reporting.

---

## 10. Network Interface Pattern

Use this pattern for Ethernet, Wi-Fi, LTE, and USB networking integration.

Preferred flow:

```text
Driver Init
    ↓
Interface Bring-Up
    ↓
Link / Registration Check
    ↓
IP Configuration
    ↓
Network Manager Registration
    ↓
Application Network Use
```

Rules:

- Applications must not directly select Ethernet, Wi-Fi, or LTE.
- Network selection belongs to `network_manager`.
- Network interfaces should report status and events to `network_manager`.
- Reconnect and failover logic should be centralized.

---

## 11. Transport Pattern

Use a transport abstraction when multiple communication backends need similar behavior.

Examples:

- UART transport for LTE AT / PPPoS
- SPI transport for ST67W611
- TCP transport for MQTT / HTTP

Preferred API shape:

```c
transport_init();
transport_open();
transport_close();
transport_send();
transport_receive();
transport_get_status();
```

Rules:

- Transport should move bytes or frames.
- Transport should not contain application protocol decisions.
- Protocol logic should live above transport.

---

## 12. Producer-Consumer Pattern

Use this pattern when one context produces data and another context processes it.

Examples:

- UART RX to parser task
- RS485 RX to Modbus manager
- Driver event to service task
- Log producer to log output task

Preferred flow:

```text
Producer
    ↓
Queue / Ring Buffer
    ↓
Consumer
```

Rules:

- Producers should not block indefinitely.
- Consumers should handle overflow or dropped data.
- Queue size must be bounded.
- Shared buffers require clear ownership.

---

## 13. Callback Pattern

Use callbacks when a lower-level module needs to notify a higher-level module without depending on it.

Preferred API shape:

```c
module_register_callback(callback_fn);
module_unregister_callback();
```

Rules:

- Callback execution time should be short.
- Callback ownership must be clear.
- Do not call callbacks from ISR unless explicitly designed for ISR-safe use.
- Prefer Event Bus or Queue when callback processing may be heavy.

---

## 14. Singleton Module Pattern

Use singleton modules for system-wide managers that naturally have only one instance.

Examples:

- `network_manager`
- `storage_manager`
- `config_manager`
- `logger`
- `event_bus`

Rules:

- Singleton state should be private to the module.
- Do not expose mutable global variables.
- Provide public APIs for access.
- Keep initialization order explicit.

---

## 15. Task Pattern

For RTOS-based modules, prefer this task structure:

```c
void module_task(void *argument)
{
    module_init();

    for (;;)
    {
        module_process();
        module_wait_or_delay();
    }
}
```

Rules:

- Task responsibility must be clear.
- Do not create unnecessary tasks.
- Prefer event-driven waiting where appropriate.
- Avoid busy loops.
- Use bounded waits and timeouts.

---

## 16. Command Pattern

Use this pattern for commands from Web Config, USB CLI, cloud, or local configuration interfaces.

Preferred flow:

```text
Command Input
    ↓
Parse
    ↓
Validate
    ↓
Authorize / Check State
    ↓
Execute Through Owner Module
    ↓
Return Result
```

Rules:

- Commands are requests, not direct hardware actions.
- Validate all command parameters.
- Commands must not bypass safety, configuration, or ownership rules.
- Execution belongs to the owning module.

---

## 17. Factory Default Pattern

Use this pattern when configuration can be reset or recovered.

Preferred API shape:

```c
config_manager_load_default();
config_manager_factory_reset();
config_manager_restore_backup();
```

Rules:

- Default configuration must be safe.
- Factory reset must not corrupt calibration or identity data unless explicitly requested.
- Recovery behavior must be deterministic.

---

## 18. Retry and Timeout Pattern

Use this pattern for external devices and communication.

Preferred behavior:

```text
Start Operation
    ↓
Wait With Timeout
    ↓
Retry If Allowed
    ↓
Report Failure
    ↓
Recover Or Enter Error State
```

Rules:

- Every external communication should have a timeout.
- Retry count must be bounded.
- Retry delay should be explicit.
- Permanent failure should be reported to the owning Manager.

---

## 19. Bring-Up Pattern

Use this pattern for hardware bring-up code.

Preferred flow:

```text
Power / Reset Check
    ↓
GPIO Init
    ↓
Peripheral Init
    ↓
Device ID / Link Check
    ↓
Basic Communication Test
    ↓
Manager Registration
    ↓
Operational Mode
```

Rules:

- Add diagnostic logs for bring-up steps.
- Verify device identity when possible.
- Keep bring-up tests isolated from application logic.
- Do not assume hardware is working until verified.

---

## 20. Pattern Selection Guide

Use this guide when creating a new module.

```text
Controls hardware directly?
    → Driver Pattern

Coordinates multiple modules?
    → Manager Pattern

Provides reusable feature logic?
    → Service Pattern

Has explicit operating states?
    → State Machine Pattern

Handles external commands?
    → Command Pattern

Moves data between contexts?
    → Producer-Consumer Pattern

Stores persistent data?
    → Storage / Configuration Pattern
```

---

## 21. AI Expectations

Before generating a new module, AI must:

1. Identify the module type.
2. Select the simplest suitable pattern.
3. Use API names consistent with the selected pattern.
4. Keep ownership clear.
5. Keep internal state private.
6. Avoid unnecessary abstraction.
7. Avoid mixing unrelated patterns in one module.

If no pattern fits clearly, ask for clarification before designing the module.
