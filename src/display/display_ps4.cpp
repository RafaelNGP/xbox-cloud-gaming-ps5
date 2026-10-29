// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "display/display.h"
#include "platform/platform.h"
#include "util/log.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
#include <emmintrin.h>
#include <smmintrin.h>

#include <orbis/libkernel.h>
#include <orbis/VideoOut.h>

namespace xc::display {

namespace {

constexpr size_t kFramePitch = kWidth * 4;
constexpr size_t kFrameBytes = (kFramePitch * kHeight + 0x1FFFFFull) & ~0x1FFFFFull; // 2MB aligned per buffer
constexpr int kBuffers = 3;
constexpr size_t kMemoryBytes = kFrameBytes * kBuffers;
constexpr size_t kMemoryAlignment = 0x200000;
constexpr int kMemoryTypeGarlic = 3;
constexpr int kMapProtection = 0x33; // RW

int g_videoHandle = -1;
void* g_mappedMemory = nullptr;
uint8_t* g_buffers[kBuffers]{};
int g_currentBuffer = 0;
int64_t g_flipMarker = 0;
std::array<int64_t, kBuffers> g_bufferMarker{-1, -1, -1};
std::mutex g_frameMutex;
std::atomic<int> g_sharpness{0};

struct Overlay {
    std::vector<uint32_t> px;
    int x = 0, y = 0, w = 0, h = 0;
    uint8_t alpha = 255;
};
std::mutex g_overlayMutex;
std::unique_ptr<Overlay> g_overlay;

struct RowScratch {
    int lastWidth = 0;
    int lastHeight = 0;
    std::array<int, kWidth> mapX{};
    std::array<int, kHeight> mapY{};

    void update(int width, int height) {
        if (width == lastWidth && height == lastHeight) return;
        lastWidth = width;
        lastHeight = height;
        for (int dx = 0; dx < static_cast<int>(kWidth); ++dx) {
            mapX[dx] = dx * width / static_cast<int>(kWidth);
        }
        for (int dy = 0; dy < static_cast<int>(kHeight); ++dy) {
            mapY[dy] = dy * height / static_cast<int>(kHeight);
        }
    }
};
static RowScratch g_scratch;

class RowPool {
public:
    static constexpr int kNumWorkers = 4;

    RowPool() {
        for (int i = 0; i < kNumWorkers; ++i) {
            workers_[i] = std::thread([this, i]() {
                platform::setThreadAffinity(platform::ThreadRole::Worker, "ps4_display");
                workerLoop(i);
            });
        }
    }

    ~RowPool() {
        {
            std::lock_guard<std::mutex> runLock(runMutex_);
            std::lock_guard<std::mutex> lock(mutex_);
            stop_ = true;
            cvWork_.notify_all();
        }
        for (auto& w : workers_) {
            if (w.joinable()) w.join();
        }
    }

    void run(const std::function<void(int yStart, int yEnd)>& fn) {
        std::unique_lock<std::mutex> runLock(runMutex_);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            currentTask_ = &fn;
            remaining_ = kNumWorkers;
            taskSeq_++;
            cvWork_.notify_all();
        }
        // Slice 4 is processed by caller (main thread or video thread)
        int rowsPerSlice = kHeight / (kNumWorkers + 1); // 216 rows
        int yStart = kNumWorkers * rowsPerSlice;
        int yEnd = kHeight;
        fn(yStart, yEnd);

