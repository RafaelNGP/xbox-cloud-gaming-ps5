// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "display/display.h"
#include "platform/platform.h"
#include "util/log.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

#if defined(XCLOUD_PS5)

extern "C" {
size_t sceKernelGetDirectMemorySize();
int sceKernelAllocateDirectMemory(int64_t search_start, int64_t search_end,
                                  size_t length, size_t alignment, int memory_type,
                                  int64_t *physical_address);
int sceKernelMapDirectMemory(void **address, size_t length, int protection, int flags,
                             int64_t physical_address, size_t alignment);
int sceVideoOutOpen(int32_t user_id, int32_t bus_type, int32_t index, const void *param);
int sceVideoOutClose(int32_t handle);
int sceVideoOutSetFlipRate(int32_t handle, int32_t rate);
void sceVideoOutSetBufferAttribute2(void *attribute, uint64_t pixel_format,
                                    uint32_t tiling_mode, uint32_t width,
                                    uint32_t height, uint64_t option,
                                    uint32_t dcc_control, uint64_t dcc_clear_color);
int sceVideoOutRegisterBuffers2(int32_t handle, int32_t set_index,
                                int32_t buffer_index_start, void *buffers,
                                int32_t buffer_count, void *attribute,
                                int32_t category, void *option);
int sceVideoOutSubmitFlip(int32_t handle, int32_t buffer_index,
                          uint32_t flip_mode, int64_t flip_argument);
int sceVideoOutWaitVblank(int32_t handle);
// 16 64-bit words; word 3 is the argument of the latest flip shown.
int sceVideoOutGetFlipStatus(int32_t handle, uint64_t status[16]);
}

