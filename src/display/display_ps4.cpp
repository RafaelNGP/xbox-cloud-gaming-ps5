// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "display/display.h"
#include "platform/ps4/video_color_ps4.h"
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

extern "C" {
int32_t sceGnmSubmitDone(void);
uint64_t sceKernelGetProcessTime(void);
int sceKernelUsleep(unsigned int microseconds);
int sceKernelMapNamedFlexibleMemory(void** address, size_t length, int protection, int flags, const char* name);
int sceKernelMunmap(void* address, size_t length);
}

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
int64_t g_physAddr = 0;
bool g_isDirectMemory = false;
uint8_t* g_buffers[kBuffers]{};
int g_currentBuffer = 0;
// Video frames flip at the next horizontal sync instead of waiting for the vertical blank:
// XCloud4 measured 43 -> 27 ms (1080p) and 33 -> 17 ms (720p) of local presentation latency
// on a PS4, with no noticeable tearing at ~58 fps; it is their default since 1.2.0. Menu
// frames (drawRgba) keep V-Sync, where latency does not matter and tearing would show.
bool g_videoFlip = false;
int g_onScreenBuffer = 0;
int g_pendingBuffer = -1;
int64_t g_flipMarker = 0;
int64_t g_pendingFlipMarker = -1;
uint64_t g_lastFlipCount = 0;
std::atomic<uint64_t> g_shownId{0};
std::atomic<uint64_t> g_shownAtUs{0};
std::recursive_mutex g_frameMutex;
std::atomic<int> g_sharpness{0};

struct SubmittedFlip {
    int64_t marker = 0;
    uint64_t submitUs = 0;
    uint64_t submitProcUs = 0;
};
constexpr size_t kSubmittedFlipRing = 16;
SubmittedFlip g_submittedFlips[kSubmittedFlipRing]{};

struct Overlay {
    std::vector<uint32_t> px;
    int x = 0, y = 0, w = 0, h = 0;
    uint8_t alpha = 255;
};
std::mutex g_overlayMutex;
std::shared_ptr<const Overlay> g_overlay;

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

// 2 helper threads (pinned to Core 4 and Core 5) + calling thread (Core 0 or Core 3)
// = 3 equal stripes of 360 rows (0..360, 360..720, 720..1080).
class RowPool {
public:
    static constexpr int kNumWorkers = 2;

    RowPool() {
        for (int i = 0; i < kNumWorkers; ++i) {
            platform::startThread(workers_[i], [this, i]() {
                const char* tname = (i == 0) ? "ps4_disp_1" : "ps4_disp_2";
                platform::setThreadAffinity(platform::ThreadRole::Worker, tname);
                workerLoop(i);
            }, 512u << 10);
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
        // Stripe 0 (rows 0..360) is processed by the calling thread
        constexpr int kRowsPerSlice = kHeight / (kNumWorkers + 1); // 360 rows
        fn(0, kRowsPerSlice);
        _mm_sfence();

        std::unique_lock<std::mutex> lock(mutex_);
        cvDone_.wait(lock, [this]() { return remaining_ == 0; });
        currentTask_ = nullptr;
        _mm_sfence();
    }

private:
    void workerLoop(int id) {
        constexpr int kRowsPerSlice = kHeight / (kNumWorkers + 1); // 360 rows
        int yStart = (id + 1) * kRowsPerSlice;
        int yEnd = (id == kNumWorkers - 1) ? static_cast<int>(kHeight) : (id + 2) * kRowsPerSlice;
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
                _mm_sfence();
            }

            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (--remaining_ == 0) {
                    cvDone_.notify_one();
                }
            }
        }
    }

    std::array<platform::Thread, kNumWorkers> workers_;
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

// The YUV -> RGB factors of one colour matrix and range, in 1/256 units, ready for
// colorRgbEight's pairwise multiply-adds. Which set applies comes from the stream's SPS
// (hw_decoder_ps4.cpp, with XCloud4's colour policy); BT.709 limited range until then.
struct ColorK {
    __m128i y_bias, k_y_r, k_y_b, k_y_gu, k_gv_0;
};
// Y factor, V->R, U->G, V->G, U->B, black level.
struct ColorFactors {
    int y, rv, gu, gv, bu, bias;
};
constexpr ColorFactors kColorSets[4] = {
    {298, 459, -55, -137, 541, 16},  // BT.709 limited range (xCloud's)
    {298, 409, -100, -208, 516, 16}, // BT.601 limited range
    {256, 403, -48, -120, 475, 0},   // BT.709 full range
    {256, 359, -88, -183, 454, 0},   // BT.601 full range
};
std::atomic<int> g_colorSet{0};

inline ColorK makeColorK(int set) {
    const ColorFactors& f = kColorSets[(set >= 0 && set < 4) ? set : 0];
    ColorK k;
    k.y_bias = _mm_set1_epi16(static_cast<short>(f.bias));
    k.k_y_r = _mm_set1_epi32((f.rv << 16) | (f.y & 0xFFFF));
    k.k_y_b = _mm_set1_epi32((f.bu << 16) | (f.y & 0xFFFF));
    k.k_y_gu = _mm_set1_epi32(((f.gu & 0xFFFF) << 16) | (f.y & 0xFFFF));
    k.k_gv_0 = _mm_set1_epi32(f.gv & 0xFFFF);
    return k;
}

