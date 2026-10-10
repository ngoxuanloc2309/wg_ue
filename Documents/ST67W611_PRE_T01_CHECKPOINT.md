# ST67W611 pre-T01 migration checkpoint

## Baseline

- Host MCU: STM32H563VIT6.
- ST67W611 firmware: `st67w611m_mission_t01_v2.0.106.bin`.
- Flash result: QConn_Flash completed with SHA256 verification success for boot2, partition, Mission T01 and LittleFS.
- STM32 SPI loopback: passed.

## Known state before T01 host migration

- The source tree still contains the previous Gateway/T02-oriented Wi-Fi integration and diagnostic SPI changes.
- Runtime previously reached the ST67 SPI transaction but received an invalid zero header instead of `AA 55`.
- The baseline is preserved here so transport and architecture changes can be compared independently.

## Next migration target

- Set `ST67_ARCH` to `W6X_ARCH_T01`.
- Follow the official NUCLEO-H563ZI `ST67W6X_CLI` initialization and SPI port semantics.
- Keep STM32 Ethernet independent; do not use the T02 `W6X_Netif_*` adapter for T01 Wi-Fi.
