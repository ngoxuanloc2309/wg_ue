#ifndef TUSB_CONFIG_H_
#define TUSB_CONFIG_H_

#ifdef __cplusplus
extern "C" {
#endif

//--------------------------------------------------------------------
// MCU / RTOS
//--------------------------------------------------------------------
#define CFG_TUSB_MCU              OPT_MCU_STM32H5
#define CFG_TUSB_OS               OPT_OS_NONE
#define CFG_TUSB_RHPORT0_MODE     (OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED)

#ifndef CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_SECTION
#endif

#ifndef CFG_TUSB_MEM_ALIGN
#define CFG_TUSB_MEM_ALIGN         __attribute__((aligned(4)))
#endif

//--------------------------------------------------------------------
// DEVICE CONFIGURATION
//--------------------------------------------------------------------
#define CFG_TUD_ENDPOINT0_SIZE     64

#define CFG_TUD_CDC                0
#define CFG_TUD_MSC                0
#define CFG_TUD_HID                0
#define CFG_TUD_MIDI               0
#define CFG_TUD_VENDOR             0
#define CFG_TUD_DFU_RUNTIME        0   // set to 1 instead of CFG_TUD_DFU if you want runtime-DFU (detach + re-enumerate)
#define CFG_TUD_DFU                1   // standalone DFU-mode bootloader

// Must be a multiple of the flash quad-word (16 bytes) for STM32H5.
// 1024 gives a good balance of USB throughput vs RAM buffer usage.
#define CFG_TUD_DFU_XFER_BUFSIZE   1024

#ifdef __cplusplus
}
#endif

#endif