// 8-pixel 16-bit SSE2 YUV -> A8B8G8R8 converter (approach from XCloud4,
// github.com/asp11edgr/XCloud4, GPL-3.0). BT.709 limited range: R = 1.164 Y + 1.793 V,
// G = 1.164 Y - 0.213 U - 0.533 V, B = 1.164 Y + 2.112 U.
static inline void colorRgbEight(const ColorK& k, __m128i y16, __m128i u16, __m128i v16,
                                 __m128i& p_lo, __m128i& p_hi) {
    const __m128i y_bias = k.y_bias;
    const __m128i uv_bias = _mm_set1_epi16(128);
    const __m128i zero16 = _mm_setzero_si128();
    const __m128i round = _mm_set1_epi32(128);
    const __m128i k_y_r = k.k_y_r;
    const __m128i k_y_b = k.k_y_b;
    const __m128i k_y_gu = k.k_y_gu;
    const __m128i k_gv_0 = k.k_gv_0;
    const __m128i alpha  = _mm_set1_epi16(static_cast<short>(0xFF00));

    __m128i c = _mm_max_epi16(_mm_sub_epi16(y16, y_bias), zero16);
    __m128i d = _mm_sub_epi16(u16, uv_bias);
    __m128i e = _mm_sub_epi16(v16, uv_bias);

    __m128i ce_lo = _mm_unpacklo_epi16(c, e);
    __m128i ce_hi = _mm_unpackhi_epi16(c, e);
    __m128i cd_lo = _mm_unpacklo_epi16(c, d);
    __m128i cd_hi = _mm_unpackhi_epi16(c, d);
    __m128i e0_lo = _mm_unpacklo_epi16(e, zero16);
    __m128i e0_hi = _mm_unpackhi_epi16(e, zero16);

    __m128i r_lo = _mm_srai_epi32(_mm_add_epi32(_mm_madd_epi16(ce_lo, k_y_r), round), 8);
    __m128i r_hi = _mm_srai_epi32(_mm_add_epi32(_mm_madd_epi16(ce_hi, k_y_r), round), 8);

    __m128i g_lo = _mm_srai_epi32(_mm_add_epi32(_mm_add_epi32(_mm_madd_epi16(cd_lo, k_y_gu), _mm_madd_epi16(e0_lo, k_gv_0)), round), 8);
    __m128i g_hi = _mm_srai_epi32(_mm_add_epi32(_mm_add_epi32(_mm_madd_epi16(cd_hi, k_y_gu), _mm_madd_epi16(e0_hi, k_gv_0)), round), 8);

    __m128i b_lo = _mm_srai_epi32(_mm_add_epi32(_mm_madd_epi16(cd_lo, k_y_b), round), 8);
    __m128i b_hi = _mm_srai_epi32(_mm_add_epi32(_mm_madd_epi16(cd_hi, k_y_b), round), 8);

    __m128i r16 = _mm_packs_epi32(r_lo, r_hi);
    __m128i g16 = _mm_packs_epi32(g_lo, g_hi);
    __m128i b16 = _mm_packs_epi32(b_lo, b_hi);

    __m128i r8 = _mm_packus_epi16(r16, r16);
    __m128i g8 = _mm_packus_epi16(g16, g16);
    __m128i b8 = _mm_packus_epi16(b16, b16);

    __m128i rg = _mm_unpacklo_epi8(r8, g8);
    __m128i ba = _mm_or_si128(_mm_unpacklo_epi8(b8, zero16), alpha);
    p_lo = _mm_unpacklo_epi16(rg, ba);
    p_hi = _mm_unpackhi_epi16(rg, ba);
}

static inline void unpackNv12UvEight(const uint8_t* uvPtr, __m128i& u16, __m128i& v16) {
    const __m128i mask_low = _mm_set1_epi16(0x00FF);
    __m128i uv_bytes = _mm_loadl_epi64(reinterpret_cast<const __m128i*>(uvPtr));
    __m128i uv_dup = _mm_unpacklo_epi16(uv_bytes, uv_bytes);
    u16 = _mm_and_si128(uv_dup, mask_low);
    v16 = _mm_srli_epi16(uv_dup, 8);
}

static inline void unpackYuv420UvEight(const uint8_t* uPtr, const uint8_t* vPtr, __m128i& u16, __m128i& v16) {
    const __m128i zero = _mm_setzero_si128();
    uint32_t u4 = 0, v4 = 0;
    std::memcpy(&u4, uPtr, sizeof(u4));
    std::memcpy(&v4, vPtr, sizeof(v4));
    __m128i u_bytes = _mm_cvtsi32_si128(static_cast<int>(u4));
    __m128i v_bytes = _mm_cvtsi32_si128(static_cast<int>(v4));
    __m128i u_dup = _mm_unpacklo_epi8(u_bytes, u_bytes);
    __m128i v_dup = _mm_unpacklo_epi8(v_bytes, v_bytes);
    u16 = _mm_unpacklo_epi8(u_dup, zero);
    v16 = _mm_unpacklo_epi8(v_dup, zero);
}

// 1280 -> 1920 register shuffle for 8 source pixels -> 12 destination pixels
static inline void shuffleScale32(__m128i p_lo, __m128i p_hi,
                                  __m128i& out0, __m128i& out1, __m128i& out2) {
    out0 = _mm_shuffle_epi32(p_lo, _MM_SHUFFLE(2, 1, 0, 0));
    __m128i p23 = _mm_srli_si128(p_lo, 8);
    __m128i p44 = _mm_shuffle_epi32(p_hi, _MM_SHUFFLE(0, 0, 0, 0));
    out1 = _mm_unpacklo_epi64(p23, p44);
    out2 = _mm_shuffle_epi32(p_hi, _MM_SHUFFLE(3, 2, 2, 1));
}

