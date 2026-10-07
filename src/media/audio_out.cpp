#include "media/audio_out.h"

#include "util/log.h"

#include <atomic>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

#if defined(XCLOUD_PS5)
extern "C" {
int sceAudioOutInit(void);
int sceAudioOutOpen(int userId, int type, int index, unsigned len, unsigned freq, unsigned param);
int sceAudioOutOutput(int handle, const void* ptr);
int sceAudioOutClose(int handle);
}
#endif

namespace xc::media {

namespace {

constexpr int kRate = 48000;
constexpr unsigned kGrain = 256;
constexpr size_t kMaxQueued = kRate * 120 / 1000;

std::mutex g_mutex;
std::deque<float> g_queue;  // interleaved stereo
std::atomic<bool> g_running{false};
std::thread g_thread;

#if defined(XCLOUD_PS5)
int g_handle = -1;

void loop() {
    std::vector<float> grain(kGrain * 2);
    while (g_running) {
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            size_t n = std::min(grain.size(), g_queue.size());
            std::copy(g_queue.begin(), g_queue.begin() + static_cast<long>(n), grain.begin());
            g_queue.erase(g_queue.begin(), g_queue.begin() + static_cast<long>(n));
            std::fill(grain.begin() + static_cast<long>(n), grain.end(), 0.0f);
        }
        // Blocks until the hardware wants the next grain (5.3 ms).
        int rc = sceAudioOutOutput(g_handle, grain.data());
        if (rc < 0) {
            XC_LOGE("sceAudioOutOutput: 0x%08x", static_cast<unsigned>(rc));
            break;
        }
    }
}
#endif

}  // namespace

bool audioStart() {
    if (g_running) return true;
#if defined(XCLOUD_PS5)
    int rc = sceAudioOutInit();
    if (rc < 0 && rc != static_cast<int>(0x8026000C)) {  // already initialised
        XC_LOGE("sceAudioOutInit: 0x%08x", static_cast<unsigned>(rc));
        return false;
    }
    // The system user (0xFF), MAIN port, float stereo: as the WoW-PS5 port.
    g_handle = sceAudioOutOpen(0xFF, 0, 0, kGrain, kRate, 4);
    if (g_handle < 0) {
        XC_LOGE("sceAudioOutOpen: 0x%08x", static_cast<unsigned>(g_handle));
        return false;
    }
    g_running = true;
    g_thread = std::thread(loop);
    XC_LOGI("audio output open (48 kHz float stereo)");
#else
    g_running = true;
#endif
    return true;
}

void audioStop() {
    if (!g_running) return;
    g_running = false;
    if (g_thread.joinable()) g_thread.join();
#if defined(XCLOUD_PS5)
    if (g_handle >= 0) {
        sceAudioOutClose(g_handle);
        g_handle = -1;
    }
#endif
    std::lock_guard<std::mutex> lock(g_mutex);
    g_queue.clear();
}

void audioPush(const float* interleaved, size_t frames) {
    if (!g_running) return;
    std::lock_guard<std::mutex> lock(g_mutex);
    g_queue.insert(g_queue.end(), interleaved, interleaved + frames * 2);
    if (g_queue.size() > kMaxQueued * 2)
        g_queue.erase(g_queue.begin(), g_queue.begin() + static_cast<long>(g_queue.size() - kMaxQueued * 2));
}

}  // namespace xc::media