        std::unique_lock<std::mutex> lock(mutex_);
        cvDone_.wait(lock, [this]() { return remaining_ == 0; });
        currentTask_ = nullptr;
        _mm_sfence();
    }

private:
    void workerLoop(int id) {
        int rowsPerSlice = kHeight / (kNumWorkers + 1); // 216 rows
        int yStart = id * rowsPerSlice;
        int yEnd = (id + 1) * rowsPerSlice;
        uint64_t lastSeq = 0;

        while (true) {
            const std::function<void(int, int)>* task = nullptr;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cvWork_.wait(lock, [this, &lastSeq]() { return stop_ || taskSeq_ > lastSeq; });
                if (stop_) break;
                task = currentTask_;
                lastSeq = taskSeq_;
            }

            if (task && *task) {
                (*task)(yStart, yEnd);
            }

            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (--remaining_ == 0) {
                    cvDone_.notify_one();
                }
            }
        }
    }

    std::array<std::thread, kNumWorkers> workers_;
    std::mutex runMutex_;
    std::mutex mutex_;
    std::condition_variable cvWork_;
    std::condition_variable cvDone_;
    const std::function<void(int, int)>* currentTask_ = nullptr;
    int remaining_ = 0;
    uint64_t taskSeq_ = 0;
    bool stop_ = false;
};

static std::unique_ptr<RowPool> g_rowPool;

inline __m128i yuvToRgbaSse(int y0, int y1, int y2, int y3,
                            int u0, int u1, int u2, int u3,
                            int v0, int v1, int v2, int v3) {
    __m128i yVec = _mm_setr_epi32(y0 - 16, y1 - 16, y2 - 16, y3 - 16);
    __m128i uVec = _mm_setr_epi32(u0 - 128, u1 - 128, u2 - 128, u3 - 128);
    __m128i vVec = _mm_setr_epi32(v0 - 128, v1 - 128, v2 - 128, v3 - 128);

    __m128i c = _mm_mullo_epi32(yVec, _mm_set1_epi32(1192));
    __m128i r = _mm_srai_epi32(_mm_add_epi32(_mm_add_epi32(c, _mm_mullo_epi32(vVec, _mm_set1_epi32(1836))), _mm_set1_epi32(512)), 10);
    __m128i g = _mm_srai_epi32(_mm_add_epi32(_mm_sub_epi32(_mm_sub_epi32(c, _mm_mullo_epi32(uVec, _mm_set1_epi32(218))), _mm_mullo_epi32(vVec, _mm_set1_epi32(546))), _mm_set1_epi32(512)), 10);
    __m128i b = _mm_srai_epi32(_mm_add_epi32(_mm_add_epi32(c, _mm_mullo_epi32(uVec, _mm_set1_epi32(2163))), _mm_set1_epi32(512)), 10);

    r = _mm_max_epi32(_mm_min_epi32(r, _mm_set1_epi32(255)), _mm_setzero_si128());
    g = _mm_max_epi32(_mm_min_epi32(g, _mm_set1_epi32(255)), _mm_setzero_si128());
    b = _mm_max_epi32(_mm_min_epi32(b, _mm_set1_epi32(255)), _mm_setzero_si128());

    __m128i rgba = _mm_or_si128(_mm_set1_epi32(static_cast<int>(0xFF000000u)),
                   _mm_or_si128(r, _mm_or_si128(_mm_slli_epi32(g, 8), _mm_slli_epi32(b, 16))));
    return rgba;
}

inline void blendOverlayRow(uint32_t* dRow, int dy, const Overlay& ov) {
    if (dy < ov.y || dy >= ov.y + ov.h) return;
    int oy = dy - ov.y;
    const uint32_t* sRow = ov.px.data() + oy * ov.w;
    for (int ox = 0; ox < ov.w; ++ox) {
        int px = ov.x + ox;
        if (px < 0 || px >= static_cast<int>(kWidth)) continue;
        uint32_t p = sRow[ox];
        uint32_t alpha = ((p >> 24) * ov.alpha) / 255;
        if (alpha == 0) continue;
        if (alpha == 255) {
            dRow[px] = p;
            continue;
        }
        uint32_t inv = 255 - alpha;
        uint32_t bg = dRow[px];
        uint32_t r = (((p & 0xFF) * alpha) + ((bg & 0xFF) * inv)) / 255;
        uint32_t g = ((((p >> 8) & 0xFF) * alpha) + (((bg >> 8) & 0xFF) * inv)) / 255;
        uint32_t b = ((((p >> 16) & 0xFF) * alpha) + (((bg >> 16) & 0xFF) * inv)) / 255;
        dRow[px] = 0xFF000000u | (b << 16) | (g << 8) | r;
    }
}