static inline void colorRowNv12Scale1to1Sse2(const ColorK& k, const uint8_t* yRow, const uint8_t* uvRow, uint32_t* dstRow) {
    const __m128i zero = _mm_setzero_si128();
    for (int x = 0; x < 1920; x += 8) {
        __m128i y_bytes = _mm_loadl_epi64(reinterpret_cast<const __m128i*>(yRow + x));
        __m128i y16 = _mm_unpacklo_epi8(y_bytes, zero);
        __m128i u16, v16, p_lo, p_hi;
        unpackNv12UvEight(uvRow + x, u16, v16);
        colorRgbEight(k, y16, u16, v16, p_lo, p_hi);
        _mm_stream_si128(reinterpret_cast<__m128i*>(dstRow + x), p_lo);
        _mm_stream_si128(reinterpret_cast<__m128i*>(dstRow + x + 4), p_hi);
    }
}

static inline void colorRowNv12Scale32SingleSse2(const ColorK& k, const uint8_t* yRow, const uint8_t* uvRow, uint32_t* dstRow) {
    const __m128i zero = _mm_setzero_si128();
    int dx = 0;
    for (int sx = 0; sx < 1280; sx += 8, dx += 12) {
        __m128i y_bytes = _mm_loadl_epi64(reinterpret_cast<const __m128i*>(yRow + sx));
        __m128i y16 = _mm_unpacklo_epi8(y_bytes, zero);
        __m128i u16, v16, p_lo, p_hi, out0, out1, out2;
        unpackNv12UvEight(uvRow + sx, u16, v16);
        colorRgbEight(k, y16, u16, v16, p_lo, p_hi);
        shuffleScale32(p_lo, p_hi, out0, out1, out2);
        _mm_stream_si128(reinterpret_cast<__m128i*>(dstRow + dx), out0);
        _mm_stream_si128(reinterpret_cast<__m128i*>(dstRow + dx + 4), out1);
        _mm_stream_si128(reinterpret_cast<__m128i*>(dstRow + dx + 8), out2);
    }
}

// Two destination rows from one source row (720p -> 1080p: rows 3m and 3m+1 both sample
// source row 2m): converted once, stored twice. It used to convert the row twice, 1080
// row conversions per frame instead of 720.
static inline void colorRowNv12Scale32DualSse2(const ColorK& k, const uint8_t* yRow, const uint8_t* uvRow,
                                               uint32_t* dstRow0, uint32_t* dstRow1) {
    const __m128i zero = _mm_setzero_si128();
    int dx = 0;
    for (int sx = 0; sx < 1280; sx += 8, dx += 12) {
        __m128i y_bytes = _mm_loadl_epi64(reinterpret_cast<const __m128i*>(yRow + sx));
        __m128i y16 = _mm_unpacklo_epi8(y_bytes, zero);
        __m128i u16, v16, p_lo, p_hi, out0, out1, out2;
        unpackNv12UvEight(uvRow + sx, u16, v16);
        colorRgbEight(k, y16, u16, v16, p_lo, p_hi);
        shuffleScale32(p_lo, p_hi, out0, out1, out2);
        _mm_stream_si128(reinterpret_cast<__m128i*>(dstRow0 + dx), out0);
        _mm_stream_si128(reinterpret_cast<__m128i*>(dstRow0 + dx + 4), out1);
        _mm_stream_si128(reinterpret_cast<__m128i*>(dstRow0 + dx + 8), out2);
        _mm_stream_si128(reinterpret_cast<__m128i*>(dstRow1 + dx), out0);
        _mm_stream_si128(reinterpret_cast<__m128i*>(dstRow1 + dx + 4), out1);
        _mm_stream_si128(reinterpret_cast<__m128i*>(dstRow1 + dx + 8), out2);
    }
}

static inline void colorRowYuv420Scale1to1Sse2(const ColorK& k, const uint8_t* yRow, const uint8_t* uRow, const uint8_t* vRow,
                                               uint32_t* dstRow) {
    const __m128i zero = _mm_setzero_si128();
    for (int x = 0; x < 1920; x += 8) {
        __m128i y_bytes = _mm_loadl_epi64(reinterpret_cast<const __m128i*>(yRow + x));
        __m128i y16 = _mm_unpacklo_epi8(y_bytes, zero);
        __m128i u16, v16, p_lo, p_hi;
        unpackYuv420UvEight(uRow + (x >> 1), vRow + (x >> 1), u16, v16);
        colorRgbEight(k, y16, u16, v16, p_lo, p_hi);
        _mm_stream_si128(reinterpret_cast<__m128i*>(dstRow + x), p_lo);
        _mm_stream_si128(reinterpret_cast<__m128i*>(dstRow + x + 4), p_hi);
    }
}

