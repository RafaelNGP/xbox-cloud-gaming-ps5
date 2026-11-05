// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// USB pads on the PS5 (pad_usb.h). The console has no driver for an Xbox
// or XInput pad: the FreeBSD kernel leaves it to ugen, whose control node
// (/dev/ugenB.A) is open to every process. Each pad gets a thread that
// keeps one interrupt IN transfer queued through the USB_FS ioctls, as
// OmniPad CE does (usb_hotplug.c, github.com/jpemendonca/OmniPad-CE,
// GPL-3.0); the wake-up packets each kind of pad wants come from there too.
// A thread looks for pads plugged in and unplugged every 1.5 s.
#include "input/pad_usb.h"
#include "util/log.h"

#include <dev/usb/usb_ioctl.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace xc::input {

namespace {

constexpr int kUsbErrNormal = 0;      // USB_ERR_NORMAL_COMPLETION (dev/usb/usbdi.h)
constexpr int kUsbErrShortXfer = 21;  // USB_ERR_SHORT_XFER
constexpr uint16_t kShortXferOk = 0x0004;  // USB_SHORT_XFER_OK (usbdi.h): a reply shorter than asked is fine
constexpr uint16_t kMediaTekVid = 0x0e8d;  // the console's own Bluetooth radio: never touched
constexpr int kInIndex = 0, kOutIndex = 1;

struct Device {
    std::string path;
    int fd = -1;
    int slot = -1;
    uint16_t vid = 0, pid = 0;
    std::string name;
    UsbPadInterface iface;
    bool hasOut = false;
    usb_fs_endpoint eps[2]{};
    void* frames[2]{};
    uint32_t lengths[2]{};
    uint8_t rx[64]{}, tx[64]{};
    bool inPending = false, inDone = false, clearStall = false;
    std::atomic<bool> stop{false}, dead{false}, attached{false}, failed{false};
    std::thread thread;
};

std::mutex g_mutex;  // g_devices, g_ignored
std::vector<std::unique_ptr<Device>> g_devices;
std::set<std::pair<std::string, ino_t>> g_ignored;  // not a pad, or it failed: until it is plugged again
std::thread g_scanner;
std::atomic<bool> g_running{false};

const char* protocolName(UsbPadProtocol p) {
    return usbPadProtocolName(p);
}

std::string hex(const uint8_t* p, int n) {
    std::string s;
    char b[4];
    for (int i = 0; i < n && i < 24; ++i) std::snprintf(b, sizeof b, "%02x ", p[i]), s += b;
    return s;
}

int controlRequest(int fd, uint8_t type, uint8_t request, uint16_t value, uint16_t index, void* data, uint16_t len) {
    usb_ctl_request r{};
    r.ucr_data = data;
    r.ucr_flags = kShortXferOk;
    r.ucr_request.bmRequestType = type;
    r.ucr_request.bRequest = request;
    USETW(r.ucr_request.wValue, value);
    USETW(r.ucr_request.wIndex, index);
    USETW(r.ucr_request.wLength, len);
    return ioctl(fd, USB_DO_REQUEST, &r) == 0 ? 0 : errno;
}

// The product's name from its string descriptor (UTF-16, ASCII kept).
std::string productName(int fd, uint8_t index) {
    if (!index) return {};
    uint8_t raw[256] = {};
    usb_gen_descriptor gd{};
    gd.ugd_data = raw;
    gd.ugd_maxlen = sizeof raw;
    gd.ugd_string_index = index;
    gd.ugd_lang_id = 0x0409;
    if (ioctl(fd, USB_GET_STRING_DESC, &gd) != 0) return {};
    std::string s;
    const size_t n = std::min<size_t>({gd.ugd_actlen, raw[0], sizeof raw});
    for (size_t i = 2; i + 1 < n; i += 2)
        if (raw[i + 1] == 0 && raw[i] >= 0x20 && raw[i] < 0x7f) s += static_cast<char>(raw[i]);
    return s;
}

// A device that is no pad of ours: its interfaces in the log (class,
// subclass, protocol and endpoints) and, for a HID one that is no boot
// keyboard / mouse, its report descriptor (a read-only request), to tell
// what another pad needs.
void logInterfaces(int fd, const std::string& path, const uint8_t* d, size_t len) {
    std::string line;
    for (size_t off = 0; off + 2 <= len; off += d[off]) {
        const size_t l = d[off];
        if (l < 2 || off + l > len) break;
        char b[64];
        if (d[off + 1] == 0x04 && l >= 9) {
            std::snprintf(b, sizeof b, " | if %u alt %u: %02x/%02x/%02x", d[off + 2], d[off + 3], d[off + 5], d[off + 6],
                          d[off + 7]);
            line += b;
        } else if (d[off + 1] == 0x05 && l >= 7) {
            std::snprintf(b, sizeof b, " ep %02x/%u", d[off + 2], d[off + 3] & 3);
            line += b;
        }
    }
    XC_LOGI("usb pad: %s: interfaces%s", path.c_str(), line.c_str());
    uint8_t iface = 0, sub = 0xff;
    bool hid = false;
    for (size_t off = 0; off + 2 <= len; off += d[off]) {
        const size_t l = d[off];
        if (l < 2 || off + l > len) break;
        if (d[off + 1] == 0x04 && l >= 9) iface = d[off + 2], hid = d[off + 5] == 0x03, sub = d[off + 6];
        if (d[off + 1] != 0x21 || l < 9 || !hid || sub != 0x00) continue;  // a HID descriptor, no boot device
        const uint16_t size = static_cast<uint16_t>(d[off + 7] | (d[off + 8] << 8));
        uint8_t report[512] = {};
        const uint16_t want = std::min<uint16_t>(size, sizeof report);
        int err = controlRequest(fd, 0x81, 0x06, 0x2200, iface, report, want);
        if (err) {
            XC_LOGI("usb pad: %s: if %u report descriptor: errno %d", path.c_str(), iface, err);
            continue;
        }
        for (int at = 0; at < want; at += 24)
            XC_LOGI("usb pad: %s: if %u report descriptor @%d: %s", path.c_str(), iface, at,
                    hex(report + at, std::min(24, want - at)).c_str());
    }
}

bool setupTransfers(Device& d) {
    int config = 0;
    if (ioctl(d.fd, USB_GET_CONFIG, &config) == 0 && (config & 0xff) == 0xff) {
        config = 0;
        if (ioctl(d.fd, USB_SET_CONFIG, &config) != 0) XC_LOGW("usb pad: %s: set config: errno %d", d.path.c_str(), errno);
    }
    usb_fs_init init{};
    init.pEndpoints = d.eps;
    init.ep_index_max = 2;
    if (ioctl(d.fd, USB_FS_INIT, &init) != 0) {
        XC_LOGW("usb pad: %s: USB_FS_INIT: errno %d (%s)", d.path.c_str(), errno, std::strerror(errno));
        return false;
    }
    usb_fs_open op{};
    op.max_bufsize = sizeof d.rx;
    op.max_frames = 1;
    op.ep_index = kInIndex;
    op.ep_no = d.iface.epIn;
    if (ioctl(d.fd, USB_FS_OPEN, &op) != 0) {
        int err = errno;
        char driver[64] = {};
        usb_gen_descriptor gd{};
        gd.ugd_data = driver;
        gd.ugd_maxlen = sizeof driver - 1;
        gd.ugd_iface_index = d.iface.number;
        bool claimed = ioctl(d.fd, USB_GET_IFACE_DRIVER, &gd) == 0;
        XC_LOGW("usb pad: %s: opening IN 0x%02x: errno %d (%s)%s%s", d.path.c_str(), d.iface.epIn, err, std::strerror(err),
                claimed ? ", interface held by the driver " : "", claimed ? driver : "");
        return false;
    }
    if (d.iface.epOut) {
        op = {};
        op.max_bufsize = sizeof d.tx;
        op.max_frames = 1;
        op.ep_index = kOutIndex;
        op.ep_no = d.iface.epOut;
        d.hasOut = ioctl(d.fd, USB_FS_OPEN, &op) == 0;
        if (!d.hasOut) XC_LOGW("usb pad: %s: opening OUT 0x%02x: errno %d", d.path.c_str(), d.iface.epOut, errno);
    }
    for (int i : {kInIndex, kOutIndex}) {
        d.frames[i] = i == kInIndex ? d.rx : d.tx;
        d.lengths[i] = sizeof d.rx;
        d.eps[i].ppBuffer = &d.frames[i];
        d.eps[i].pLength = &d.lengths[i];
        d.eps[i].nFrames = 1;
        d.eps[i].flags = USB_FS_FLAG_SINGLE_SHORT_OK;
    }
    return true;
}

// One report off the IN endpoint: its size, 0 when none came within
// `waitMs`, -1 on an error. The transfer stays queued between calls (no
// kernel timeout): a pad that reports only on change (the 8BitDo dongle)
// would lose a press landing between a timed-out transfer and the next.
int receive(Device& d, uint8_t* out, int waitMs) {
    usb_fs_endpoint& ep = d.eps[kInIndex];
    if (!d.inPending) {
        d.lengths[kInIndex] = sizeof d.rx;
        ep.timeout = 0;
        ep.flags = USB_FS_FLAG_SINGLE_SHORT_OK | (d.clearStall ? USB_FS_FLAG_CLEAR_STALL : 0);
        d.clearStall = false;
        usb_fs_start start{};
        start.ep_index = kInIndex;
        if (ioctl(d.fd, USB_FS_START, &start) != 0 && errno != EBUSY) return -1;
        d.inPending = true;
    }
    for (int spins = waitMs / 4 + 1; spins > 0; --spins) {
        bool done = d.inDone;
        if (!done) {
            usb_fs_complete complete{};
            if (ioctl(d.fd, USB_FS_COMPLETE, &complete) == 0) {
                if (complete.ep_index != kInIndex) continue;  // an OUT transfer that ended late
                done = true;
            } else if (errno != EBUSY && errno != EAGAIN && errno != ETIMEDOUT) {
                return -1;
            }
        }
        if (done) {
            d.inDone = d.inPending = false;
            if (ep.status != kUsbErrNormal && ep.status != kUsbErrShortXfer) {
                d.clearStall = true;
                return -1;
            }
            const int n = static_cast<int>(std::min<uint32_t>(d.lengths[kInIndex], sizeof d.rx));
            std::memcpy(out, d.rx, static_cast<size_t>(n));
            return n;
        }
        fd_set rset;
        FD_ZERO(&rset);
        FD_SET(d.fd, &rset);
        timeval wait{0, 4000};
        select(d.fd + 1, &rset, nullptr, nullptr, &wait);
    }
    return 0;
}

// One packet out the OUT endpoint; false when it was not sent.
bool send(Device& d, const uint8_t* data, size_t len, int timeoutMs = 80) {
    if (!d.hasOut) return false;
    usb_fs_endpoint& ep = d.eps[kOutIndex];
    len = std::min(len, sizeof d.tx);
    std::memcpy(d.tx, data, len);
    d.lengths[kOutIndex] = static_cast<uint32_t>(len);
    ep.timeout = static_cast<uint16_t>(timeoutMs);
    usb_fs_start start{};
    start.ep_index = kOutIndex;
    if (ioctl(d.fd, USB_FS_START, &start) != 0 && errno != EBUSY) return false;
    for (int spins = timeoutMs / 4 + 1; spins > 0; --spins) {
        usb_fs_complete complete{};
        if (ioctl(d.fd, USB_FS_COMPLETE, &complete) == 0) {
            if (complete.ep_index == kInIndex) {  // the queued IN transfer: the next receive() takes it
                d.inDone = true;
                continue;
            }
            return ep.status == kUsbErrNormal || ep.status == kUsbErrShortXfer;
        }
        if (errno != EBUSY && errno != EAGAIN) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(4));
    }
    return false;
}

