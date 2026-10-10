# ST67W611 T02 reference research

Date: 2026-08-03  
Scope: compare GatewayV2 with the official STM32H563ZI T02 application.  
This note records research only; no Gateway source code is changed by this note.

## Executive conclusion

The official ST project that demonstrates the T02 path is:

`libs/x-cube-st67w61/Projects/NUCLEO-H563ZI/Applications/ST67W6X/ST67W6X_CLI_LWIP`

It is the closest known-good reference because it uses an STM32H563 host, the T02 architecture, SPI transport, host-side LwIP, and the CLI `ping` command.  ST documents successful output such as `5 packets transmitted, 5 received, 0% packet loss` for the CLI ping flow.

The current Gateway reaches `CS ASSERT`, but does not reach a completed `W6X_Init()`.  Therefore the current failure is below Wi-Fi association and below IP/ping.  It is a host-task/SPI-transport initialization problem first.

## Official T02 flow

The official H563 T02 application initializes the hardware before the scheduler:

1. GPIO
2. GPDMA1
3. ICACHE
4. SPI1
5. USART3
6. `MX_ST67W6X_Init()`
7. scheduler/application task

`MX_ST67W6X_Init()` creates the ST67 application task.  The task then runs the application sequence in `main_app.c`:

1. register callbacks
2. `W6X_Init()`
3. `W6X_WiFi_Init()`
4. `MX_LWIP_Init()`
5. CLI/network operations, including ping

The official T02 compiler define is `ST67_ARCH=W6X_ARCH_T02`.  The Gateway currently uses numeric `ST67_ARCH=2`; this builds and is probably equivalent, but the symbolic vendor define is the unambiguous reference.

## Comparison with GatewayV2

| Area | Official H563 T02 reference | GatewayV2 | Assessment |
|---|---|---|---|
| Application task | Dedicated `MX_ST67W6X_Init()` task, stack `4096U >> 2U`, priority `24U` | On-demand `wifi_connect_task`, priority `20` | Significant architecture difference; use the official task model for a clean A/B test |
| Vendor task priorities | `W61_MDM_RX_TASK_PRIO=54`, `SPI_THREAD_PRIO=53` from vendor defaults | Forced to `9` and `8` in `CMakeLists.txt` | Significant reference mismatch; lowering these was a diagnostic experiment, not a vendor-equivalent configuration |
| T02 selection | `ST67_ARCH=W6X_ARCH_T02` | `ST67_ARCH=2` | Likely equivalent, but should be compared symbolically |
| Wi-Fi autoconnect | `W6X_WIFI_AUTOCONNECT=1` | `W6X_WIFI_AUTOCONNECT=0` | Behavioral difference after initialization; not the primary cause of stopping before `W6X_Init()` returns |
| SPI hardware init | SPI1/GPDMA initialized before scheduler | Same prerequisite is present in Gateway startup | Broadly aligned; compare exact CubeMX DMA/channel settings next |
| RX-only DMA | Vendor reference uses `HAL_SPI_TransmitReceive_DMA()` with a dummy TX byte | Gateway currently uses `HAL_SPI_TransmitReceive_DMA()` for its transfer paths | This part is close to the current vendor template; do not infer a DMA bug from the API alone |
| Boot/RDY handling | Vendor port uses the reference sequencing and transaction callbacks | Gateway adds custom delays, readiness diagnostics, and RDY level recovery | Useful diagnostics, but not a clean baseline against the vendor implementation |
| Runtime networking | T02 has LwIP on the STM32 host | Gateway also intends host-side LwIP | Correct architectural target; ping is only meaningful after `W6X_Init()` and LwIP initialization complete |

## What the successful ping example proves

The official CLI/LwIP example uses the ST67W6X CLI after initialization.  A documented command is:

```text
ping www.google.com -c 5
```

The H563 T02 README also documents the numeric form:

```text
ping 8.8.8.8 -s 64 -c 4 -i 1000
```

Expected evidence is a completed initialization, a working host-side network interface, and packet statistics such as 4/4 or 5/5 received.  This is not evidence that the bootloader UART firmware or the T02 image alone proves SPI runtime correctness.

## Interpretation of the current log

Current evidence:

```text
SPI port initialized
RDY ... LOW
RDY rising callback
spi_port_is_ready=HIGH
CS ASSERT
```

This confirms that:

- STM32 reaches the port initialization code;
- ST67 asserts RDY at least once;
- the first SPI transaction starts and chip select is asserted.

It does not yet confirm:

- valid SPI header exchange;
- correct DMA completion/error callback;
- correct cache maintenance;
- successful vendor transaction semaphore hand-off;
- `W6X_Init()` return.

The earlier reboot after `CS ASSERT` must be classified using the reset-cause log already added to the working tree.  If reset flags are clear, suspect a manual/programmer reset or power event; if a fault/watchdog flag is set, inspect the corresponding handler/task stack.  Do not treat the absence of further UART text as proof of a Wi-Fi failure.

## Highest-value comparison order

1. Compare the Gateway application/task structure against the official `MX_ST67W6X_Init()` and `main_app.c` flow.
2. Restore vendor-default ST67 task priorities for one controlled reference build; do not mix priority changes with SPI protocol changes.
3. Compare the complete official H563 `.ioc` and generated `spi.c`, `stm32h5xx_it.c`, and DMA setup against Gateway, including channel assignment, request IDs, direction, data alignment, priority, and interrupt routing.
4. Run with the vendor SPI port implementation as the baseline.  Keep diagnostics outside the vendor transport path where possible.
5. Only after `W6X_Init()` returns, test T02 LwIP and the documented ping commands.

## Sources

- [ST X-CUBE-ST67W61 architecture](https://wiki.st.com/stm32mcu/wiki/Connectivity%3AX-CUBE-ST67W61_Architecture)
- [ST X-CUBE-ST67W61 overview](https://wiki.st.com/stm32mcu/wiki/Connectivity%3AX-CUBE-ST67W61_Overview)
- [ST Wi-Fi CubeMX pack usage](https://wiki.st.com/stm32mcu/wiki/Connectivity%3AWi-Fi_How_to_use_X_CUBE_ST67W61_STM32CubeMx_pack)
- [ST ST67W6X CLI application and ping example](https://wiki.st.com/stm32mcu/wiki/Connectivity%3AWi-Fi_ST67W6X_CLI_Application)
- [ST Wi-Fi MCU hardware setup](https://wiki.st.com/stm32mcu/wiki/Connectivity%3AWi-Fi_MCU_Hardware_Setup)
- [ST community: ST67W61 Nucleo and H563ZI Nucleo](https://community.st.com/t5/interface-and-connectivity-ics/st67w61-nucleo-and-h563zi-nucleo/td-p/860379)
- [ST community: ST67W61 pass-through mode](https://community.st.com/interface-and-connectivity-ics-52/st67w61-pass-through-mode-154763)
- [UM3475: Getting started with X-CUBE-ST67W61](https://www.st.com/resource/en/user_manual/um3475-getting-started-with-xcubest67w61-stmicroelectronics.pdf)