extern "C" int sceKernelMapNamedFlexibleMemory(void** address, size_t length, int protection, int flags, const char* name);

} // namespace

bool init(bool /*preferGpu*/) {
    if (g_videoHandle >= 0) return true;
    int handle = sceVideoOutOpen(ORBIS_USER_SERVICE_USER_ID_SYSTEM, 0, 0, nullptr);
    if (handle < 0) {
        handle = sceVideoOutOpen(0xFF, 0, 0, nullptr);
    }
    if (handle < 0) {
        XC_LOGE("display: sceVideoOutOpen failed: 0x%08x", static_cast<unsigned>(handle));
        return false;
    }
    sceVideoOutSetFlipRate(handle, 0); // 60 Hz

    void* mapped = nullptr;
    int64_t physAddr = 0;
    int rc = -1;
    size_t directMemSize = sceKernelGetDirectMemorySize();
    if (directMemSize >= kMemoryBytes) {
        rc = sceKernelAllocateDirectMemory(0, static_cast<int64_t>(directMemSize),
                                           kMemoryBytes, kMemoryAlignment, kMemoryTypeGarlic, &physAddr);
        if (rc == 0) {
            rc = sceKernelMapDirectMemory(&mapped, kMemoryBytes, kMapProtection, 0, physAddr, kMemoryAlignment);
        }
    }
    if (rc != 0 || !mapped) {
        // Fallback to flexible memory (reliable on all PS4 jailbreak environments)
        rc = sceKernelMapNamedFlexibleMemory(&mapped, kMemoryBytes, kMapProtection, 0, "display_fb");
        if (rc != 0 || !mapped) {
            XC_LOGE("display: allocate direct and flexible memory failed: 0x%08x", static_cast<unsigned>(rc));
            sceVideoOutClose(handle);
            return false;
        }
        XC_LOGI("display: %zu bytes of flexible memory at %p (direct memory fallback)", kMemoryBytes, mapped);
    } else {
        XC_LOGI("display: %zu bytes of direct memory at %p", kMemoryBytes, mapped);
    }

    g_mappedMemory = mapped;
    g_videoHandle = handle;

    for (int i = 0; i < kBuffers; ++i) {
        g_buffers[i] = static_cast<uint8_t*>(mapped) + i * kFrameBytes;
        std::memset(g_buffers[i], 0, kWidth * kHeight * 4);
    }

    OrbisVideoOutBufferAttribute attr{};
    sceVideoOutSetBufferAttribute(&attr, ORBIS_VIDEO_OUT_PIXEL_FORMAT_A8B8G8R8_SRGB,
                                  ORBIS_VIDEO_OUT_TILING_MODE_LINEAR,
                                  ORBIS_VIDEO_OUT_ASPECT_RATIO_16_9,
                                  kWidth, kHeight, kWidth);

    rc = sceVideoOutRegisterBuffers(handle, 0, reinterpret_cast<void* const*>(g_buffers), kBuffers, &attr);
    if (rc != 0) {
        XC_LOGE("display: sceVideoOutRegisterBuffers failed: 0x%08x", static_cast<unsigned>(rc));
        sceVideoOutClose(handle);
        g_videoHandle = -1;
        return false;
    }

    g_rowPool = std::make_unique<RowPool>();

    XC_LOGI("display: PS4 VideoOut initialized (3 linear 1080p buffers, handle %d, 4 SIMD worker threads)", handle);
    return true;
}

void shutdown() {
    g_rowPool.reset();
    if (g_videoHandle >= 0) {
        sceVideoOutClose(g_videoHandle);
        g_videoHandle = -1;
    }
    g_mappedMemory = nullptr;
}

std::mutex& frameMutex() { return g_frameMutex; }

