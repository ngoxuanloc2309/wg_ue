# GatewayV2 NCP Loader

STM32H563VIT6 host loader for programming the ST67W611M1 through QConn_Flash.
The project is adapted from ST's NUCLEO-H563ZI NCP_Loader and uses the GatewayV2
schematic pinout.

## Hardware mapping

| Gateway pin | Signal | Direction |
|---|---|---|
| PA9 | DEBUG_TX / LPUART1_TX | STM32 -> PC/QConn |
| PA10 | DEBUG_RX / LPUART1_RX | PC/QConn -> STM32 |
| PD5 | USART2_TX / ST67 UART_RX | STM32 -> ST67 |
| PD6 | USART2_RX / ST67 UART_TX | ST67 -> STM32 |
| PD1 | ST67_EN | STM32 output |
| PD2 | ST67_BOOT | STM32 output |

The schematic net names `ST67W_TX` and `ST67W_RX` describe the module side.
Therefore USART2 TX/RX swap remains disabled.

## Build modes

### AUTO mode (normal Gateway programming flow)

AUTO mode controls BOOT/CHIP_EN and performs a direct GPIO bit-copy UART
bridge. It is built optimized and has interrupts disabled before the 2 Mbps
pass-through loop, matching the timing-sensitive ST reference design.

```bat
build_ncp_loader_auto.bat
flash_ncp_loader_auto.bat
flash_st67_t02_auto.bat COM5 2.0.106 2000000
```

The STM32 debug UART must be connected to the PC at 2,000,000 baud, 8N1, with
no flow control. Do not open another terminal on the same COM port.

### Manual/direct USB-TTL mode

The non-AUTO image places all four UART GPIOs and BOOT/CHIP_EN control pins in
high impedance. The external operator controls BOOT/CHIP_EN and connects the
USB-TTL directly to ST67 UART:

```text
USB-TTL TX -> ST67 UART_RX (PD5 net)
USB-TTL RX <- ST67 UART_TX (PD6 net)
USB-TTL GND <-> Gateway/ST67 GND
```

Use a USB-TTL adapter whose logic level matches ST67 VDDIO.

```bat
build_ncp_loader.bat
flash_ncp_loader.bat
flash_st67_t02.bat COM5 2.0.106 2000000
```

For Mission T01, use the corresponding T01 configuration and image:

```bat
flash_st67_t01.bat COM5 2.0.106 2000000
```

The Gateway AUTO-loader variant is:

```bat
flash_st67_t01_auto.bat COM26 2.0.106 2000000
```

## ST67 boot sequence

AUTO mode applies the reference sequence:

```text
BOOT = High
CHIP_EN = Low for 100 ms
CHIP_EN = High
wait 100 ms
BOOT = Low
```

BOOT is sampled during the CHIP_EN reset/release sequence; it does not need to
remain high after the reset sequence.

## Flash tool

The QConn configuration and firmware must come from the same X-CUBE-ST67W61
release. The official T01/T02/MFG configurations all initialize the same ST67
boot2 before programming their respective images.

For GatewayV2, use the generated AUTO script so the `vxxx` firmware placeholder
is replaced with the selected version. The script also passes `efusedata.bin`
and checks QConn's exit code.

## Validation

A successful run reaches all of the following:

```text
shake hand success
ChipID: <device-specific ID>
jedec_id:c46016
capacity:4.0M
Verify success
Program Finished
[All Success]
```

If handshake and bootinfo succeed but `Set flash config` or JEDEC read fails,
investigate the ST67 module/flash hardware or module revision before changing
the STM32 UART bridge.
