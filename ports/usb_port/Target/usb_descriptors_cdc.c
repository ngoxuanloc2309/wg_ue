/*
 * usb_descriptors_cdc.c - descriptors of the CDC-only USB device
 * (GW_USB_CDC_ENABLE=1, see CMakeLists.txt).
 *
 * One configuration, one CDC-ACM (a virtual COM port). It replaces
 * usb_descriptors.c, which describes the RNDIS/ECM + ACM composite device of
 * GW_USB_NETIF_ENABLE=1; the two are never built together.
 */
#include <stddef.h>
#include <stdint.h>

#include "tusb.h"
#include "stm32h5xx_hal.h"

/* A product id that differs from the composite device's (0x4021): the PC keeps
 * driver state per VID/PID, so a changed descriptor set must not reuse it. */
#define GW_USB_CDC_VID                  0xCAFEU
#define GW_USB_CDC_PID                  0x4001U
#define GW_USB_CDC_BCD_USB              0x0200U
#define GW_USB_CDC_BCD_DEVICE           0x0100U

#define GW_USB_CDC_CONFIG_NUMBER        1U
#define GW_USB_CDC_CONFIG_POWER_MA      100U
#define GW_USB_CDC_CONFIG_ATTRIBUTE     0U

#define GW_USB_CDC_EP_NOTIF             0x81U
#define GW_USB_CDC_EP_OUT               0x02U
#define GW_USB_CDC_EP_IN                0x82U
#define GW_USB_CDC_EP_NOTIF_SIZE        8U
#define GW_USB_CDC_EP_DATA_SIZE         64U   /* full-speed bulk maximum */

#define GW_USB_CDC_UID_WORDS            3U
#define GW_USB_CDC_SERIAL_CHARS         (GW_USB_CDC_UID_WORDS * 8U)
#define GW_USB_CDC_STRING_MAX_CHARS     32U
#define GW_USB_CDC_NIBBLES_PER_WORD     8U
#define GW_USB_CDC_NIBBLE_BITS          4U
#define GW_USB_CDC_NIBBLE_MASK          0xFU
#define GW_USB_CDC_LANGID_ENGLISH_US    0x0409U

enum
{
    STRID_LANGID = 0,
    STRID_MANUFACTURER,
    STRID_PRODUCT,
    STRID_SERIAL,
    STRID_CDC_ACM,
    STRID_COUNT
};

enum
{
    ITF_NUM_CDC = 0,
    ITF_NUM_CDC_DATA,
    ITF_NUM_TOTAL
};

#define GW_USB_CDC_CONFIG_TOTAL_LEN     (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN)

static const tusb_desc_device_t s_desc_device =
{
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = GW_USB_CDC_BCD_USB,
    /* IAD device class: the standard TinyUSB layout for a CDC function. */
    .bDeviceClass       = TUSB_CLASS_MISC,
    .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol    = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = GW_USB_CDC_VID,
    .idProduct          = GW_USB_CDC_PID,
    .bcdDevice          = GW_USB_CDC_BCD_DEVICE,
    .iManufacturer      = STRID_MANUFACTURER,
    .iProduct           = STRID_PRODUCT,
    .iSerialNumber      = STRID_SERIAL,
    .bNumConfigurations = 1
};

static const uint8_t s_desc_configuration[] =
{
    TUD_CONFIG_DESCRIPTOR(GW_USB_CDC_CONFIG_NUMBER, ITF_NUM_TOTAL, 0,
                          GW_USB_CDC_CONFIG_TOTAL_LEN,
                          GW_USB_CDC_CONFIG_ATTRIBUTE, GW_USB_CDC_CONFIG_POWER_MA),
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, STRID_CDC_ACM,
                       GW_USB_CDC_EP_NOTIF, GW_USB_CDC_EP_NOTIF_SIZE,
                       GW_USB_CDC_EP_OUT, GW_USB_CDC_EP_IN, GW_USB_CDC_EP_DATA_SIZE)
};

static const char *const s_string_table[STRID_COUNT] =
{
    [STRID_LANGID]       = NULL,                  /* handled separately */
    [STRID_MANUFACTURER] = "Synaptix.,JSC",
    [STRID_PRODUCT]      = "Synaptix Device",
    [STRID_SERIAL]       = NULL,                  /* built from the MCU unique id */
    [STRID_CDC_ACM]      = "Synaptix COM Port"
};

/* One buffer is enough: TinyUSB copies the descriptor out before the next
 * GET_DESCRIPTOR(STRING) request is processed. +1 for the header word. */
static uint16_t s_desc_string[GW_USB_CDC_STRING_MAX_CHARS + 1U];

const uint8_t *tud_descriptor_device_cb(void)
{
    return (const uint8_t *)&s_desc_device;
}

const uint8_t *tud_descriptor_configuration_cb(uint8_t index)
{
    (void)index;   /* single configuration */
    return s_desc_configuration;
}

/* Unique serial: a fixed per-device string keeps the same COM port number on
 * the PC across re-plugs. Hex of the three 32-bit unique-id words. */
static uint8_t gw_usb_cdc_fill_serial(uint16_t *dst)
{
    const uint32_t uid[GW_USB_CDC_UID_WORDS] =
    {
        HAL_GetUIDw0(), HAL_GetUIDw1(), HAL_GetUIDw2()
    };
    static const char hex_digits[] = "0123456789ABCDEF";
    uint8_t count = 0U;

    for (uint32_t word = 0U; word < GW_USB_CDC_UID_WORDS; word++)
    {
        for (uint32_t nibble = 0U; nibble < GW_USB_CDC_NIBBLES_PER_WORD; nibble++)
        {
            uint32_t shift = (GW_USB_CDC_NIBBLES_PER_WORD - 1U - nibble) * GW_USB_CDC_NIBBLE_BITS;
            uint32_t value = (uid[word] >> shift) & GW_USB_CDC_NIBBLE_MASK;
            dst[count] = (uint16_t)hex_digits[value];
            count++;
        }
    }
    return count;
}

const uint16_t *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    (void)langid;
    uint8_t char_count = 0U;

    if (index == STRID_LANGID)
    {
        s_desc_string[1] = GW_USB_CDC_LANGID_ENGLISH_US;
        char_count = 1U;
    }
    else if (index == STRID_SERIAL)
    {
        char_count = gw_usb_cdc_fill_serial(&s_desc_string[1]);
    }
    else
    {
        if (index >= STRID_COUNT)
        {
            return NULL;
        }
        const char *text = s_string_table[index];
        if (text == NULL)
        {
            return NULL;
        }
        while ((text[char_count] != '\0') && (char_count < GW_USB_CDC_STRING_MAX_CHARS))
        {
            s_desc_string[1U + char_count] = (uint16_t)(uint8_t)text[char_count];
            char_count++;
        }
    }

    /* Header word: descriptor type in the high byte, total length in bytes. */
    s_desc_string[0] = (uint16_t)(((uint16_t)TUSB_DESC_STRING << 8) |
                                  (uint16_t)((2U * char_count) + 2U));
    return s_desc_string;
}