namespace {
// Picks the back buffer for the next frame: not on screen, not queued.
// Without `wait`, gives up at once when all are busy.
bool acquireBuffer(bool wait = true) {
    for (int attempt = 0; attempt < 4; ++attempt) {
        OrbisVideoOutFlipStatus status{};
        int64_t shown = g_flipMarker;  // no status: assume everything shown
        if (g_flipMarker && sceVideoOutGetFlipStatus(g_videoHandle, &status) == 0 && status.flipArg > 0)
            shown = status.flipArg;
        int onScreen = -1;
        for (int i = 0; i < kBuffers; ++i)
            if (g_bufferMarker[i] >= 0 && g_bufferMarker[i] <= shown &&
                (onScreen < 0 || g_bufferMarker[i] > g_bufferMarker[onScreen]))
                onScreen = i;
        int best = -1;
        for (int i = 0; i < kBuffers; ++i) {
            if (i == onScreen || (g_bufferMarker[i] >= 0 && g_bufferMarker[i] > shown)) continue;
            if (best < 0 || g_bufferMarker[i] < g_bufferMarker[best]) best = i;
        }
        if (best >= 0) {
            g_currentBuffer = best;
            return true;
        }
        if (!wait) return false;
        sceVideoOutWaitVblank();
    }
    // Flip status not moving: fall back to round robin.
    g_currentBuffer = static_cast<int>(g_flipMarker % kBuffers);
    return true;
}
} // namespace

void present() {
    if (g_videoHandle < 0) return;
    int rc = sceVideoOutSubmitFlip(g_videoHandle, g_currentBuffer, ORBIS_VIDEO_OUT_FLIP_VSYNC, ++g_flipMarker);
    if (rc != 0) {
        XC_LOGW("display: submit flip: 0x%08x", static_cast<unsigned>(rc));
    }
    g_bufferMarker[g_currentBuffer] = g_flipMarker;
}

void drawRgba(const uint32_t* pixels) {
    if (!pixels || g_videoHandle < 0) return;
    if (!acquireBuffer(true)) return;
    uint32_t* dst = reinterpret_cast<uint32_t*>(g_buffers[g_currentBuffer]);

    if (!g_rowPool) {
        const __m128i* s = reinterpret_cast<const __m128i*>(pixels);
        __m128i* d = reinterpret_cast<__m128i*>(dst);
        size_t count = (kWidth * kHeight * sizeof(uint32_t)) / sizeof(__m128i);
        for (size_t i = 0; i < count; i += 4) {
            _mm_stream_si128(d + i, _mm_loadu_si128(s + i));
            _mm_stream_si128(d + i + 1, _mm_loadu_si128(s + i + 1));
            _mm_stream_si128(d + i + 2, _mm_loadu_si128(s + i + 2));
            _mm_stream_si128(d + i + 3, _mm_loadu_si128(s + i + 3));
        }
        _mm_sfence();
        return;
    }

    g_rowPool->run([dst, pixels](int yStart, int yEnd) {
        for (int y = yStart; y < yEnd; ++y) {
            const __m128i* s = reinterpret_cast<const __m128i*>(pixels + y * kWidth);
            __m128i* d = reinterpret_cast<__m128i*>(dst + y * kWidth);
            constexpr size_t kVecsPerRow = (kWidth * sizeof(uint32_t)) / sizeof(__m128i); // 480
            for (size_t i = 0; i < kVecsPerRow; i += 4) {
                _mm_stream_si128(d + i, _mm_loadu_si128(s + i));
                _mm_stream_si128(d + i + 1, _mm_loadu_si128(s + i + 1));
                _mm_stream_si128(d + i + 2, _mm_loadu_si128(s + i + 2));
                _mm_stream_si128(d + i + 3, _mm_loadu_si128(s + i + 3));
            }
        }
    });
}

void pump() {}

void idleWait(uint32_t ms) {
    platform::sleepMs(ms);
}