namespace xc::display {

namespace {
constexpr size_t kFrameBytes = 0xA00000;  // 10 MB: 15x9 macro tiles, 2 MB aligned
constexpr int kBuffers = 3;
constexpr size_t kMemoryBytes = kFrameBytes * kBuffers;
constexpr size_t kMemoryAlignment = 0x200000;
constexpr int kMemoryTypeGarlic = 3;
constexpr int kMapProtection = 0x33;
constexpr uint64_t kPixelFormatRGBA8 = UINT64_C(0x8000000022000000);

struct VideoBuffer {
    void* data;
    void* metadata;
    void* res0;
    void* res1;
};

int g_videoHandle = -1;
void* g_mappedMemory = nullptr;
int g_currentBuffer = 0;
// Triple buffering: each flip carries an increasing marker, VideoOut reports
// the marker of the flip on screen. A buffer can be drawn into once a later
// flip than its own has shown; with three, one normally is, so drawing
// never waits for vblank (it did with two, which made a 60 fps stream fall
// behind whenever two frames arrived within one refresh).
int64_t g_flipMarker = 0;
std::array<int64_t, kBuffers> g_bufferMarker{};

// PS5 VideoOut swizzled 64KB macro-tile mapping
constexpr size_t tiledByteOffset(unsigned x, unsigned y) {
    uint32_t offset = ((y << 4) & 0x70U) ^ ((y << 5) & 0xf00U) ^ ((y << 9) & 0x1000U) ^
                      ((y << 8) & 0x4000U) ^ ((x << 2) & 0xcU) ^ ((x << 5) & 0x380U) ^
                      ((x << 4) & 0x400U) ^ ((x << 6) & 0x800U) ^ ((x << 9) & 0xa000U);
    uint32_t blocksPerRow = (kWidth + 127U) >> 7;
    uint32_t blockIndex = (y >> 7) * blocksPerRow + (x >> 7);
    return (static_cast<size_t>(blockIndex) << 16) + offset;
}

// 128x128 pixel macro tiles covering the screen (the last row is partial).
constexpr unsigned kTilesX = (kWidth + 127) / 128;
constexpr unsigned kTilesY = (kHeight + 127) / 128;
constexpr size_t kUsedBytes = static_cast<size_t>(kTilesX) * kTilesY * 0x10000;

void flushRange(void *address, size_t length) {
    auto *at = static_cast<uint8_t *>(address);
    const auto *end = at + length;
    for (; at < end; at += 64)
        __asm__ volatile("clflush (%0)" : : "r"(at) : "memory");
    __asm__ volatile("mfence" ::: "memory");
}
} // namespace

bool init() {
    g_videoHandle = sceVideoOutOpen(0xff, 0, 0, nullptr);
    if (g_videoHandle < 0) {
        XC_LOGE("sceVideoOutOpen failed: %d", g_videoHandle);
        return false;
    }

    size_t poolSize = sceKernelGetDirectMemorySize();
    if (poolSize < kMemoryBytes) {
        XC_LOGE("Insufficient direct memory: %zu", poolSize);
        return false;
    }

    int64_t phys = 0;
    int rc = sceKernelAllocateDirectMemory(0, static_cast<int64_t>(poolSize), kMemoryBytes,
                                           kMemoryAlignment, kMemoryTypeGarlic, &phys);
    if (rc < 0) {
        XC_LOGE("sceKernelAllocateDirectMemory failed: 0x%08x", rc);
        return false;
    }

    rc = sceKernelMapDirectMemory(&g_mappedMemory, kMemoryBytes, kMapProtection, 0, phys, kMemoryAlignment);
    if (rc < 0 || !g_mappedMemory) {
        XC_LOGE("sceKernelMapDirectMemory failed: 0x%08x", rc);
        return false;
    }

    std::array<VideoBuffer, kBuffers> buffers{};
    for (int i = 0; i < kBuffers; ++i)
        buffers[i] = {static_cast<uint8_t*>(g_mappedMemory) + i * kFrameBytes, nullptr, nullptr, nullptr};

    uint8_t attr[80]{};
    sceVideoOutSetFlipRate(g_videoHandle, 0);
    sceVideoOutSetBufferAttribute2(attr, kPixelFormatRGBA8, 0, kWidth, kHeight, 0, 0, 0);

    rc = sceVideoOutRegisterBuffers2(g_videoHandle, 0, 0, buffers.data(), kBuffers, attr, 0, nullptr);
    if (rc < 0) {
        XC_LOGE("sceVideoOutRegisterBuffers2 failed: 0x%08x", rc);
        return false;
    }

    XC_LOGI("PS5 Display initialized (1080p, triple buffered)");
    return true;
}

std::mutex& frameMutex() {
    static std::mutex m;
    return m;
}

void shutdown() {
    if (g_videoHandle >= 0) {
        sceVideoOutClose(g_videoHandle);
        g_videoHandle = -1;
    }
}

namespace {
// Picks the back buffer for the next frame: not on screen, not queued.
// Without `wait`, gives up at once when all are busy.
bool acquireBuffer(bool wait = true) {
    for (int attempt = 0; attempt < 4; ++attempt) {
        uint64_t status[16] = {};
        int64_t shown = g_flipMarker;  // no status: assume everything shown
        if (g_flipMarker && sceVideoOutGetFlipStatus(g_videoHandle, status) == 0)
            shown = static_cast<int64_t>(status[3]);
        int onScreen = -1;
        for (int i = 0; i < kBuffers; ++i)
            if (g_bufferMarker[i] <= shown && (onScreen < 0 || g_bufferMarker[i] > g_bufferMarker[onScreen]))
                onScreen = i;
        int best = -1;
        for (int i = 0; i < kBuffers; ++i) {
            if (i == onScreen || g_bufferMarker[i] > shown) continue;
            if (best < 0 || g_bufferMarker[i] < g_bufferMarker[best]) best = i;
        }
        if (best >= 0) {
            g_currentBuffer = best;
            return true;
        }
        if (!wait) return false;
        sceVideoOutWaitVblank(g_videoHandle);
    }
    // Flip status not moving: fall back to round robin.
    g_currentBuffer = static_cast<int>(g_flipMarker % kBuffers);
    return true;
}
}  // namespace

void present() {
    if (g_videoHandle < 0 || !g_mappedMemory) return;
    uint8_t* base = static_cast<uint8_t*>(g_mappedMemory) + (g_currentBuffer * kFrameBytes);
    flushRange(base, kUsedBytes);
    g_bufferMarker[g_currentBuffer] = ++g_flipMarker;
    sceVideoOutSubmitFlip(g_videoHandle, g_currentBuffer, 1, g_flipMarker);
}

namespace {

// Pixel position of each 32-bit word inside a 64 KB macro tile, so a tile is
// written front to back (sequential stores into write-combined memory).
struct TileOrder {
    std::array<uint8_t, 16384> dx{}, dy{};
    TileOrder() {
        for (unsigned y = 0; y < 128; ++y)
            for (unsigned x = 0; x < 128; ++x) {
                size_t word = tiledByteOffset(x, y) / 4;  // tile 0
                dx[word] = static_cast<uint8_t>(x);
                dy[word] = static_cast<uint8_t>(y);
            }
    }
};
const TileOrder& tileOrder() {
    static const TileOrder t;
    return t;
}

struct YuvJob {
    const uint8_t *y, *u, *v;
    int strideY, strideU, strideV;
    int width, height;
    uint8_t* base;
    std::vector<uint16_t> srcX, srcY;  // screen -> picture coordinate
    // Bilinear chroma: the two chroma columns/rows around each screen
    // pixel and the weight of the first, in eighths (columns) and quarters
    // (rows). H.264's default siting: chroma sits on the even luma columns
    // and halfway between two luma rows.
    std::vector<uint16_t> cx0, cx1, cy0, cy1;
    std::vector<uint8_t> wx, wy;
};

inline uint8_t clamp8(int v) { return static_cast<uint8_t>(v < 0 ? 0 : v > 255 ? 255 : v); }

void convertTiles(const YuvJob& job, unsigned first, unsigned last) {
    const TileOrder& order = tileOrder();
    for (unsigned t = first; t < last; ++t) {
        unsigned tx = t % kTilesX, ty = t / kTilesX;
        auto* out = reinterpret_cast<uint32_t*>(job.base + static_cast<size_t>(t) * 0x10000);
        for (unsigned i = 0; i < 16384; ++i) {
            unsigned x = tx * 128 + order.dx[i];
            unsigned y = ty * 128 + order.dy[i];
            if (x >= kWidth || y >= kHeight) {
                out[i] = 0xFF000000u;
                continue;
            }
            unsigned sx = job.srcX[x], sy = job.srcY[y];
            // BT.709 limited range, 10-bit fixed point.
            int c = (static_cast<int>(job.y[sy * job.strideY + sx]) - 16) * 1192;
            const uint8_t* u0 = job.u + job.cy0[y] * job.strideU;
            const uint8_t* u1 = job.u + job.cy1[y] * job.strideU;
            const uint8_t* v0 = job.v + job.cy0[y] * job.strideV;
            const uint8_t* v1 = job.v + job.cy1[y] * job.strideV;
            unsigned ca = job.cx0[x], cb = job.cx1[x];
            int wxa = job.wx[x], wxb = 2 - wxa, wya = job.wy[y], wyb = 4 - wya;
            // Weights total 2 * 4 = 8.
            int d = ((wya * (wxa * u0[ca] + wxb * u0[cb]) + wyb * (wxa * u1[ca] + wxb * u1[cb]) + 4) >> 3) - 128;
            int e = ((wya * (wxa * v0[ca] + wxb * v0[cb]) + wyb * (wxa * v1[ca] + wxb * v1[cb]) + 4) >> 3) - 128;
            uint32_t r = clamp8((c + 1836 * e + 512) >> 10);
            uint32_t g = clamp8((c - 218 * d - 546 * e + 512) >> 10);
            uint32_t b = clamp8((c + 2163 * d + 512) >> 10);
            out[i] = 0xFF000000u | (b << 16) | (g << 8) | r;
        }
    }
}

// A few persistent workers that split the tiles of one picture.
class TilePool {
public:
    TilePool() {
        unsigned n = std::max(1u, std::min(4u, std::thread::hardware_concurrency() / 2));
        for (unsigned i = 0; i < n; ++i) {
            workers_.emplace_back();
            platform::startThread(workers_.back(), [this, i, n] { loop(i, n); }, 256u << 10);
        }
    }
    void run(std::function<void(unsigned, unsigned)> job) {
        {
            std::lock_guard<std::mutex> lock(m_);
            job_ = std::move(job);
            pending_ = static_cast<int>(workers_.size());
            ++generation_;
        }
        cv_.notify_all();
        std::unique_lock<std::mutex> lock(m_);
        done_.wait(lock, [this] { return pending_ == 0; });
    }

private:
    void loop(unsigned index, unsigned count) {
        uint64_t seen = 0;
        for (;;) {
            std::function<void(unsigned, unsigned)> job;
            {
                std::unique_lock<std::mutex> lock(m_);
                cv_.wait(lock, [&] { return generation_ != seen; });
                seen = generation_;
                job = job_;
            }
            constexpr unsigned kTiles = kTilesX * kTilesY;
            job(kTiles * index / count, kTiles * (index + 1) / count);
            std::lock_guard<std::mutex> lock(m_);
            if (--pending_ == 0) done_.notify_one();
        }
    }