// What each kind of pad waits for before it reports.
void wakeUp(Device& d) {
    if (d.iface.protocol == UsbPadProtocol::Gip) {
        // Power on, the "S" init and the Xbox button's light.
        static const uint8_t powerOn[] = {0x05, 0x20, 0x00, 0x01, 0x00};
        static const uint8_t sInit[] = {0x05, 0x20, 0x00, 0x0f, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00,
                                        0x00, 0x55, 0x53, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
        static const uint8_t ledOn[] = {0x0a, 0x20, 0x00, 0x03, 0x00, 0x01, 0x14};
        bool a = send(d, powerOn, sizeof powerOn);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        bool b = send(d, sInit, sizeof sInit);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        bool c = send(d, ledOn, sizeof ledOn);
        XC_LOGI("usb pad %d: GIP power on %s, init %s, light %s", d.slot, a ? "ok" : "failed", b ? "ok" : "failed",
                c ? "ok" : "failed");
    } else {
        // The player 1 light, which XInput hosts set as a pad connects: the
        // 8BitDo Ultimate dongle stays silent without it and drops off USB
        // after ~10 s.
        static const uint8_t led[] = {0x01, 0x03, 0x02};
        bool ok = send(d, led, sizeof led);
        XC_LOGI("usb pad %d: XInput light %s", d.slot, ok ? "sent" : "not sent");
    }
}

unsigned buttonBits(const ControllerState& s) {
    const bool b[] = {s.btnA, s.btnB, s.btnX, s.btnY, s.dpadUp, s.dpadDown, s.dpadLeft, s.dpadRight,
                      s.btnL1, s.btnR1, s.btnL3, s.btnR3, s.btnOptions, s.btnTouchpad, s.btnNexus,
                      s.triggerL2 > 0.5f, s.triggerR2 > 0.5f};
    unsigned bits = 0;
    for (size_t i = 0; i < sizeof b; ++i) bits |= b[i] ? 1u << i : 0;
    return bits;
}

std::string buttonNames(unsigned bits) {
    static const char* names[] = {"A", "B", "X", "Y", "Up", "Down", "Left", "Right", "LB",
                                  "RB", "LS", "RS", "Menu", "View", "Xbox", "LT", "RT"};
    std::string s;
    for (size_t i = 0; i < sizeof names / sizeof *names; ++i)
        if (bits & (1u << i)) s += s.empty() ? names[i] : std::string(" ") + names[i];
    return s.empty() ? "none" : s;
}

void readLoop(Device* d) {
    // The XInput wake-up through the control endpoint some 2.4G dongles
    // want (OmniPad CE's handshake), before the transfers start.
    if (d->iface.protocol == UsbPadProtocol::XInput) {
        uint8_t buf[32];
        int r1 = controlRequest(d->fd, 0xc1, 0x01, 0x0100, d->iface.number, buf, 20);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        uint8_t led[] = {0x01, 0x03, 0x02};
        int r2 = controlRequest(d->fd, 0x21, 0x09, 0x0200, d->iface.number, led, sizeof led);
        XC_LOGI("usb pad: %s: XInput wake-up requests: errno %d, %d", d->path.c_str(), r1, r2);
    }
    if (!setupTransfers(*d)) {
        d->failed = true;
        d->dead = true;
        return;
    }
    XC_LOGI("usb pad %d: %s (%04x:%04x, %s, IN 0x%02x, OUT 0x%02x%s) on %s", d->slot, d->name.c_str(), d->vid, d->pid,
            protocolName(d->iface.protocol), d->iface.epIn, d->iface.epOut, d->hasOut ? "" : " not opened",
            d->path.c_str());
    usbPadAttached(d->slot, d->name);
    d->attached = true;
    wakeUp(*d);

    ControllerState state;
    uint8_t buf[64];
    int errors = 0, shown = 0, changesLogged = 0, lightPlayer = -1;
    unsigned buttons = 0;
    auto lastOther = std::chrono::steady_clock::time_point{};
    while (!d->stop) {
        // XInput pads show their player on their four lights (01 03 06..09).
        if (const int player = usbPadPlayer(d->slot);
            d->iface.protocol == UsbPadProtocol::XInput && player >= 0 && player != lightPlayer) {
            const uint8_t led[] = {0x01, 0x03, static_cast<uint8_t>(0x06 + player)};
            send(*d, led, sizeof led, 40);
            lightPlayer = player;
        }
        const int n = receive(*d, buf, 20);
        if (n < 0) {
            if (++errors >= 50) {
                XC_LOGW("usb pad %d: %d errors in a row (errno %d): closed", d->slot, errors, errno);
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(4));
            continue;
        }
        if (n == 0) continue;
        errors = 0;
        if (d->iface.protocol == UsbPadProtocol::Gip)
            for (const auto& ack : gipAcksFor(buf, static_cast<size_t>(n))) send(*d, ack.data(), ack.size(), 40);
        if (shown < 6) XC_LOGI("usb pad %d: report %d: %d bytes: %s", d->slot, ++shown, n, hex(buf, n).c_str());
        if (parseUsbPadReport(d->iface.protocol, buf, static_cast<size_t>(n), state)) {
            usbPadState(d->slot, state);
            // The first presses, to check the buttons from the log.
            const unsigned now = buttonBits(state);
            if (now != buttons && changesLogged < 80) {
                ++changesLogged;
                XC_LOGI("usb pad %d: buttons %s, sticks %.2f,%.2f %.2f,%.2f, triggers %.2f %.2f", d->slot,
                        buttonNames(now).c_str(), state.rawLeftX, state.rawLeftY, state.rawRightX, state.rawRightY,
                        state.triggerL2, state.triggerR2);
            }
            buttons = now;
        } else if (std::chrono::steady_clock::now() - lastOther > std::chrono::seconds(5)) {
            lastOther = std::chrono::steady_clock::now();
            XC_LOGD("usb pad %d: other report, %d bytes: %s", d->slot, n, hex(buf, n).c_str());
        }
    }
    usb_fs_uninit uninit{};
    ioctl(d->fd, USB_FS_UNINIT, &uninit);
    d->dead = true;
}

void closeDevice(Device& d) {
    d.stop = true;
    if (d.thread.joinable()) d.thread.join();
    if (d.attached) usbPadDetached(d.slot);
    if (d.fd >= 0) close(d.fd);
    d.fd = -1;
}

int freeSlot() {
    for (int s = 0; s < kMaxUsbPads; ++s) {
        bool used = false;
        for (const auto& d : g_devices) used |= d->slot == s;
        if (!used) return s;
    }
    return -1;
}

// A device seen for the first time: a pad gets a slot and its thread;
// anything else is left alone until it is plugged in again.
void probe(const std::string& path, ino_t ino) {
    const auto key = std::make_pair(path, ino);
    int fd = open(path.c_str(), O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        XC_LOGW("usb pad: %s: open: errno %d (%s)", path.c_str(), errno, std::strerror(errno));
        g_ignored.insert(key);
        return;
    }
    usb_device_descriptor dd{};
    if (ioctl(fd, USB_GET_DEVICE_DESC, &dd) != 0) {
        XC_LOGW("usb pad: %s: device descriptor: errno %d", path.c_str(), errno);
        close(fd);
        g_ignored.insert(key);
        return;
    }
    const uint16_t vid = UGETW(dd.idVendor), pid = UGETW(dd.idProduct);
    UsbPadInterface iface;
    uint8_t config[1024] = {};
    size_t configLen = 0;
    // Not the console's radios (class E0, or MediaTek's), not a hub.
    const bool other = vid == kMediaTekVid || dd.bDeviceClass == 0x09 || dd.bDeviceClass == 0xe0;
    if (!other) {
        usb_gen_descriptor gd{};
        gd.ugd_data = config;
        gd.ugd_maxlen = sizeof config;
        if (ioctl(fd, USB_GET_FULL_DESC, &gd) == 0) {
            configLen = std::min<size_t>(gd.ugd_actlen, sizeof config);
            iface = findUsbPadInterface(config, configLen);
        }
    }
    if (iface.protocol == UsbPadProtocol::None) {
        XC_LOGI("usb pad: %s: %04x:%04x (class %02x) is not an Xbox / XInput pad", path.c_str(), vid, pid,
                dd.bDeviceClass);
        if (!other && configLen) logInterfaces(fd, path, config, configLen);
        close(fd);
        g_ignored.insert(key);
        return;
    }
    const int slot = freeSlot();
    if (slot < 0) {
        close(fd);  // tried again on the next scan, once a pad leaves
        return;
    }
    auto d = std::make_unique<Device>();
    d->path = path;
    d->fd = fd;
    d->slot = slot;
    d->vid = vid;
    d->pid = pid;
    d->iface = iface;
    d->name = productName(fd, dd.iProduct);
    if (d->name.empty()) d->name = protocolName(iface.protocol) + std::string(" pad");
    d->thread = std::thread(readLoop, d.get());
    g_devices.push_back(std::move(d));
}

void scan() {
    std::lock_guard<std::mutex> lock(g_mutex);
    // Gone (unplugged, or its thread gave up): closed.
    for (auto it = g_devices.begin(); it != g_devices.end();) {
        Device& d = **it;
        struct stat st {};
        if (d.dead || stat(d.path.c_str(), &st) != 0) {
            if (d.failed) {
                if (stat(d.path.c_str(), &st) == 0) g_ignored.insert({d.path, st.st_ino});
                XC_LOGW("usb pad: %s left alone until it is plugged in again", d.path.c_str());
            }
            closeDevice(d);
            it = g_devices.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = g_ignored.begin(); it != g_ignored.end();) {
        struct stat st {};
        if (stat(it->first.c_str(), &st) != 0 || st.st_ino != it->second) it = g_ignored.erase(it);
        else ++it;
    }
    // New ones. Address 1 of each bus is its root hub.
    for (int bus = 0; bus < 8; ++bus) {
        for (int addr = 2; addr < 32; ++addr) {
            char path[32];
            std::snprintf(path, sizeof path, "/dev/ugen%d.%d", bus, addr);
            struct stat st {};
            if (stat(path, &st) != 0) continue;
            bool known = g_ignored.count({path, st.st_ino}) != 0;
            for (const auto& d : g_devices) known |= d->path == path;
            if (!known) probe(path, st.st_ino);
        }
    }
}

void scanLoop() {
    while (g_running) {
        scan();
        for (int i = 0; i < 15 && g_running; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    for (auto& d : g_devices) closeDevice(*d);
    g_devices.clear();
    g_ignored.clear();
}

}  // namespace

void usbPadsStart() {
    if (g_running.exchange(true)) return;
    XC_LOGI("usb pad: looking for Xbox / XInput pads on /dev/ugen*");
    g_scanner = std::thread(scanLoop);
}

void usbPadsStop() {
    if (!g_running.exchange(false)) return;
    if (g_scanner.joinable()) g_scanner.join();
}

}  // namespace xc::input