bool drawYuv420(const uint8_t* y, const uint8_t* u, const uint8_t* v,
                int strideY, int strideU, int strideV,
                int width, int height, bool wait) {
    if (g_videoHandle < 0 || !y || !u || !v || width <= 0 || height <= 0) return false;
    if (!acquireBuffer(wait)) return false;
    uint32_t* dst = reinterpret_cast<uint32_t*>(g_buffers[g_currentBuffer]);

    g_scratch.update(width, height);

    std::unique_ptr<Overlay> activeOverlay;
    {
        std::lock_guard<std::mutex> lock(g_overlayMutex);
        if (g_overlay) {
            activeOverlay = std::make_unique<Overlay>(*g_overlay);
        }
    }

    auto renderSlice = [y, u, v, strideY, strideU, strideV, dst, &activeOverlay](int yStart, int yEnd) {
        const auto& mapX = g_scratch.mapX;
        const auto& mapY = g_scratch.mapY;

        for (int dy = yStart; dy < yEnd; ++dy) {
            int sy = mapY[dy];
            const uint8_t* yRow = y + sy * strideY;
            const uint8_t* uRow = u + (sy / 2) * strideU;
            const uint8_t* vRow = v + (sy / 2) * strideV;
            uint32_t* dRow = dst + dy * kWidth;

            for (int dx = 0; dx < static_cast<int>(kWidth); dx += 4) {
                int sx0 = mapX[dx];
                int sx1 = mapX[dx + 1];
                int sx2 = mapX[dx + 2];
                int sx3 = mapX[dx + 3];

                int y0 = yRow[sx0];
                int y1 = yRow[sx1];
                int y2 = yRow[sx2];
                int y3 = yRow[sx3];

                int u0 = uRow[sx0 / 2];
                int u1 = uRow[sx1 / 2];
                int u2 = uRow[sx2 / 2];
                int u3 = uRow[sx3 / 2];

                int v0 = vRow[sx0 / 2];
                int v1 = vRow[sx1 / 2];
                int v2 = vRow[sx2 / 2];
                int v3 = vRow[sx3 / 2];

                __m128i rgba = yuvToRgbaSse(y0, y1, y2, y3, u0, u1, u2, u3, v0, v1, v2, v3);
                _mm_stream_si128(reinterpret_cast<__m128i*>(&dRow[dx]), rgba);
            }

            if (activeOverlay) {
                _mm_sfence();
                blendOverlayRow(dRow, dy, *activeOverlay);
            }
        }
    };

    if (g_rowPool) {
        g_rowPool->run(renderSlice);
    } else {
        renderSlice(0, kHeight);
        _mm_sfence();
    }
    return true;
}

