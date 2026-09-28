/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef REPRISE_WINUSB_H
#define REPRISE_WINUSB_H

#include <stdint.h>

static const uint8_t upload_os_string[] = {
    18, 3, 'M', 0, 'S', 0, 'F', 0, 'T', 0, '1', 0, '0', 0, '0', 0, 0x51, 0
};

static const uint8_t upload_compatible_id[] = {
    40, 0, 0, 0, 0, 1, 4, 0, 1, 0, 0, 0, 0, 0, 0, 0,
    0, 1, 'W', 'I', 'N', 'U', 'S', 'B', 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

/* Microsoft OS 1.0 extended properties, little endian on the S5L8702. */
static const struct __attribute__((packed)) {
    uint32_t length;
    uint16_t version, index, count;
    uint32_t property_length, type;
    uint16_t name_length;
    uint16_t name[20];
    uint32_t data_length;
    uint16_t guid[39];
} upload_properties = {
    142, 0x0100, 5, 1, 132, 1, 40, u"DeviceInterfaceGUID", 78,
    u"{FDF2BF46-3E26-4658-9AB1-EC731F374D92}"
};

static inline unsigned upload_os_descriptor(uint8_t type, uint8_t request,
    uint16_t value, uint16_t index, const void **data)
{
    if (type == 0x80 && request == 6 && value == 0x03ee && index == 0) {
        *data = upload_os_string;
        return sizeof(upload_os_string);
    }
    if (request != 0x51 || value != 0)
        return 0;
    if (type == 0xc0 && index == 4) {
        *data = upload_compatible_id;
        return sizeof(upload_compatible_id);
    }
    if ((type == 0xc0 || type == 0xc1) && index == 5) {
        *data = &upload_properties;
        return sizeof(upload_properties);
    }
    return 0;
}

#endif