static inline void colorRowYuv420Scale32SingleSse2(const ColorK& k, const uint8_t* yRow, const uint8_t* uRow, const uint8_t* vRow,
                                                   uint32_t* dstRow) {
    const __m128i zero = _mm_setzero_si128();
    int dx = 0;
    for (int sx = 0; sx < 1280; sx += 8, dx += 12) {
        __m128i y_bytes = _mm_loadl_epi64(reinterpret_cast<const __m128i*>(yRow + sx));
        __m128i y16 = _mm_unpacklo_epi8(y_bytes, zero);
        __m128i u16, v16, p_lo, p_hi, out0, out1, out2;
        unpackYuv420UvEight(uRow + (sx >> 1), vRow + (sx >> 1), u16, v16);
        colorRgbEight(k, y16, u16, v16, p_lo, p_hi);
        shuffleScale32(p_lo, p_hi, out0, out1, out2);
        _mm_stream_si128(reinterpret_cast<__m128i*>(dstRow + dx), out0);
        _mm_stream_si128(reinterpret_cast<__m128i*>(dstRow + dx + 4), out1);
        _mm_stream_si128(reinterpret_cast<__m128i*>(dstRow + dx + 8), out2);
    }
}

static inline void colorRowYuv420Scale32DualSse2(const ColorK& k, const uint8_t* yRow, const uint8_t* uRow, const uint8_t* vRow,
                                                 uint32_t* dstRow0, uint32_t* dstRow1) {
    const __m128i zero = _mm_setzero_si128();
    int dx = 0;
    for (int sx = 0; sx < 1280; sx += 8, dx += 12) {
        __m128i y_bytes = _mm_loadl_epi64(reinterpret_cast<const __m128i*>(yRow + sx));
        __m128i y16 = _mm_unpacklo_epi8(y_bytes, zero);
        __m128i u16, v16, p_lo, p_hi, out0, out1, out2;
        unpackYuv420UvEight(uRow + (sx >> 1), vRow + (sx >> 1), u16, v16);
        colorRgbEight(k, y16, u16, v16, p_lo, p_hi);
        shuffleScale32(p_lo, p_hi, out0, out1, out2);
        _mm_stream_si128(reinterpret_cast<__m128i*>(dstRow0 + dx), out0);
        _mm_stream_si128(reinterpret_cast<__m128i*>(dstRow0 + dx + 4), out1);
        _mm_stream_si128(reinterpret_cast<__m128i*>(dstRow0 + dx + 8), out2);
        _mm_stream_si128(reinterpret_cast<__m128i*>(dstRow1 + dx), out0);
        _mm_stream_si128(reinterpret_cast<__m128i*>(dstRow1 + dx + 4), out1);
        _mm_stream_si128(reinterpret_cast<__m128i*>(dstRow1 + dx + 8), out2);
    }
}

// The scanout buffers are write-combined GPU memory: reading them back costs dozens of
// times a normal read, and the overlay blend used to read every blended pixel there
// (any HUD, menu or toast on screen cost several ms per frame). Rows under the overlay are
// now converted into cached memory, blended there and streamed out whole; the scanout is
// only ever written (XCloud4's rule too).
inline bool overlayCoversRow(const Overlay* ov, int dy) {
    return ov && dy >= ov->y && dy < ov->y + ov->h && ov->x < static_cast<int>(kWidth) && ov->x + ov->w > 0;
}

inline void blendOverlayRow(uint32_t* row, int dy, const Overlay& ov) {
    int ox0 = std::max(0, -ov.x);
    int ox1 = std::min(ov.w, static_cast<int>(kWidth) - ov.x);
    const uint32_t* sRow = ov.px.data() + (dy - ov.y) * ov.w;
    for (int ox = ox0; ox < ox1; ++ox) {
        uint32_t p = sRow[ox];
        uint32_t alpha = ((p >> 24) * ov.alpha) / 255;
        if (alpha == 0) continue;
        int px = ov.x + ox;
        if (alpha == 255) {
            row[px] = p;
            continue;
        }
        uint32_t inv = 255 - alpha;
        uint32_t bg = row[px];
        uint32_t r = (((p & 0xFF) * alpha) + ((bg & 0xFF) * inv)) / 255;
        uint32_t g = ((((p >> 8) & 0xFF) * alpha) + (((bg >> 8) & 0xFF) * inv)) / 255;
        uint32_t b = ((((p >> 16) & 0xFF) * alpha) + (((bg >> 16) & 0xFF) * inv)) / 255;
        row[px] = 0xFF000000u | (b << 16) | (g << 8) | r;
    }
}

inline void streamRow(uint32_t* dstRow, const uint32_t* srcRow) {
    const __m128i* src = reinterpret_cast<const __m128i*>(srcRow);
    __m128i* d = reinterpret_cast<__m128i*>(dstRow);
    for (size_t i = 0; i < (kWidth * sizeof(uint32_t)) / sizeof(__m128i); ++i) {
        _mm_stream_si128(d + i, _mm_load_si128(src + i));
    }
}

// Where a slice writes screen row dy: straight to the scanout, or into scratch row k when
// the overlay covers it; commit() then blends and streams the scratch row out.
struct RowTargets {
    uint32_t* dst;
    const Overlay* ov;
    alignas(16) uint32_t scratch[3][kWidth];
    uint32_t* at(int dy, int k) { return overlayCoversRow(ov, dy) ? scratch[k] : dst + dy * kWidth; }
    void commit(int dy, int k) {
        if (!overlayCoversRow(ov, dy)) return;
        blendOverlayRow(scratch[k], dy, *ov);
        streamRow(dst + dy * kWidth, scratch[k]);
    }
};

