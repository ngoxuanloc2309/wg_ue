# Embedded C Programming Guidelines

## Purpose

This document defines the Embedded C programming guidelines for the Gateway V2 firmware.

The objective is to ensure that all generated and manually written code is:

- Readable
- Consistent
- Deterministic
- Maintainable
- Testable
- Safe for production
- Naturally aligned with MISRA C principles

These rules apply to all firmware modules unless explicitly documented otherwise.

---

# 1. General Principles

Always write code that is:

- Simple
- Explicit
- Predictable
- Modular
- Easy to review
- Easy to debug

Prefer clarity over cleverness.

Prefer maintainability over reducing the number of lines.

Every line of code should have a clear purpose.

---

# 2. Language

- Use ISO C.
- Write portable Embedded C.
- Avoid compiler-specific extensions.
- Avoid undefined and implementation-defined behavior.
- Do not depend on compiler optimizations for correctness.

---

# 3. File Organization

Each module should contain:

```text
module_name.h
module_name.c
```

Every module has a single responsibility.

Do not combine unrelated functionality into one source file.

Avoid files such as:

```text
common.c
utils.c
misc.c
system.c
```

unless their responsibility is clearly defined.

---

# 4. Header Files

Header files expose only the public interface.

Header files should contain:

- include guard
- required includes
- public macros
- public typedefs
- public enums
- public structures
- public function declarations

Do not place private variables inside header files.

Do not expose implementation details.

---

# 5. Include Rules

Order includes as:

```c
#include "module_name.h"

#include "project_header.h"

#include "other_module.h"

#include "stm32h5xx_hal.h"

#include <stdint.h>
#include <stdbool.h>
```

Include only what is required.

Avoid circular dependencies.

---

# 6. Naming Convention

Use `snake_case`.

Examples

Files

```text
network_manager.c
storage_manager.c
modbus_manager.c
```

Functions

```c
network_manager_init();
network_manager_process();
network_manager_start();
```

Variables

```c
retry_count
timeout_ms
connection_state
```

Boolean

```c
is_connected
is_initialized
has_error
can_transmit
```

Constants

```c
NETWORK_TIMEOUT_MS
MODBUS_MAX_FRAME_SIZE
```

Types

```c
network_state_t
network_config_t
mqtt_client_t
```

Callbacks

```c
network_event_callback_t
```

---

# 7. Types

Use fixed-width integer types.

```c
uint8_t
uint16_t
uint32_t
int32_t
```

Use

```c
bool
```

for boolean values.

Avoid raw

```c
int
long
char
```

for protocol fields, hardware registers or persistent storage.

---

# 8. Variables

Variables should have the smallest possible scope.

Initialize variables before use.

Private module variables shall be

```c
static
```

Avoid global variables.

If global state is required, ownership must be clearly defined.

---

# 9. Constants and Macros

Never use magic numbers.

Prefer

```c
#define NETWORK_TIMEOUT_MS      1000U
```

instead of

```c
1000
```

Prefer

```c
static const
```

over macros whenever appropriate.

Function-like macros should be avoided.

---

# 10. Enumerations

Use typedef enum.

Always provide an explicit zero value.

Example

```c
typedef enum
{
    NETWORK_STATE_DOWN = 0,
    NETWORK_STATE_CONNECTING,
    NETWORK_STATE_UP
} network_state_t;
```

---

# 11. Structures

Use typedef struct.

Keep structures focused on a single purpose.

Avoid exposing private implementation structures through public headers unless necessary.

---

# 12. Functions

Each function should perform one task.

Keep functions short.

Prefer early return.

Validate all public arguments.

Good

```c
if (config == NULL)
{
    return APP_ERR_INVALID_ARG;
}
```

Avoid deeply nested functions.

Avoid side effects.

---

# 13. Public API Design

Public APIs should describe **what** they do.

Examples

```c
network_manager_init();

network_manager_start();

network_manager_stop();

network_manager_process();

network_manager_get_status();
```

Avoid generic names.

```c
init();

run();

execute();

start();
```

---

# 14. Static Functions

Private helper functions shall be declared

```c
static
```

Private functions should never appear in header files.

---

# 15. Control Flow

Always use braces.

Good

```c
if (is_connected)
{
    process_frame();
}
```

Avoid

```c
if (is_connected)
    process_frame();
```

Keep conditions simple.

Split complex expressions into meaningful variables.

Prefer

```c
bool timeout_expired = elapsed_ms >= timeout_ms;

if (timeout_expired)
{
    ...
}
```

instead of long expressions.

---

# 16. Error Handling

Functions that can fail should return an explicit status.

Example

```c
typedef enum
{
    APP_OK = 0,
    APP_ERR_INVALID_ARG,
    APP_ERR_TIMEOUT,
    APP_ERR_BUSY,
    APP_ERR_NOT_READY,
    APP_ERR_HW_FAIL
} app_status_t;
```

Always check return values.

Never ignore errors silently.

---

# 17. Comments

Comments explain

WHY

not

WHAT

Good

```c
/* Keep DE enabled until the UART transmission is fully completed. */
```

Avoid

```c
/* Increment retry counter. */
retry_count++;
```

---

# 18. Defensive Programming

Assume

- hardware can fail
- communication can timeout
- configuration can be corrupted
- reset can happen unexpectedly

Therefore

- validate inputs
- validate outputs
- check return values
- initialize state
- use timeout protection
- use safe defaults

---

# 19. MISRA-Oriented Rules

Write code that naturally complies with MISRA C.

Avoid:

- recursion
- dynamic allocation
- pointer arithmetic
- assignment inside conditions
- implicit type conversion
- hidden side effects
- unused code
- dead code
- complex expressions

Document every intentional deviation.

---

# 20. Forbidden Patterns

Do not:

- use magic numbers
- write God functions
- access another module's internal state
- expose global mutable variables
- place business logic inside drivers
- call HAL directly outside HAL/BSP or Drivers
- ignore return values
- use runtime malloc/free
- duplicate code unnecessarily

---

# 21. AI Expectations

When generating code:

- Follow the existing project style.
- Prefer consistency over personal preference.
- Write production-quality Embedded C.
- Keep interfaces minimal.
- Keep implementations private.
- Validate inputs.
- Return explicit status codes.
- Write deterministic code.
- Generate code that is easy to review and maintain.

If a requested implementation conflicts with these guidelines, explain the conflict before generating code.