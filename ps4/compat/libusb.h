#ifndef _PS4_COMPAT_LIBUSB_H_
#define _PS4_COMPAT_LIBUSB_H_

#include <stdint.h>

// OpenOrbis Usbd.h includes <libusb.h>, but places types in <orbis/_types/libusb.h>
// and forward declares struct libusb_endpoint_descriptor without defining it.
struct libusb_endpoint_descriptor {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bEndpointAddress;
    uint8_t  bmAttributes;
    uint16_t wMaxPacketSize;
    uint8_t  bInterval;
    uint8_t  bRefresh;
    uint8_t  bSynchAddress;
    const unsigned char *extra;
    int32_t  extra_length;
};

#include <orbis/_types/libusb.h>

#endif // _PS4_COMPAT_LIBUSB_H_