// The system expects sceGnmSubmitDone() regularly, also while nothing is drawn: without it
// a suspend (PS button, closing the app) can fault with CE-34878-0 (XCloud4 hit this too).
// pump() used to skip it whenever a frame was being drawn; now every caller gets through,
// serialised by a lock of its own that is never held across a draw.
std::mutex g_submitDoneMutex;
void submitDone() {
    std::lock_guard<std::mutex> lock(g_submitDoneMutex);
    sceGnmSubmitDone();
}

void pollFlipStatus() {
    if (g_videoHandle < 0 || g_flipMarker == 0) return;
    OrbisVideoOutFlipStatus status{};
    if (sceVideoOutGetFlipStatus(g_videoHandle, &status) != 0) return;

    if (status.currentBuffer >= 0 && status.currentBuffer < kBuffers) {
        g_onScreenBuffer = status.currentBuffer;
    }
    if (status.flipArg > 0) {
        uint64_t newId = static_cast<uint64_t>(status.flipArg);
        if (newId != g_shownId.load(std::memory_order_relaxed)) {
            uint64_t nowUs = platform::nowUs();
            uint64_t procNowUs = sceKernelGetProcessTime();
            const SubmittedFlip& sub = g_submittedFlips[newId % kSubmittedFlipRing];
            uint64_t shownUs = 0;
            if (sub.marker == status.flipArg && sub.submitUs > 0) {
                if (status.ptime > 0 && status.ptime >= sub.submitProcUs && status.ptime <= procNowUs) {
                    shownUs = sub.submitUs + (status.ptime - sub.submitProcUs);
                } else if (nowUs >= sub.submitUs && (nowUs - sub.submitUs) <= 50000ULL) {
                    shownUs = nowUs;
                }
            } else if (status.ptime > 0 && procNowUs >= status.ptime && (procNowUs - status.ptime) < 100000ULL) {
                uint64_t ageUs = procNowUs - status.ptime;
                if (nowUs > ageUs) shownUs = nowUs - ageUs;
            }
            g_shownAtUs.store(shownUs, std::memory_order_relaxed);
            g_shownId.store(newId, std::memory_order_release);
        }
        if (g_pendingBuffer >= 0 && status.flipArg >= g_pendingFlipMarker) {
            g_onScreenBuffer = g_pendingBuffer;
            g_pendingBuffer = -1;
        }
    }
    if (g_pendingBuffer >= 0 && status.numGpuFlipPending == 0 && status.numFlipPending == 0 &&
        status.num != g_lastFlipCount) {
        g_onScreenBuffer = g_pendingBuffer;
        g_pendingBuffer = -1;
    }
    g_lastFlipCount = status.num;
}

void waitPendingFlip() {
    if (g_videoHandle < 0 || g_pendingBuffer < 0) return;
    for (int i = 0; i < 200; ++i) {
        pollFlipStatus();
        if (g_pendingBuffer < 0) return;
        sceKernelUsleep(250);
    }
    // Timeout fallback (50 ms): promote pending buffer so we never deadlock
    g_onScreenBuffer = g_pendingBuffer;
    g_pendingBuffer = -1;
}

// Non-blocking backbuffer selection: with 3 buffers (1 on screen, at most 1 pending flip),
// there is always 1 free buffer ready for CPU conversion while the previous flip is in flight.
bool acquireBuffer(bool /*wait*/ = true) {
    if (g_videoHandle < 0) return false;
    pollFlipStatus();
    for (int i = 0; i < kBuffers; ++i) {
        int cand = (g_currentBuffer + 1 + i) % kBuffers;
        if (cand != g_onScreenBuffer && cand != g_pendingBuffer) {
            g_currentBuffer = cand;
            return true;
        }
    }
    g_currentBuffer = (g_onScreenBuffer + 1) % kBuffers;
    return true;
}

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
    bool isDirect = false;
    int rc = -1;
    size_t directMemSize = sceKernelGetDirectMemorySize();
    if (directMemSize >= kMemoryBytes) {
        rc = sceKernelAllocateDirectMemory(0, static_cast<int64_t>(directMemSize),
                                           kMemoryBytes, kMemoryAlignment, kMemoryTypeGarlic, &physAddr);
        if (rc == 0) {
            rc = sceKernelMapDirectMemory(&mapped, kMemoryBytes, kMapProtection, 0, physAddr, kMemoryAlignment);
            if (rc == 0 && mapped) {
                isDirect = true;
            } else {
                sceKernelReleaseDirectMemory(physAddr, kMemoryBytes);
                physAddr = 0;
                mapped = nullptr;
            }
        }
    }
    if (!isDirect || !mapped) {
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
    g_physAddr = physAddr;
    g_isDirectMemory = isDirect;
    g_videoHandle = handle;
    g_currentBuffer = 0;
    g_onScreenBuffer = 0;
    g_pendingBuffer = -1;
    g_flipMarker = 0;
    g_pendingFlipMarker = -1;
    g_shownId = 0;
    g_shownAtUs = 0;
    std::memset(g_submittedFlips, 0, sizeof(g_submittedFlips));

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

    // Submit initial cleared buffer 0 and notify GnmDriver
    int64_t initMarker = ++g_flipMarker;
    uint64_t subProcUs = sceKernelGetProcessTime();
    uint64_t subUs = platform::nowUs();
    if (sceVideoOutSubmitFlip(handle, 0, ORBIS_VIDEO_OUT_FLIP_VSYNC, initMarker) == 0) {
        g_submittedFlips[static_cast<uint64_t>(initMarker) % kSubmittedFlipRing] = {initMarker, subUs, subProcUs};
        g_pendingBuffer = 0;
        g_pendingFlipMarker = initMarker;
        waitPendingFlip();
    }
    submitDone();

    XC_LOGI("display: PS4 VideoOut initialized (3 linear 1080p buffers, handle %d, 3-stripe SIMD pool)", handle);
    return true;
}