bool drawNv12(const uint8_t* y, const uint8_t* uv, int strideY, int strideUV,
              int width, int height, bool wait) {
    if (g_videoHandle < 0 || !y || !uv || width <= 0 || height <= 0) return false;
    if (!acquireBuffer(wait)) return false;
    uint32_t* dst = reinterpret_cast<uint32_t*>(g_buffers[g_currentBuffer]);

    g_scratch.update(width, height);

    std::unique_ptr<Overlay> activeOverlay;
    {
        std::lock_guard<std::mutex> lock(g_overlayMutex);
        if (g_overlay) {
            activeOverlay = std::make_unique<Overlay>(*g_overlay);
        }
    }

    auto renderSlice = [y, uv, strideY, strideUV, dst, &activeOverlay](int yStart, int yEnd) {
        const auto& mapX = g_scratch.mapX;
        const auto& mapY = g_scratch.mapY;

        for (int dy = yStart; dy < yEnd; ++dy) {
            int sy = mapY[dy];
            const uint8_t* yRow = y + sy * strideY;
            const uint8_t* uvRow = uv + (sy / 2) * strideUV;
            uint32_t* dRow = dst + dy * kWidth;

            for (int dx = 0; dx < static_cast<int>(kWidth); dx += 4) {
                int sx0 = mapX[dx];
                int sx1 = mapX[dx + 1];
                int sx2 = mapX[dx + 2];
                int sx3 = mapX[dx + 3];

                int y0 = yRow[sx0];
                int y1 = yRow[sx1];
                int y2 = yRow[sx2];
                int y3 = yRow[sx3];

                int u0 = uvRow[(sx0 / 2) * 2];
                int v0 = uvRow[(sx0 / 2) * 2 + 1];

                int u1 = uvRow[(sx1 / 2) * 2];
                int v1 = uvRow[(sx1 / 2) * 2 + 1];

                int u2 = uvRow[(sx2 / 2) * 2];
                int v2 = uvRow[(sx2 / 2) * 2 + 1];

                int u3 = uvRow[(sx3 / 2) * 2];
                int v3 = uvRow[(sx3 / 2) * 2 + 1];

                __m128i rgba = yuvToRgbaSse(y0, y1, y2, y3, u0, u1, u2, u3, v0, v1, v2, v3);
                _mm_stream_si128(reinterpret_cast<__m128i*>(&dRow[dx]), rgba);
            }

            if (activeOverlay) {
                _mm_sfence();
                blendOverlayRow(dRow, dy, *activeOverlay);
            }
        }
    };

    if (g_rowPool) {
        g_rowPool->run(renderSlice);
    } else {
        renderSlice(0, kHeight);
        _mm_sfence();
    }
    return true;
}

void setOverlay(const uint32_t* pixels, int x, int y, int w, int h, uint8_t opacity) {
    std::lock_guard<std::mutex> lock(g_overlayMutex);
    if (!pixels || w <= 0 || h <= 0 || opacity == 0) {
        g_overlay.reset();
        return;
    }
    g_overlay = std::make_unique<Overlay>();
    g_overlay->x = x;
    g_overlay->y = y;
    g_overlay->w = w;
    g_overlay->h = h;
    g_overlay->alpha = opacity;
    g_overlay->px.assign(pixels, pixels + static_cast<size_t>(w) * h);
}

void setSharpness(int amount) { g_sharpness = std::clamp(amount, 0, 256); }
void setDeband(int) {}
void setUpscaler(int) {}
void setRestore(bool) {}
DisplayCaps caps() { return {}; }
void setSwapImages(int) {}
void setPresentWait(bool) {}
bool canSwitchRefresh() { return false; }
bool setHighRefresh(bool) { return false; }
bool bringUpTest(int) { return false; }
uint64_t lastPresentId() { return static_cast<uint64_t>(g_flipMarker); }
bool lastShown(uint64_t& id, uint64_t& atUs) {
    if (g_videoHandle < 0) return false;
    OrbisVideoOutFlipStatus status{};
    if (g_flipMarker && sceVideoOutGetFlipStatus(g_videoHandle, &status) == 0 && status.flipArg > 0) {
        id = static_cast<uint64_t>(status.flipArg);
        atUs = platform::nowUs();
        return true;
    }
    id = static_cast<uint64_t>(g_flipMarker);
    atUs = platform::nowUs();
    return true;
}
bool readBackRgb(std::vector<uint8_t>& rgb, int& width, int& height) {
    std::lock_guard<std::mutex> lock(g_frameMutex);
    if (!g_buffers[g_currentBuffer]) return false;
    width = kWidth;
    height = kHeight;
    const uint32_t* base = reinterpret_cast<const uint32_t*>(g_buffers[g_currentBuffer]);
    rgb.resize(static_cast<size_t>(kWidth) * kHeight * 3);
    for (size_t i = 0; i < static_cast<size_t>(kWidth) * kHeight; ++i) {
        uint32_t p = base[i];
        rgb[i * 3 + 0] = static_cast<uint8_t>(p);
        rgb[i * 3 + 1] = static_cast<uint8_t>(p >> 8);
        rgb[i * 3 + 2] = static_cast<uint8_t>(p >> 16);
    }
    return true;
}

} // namespace xc::display
