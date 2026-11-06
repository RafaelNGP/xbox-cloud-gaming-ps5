// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// USB pads on the PS4 (pad_usb.h).
// Native in-app USB controller driver subsystem using libSceUsbd (Orbis OS userland USB).
// Ported and adapted from OneFamily (ChiricoG/OneFamily, MIT) and OmniPad CE.
#include "input/pad_usb.h"
#include "input/pad_usb_hid.h"
#include "platform/platform.h"
#include "util/log.h"

#include <stdint.h>
#include <cstring>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <orbis/Usbd.h>

extern "C" {
int sceKernelLoadStartModule(const char* path, size_t argc, const void* argv, uint32_t flags, void* opt, int* res);
int32_t sceKernelDlsym(int32_t handle, const char* symbol, void** address);
int sceKernelStopUnloadModule(int handle, size_t argc, const void* argv, uint32_t flags, void* opt, int* res);
}

namespace xc::input {

namespace {

// libSceUsbd dynamic function pointer types
typedef int32_t (*sceUsbdInit_t)(void);
typedef int32_t (*sceUsbdExit_t)(void);
typedef int32_t (*sceUsbdGetDeviceList_t)(libusb_device***);
typedef int32_t (*sceUsbdFreeDeviceList_t)(libusb_device**);
typedef int32_t (*sceUsbdGetDeviceDescriptor_t)(libusb_device*, struct libusb_device_descriptor*);
typedef int32_t (*sceUsbdGetActiveConfigDescriptor_t)(libusb_device*, struct libusb_config_descriptor**);
typedef int32_t (*sceUsbdGetConfigDescriptor_t)(libusb_device*, uint8_t, struct libusb_config_descriptor**);
typedef int32_t (*sceUsbdFreeConfigDescriptor_t)(struct libusb_config_descriptor*);
typedef int32_t (*sceUsbdOpen_t)(libusb_device*, libusb_device_handle**);
typedef void    (*sceUsbdClose_t)(libusb_device_handle*);
typedef int32_t (*sceUsbdClaimInterface_t)(libusb_device_handle*, int32_t);
typedef int32_t (*sceUsbdReleaseInterface_t)(libusb_device_handle*, int32_t);
typedef int32_t (*sceUsbdSetInterfaceAltSetting_t)(libusb_device_handle*, int32_t, int32_t);
typedef int32_t (*sceUsbdInterruptTransfer_t)(libusb_device_handle*, unsigned char, unsigned char*, int32_t, int32_t*, uint32_t);
typedef int32_t (*sceUsbdControlTransfer_t)(libusb_device_handle*, uint8_t, uint8_t, uint16_t, uint16_t, unsigned char*, uint16_t, uint32_t);
typedef int32_t (*sceUsbdCheckConnected_t)(libusb_device_handle*);
typedef int32_t (*sceUsbdClearHalt_t)(libusb_device_handle*, unsigned char);
typedef void    (*sceUsbdDetachKernelDriver_t)(libusb_device_handle*, int32_t);
typedef int32_t (*sceUsbdGetStringDescriptorAscii_t)(libusb_device_handle*, uint8_t, uint16_t, unsigned char*, int32_t);
typedef uint8_t (*sceUsbdGetBusNumber_t)(libusb_device*);
typedef uint8_t (*sceUsbdGetDeviceAddress_t)(libusb_device*);

static int g_hUsbd = 0;
static sceUsbdInit_t                       p_sceUsbdInit = nullptr;
static sceUsbdExit_t                       p_sceUsbdExit = nullptr;
static sceUsbdGetDeviceList_t              p_sceUsbdGetDeviceList = nullptr;
static sceUsbdFreeDeviceList_t             p_sceUsbdFreeDeviceList = nullptr;
static sceUsbdGetDeviceDescriptor_t        p_sceUsbdGetDeviceDescriptor = nullptr;
static sceUsbdGetActiveConfigDescriptor_t  p_sceUsbdGetActiveConfigDescriptor = nullptr;
static sceUsbdGetConfigDescriptor_t        p_sceUsbdGetConfigDescriptor = nullptr;
static sceUsbdFreeConfigDescriptor_t       p_sceUsbdFreeConfigDescriptor = nullptr;
static sceUsbdOpen_t                       p_sceUsbdOpen = nullptr;
static sceUsbdClose_t                      p_sceUsbdClose = nullptr;
static sceUsbdClaimInterface_t             p_sceUsbdClaimInterface = nullptr;
static sceUsbdReleaseInterface_t           p_sceUsbdReleaseInterface = nullptr;
static sceUsbdSetInterfaceAltSetting_t     p_sceUsbdSetInterfaceAltSetting = nullptr;
static sceUsbdInterruptTransfer_t          p_sceUsbdInterruptTransfer = nullptr;
static sceUsbdControlTransfer_t            p_sceUsbdControlTransfer = nullptr;
static sceUsbdCheckConnected_t             p_sceUsbdCheckConnected = nullptr;
static sceUsbdClearHalt_t                  p_sceUsbdClearHalt = nullptr;
static sceUsbdDetachKernelDriver_t         p_sceUsbdDetachKernelDriver = nullptr;
static sceUsbdGetStringDescriptorAscii_t   p_sceUsbdGetStringDescriptorAscii = nullptr;
static sceUsbdGetBusNumber_t               p_sceUsbdGetBusNumber = nullptr;
static sceUsbdGetDeviceAddress_t           p_sceUsbdGetDeviceAddress = nullptr;

bool loadUsbd() {
    if (g_hUsbd > 0) return true;
    int res = 0;
    int h = sceKernelLoadStartModule("/system/common/lib/libSceUsbd.sprx", 0, nullptr, 0, nullptr, &res);
    if (h < 0) {
        h = sceKernelLoadStartModule("libSceUsbd.sprx", 0, nullptr, 0, nullptr, &res);
    }
    if (h <= 0) {
        XC_LOGW("usb pad: failed to load libSceUsbd.sprx: 0x%08x", static_cast<unsigned>(h));
        return false;
    }
    g_hUsbd = h;

#define RESOLVE(fn) sceKernelDlsym(g_hUsbd, #fn, reinterpret_cast<void**>(&p_##fn))
    RESOLVE(sceUsbdInit);
    RESOLVE(sceUsbdExit);
    RESOLVE(sceUsbdGetDeviceList);
    RESOLVE(sceUsbdFreeDeviceList);
    RESOLVE(sceUsbdGetDeviceDescriptor);
    RESOLVE(sceUsbdGetActiveConfigDescriptor);
    RESOLVE(sceUsbdGetConfigDescriptor);
    RESOLVE(sceUsbdFreeConfigDescriptor);
    RESOLVE(sceUsbdOpen);
    RESOLVE(sceUsbdClose);
    RESOLVE(sceUsbdClaimInterface);
    RESOLVE(sceUsbdReleaseInterface);
    RESOLVE(sceUsbdSetInterfaceAltSetting);
    RESOLVE(sceUsbdInterruptTransfer);
    RESOLVE(sceUsbdControlTransfer);
    RESOLVE(sceUsbdCheckConnected);
    RESOLVE(sceUsbdClearHalt);
    RESOLVE(sceUsbdDetachKernelDriver);
    RESOLVE(sceUsbdGetStringDescriptorAscii);
    RESOLVE(sceUsbdGetBusNumber);
    RESOLVE(sceUsbdGetDeviceAddress);
#undef RESOLVE

    if (!p_sceUsbdInit || !p_sceUsbdGetDeviceList || !p_sceUsbdOpen || !p_sceUsbdInterruptTransfer) {
        XC_LOGE("usb pad: failed to resolve required libSceUsbd symbols");
        return false;
    }
    p_sceUsbdInit();
    XC_LOGI("usb pad: libSceUsbd loaded and initialized (handle 0x%08x)", g_hUsbd);
    return true;
}

int32_t getActiveConfigDescriptor(libusb_device* dev, struct libusb_config_descriptor** config) {
    if (p_sceUsbdGetActiveConfigDescriptor && p_sceUsbdGetActiveConfigDescriptor(dev, config) == 0 && *config) {
        return 0;
    }
    if (p_sceUsbdGetConfigDescriptor && p_sceUsbdGetConfigDescriptor(dev, 0, config) == 0 && *config) {
        return 0;
    }
    return -1;
}

void freeConfigDescriptor(struct libusb_config_descriptor* config) {
    if (p_sceUsbdFreeConfigDescriptor && config) {
        p_sceUsbdFreeConfigDescriptor(config);
    }
}

// Controller hardware identifiers and interface masks
constexpr uint16_t SONY_VID = 0x054C;
constexpr uint16_t SONY_DS3_PID = 0x0268;
constexpr uint16_t SONY_DS4_V1_PID = 0x05C4;
constexpr uint16_t SONY_DS4_V2_PID = 0x09CC;
constexpr uint16_t SONY_DS4_PROT_PID = 0x05C5;
constexpr uint16_t SONY_DS4_DONGLE_PID = 0x0BA0;
constexpr uint16_t SONY_DUALSENSE_PID = 0x0CE6;
constexpr uint16_t SONY_DUALSENSE_EDGE_PID = 0x0DF2;

constexpr uint16_t XBOX_VID = 0x045E;
constexpr uint16_t XBOX360_PID = 0x028E;
constexpr uint16_t PDP_VID = 0x0E6F;
constexpr uint16_t VICTRIX_PRO_BFG_PID = 0x024C;

constexpr uint16_t SWITCH_PRO_VID = 0x057E;
constexpr uint16_t SWITCH_PRO_PID = 0x2009;
constexpr uint16_t SWITCH_ROCKCANDY_PID = 0x0187;

constexpr uint16_t GOOGLE_VID = 0x18D1;
constexpr uint16_t STADIA_PID = 0x9400;

bool isNativePs4Controller(uint16_t vid, uint16_t pid) {
    if (vid == SONY_VID) {
        if (pid == SONY_DS4_V1_PID || pid == SONY_DS4_V2_PID || pid == SONY_DS4_PROT_PID || pid == SONY_DS4_DONGLE_PID) {
            return true;
        }
    }
    return false;
}

bool isXboxOnePid(uint16_t pid) {
    static const uint16_t pids[] = {0x02D1, 0x02DD, 0x02E3, 0x02EA, 0x0B00, 0x0B0A, 0x0B12};
    for (uint16_t p : pids) {
        if (p == pid) return true;
    }
    return false;
}

bool isSwitchProPid(uint16_t pid) {
    static const uint16_t pids[] = {0x2009, 0x2006, 0x2007, 0x200E, 0x2017, 0x2019, 0x201E};
    for (uint16_t p : pids) {
        if (p == pid) return true;
    }
    return false;
}

struct DetectedPad {
    UsbPadProtocol protocol = UsbPadProtocol::None;
    uint8_t interfaceNum = 0;
    uint8_t altSetting = 0;
    uint8_t epIn = 0;
    uint8_t epOut = 0;
    uint8_t kbdInterfaceNum = 0xFF;
    std::string name;
};

bool detectXboxByDescriptor(libusb_device* dev, DetectedPad& out) {
    struct libusb_config_descriptor* cfg = nullptr;
    if (getActiveConfigDescriptor(dev, &cfg) != 0 || !cfg) return false;

    bool found = false;
    for (int i = 0; i < cfg->bNumInterfaces && !found; ++i) {
        const struct libusb_interface* intf = &cfg->interface[i];
        for (int a = 0; a < intf->num_altsetting && !found; ++a) {
            const struct libusb_interface_descriptor* alt = &intf->altsetting[a];
            if (alt->bInterfaceClass != 0xFF) continue;

            UsbPadProtocol proto = UsbPadProtocol::None;
            if (alt->bInterfaceSubClass == 0x5D && (alt->bInterfaceProtocol == 0x01 || alt->bInterfaceProtocol == 0x81 || alt->bInterfaceProtocol == 0x00)) {
                proto = UsbPadProtocol::XInput;
            } else if (alt->bInterfaceSubClass == 0x47 && alt->bInterfaceProtocol == 0xD0) {
                proto = UsbPadProtocol::Gip;
            } else {
                continue;
            }

            uint8_t epIn = 0, epOut = 0;
            for (int k = 0; k < alt->bNumEndpoints; ++k) {
                const struct libusb_endpoint_descriptor* ep = &alt->endpoint[k];
                if ((ep->bmAttributes & 0x03) != 0x03) continue; // Interrupt
                if (ep->bEndpointAddress & 0x80) {
                    if (!epIn) epIn = ep->bEndpointAddress;
                } else {
                    if (!epOut) epOut = ep->bEndpointAddress;
                }
            }
            if (!epIn) continue;

            out.protocol = proto;
            out.interfaceNum = alt->bInterfaceNumber;
            out.altSetting = alt->bAlternateSetting;
            out.epIn = epIn;
            out.epOut = epOut;
            out.kbdInterfaceNum = 0xFF;
            found = true;
        }
    }

    if (found) {
        // Scan for composite keyboard interface
        for (int i = 0; i < cfg->bNumInterfaces; ++i) {
            const struct libusb_interface* intf = &cfg->interface[i];
            for (int a = 0; a < intf->num_altsetting; ++a) {
                const struct libusb_interface_descriptor* alt = &intf->altsetting[a];
                if (alt->bInterfaceClass == 0x03 && alt->bInterfaceSubClass == 1 && alt->bInterfaceProtocol == 1) {
                    out.kbdInterfaceNum = alt->bInterfaceNumber;
                    break;
                }
            }
        }
    }

    freeConfigDescriptor(cfg);
    return found;
}

bool detectGenericHidEndpoints(libusb_device* dev, DetectedPad& out) {
    struct libusb_config_descriptor* cfg = nullptr;
    if (getActiveConfigDescriptor(dev, &cfg) != 0 || !cfg) return false;

    bool found = false;
    for (int i = 0; i < cfg->bNumInterfaces && !found; ++i) {
        const struct libusb_interface* intf = &cfg->interface[i];
        for (int a = 0; a < intf->num_altsetting && !found; ++a) {
            const struct libusb_interface_descriptor* alt = &intf->altsetting[a];
            if (alt->bInterfaceClass != 0x03) continue; // HID
            if (alt->bInterfaceSubClass == 1 && alt->bInterfaceProtocol == 1) continue; // Skip keyboard

            uint8_t epIn = 0, epOut = 0;
            for (int k = 0; k < alt->bNumEndpoints; ++k) {
                const struct libusb_endpoint_descriptor* ep = &alt->endpoint[k];
                if ((ep->bmAttributes & 0x03) != 0x03) continue;
                if (ep->bEndpointAddress & 0x80) {
                    if (!epIn) epIn = ep->bEndpointAddress;
                } else {
                    if (!epOut) epOut = ep->bEndpointAddress;
                }
            }
            if (!epIn) continue;

            out.protocol = UsbPadProtocol::GenericHid;
            out.interfaceNum = alt->bInterfaceNumber;
            out.altSetting = alt->bAlternateSetting;
            out.epIn = epIn;
            out.epOut = epOut;
            out.kbdInterfaceNum = 0xFF;
            found = true;
        }
    }

    if (found) {
        for (int i = 0; i < cfg->bNumInterfaces; ++i) {
            const struct libusb_interface* intf = &cfg->interface[i];
            for (int a = 0; a < intf->num_altsetting; ++a) {
                const struct libusb_interface_descriptor* alt = &intf->altsetting[a];
                if (alt->bInterfaceClass == 0x03 && alt->bInterfaceSubClass == 1 && alt->bInterfaceProtocol == 1 &&
                    alt->bInterfaceNumber != out.interfaceNum) {
                    out.kbdInterfaceNum = alt->bInterfaceNumber;
                    break;
                }
            }
        }
    }

    freeConfigDescriptor(cfg);
    return found;
}

bool isDualShock4Controller(uint16_t vid, uint16_t pid) {
    if (vid == SONY_VID && (pid == SONY_DS4_V1_PID || pid == SONY_DS4_V2_PID || pid == SONY_DS4_PROT_PID || pid == SONY_DS4_DONGLE_PID)) {
        return false; // Handled natively by Orbis ScePad
    }
    if (vid == 0x146B && (pid == 0x0601 || pid == 0x0602 || pid == 0x0604 || pid == 0x0609)) return true; // Nacon
    if (vid == 0x1532 && (pid == 0x0401 || pid == 0x1000 || pid == 0x1004 || pid == 0x1007 || pid == 0x1008)) return true; // Razer
    if (vid == 0x0F0D && (pid == 0x0055 || pid == 0x0066 || pid == 0x0084 || pid == 0x00A0 || pid == 0x00EE)) return true; // Hori
    if (vid == 0x2C22 && (pid == 0x2000 || pid == 0x2300 || pid == 0x2500)) return true; // Qanba
    if (vid == 0x0C12 && (pid == 0x0E10 || pid == 0x0CF2 || pid == 0x1CF2)) return true; // Brook / Zeroplus
    return false;
}

bool identifyController(libusb_device* dev, const struct libusb_device_descriptor& desc, DetectedPad& out) {
    uint16_t vid = desc.idVendor;
    uint16_t pid = desc.idProduct;

    // 0. Skip native PS4 controllers (handled natively by ScePad)
    if (isNativePs4Controller(vid, pid)) return false;

    // 1. Sony DualShock 3
    if (vid == SONY_VID && pid == SONY_DS3_PID) {
        out.protocol = UsbPadProtocol::DualShock3;
        out.interfaceNum = 0;
        out.epIn = 0x81;
        out.epOut = 0x02;
        out.name = "DualShock 3";
        return true;
    }

    // 1b. Third-party DualShock 4 controllers (Nacon, Razer, Hori, Qanba, Brook, etc.)
    if (isDualShock4Controller(vid, pid)) {
        out.protocol = UsbPadProtocol::DualShock4;
        out.name = "PS4 Controller";
        if (!detectGenericHidEndpoints(dev, out)) {
            out.interfaceNum = 0;
            out.epIn = 0x81;
            out.epOut = 0x01;
        }
        out.protocol = UsbPadProtocol::DualShock4;
        return true;
    }

    // 2. Sony DualSense / DualSense Edge
    if (vid == SONY_VID && (pid == SONY_DUALSENSE_PID || pid == SONY_DUALSENSE_EDGE_PID)) {
        out.protocol = UsbPadProtocol::DualSense;
        out.name = (pid == SONY_DUALSENSE_EDGE_PID) ? "DualSense Edge" : "DualSense";
        if (!detectGenericHidEndpoints(dev, out)) {
            out.interfaceNum = 3;
            out.epIn = 0x84;
            out.epOut = 0x03;
        }
        out.protocol = UsbPadProtocol::DualSense;
        return true;
    }

    // 3. Official Xbox 360 & Xbox One
    if (vid == XBOX_VID) {
        if (pid == XBOX360_PID) {
            out.protocol = UsbPadProtocol::XInput;
            out.name = "Xbox 360 Controller";
            if (!detectXboxByDescriptor(dev, out)) {
                out.interfaceNum = 0;
                out.epIn = 0x81;
                out.epOut = 0x02;
            }
            return true;
        }
        if (isXboxOnePid(pid)) {
            out.protocol = UsbPadProtocol::Gip;
            out.name = "Xbox Wireless Controller";
            if (!detectXboxByDescriptor(dev, out)) {
                out.interfaceNum = 0;
                out.altSetting = 1;
                out.epIn = 0x82;
                out.epOut = 0x02;
            }
            return true;
        }
    }

    // 4. PDP Victrix Pro BFG Xbox
    if (vid == PDP_VID && pid == VICTRIX_PRO_BFG_PID) {
        out.protocol = UsbPadProtocol::Gip;
        out.name = "Victrix Pro BFG";
        if (!detectXboxByDescriptor(dev, out)) {
            out.interfaceNum = 0;
            out.altSetting = 1;
            out.epIn = 0x82;
            out.epOut = 0x02;
        }
        return true;
    }

    // 5. Nintendo Switch Pro & Joy-Con Grip
    if (vid == SWITCH_PRO_VID && isSwitchProPid(pid)) {
        out.protocol = UsbPadProtocol::SwitchPro;
        out.name = "Nintendo Switch Pro Controller";
        if (!detectGenericHidEndpoints(dev, out)) {
            out.interfaceNum = 0;
            out.epIn = 0x81;
            out.epOut = 0x01;
        }
        out.protocol = UsbPadProtocol::SwitchPro;
        return true;
    }

    // 6. Switch Input-Only (PDP Rock Candy)
    if (vid == PDP_VID && pid == SWITCH_ROCKCANDY_PID) {
        out.protocol = UsbPadProtocol::SwitchInputOnly;
        out.interfaceNum = 0;
        out.epIn = 0x81;
        out.epOut = 0;
        out.name = "Switch Input-Only Controller";
        return true;
    }

    // 7. Google Stadia
    if (vid == GOOGLE_VID && pid == STADIA_PID) {
        out.protocol = UsbPadProtocol::Stadia;
        out.interfaceNum = 0;
        out.epIn = 0x81;
        out.epOut = 0x01;
        out.name = "Google Stadia Controller";
        return true;
    }

    // 8. Descriptor-based detection (XInput 8BitDo/clones and GIP pads)
    if (detectXboxByDescriptor(dev, out)) {
        out.name = (out.protocol == UsbPadProtocol::Gip) ? "GIP Controller" : "XInput Controller";
        return true;
    }

    // 9. Generic HID fallback
    if (detectGenericHidEndpoints(dev, out)) {
        out.name = "Generic USB Gamepad";
        return true;
    }

    return false;
}

struct Device {
    libusb_device_handle* handle = nullptr;
    int slot = -1;
    uint8_t bus = 0;
    uint8_t addr = 0;
    uint16_t vid = 0, pid = 0;
    std::string name;
    UsbPadProtocol protocol = UsbPadProtocol::None;
    uint8_t interfaceNum = 0;
    uint8_t altSetting = 0;
    uint8_t epIn = 0;
    uint8_t epOut = 0;
    uint8_t kbdInterfaceNum = 0xFF;
    bool hidConfigured = false;
    GenericHidContext hidCtx{};
    int disconnectErrors = 0;
    std::atomic<uint32_t> pendingRumble{0};
    std::atomic<bool> hasPendingRumble{false};
    std::atomic<bool> stop{false};
    std::atomic<bool> dead{false};
    std::atomic<bool> attached{false};
    std::thread thread;
};

std::mutex g_mutex;
std::vector<std::unique_ptr<Device>> g_devices;
std::thread g_scanner;
std::atomic<bool> g_running{false};

int freeSlot() {
    for (int s = 0; s < kMaxUsbPads; ++s) {
        bool used = false;
        for (const auto& d : g_devices) used |= d->slot == s;
        if (!used) return s;
    }
    return -1;
}

void sendInitHandshake(Device& d) {
    if (!d.handle) return;

    if (d.protocol == UsbPadProtocol::XInput) {
        uint8_t dummy[20] = {};
        if (p_sceUsbdControlTransfer) {
            p_sceUsbdControlTransfer(d.handle, 0xC1, 0x01, 0x0100, d.interfaceNum, dummy, sizeof(dummy), 100);
        }
        if (d.epOut && p_sceUsbdInterruptTransfer) {
            uint8_t ledPkt[3] = {0x01, 0x03, 0x02}; // Player 1 LED
            int transferred = 0;
            p_sceUsbdInterruptTransfer(d.handle, d.epOut, ledPkt, sizeof(ledPkt), &transferred, 100);
        }
        XC_LOGI("usb pad %d: XInput wake-up sent", d.slot);
    } else if (d.protocol == UsbPadProtocol::Gip) {
        static uint8_t s_seq = 1;
        auto sendPacket = [&](const uint8_t* p, size_t sz) {
            if (!d.epOut || !p_sceUsbdInterruptTransfer) return;
            int transferred = 0;
            p_sceUsbdInterruptTransfer(d.handle, d.epOut, const_cast<uint8_t*>(p), static_cast<int32_t>(sz), &transferred, 100);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        };
        const uint8_t powerOn[] = {0x05, 0x20, s_seq++, 0x01, 0x00};
        const uint8_t sInit[]   = {0x05, 0x20, s_seq++, 0x0F, 0x06};
        const uint8_t horiInit[] = {0x01, 0x20, s_seq++, 0x09, 0x00, 0x04, 0x20, 0x3a, 0x00, 0x00, 0x00, 0x80, 0x00};
        const uint8_t pdpInit1[] = {0x0a, 0x20, s_seq++, 0x03, 0x00, 0x01, 0x14};
        const uint8_t pdpInit2[] = {0x06, 0x20, s_seq++, 0x02, 0x01, 0x00};
        sendPacket(powerOn, sizeof(powerOn));
        sendPacket(sInit, sizeof(sInit));
        sendPacket(horiInit, sizeof(horiInit));
        sendPacket(pdpInit1, sizeof(pdpInit1));
        sendPacket(pdpInit2, sizeof(pdpInit2));
        XC_LOGI("usb pad %d: GIP initialization handshake sent", d.slot);
    } else if (d.protocol == UsbPadProtocol::DualShock3) {
        uint8_t magic[5] = {0xF4, 0x42, 0x03, 0x00, 0x00};
        if (p_sceUsbdControlTransfer) {
            p_sceUsbdControlTransfer(d.handle, 0x21, 0x09, 0x03F4, d.interfaceNum, magic, sizeof(magic), 1000);
            uint8_t dummy[64] = {};
            p_sceUsbdControlTransfer(d.handle, 0xA1, 0x01, 0x03F2, d.interfaceNum, dummy, 17, 100);
            p_sceUsbdControlTransfer(d.handle, 0xA1, 0x01, 0x03F5, d.interfaceNum, dummy, 8, 100);
        }
        XC_LOGI("usb pad %d: DualShock 3 operational handshake sent", d.slot);
    } else if (d.protocol == UsbPadProtocol::DualSense) {
        if (p_sceUsbdControlTransfer) {
            uint8_t desc[512] = {};
            p_sceUsbdControlTransfer(d.handle, 0x81, 0x06, 0x2200, d.interfaceNum, desc, sizeof(desc), 200);
            uint8_t pairBuf[20] = {};
            p_sceUsbdControlTransfer(d.handle, 0xA1, 0x01, 0x0309, d.interfaceNum, pairBuf, sizeof(pairBuf), 200);
            uint8_t fwBuf[64] = {};
            p_sceUsbdControlTransfer(d.handle, 0xA1, 0x01, 0x0320, d.interfaceNum, fwBuf, sizeof(fwBuf), 200);
            uint8_t calBuf[41] = {};
            p_sceUsbdControlTransfer(d.handle, 0xA1, 0x01, 0x0305, d.interfaceNum, calBuf, sizeof(calBuf), 200);
        }
        if (p_sceUsbdClearHalt) {
            p_sceUsbdClearHalt(d.handle, d.epIn);
            if (d.epOut) p_sceUsbdClearHalt(d.handle, d.epOut);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
        if (d.epOut && p_sceUsbdInterruptTransfer) {
            uint8_t resetPkt[64] = {};
            resetPkt[0] = 0x02;
            resetPkt[39] = 0x02;
            resetPkt[42] = 0x02;
            int transferred = 0;
            p_sceUsbdInterruptTransfer(d.handle, d.epOut, resetPkt, sizeof(resetPkt), &transferred, 150);
            std::this_thread::sleep_for(std::chrono::milliseconds(15));
            uint8_t ledPkt[64] = {};
            ledPkt[0] = 0x02;
            ledPkt[2] = 0x04 | 0x10;
            ledPkt[44] = 0x04; // Player 1 LED
            ledPkt[47] = 128;  // Lightbar blue
            p_sceUsbdInterruptTransfer(d.handle, d.epOut, ledPkt, sizeof(ledPkt), &transferred, 150);
        }
        XC_LOGI("usb pad %d: DualSense USB handshake sent", d.slot);
    } else if (d.protocol == UsbPadProtocol::SwitchPro) {
        if (d.epIn && d.epOut && p_sceUsbdInterruptTransfer) {
            uint8_t rx[64];
            int transferred = 0;
            for (int i = 0; i < 3; ++i) {
                p_sceUsbdInterruptTransfer(d.handle, d.epIn, rx, sizeof(rx), &transferred, 10);
            }
            uint8_t pkt[64] = {};
            pkt[0] = 0x80; pkt[1] = 0x02;
            p_sceUsbdInterruptTransfer(d.handle, d.epOut, pkt, sizeof(pkt), &transferred, 50);
            pkt[1] = 0x04;
            p_sceUsbdInterruptTransfer(d.handle, d.epOut, pkt, sizeof(pkt), &transferred, 50);
            // Subcommand 0x03 (Set report mode to 0x30 Full Mode)
            std::memset(pkt, 0, sizeof(pkt));
            pkt[0] = 0x01; pkt[1] = 0x00; pkt[10] = 0x03; pkt[11] = 0x30;
            p_sceUsbdInterruptTransfer(d.handle, d.epOut, pkt, sizeof(pkt), &transferred, 100);
            // Subcommand 0x40 (Enable IMU)
            pkt[10] = 0x40; pkt[11] = 0x01;
            p_sceUsbdInterruptTransfer(d.handle, d.epOut, pkt, sizeof(pkt), &transferred, 100);
            // Subcommand 0x30 (Player 1 LED)
            pkt[10] = 0x30; pkt[11] = 0x01;
            p_sceUsbdInterruptTransfer(d.handle, d.epOut, pkt, sizeof(pkt), &transferred, 100);
        }
        XC_LOGI("usb pad %d: Switch Pro wake-up sent", d.slot);
    } else if (d.protocol == UsbPadProtocol::GenericHid) {
        if (p_sceUsbdControlTransfer) {
            uint8_t reportDesc[512] = {};
            int r = p_sceUsbdControlTransfer(d.handle, 0x81, 0x06, 0x2200, d.interfaceNum, reportDesc, sizeof(reportDesc), 200);
            if (r > 0 && parseHidDescriptor(d.hidCtx, reportDesc, static_cast<size_t>(r), d.vid, d.pid)) {
                d.hidConfigured = true;
                XC_LOGI("usb pad %d: Generic HID descriptor parsed (%d bytes)", d.slot, r);
            }
        }
    }
}

void sendDeviceRumble(Device& d, uint8_t large, uint8_t small) {
    if (!d.handle) return;
    if (d.protocol == UsbPadProtocol::XInput) {
        if (d.epOut && p_sceUsbdInterruptTransfer) {
            uint8_t pkt[8] = {0x00, 0x08, 0x00, large, small, 0x00, 0x00, 0x00};
            int transferred = 0;
            p_sceUsbdInterruptTransfer(d.handle, d.epOut, pkt, sizeof(pkt), &transferred, 50);
        }
    } else if (d.protocol == UsbPadProtocol::Gip) {
        if (d.epOut && p_sceUsbdInterruptTransfer) {
            static uint8_t s_rumbleSeq = 1;
            uint8_t pkt[13] = {
                0x09, 0x00, s_rumbleSeq++, 0x09, 0x00,
                0x0F,
                0x00, 0x00,
                large, small,
                0xFF, 0x00, 0xFF
            };
            int transferred = 0;
            p_sceUsbdInterruptTransfer(d.handle, d.epOut, pkt, sizeof(pkt), &transferred, 50);
        }
    } else if (d.protocol == UsbPadProtocol::DualShock4) {
        if (d.epOut && p_sceUsbdInterruptTransfer) {
            uint8_t pkt[32] = {};
            pkt[0] = 0x05;
            pkt[1] = 0xFF;
            pkt[2] = 0x04;
            pkt[4] = small;
            pkt[5] = large;
            int transferred = 0;
            p_sceUsbdInterruptTransfer(d.handle, d.epOut, pkt, sizeof(pkt), &transferred, 50);
        }
    } else if (d.protocol == UsbPadProtocol::DualSense) {
        if (d.epOut && p_sceUsbdInterruptTransfer) {
            uint8_t pkt[48] = {};
            pkt[0] = 0x02;
            pkt[1] = 0x03;
            pkt[2] = 0x14;
            pkt[3] = small;
            pkt[4] = large;
            int transferred = 0;
            p_sceUsbdInterruptTransfer(d.handle, d.epOut, pkt, sizeof(pkt), &transferred, 50);
        }
    } else if (d.protocol == UsbPadProtocol::DualShock3) {
        if (p_sceUsbdControlTransfer) {
            uint8_t pkt[49] = {};
            pkt[0] = 0x01;
            pkt[1] = 0x00;
            pkt[2] = small ? 0x01 : 0x00;
            pkt[3] = 0x00;
            pkt[4] = large;
            p_sceUsbdControlTransfer(d.handle, 0x21, 0x09, 0x0201, d.interfaceNum, pkt, sizeof(pkt), 50);
        }
    } else if (d.protocol == UsbPadProtocol::SwitchPro) {
        if (d.epOut && p_sceUsbdInterruptTransfer) {
            static uint8_t s_proSeq = 0;
            uint8_t pkt[49] = {};
            pkt[0] = 0x10;
            pkt[1] = (s_proSeq++) & 0x0F;
            if (large > 0 || small > 0) {
                pkt[2] = 0x28; pkt[3] = 0x88; pkt[4] = 0x60; pkt[5] = 0x61;
                pkt[6] = 0x28; pkt[7] = 0x88; pkt[8] = 0x60; pkt[9] = 0x61;
            } else {
                pkt[2] = 0x00; pkt[3] = 0x01; pkt[4] = 0x40; pkt[5] = 0x40;
                pkt[6] = 0x00; pkt[7] = 0x01; pkt[8] = 0x40; pkt[9] = 0x40;
            }
            int transferred = 0;
            p_sceUsbdInterruptTransfer(d.handle, d.epOut, pkt, sizeof(pkt), &transferred, 50);
        }
    }
}

void readLoop(Device* d) {
    std::string tname = "ps4_usb_" + std::to_string(d->slot);
    platform::setThreadAffinity(platform::ThreadRole::Worker, tname.c_str());
    XC_LOGI("usb pad %d: read thread started for %s (%04x:%04x, %s)", d->slot, d->name.c_str(), d->vid, d->pid,
            usbPadProtocolName(d->protocol));
    usbPadAttached(d->slot, d->name);
    d->attached = true;
    sendInitHandshake(*d);

    auto checkRumble = [&]() {
        if (d->hasPendingRumble.exchange(false, std::memory_order_acq_rel)) {
            uint32_t val = d->pendingRumble.load(std::memory_order_relaxed);
            uint8_t large = static_cast<uint8_t>(val >> 8);
            uint8_t small = static_cast<uint8_t>(val & 0xFF);
            sendDeviceRumble(*d, large, small);
        }
    };

    ControllerState state;
    uint8_t buf[256];
    int errors = 0;
    while (!d->stop) {
        checkRumble();
        int transferred = 0;
        int rc = p_sceUsbdInterruptTransfer(d->handle, d->epIn, buf, sizeof(buf), &transferred, 20);
        checkRumble();
        bool isTimeout = (rc == -7 || rc == static_cast<int>(0x80240007));
        if (rc != 0 || transferred <= 0) {
            if (rc != 0 && !isTimeout) {
                if (++errors >= 50) {
                    XC_LOGW("usb pad %d: %d transfer errors (0x%08x): closed", d->slot, errors, static_cast<unsigned>(rc));
                    break;
                }
            } else {
                errors = 0;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }
        errors = 0;

        // GIP ACK responses using standard gipAcksFor
        if (d->protocol == UsbPadProtocol::Gip && d->epOut && p_sceUsbdInterruptTransfer) {
            for (const auto& ack : gipAcksFor(buf, static_cast<size_t>(transferred))) {
                int sent = 0;
                p_sceUsbdInterruptTransfer(d->handle, d->epOut, const_cast<uint8_t*>(ack.data()), static_cast<int32_t>(ack.size()), &sent, 40);
            }
        }

        bool ok = false;
        if (d->protocol == UsbPadProtocol::GenericHid) {
            if (d->hidConfigured) {
                ok = parseGenericHidReport(d->hidCtx, buf, static_cast<size_t>(transferred), state);
            } else {
                ok = parseGenericHidFallback(buf, static_cast<size_t>(transferred), state);
            }
        } else {
            ok = parseUsbPadReport(d->protocol, buf, static_cast<size_t>(transferred), state);
        }
        if (ok) {
            usbPadState(d->slot, state);
        }
    }

    sendDeviceRumble(*d, 0, 0);
    d->dead = true;
    XC_LOGI("usb pad %d: read thread finished", d->slot);
}

void closeDevice(Device& d) {
    d.stop = true;
    if (d.thread.joinable()) d.thread.join();
    if (d.attached) usbPadDetached(d.slot);
    if (d.handle) {
        if (d.kbdInterfaceNum != 0xFF && p_sceUsbdReleaseInterface) {
            p_sceUsbdReleaseInterface(d.handle, d.kbdInterfaceNum);
        }
        if (p_sceUsbdReleaseInterface) {
            p_sceUsbdReleaseInterface(d.handle, d.interfaceNum);
        }
        if (p_sceUsbdClose) {
            p_sceUsbdClose(d.handle);
        }
        d.handle = nullptr;
    }
}

void scan() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!usbPadsEnabled()) {
        for (auto& d : g_devices) closeDevice(*d);
        g_devices.clear();
        return;
    }

    // 1. Remove disconnected / dead devices
    for (auto it = g_devices.begin(); it != g_devices.end();) {
        Device& d = **it;
        bool disconnected = d.dead;
        if (!disconnected && d.handle && p_sceUsbdCheckConnected) {
            if (p_sceUsbdCheckConnected(d.handle) != 0) {
                if (++d.disconnectErrors >= 3) disconnected = true;
            } else {
                d.disconnectErrors = 0;
            }
        }
        if (disconnected) {
            XC_LOGI("usb pad %d: disconnecting %s", d.slot, d.name.c_str());
            closeDevice(d);
            it = g_devices.erase(it);
        } else {
            ++it;
        }
    }

    // 2. Discover new devices
    libusb_device** list = nullptr;
    int32_t count = p_sceUsbdGetDeviceList(&list);
    if (count <= 0 || !list) return;

    for (int i = 0; i < count; ++i) {
        libusb_device* dev = list[i];
        if (!dev) continue;

        uint8_t bus = p_sceUsbdGetBusNumber ? p_sceUsbdGetBusNumber(dev) : 0;
        uint8_t addr = p_sceUsbdGetDeviceAddress ? p_sceUsbdGetDeviceAddress(dev) : 0;

        struct libusb_device_descriptor desc{};
        if (p_sceUsbdGetDeviceDescriptor(dev, &desc) != 0) continue;

        // Skip if already managed (by bus/address or VID/PID/interface if bus/addr unavailable)
        bool alreadyOpen = false;
        for (const auto& d : g_devices) {
            if (bus != 0 && addr != 0) {
                if (d->bus == bus && d->addr == addr) {
                    alreadyOpen = true;
                    break;
                }
            } else if (d->vid == desc.idVendor && d->pid == desc.idProduct) {
                alreadyOpen = true;
                break;
            }
        }
        if (alreadyOpen) continue;

        DetectedPad det;
        if (!identifyController(dev, desc, det)) continue;

        int slot = freeSlot();
        if (slot < 0) {
            XC_LOGW("usb pad: all %d slots taken, cannot attach %s", kMaxUsbPads, det.name.c_str());
            continue;
        }

        libusb_device_handle* h = nullptr;
        if (p_sceUsbdOpen(dev, &h) != 0 || !h) {
            XC_LOGW("usb pad: sceUsbdOpen failed for %s (%04x:%04x)", det.name.c_str(), desc.idVendor, desc.idProduct);
            continue;
        }

        // Detach secondary composite keyboard if present (prevents OS input hijack)
        if (det.kbdInterfaceNum != 0xFF) {
            if (p_sceUsbdDetachKernelDriver) p_sceUsbdDetachKernelDriver(h, det.kbdInterfaceNum);
            if (p_sceUsbdClaimInterface) p_sceUsbdClaimInterface(h, det.kbdInterfaceNum);
            XC_LOGI("usb pad: detached secondary composite keyboard interface %d", det.kbdInterfaceNum);
        }

        // Set alt setting if needed (e.g. Xbox One GIP alt 1)
        if (det.altSetting > 0 && p_sceUsbdSetInterfaceAltSetting) {
            p_sceUsbdSetInterfaceAltSetting(h, det.interfaceNum, det.altSetting);
        }

        if (p_sceUsbdDetachKernelDriver) p_sceUsbdDetachKernelDriver(h, det.interfaceNum);
        if (p_sceUsbdClaimInterface && p_sceUsbdClaimInterface(h, det.interfaceNum) != 0) {
            XC_LOGW("usb pad: claim interface %d failed for %s", det.interfaceNum, det.name.c_str());
            p_sceUsbdClose(h);
            continue;
        }

        auto d = std::make_unique<Device>();
        d->handle = h;
        d->slot = slot;
        d->bus = bus;
        d->addr = addr;
        d->vid = desc.idVendor;
        d->pid = desc.idProduct;
        d->name = det.name;
        d->protocol = det.protocol;
        d->interfaceNum = det.interfaceNum;
        d->altSetting = det.altSetting;
        d->epIn = det.epIn;
        d->epOut = det.epOut;
        d->kbdInterfaceNum = det.kbdInterfaceNum;
        d->thread = std::thread(readLoop, d.get());

        g_devices.push_back(std::move(d));
    }

    p_sceUsbdFreeDeviceList(list);
}

void scanLoop() {
    platform::setThreadAffinity(platform::ThreadRole::Worker, "ps4_usb_scan");
    XC_LOGI("usb pad: starting PS4 USB scan loop");
    while (g_running) {
        scan();
        for (int i = 0; i < 15 && g_running; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    for (auto& d : g_devices) closeDevice(*d);
    g_devices.clear();
    XC_LOGI("usb pad: PS4 USB scan loop terminated");
}

}  // namespace

void setUsbPadSlotRumble(int slot, uint8_t large, uint8_t small) {
    std::lock_guard<std::mutex> lock(g_mutex);
    for (auto& d : g_devices) {
        if (d->slot == slot) {
            uint32_t val = (static_cast<uint32_t>(large) << 8) | small;
            d->pendingRumble.store(val, std::memory_order_relaxed);
            d->hasPendingRumble.store(true, std::memory_order_release);
            break;
        }
    }
}

void usbPadsStart() {
    if (!usbPadsEnabled()) {
        XC_LOGI("usb pad: USB controllers driver disabled in settings");
        return;
    }
    if (g_running.exchange(true)) return;
    if (!loadUsbd()) {
        g_running = false;
        return;
    }
    XC_LOGI("usb pad: looking for controllers on PS4 USB (libSceUsbd)");
    g_scanner = std::thread(scanLoop);
}

void usbPadsStop() {
    if (!g_running.exchange(false)) return;
    if (g_scanner.joinable()) g_scanner.join();
    if (p_sceUsbdExit) {
        p_sceUsbdExit();
    }
    if (g_hUsbd > 0) {
        int res = 0;
        sceKernelStopUnloadModule(g_hUsbd, 0, nullptr, 0, nullptr, &res);
        g_hUsbd = 0;
    }
}

}  // namespace xc::input