void shutdown() {
    g_rowPool.reset();
    if (g_videoHandle >= 0) {
        waitPendingFlip();
        submitDone();
        sceVideoOutClose(g_videoHandle);
        g_videoHandle = -1;
    }
    if (g_mappedMemory) {
        sceKernelMunmap(g_mappedMemory, kMemoryBytes);
        g_mappedMemory = nullptr;
    }
    if (g_isDirectMemory && g_physAddr != 0) {
        sceKernelReleaseDirectMemory(g_physAddr, kMemoryBytes);
        g_physAddr = 0;
        g_isDirectMemory = false;
    }
    for (auto& b : g_buffers) b = nullptr;
}

std::recursive_mutex& frameMutex() { return g_frameMutex; }

void present() {
    if (g_videoHandle < 0) return;
    waitPendingFlip();
    int64_t marker = ++g_flipMarker;
    uint64_t subProcUs = sceKernelGetProcessTime();
    uint64_t subUs = platform::nowUs();
    const int flipMode = g_videoFlip ? ORBIS_VIDEO_OUT_FLIP_HSYNC : ORBIS_VIDEO_OUT_FLIP_VSYNC;
    int rc = sceVideoOutSubmitFlip(g_videoHandle, g_currentBuffer, flipMode, marker);
    if (rc != 0) {
        XC_LOGW("display: submit flip: 0x%08x", static_cast<unsigned>(rc));
    } else {
        g_submittedFlips[static_cast<uint64_t>(marker) % kSubmittedFlipRing] = {marker, subUs, subProcUs};
        g_pendingBuffer = g_currentBuffer;
        g_pendingFlipMarker = marker;
    }
    submitDone();
}

