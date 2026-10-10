#include "tusb.h"

//--------------------------------------------------------------------
// Device Descriptor
//--------------------------------------------------------------------
tusb_desc_device_t const desc_device =
{
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,

    // Class/Subclass/Protocol are defined per-interface for DFU
    .bDeviceClass       = 0x00,
    .bDeviceSubClass    = 0x00,
    .bDeviceProtocol    = 0x00,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,

    .idVendor           = 0xCAFE,   // <-- replace with your VID
    .idProduct          = 0x4DF0,   // <-- replace with your PID
    .bcdDevice          = 0x0100,

    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,

    .bNumConfigurations = 0x01
};

uint8_t const * tud_descriptor_device_cb(void)
{
    return (uint8_t const *) &desc_device;
}

//--------------------------------------------------------------------
// DFU Functional Descriptor + Configuration
//--------------------------------------------------------------------
// Alternate settings = flash "partitions" the host can target with
// `dfu-util -a <n>`. Here we expose exactly one: the application slot.
enum
{
    ITF_NUM_DFU_MODE = 0,
    ITF_NUM_TOTAL
};

#define CONFIG_TOTAL_LEN   (TUD_CONFIG_DESC_LEN + TUD_DFU_DESC_LEN(1))

// wTransferSize: max block size the host will use for DNLOAD/UPLOAD.
// Keep it == CFG_TUD_DFU_XFER_BUFSIZE.
#define DFU_ATTRS   (DFU_ATTR_CAN_DOWNLOAD | DFU_ATTR_CAN_UPLOAD | DFU_ATTR_MANIFESTATION_TOLERANT)

uint8_t const desc_configuration[] =
{
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),

    TUD_DFU_DESCRIPTOR(ITF_NUM_DFU_MODE, 1, 4, DFU_ATTRS, 1000, CFG_TUD_DFU_XFER_BUFSIZE)
};

uint8_t const * tud_descriptor_configuration_cb(uint8_t index)
{
    (void) index;
    return desc_configuration;
}

//--------------------------------------------------------------------
// String Descriptors
//--------------------------------------------------------------------
char const *string_desc_arr[] =
{
    (const char[]) { 0x09, 0x04 },   // 0: English (0x0409)
    "SynaptiX",                  // 1: Manufacturer
    "GatewayV2 DFU Mode",        // 2: Product
    "123456789012",                  // 3: Serial (fill in from UID)
    "Application",                   // 4: DFU alt-setting 0 name
};

static uint16_t _desc_str[32];

uint16_t const * tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    (void) langid;
    uint8_t chr_count;

    if (index == 0)
    {
        memcpy(&_desc_str[1], string_desc_arr[0], 2);
        chr_count = 1;
    }
    else
    {
        if (index >= sizeof(string_desc_arr) / sizeof(string_desc_arr[0])) return NULL;

        const char *str = string_desc_arr[index];
        chr_count = (uint8_t) strlen(str);
        if (chr_count > 31) chr_count = 31;

        for (uint8_t i = 0; i < chr_count; i++) _desc_str[1 + i] = str[i];
    }

    _desc_str[0] = (uint16_t) ((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));
    return _desc_str;
}