    std::vector<platform::Thread> workers_;
    std::mutex m_;
    std::condition_variable cv_, done_;
    std::function<void(unsigned, unsigned)> job_;
    int pending_ = 0;
    uint64_t generation_ = 0;
};

}  // namespace

bool readBackRgb(std::vector<uint8_t>& rgb) {
    if (!g_mappedMemory) return false;
    const auto* base = static_cast<const uint8_t*>(g_mappedMemory) + (g_currentBuffer * kFrameBytes);
    rgb.resize(static_cast<size_t>(kWidth) * kHeight * 3);
    for (unsigned y = 0; y < kHeight; ++y)
        for (unsigned x = 0; x < kWidth; ++x) {
            uint32_t p;
            std::memcpy(&p, base + tiledByteOffset(x, y), 4);
            uint8_t* o = &rgb[(static_cast<size_t>(y) * kWidth + x) * 3];
            o[0] = static_cast<uint8_t>(p);
            o[1] = static_cast<uint8_t>(p >> 8);
            o[2] = static_cast<uint8_t>(p >> 16);
        }
    return true;
}

TilePool& tilePool() {
    static TilePool pool;
    return pool;
}

void drawRgba(const uint32_t* pixels) {
    if (!g_mappedMemory) return;
    acquireBuffer();
    uint8_t* base = static_cast<uint8_t*>(g_mappedMemory) + (g_currentBuffer * kFrameBytes);
    tilePool().run([pixels, base](unsigned first, unsigned last) {
        const TileOrder& order = tileOrder();
        for (unsigned t = first; t < last; ++t) {
            unsigned tx = t % kTilesX, ty = t / kTilesX;
            auto* out = reinterpret_cast<uint32_t*>(base + static_cast<size_t>(t) * 0x10000);
            for (unsigned i = 0; i < 16384; ++i) {
                unsigned x = tx * 128 + order.dx[i], y = ty * 128 + order.dy[i];
                out[i] = (x < kWidth && y < kHeight) ? (pixels[y * kWidth + x] | 0xFF000000u) : 0xFF000000u;
            }
        }
    });
}

bool drawYuv420(const uint8_t* y, const uint8_t* u, const uint8_t* v, int strideY, int strideU, int strideV,
                int width, int height, bool wait) {
    if (!g_mappedMemory || width <= 0 || height <= 0) return false;
    if (!acquireBuffer(wait)) return false;
    static YuvJob job;
    if (job.width != width || job.height != height) {
        job.srcX.resize(kWidth);
        job.srcY.resize(kHeight);
        for (unsigned x = 0; x < kWidth; ++x) job.srcX[x] = static_cast<uint16_t>(x * static_cast<unsigned>(width) / kWidth);
        for (unsigned yy = 0; yy < kHeight; ++yy)
            job.srcY[yy] = static_cast<uint16_t>(yy * static_cast<unsigned>(height) / kHeight);
        int cw = (width + 1) / 2, ch = (height + 1) / 2;
        job.cx0.resize(kWidth);
        job.cx1.resize(kWidth);
        job.wx.resize(kWidth);
        for (unsigned x = 0; x < kWidth; ++x) {
            int sx = job.srcX[x];
            int c0 = sx / 2;
            bool between = (sx & 1) != 0;  // odd luma column: halfway between two chroma samples
            job.cx0[x] = static_cast<uint16_t>(c0);
            job.cx1[x] = static_cast<uint16_t>(std::min(c0 + (between ? 1 : 0), cw - 1));
            job.wx[x] = between ? 1 : 2;  // of 2
        }
        job.cy0.resize(kHeight);
        job.cy1.resize(kHeight);
        job.wy.resize(kHeight);
        for (unsigned yy = 0; yy < kHeight; ++yy) {
            int sy = job.srcY[yy];
            // Chroma row r sits at luma row 2r + 0.5: even rows take 1/4 of
            // the row above and 3/4 of their own, odd rows 3/4 of their own
            // and 1/4 of the row below.
            int r = sy / 2;
            if (sy & 1) {
                job.cy0[yy] = static_cast<uint16_t>(r);
                job.cy1[yy] = static_cast<uint16_t>(std::min(r + 1, ch - 1));
                job.wy[yy] = 3;  // of 4, for cy0
            } else {
                job.cy0[yy] = static_cast<uint16_t>(std::max(r - 1, 0));
                job.cy1[yy] = static_cast<uint16_t>(r);
                job.wy[yy] = 1;
            }
        }
    }
    job.y = y;
    job.u = u;
    job.v = v;
    job.strideY = strideY;
    job.strideU = strideU;
    job.strideV = strideV;
    job.width = width;
    job.height = height;
    job.base = static_cast<uint8_t*>(g_mappedMemory) + (g_currentBuffer * kFrameBytes);
    tilePool().run([](unsigned first, unsigned last) { convertTiles(job, first, last); });
    return true;
}

} // namespace xc::display

#else

// Host PC fallback implementation
namespace xc::display {
std::mutex& frameMutex() {
    static std::mutex m;
    return m;
}
bool init() { return true; }
void shutdown() {}
void present() {}
bool drawYuv420(const uint8_t*, const uint8_t*, const uint8_t*, int, int, int, int, int, bool) { return true; }
void drawRgba(const uint32_t*) {}
bool readBackRgb(std::vector<uint8_t>&) { return false; }
} // namespace xc::display

#endif