void drawRgba(const uint32_t* pixels) {
    if (!pixels || g_videoHandle < 0) return;
    g_videoFlip = false;
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

void pump() {
    {
        std::unique_lock<std::recursive_mutex> lock(g_frameMutex, std::try_to_lock);
        if (lock.owns_lock()) pollFlipStatus();
    }
    submitDone();
}

void idleWait(uint32_t ms) {
    platform::sleepMs(ms);
}

bool drawYuv420(const uint8_t* y, const uint8_t* u, const uint8_t* v,
                int strideY, int strideU, int strideV,
                int width, int height, bool wait) {
    int cw = (width + 1) / 2;
    if (g_videoHandle < 0 || !y || !u || !v || width <= 0 || height <= 0 ||
        strideY < width || strideU < cw || strideV < cw) return false;
    if (!acquireBuffer(wait)) return false;
    g_videoFlip = true;
    uint32_t* dst = reinterpret_cast<uint32_t*>(g_buffers[g_currentBuffer]);

    const bool is1080p = (width == 1920 && height == 1080);
    const bool is720p = (width == 1280 && height == 720);
    if (!is1080p && !is720p) {
        g_scratch.update(width, height);
    }

    std::shared_ptr<const Overlay> activeOverlay;
    {
        std::lock_guard<std::mutex> lock(g_overlayMutex);
        activeOverlay = g_overlay;
    }

    const ColorK k = makeColorK(g_colorSet.load(std::memory_order_relaxed));
    auto renderSlice = [y, u, v, strideY, strideU, strideV, is1080p, is720p, dst, &activeOverlay, &k](int yStart, int yEnd) {
        RowTargets rows;
        rows.dst = dst;
        rows.ov = activeOverlay.get();
        if (is1080p) {
            for (int dy = yStart; dy < yEnd; ++dy) {
                const uint8_t* yRow = y + dy * strideY;
                const uint8_t* uRow = u + (dy >> 1) * strideU;
                const uint8_t* vRow = v + (dy >> 1) * strideV;
                colorRowYuv420Scale1to1Sse2(k, yRow, uRow, vRow, rows.at(dy, 0));
                rows.commit(dy, 0);
            }
        } else if (is720p) {
            int dy = yStart;
            while (dy < yEnd) {
                if ((dy % 3) == 0 && dy + 2 < yEnd) {
                    int sy0 = (dy / 3) * 2;
                    int sy1 = sy0 + 1;
                    colorRowYuv420Scale32DualSse2(k, y + sy0 * strideY,
                                                  u + (sy0 >> 1) * strideU,
                                                  v + (sy0 >> 1) * strideV,
                                                  rows.at(dy, 0),
                                                  rows.at(dy + 1, 1));
                    colorRowYuv420Scale32SingleSse2(k, y + sy1 * strideY,
                                                    u + (sy1 >> 1) * strideU,
                                                    v + (sy1 >> 1) * strideV,
                                                    rows.at(dy + 2, 2));
                    rows.commit(dy, 0);
                    rows.commit(dy + 1, 1);
                    rows.commit(dy + 2, 2);
                    dy += 3;
                } else {
                    int sy = (dy * 720) / 1080;
                    colorRowYuv420Scale32SingleSse2(k, y + sy * strideY,
                                                    u + (sy >> 1) * strideU,
                                                    v + (sy >> 1) * strideV,
                                                    rows.at(dy, 0));
                    rows.commit(dy, 0);
                    ++dy;
                }
            }
        } else {
            const auto& mapX = g_scratch.mapX;
            const auto& mapY = g_scratch.mapY;
            for (int dy = yStart; dy < yEnd; ++dy) {
                int sy = mapY[dy];
                const uint8_t* yRow = y + sy * strideY;
                const uint8_t* uRow = u + (sy >> 1) * strideU;
                const uint8_t* vRow = v + (sy >> 1) * strideV;
                uint32_t* dRow = rows.at(dy, 0);
                for (int dx = 0; dx < static_cast<int>(kWidth); dx += 8) {
                    __m128i y16 = _mm_setr_epi16(
                        yRow[mapX[dx + 0]], yRow[mapX[dx + 1]], yRow[mapX[dx + 2]], yRow[mapX[dx + 3]],
                        yRow[mapX[dx + 4]], yRow[mapX[dx + 5]], yRow[mapX[dx + 6]], yRow[mapX[dx + 7]]);
                    __m128i u16 = _mm_setr_epi16(
                        uRow[mapX[dx + 0] >> 1], uRow[mapX[dx + 1] >> 1], uRow[mapX[dx + 2] >> 1], uRow[mapX[dx + 3] >> 1],
                        uRow[mapX[dx + 4] >> 1], uRow[mapX[dx + 5] >> 1], uRow[mapX[dx + 6] >> 1], uRow[mapX[dx + 7] >> 1]);
                    __m128i v16 = _mm_setr_epi16(
                        vRow[mapX[dx + 0] >> 1], vRow[mapX[dx + 1] >> 1], vRow[mapX[dx + 2] >> 1], vRow[mapX[dx + 3] >> 1],
                        vRow[mapX[dx + 4] >> 1], vRow[mapX[dx + 5] >> 1], vRow[mapX[dx + 6] >> 1], vRow[mapX[dx + 7] >> 1]);
                    __m128i p_lo, p_hi;
                    colorRgbEight(k, y16, u16, v16, p_lo, p_hi);
                    _mm_stream_si128(reinterpret_cast<__m128i*>(dRow + dx), p_lo);
                    _mm_stream_si128(reinterpret_cast<__m128i*>(dRow + dx + 4), p_hi);
                }
                rows.commit(dy, 0);
            }
        }
        _mm_sfence();
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
    if (g_videoHandle < 0 || !y || !uv || width <= 0 || height <= 0 ||
        strideY < width || strideUV < width) return false;
    if (!acquireBuffer(wait)) return false;
    g_videoFlip = true;
    uint32_t* dst = reinterpret_cast<uint32_t*>(g_buffers[g_currentBuffer]);

    const bool is1080p = (width == 1920 && height == 1080);
    const bool is720p = (width == 1280 && height == 720);
    if (!is1080p && !is720p) {
        g_scratch.update(width, height);
    }

    std::shared_ptr<const Overlay> activeOverlay;
    {
        std::lock_guard<std::mutex> lock(g_overlayMutex);
        activeOverlay = g_overlay;
    }

    const ColorK k = makeColorK(g_colorSet.load(std::memory_order_relaxed));
    auto renderSlice = [y, uv, strideY, strideUV, is1080p, is720p, dst, &activeOverlay, &k](int yStart, int yEnd) {
        RowTargets rows;
        rows.dst = dst;
        rows.ov = activeOverlay.get();
        if (is1080p) {
            for (int dy = yStart; dy < yEnd; ++dy) {
                const uint8_t* yRow = y + dy * strideY;
                const uint8_t* uvRow = uv + (dy >> 1) * strideUV;
                colorRowNv12Scale1to1Sse2(k, yRow, uvRow, rows.at(dy, 0));
                rows.commit(dy, 0);
            }
        } else if (is720p) {
            int dy = yStart;
            while (dy < yEnd) {
                // Both dy=3*m and dy=3*m+1 sample sy=2*m: convert once and stream to both rows!
                if ((dy % 3) == 0 && dy + 2 < yEnd) {
                    int sy0 = (dy / 3) * 2;
                    int sy1 = sy0 + 1;
                    colorRowNv12Scale32DualSse2(k, y + sy0 * strideY,
                                                uv + (sy0 >> 1) * strideUV,
                                                rows.at(dy, 0),
                                                rows.at(dy + 1, 1));
                    colorRowNv12Scale32SingleSse2(k, y + sy1 * strideY,
                                                  uv + (sy1 >> 1) * strideUV,
                                                  rows.at(dy + 2, 2));
                    rows.commit(dy, 0);
                    rows.commit(dy + 1, 1);
                    rows.commit(dy + 2, 2);
                    dy += 3;
                } else {
                    int sy = (dy * 720) / 1080;
                    colorRowNv12Scale32SingleSse2(k, y + sy * strideY,
                                                  uv + (sy >> 1) * strideUV,
                                                  rows.at(dy, 0));
                    rows.commit(dy, 0);
                    ++dy;
                }
            }
        } else {
            const auto& mapX = g_scratch.mapX;
            const auto& mapY = g_scratch.mapY;
            for (int dy = yStart; dy < yEnd; ++dy) {
                int sy = mapY[dy];
                const uint8_t* yRow = y + sy * strideY;
                const uint8_t* uvRow = uv + (sy >> 1) * strideUV;
                uint32_t* dRow = rows.at(dy, 0);
                for (int dx = 0; dx < static_cast<int>(kWidth); dx += 8) {
                    int c0 = (mapX[dx + 0] >> 1) * 2;
                    int c1 = (mapX[dx + 1] >> 1) * 2;
                    int c2 = (mapX[dx + 2] >> 1) * 2;
                    int c3 = (mapX[dx + 3] >> 1) * 2;
                    int c4 = (mapX[dx + 4] >> 1) * 2;
                    int c5 = (mapX[dx + 5] >> 1) * 2;
                    int c6 = (mapX[dx + 6] >> 1) * 2;
                    int c7 = (mapX[dx + 7] >> 1) * 2;
                    __m128i y16 = _mm_setr_epi16(
                        yRow[mapX[dx + 0]], yRow[mapX[dx + 1]], yRow[mapX[dx + 2]], yRow[mapX[dx + 3]],
                        yRow[mapX[dx + 4]], yRow[mapX[dx + 5]], yRow[mapX[dx + 6]], yRow[mapX[dx + 7]]);
                    __m128i u16 = _mm_setr_epi16(
                        uvRow[c0], uvRow[c1], uvRow[c2], uvRow[c3],
                        uvRow[c4], uvRow[c5], uvRow[c6], uvRow[c7]);
                    __m128i v16 = _mm_setr_epi16(
                        uvRow[c0 + 1], uvRow[c1 + 1], uvRow[c2 + 1], uvRow[c3 + 1],
                        uvRow[c4 + 1], uvRow[c5 + 1], uvRow[c6 + 1], uvRow[c7 + 1]);
                    __m128i p_lo, p_hi;
                    colorRgbEight(k, y16, u16, v16, p_lo, p_hi);
                    _mm_stream_si128(reinterpret_cast<__m128i*>(dRow + dx), p_lo);
                    _mm_stream_si128(reinterpret_cast<__m128i*>(dRow + dx + 4), p_hi);
                }
                rows.commit(dy, 0);
            }
        }
        _mm_sfence();
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
    auto overlay = std::make_shared<Overlay>();
    overlay->x = x;
    overlay->y = y;
    overlay->w = w;
    overlay->h = h;
    overlay->alpha = opacity;
    overlay->px.assign(pixels, pixels + static_cast<size_t>(w) * h);
    g_overlay = std::move(overlay);
}

void setSharpness(int amount) { g_sharpness = std::clamp(amount, 0, 256); }

namespace ps4 {
void setVideoColor(bool bt601, bool fullRange) {
    g_colorSet.store((bt601 ? 1 : 0) + (fullRange ? 2 : 0), std::memory_order_relaxed);
}
}  // namespace ps4
void setDeband(int) {}
void setUpscaler(int) {}
void setRestore(bool) {}
DisplayCaps caps() {
    DisplayCaps c{};
    c.asyncPresent = true;
    // XCloud4's measured 1080p60 on a base PS4 (about 58 fps) was at 7-13 Mbps; past that
    // the hardware decoder falls behind. The user's own cap (Settings) still goes higher.
    c.defaultMaxBitrate = 13000000;
    // XCloud4's limit: past 250 ms of backlog, drop it and wait for an IDR.
    c.maxVideoLagMs = 250;
    return c;
}
void setSwapImages(int) {}
void setPresentWait(bool) {}
bool canSwitchRefresh() { return false; }
bool setHighRefresh(bool) { return false; }
bool bringUpTest(int) { return false; }
uint64_t lastPresentId() {
    std::lock_guard<std::recursive_mutex> lock(g_frameMutex);
    return static_cast<uint64_t>(g_flipMarker);
}
bool lastShown(uint64_t& id, uint64_t& atUs) {
    std::lock_guard<std::recursive_mutex> lock(g_frameMutex);
    if (g_videoHandle < 0) return false;
    pollFlipStatus();
    uint64_t sid = g_shownId.load(std::memory_order_acquire);
    uint64_t sat = g_shownAtUs.load(std::memory_order_relaxed);
    if (sid > 0 && sat > 0) {
        id = sid;
        atUs = sat;
        return true;
    }
    return false;
}
bool readBackRgb(std::vector<uint8_t>& rgb, int& width, int& height) {
    try {
        std::lock_guard<std::recursive_mutex> lock(g_frameMutex);
        pollFlipStatus();
        int bufIdx = (g_onScreenBuffer >= 0 && g_onScreenBuffer < kBuffers) ? g_onScreenBuffer : g_currentBuffer;
        if (!g_buffers[bufIdx]) return false;
        width = kWidth;
        height = kHeight;
        const uint32_t* base = reinterpret_cast<const uint32_t*>(g_buffers[bufIdx]);
        rgb.resize(static_cast<size_t>(kWidth) * kHeight * 3);
        for (size_t i = 0; i < static_cast<size_t>(kWidth) * kHeight; ++i) {
            uint32_t p = base[i];
            rgb[i * 3 + 0] = static_cast<uint8_t>(p);
            rgb[i * 3 + 1] = static_cast<uint8_t>(p >> 8);
            rgb[i * 3 + 2] = static_cast<uint8_t>(p >> 16);
        }
        return true;
    } catch (const std::exception& e) {
        XC_LOGW("display: readBackRgb failed: %s", e.what());
        return false;
    }
}

} // namespace xc::display
