# Runtime and Release Baseline

**Document ID:** FW-005  
**Version:** 1.0  
**Status:** Current bring-up baseline  
**Last Updated:** 2026-07-20

## Boot and flash mode

The current hardware validation image runs without the bootloader:

| Item | Value |
|------|-------|
| MCU | STM32H563, 2 MB internal flash |
| Application vector table | `0x08000000` |
| Linker script | `STM32H563xx_FLASH.ld` |
| Image | `build/GatewayV2.bin` |
| Flash script | `flash.bat` |
| Flash command address | `0x08000000` |

After a clean build, verify `build/GatewayV2.map` contains:

```text
.isr_vector 0x08000000
```

Do not flash a binary whose map still places `.isr_vector` at `0x08010000`.
The bootloader is a separate target and is tested only with `bootloader/flash.bat`.

## Logger baseline

`libs/logger` is a Git submodule. GatewayV2 keeps the existing logging API:

```c
logger_init(LOGGER_INFO, output_callback);
LOGD("TAG", "value=%d", value);
LOGI("TAG", "connected");
LOGW("TAG", "retry=%d", retry);
LOGE("TAG", "error=%d", error);
```

The logger uses a direct output callback and does not access UART or HAL directly.
Formatting is bounded by `LOGGER_MESSAGE_SIZE` (default 4096 bytes). FreeRTOS
builds use a finite mutex timeout; if the logger cannot safely acquire its mutex,
the message is dropped instead of blocking indefinitely. The banner is disabled
by default.

## FreeRTOS runtime baseline

The application uses a simple default task and a separate system monitor:

| Component | Configuration |
|-----------|---------------|
| Network task | `GW_TASK_PRIO_NETWORK=12` |
| Modbus tasks | `GW_TASK_PRIO_MODBUS=11` |
| Broker task | `GW_TASK_PRIO_BROKER=11` |
| Shell task | `GW_TASK_PRIO_SHELL=11` |
| Monitor period | `GW_MONITOR_PERIOD_MS=30000` |

The monitor reports heap usage, minimum heap, CPU usage, and task stack high-water
marks. A healthy boot must continue producing monitor output and the shell must
respond to `help`.

## USB isolation

USB networking is disabled by default while USB DRD FS/TinyUSB bring-up is pending:

```c
#define GW_USB_NETIF_ENABLE 0
```

Ethernet/lwIP initialization is independent from the disabled USB network task.
USB can be enabled only after a separate USB initialization and enumeration test.

## Release validation

1. Run a clean configure/build.
2. Verify the map vector address is `0x08000000` for direct-boot testing.
3. Flash with `flash.bat` and verify the programmer reports `0x08000000`.
4. Confirm the first system log appears after boot.
5. Send `help` through the serial shell.
6. Wait at least 30 seconds for monitor output.
7. Observe heap, stack, and CPU usage for at least 10 minutes.
8. Test Ethernet link/DHCP independently from USB.
