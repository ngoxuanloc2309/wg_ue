/* Minimal stage-0 bootloader.
 *
 * The normal boot path deliberately has no HAL, serial, USB, metadata or
 * flash-controller dependency.  Its only responsibility is to transfer
 * control to the application linked at PRIMARY_APP_FLASH_START_ADDRESS.
 * OTA/DFU can be added behind an explicit update request once this path is
 * proven independently.
 */
#include "stm32h563xx.h"
#include "flash_define.h"
#include <stdint.h>

#define BOOT_PROBE        ((volatile uint32_t *)0x2000000CU)
#define BOOT_PROBE_ENTER  0xB0071000U
#define BOOT_PROBE_VALID  0xB0071001U
#define BOOT_PROBE_JUMP   0xB0071002U
#define BOOT_PROBE_ERROR  0xB00710FFU
#define BOOT_PROBE_RECOVERY 0xB00710D0U

/* Reserved shared-RAM request word. The application can write this key and
 * reset the MCU to request recovery without adding a bootloader dependency to
 * the normal application startup path. */
#define BOOT_REQUEST      ((volatile uint32_t *)0x20000000U)
#define BOOT_REQUEST_KEY  0xDEADBEEFU

static int application_vector_valid(uint32_t address)
{
    const uint32_t end = address + PRIMARY_APP_FLASH_SIZE;
    const uint32_t msp = *(const volatile uint32_t *)address;
    const uint32_t reset = *(const volatile uint32_t *)(address + 4U);
    const uint32_t reset_address = reset & ~1U;

    /* This image uses the 640 KiB SRAM map: 0x20000000..0x200A0000.
     * The initial MSP may equal the first byte above the SRAM range. */
    if (msp < 0x20000000U || msp > 0x200A0000U || (msp & 7U) != 0U) {
        return 0;
    }
    if ((reset & 1U) == 0U || reset_address < address || reset_address >= end) {
        return 0;
    }
    return 1;
}

__attribute__((noreturn, noinline)) static void boot_recovery(void)
{
    *BOOT_PROBE = BOOT_PROBE_RECOVERY;
    __disable_irq();
    for (;;) {
        __NOP();
    }
}

static int recovery_requested(void)
{
    if (*BOOT_REQUEST != BOOT_REQUEST_KEY) {
        return 0;
    }
    *BOOT_REQUEST = 0U;
    return 1;
}

__attribute__((noreturn, noinline)) static void jump_to_application(uint32_t address)
{
    const uint32_t *vector = (const uint32_t *)address;
    void (*reset_handler)(void) = (void (*)(void))vector[1];

    __disable_irq();
    SysTick->CTRL = 0U;
    SysTick->LOAD = 0U;
    SysTick->VAL = 0U;

    /* Do not leak bootloader IRQ state into FreeRTOS/application startup. */
    for (uint32_t i = 0U; i < 8U; ++i) {
        NVIC->ICER[i] = 0xFFFFFFFFU;
        NVIC->ICPR[i] = 0xFFFFFFFFU;
    }

    SCB->VTOR = address;
    __DSB();
    __ISB();
    __set_CONTROL(0U);
    __ISB();
    *BOOT_PROBE = BOOT_PROBE_JUMP;
    __set_MSP(vector[0]);
    __DSB();
    __ISB();

    /* Do not use a C call after changing MSP: its prologue/epilogue could
     * touch the application's stack before the application's reset code. */
    __asm volatile ("bx %0" : : "r" (reset_handler) : "memory");
    __builtin_unreachable();
}

int main(void)
{
    *BOOT_PROBE = BOOT_PROBE_ENTER;

    if (recovery_requested()) {
        boot_recovery();
    }

    if (!application_vector_valid(PRIMARY_APP_FLASH_START_ADDRESS)) {
        boot_recovery();
    }

    *BOOT_PROBE = BOOT_PROBE_VALID;
    jump_to_application(PRIMARY_APP_FLASH_START_ADDRESS);
}
