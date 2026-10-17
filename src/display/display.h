// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#pragma once

#include <cstdint>
#include <cstddef>
#include <mutex>
#include <vector>

namespace xc::display {

constexpr unsigned kWidth = 1920;
constexpr unsigned kHeight = 1080;

// The GPU (display/gpu.h: FSR upscaling to the 4K display) when `preferGpu`
// and it comes up, else the CPU path below. The backend is picked at link
// time: display_ps5.cpp on the console, display_sdl.cpp in the desktop app,
// display_null.cpp (no screen) for psbox-headless and the tests.
bool init(bool preferGpu = true);
void shutdown();
// Held by whoever draws a frame (UI thread or video thread).
std::mutex& frameMutex();
void present();

// Copies a linear 1920x1080 RGBA (0xAABBGGRR) image to the back buffer
// (multi-threaded). Call present() afterwards.
void drawRgba(const uint32_t* pixels);

// Each pass of the main loop, on the menus and while streaming (main
// thread). Where the frames are drawn by the main thread alone (the
// desktop's SDL_Renderer), the newest one present() handed over (the video
// thread's, with its overlay, or the menu) is uploaded and shown here. A
// no-op where present() shows the frame itself (PS5, null).
void pump();
// The streaming loop's pause between passes (~120 Hz input): `ms`, as the
// PS5 sleeps; the desktop wakes early for a new video frame or an input
// event.
void idleWait(uint32_t ms);

// Converts a BT.709 limited-range YUV 4:2:0 picture to the back buffer,
// scaled to the full screen (multi-threaded). Call present() afterwards.
// With two flips still queued it waits for a vblank, or, without `wait`,
// returns false and draws nothing.
bool drawYuv420(const uint8_t* y, const uint8_t* u, const uint8_t* v, int strideY, int strideU, int strideV,
                int width, int height, bool wait);

// A picture laid over every video frame drawYuv420() draws from now on (the
// in-game menu, the statistics): `w` x `h` RGBA pixels at (x, y), blended at
// `opacity`. Null `pixels` removes it. Any thread.
void setOverlay(const uint32_t* pixels, int x, int y, int w, int h, uint8_t opacity);

// Sharpening of the video (drawYuv420), 0 = off .. 256 = full CAS.
void setSharpness(int amount);
// Smoothing of compression blocks in flat areas (GPU only): 0 off, 1 low, 2 high.
void setDeband(int level);
// GPU only: 0 FSR 1, 1 Anime4K (gpu::setUpscaler).
void setUpscaler(int mode);
// Anime4K Restore before the upscale (GPU only).
void setRestore(bool on);

// What this display can do, as of now (the GPU's part once init() has run):
// an option it can't honour is hidden or has no effect.
struct DisplayCaps {
    bool upscalers = false;          // setUpscaler / setRestore
    bool deband = false;             // setDeband
    bool sharpen = false;            // setSharpness
    bool highRefreshSwitch = false;  // setHighRefresh
    bool presentTiming = false;      // lastPresentId / lastShown
};
DisplayCaps caps();

// Before init(): swapchain images (GPU only; 2 = lowest latency, the default).
void setSwapImages(int count);
// Before init(): wait for each frame to reach the screen before the next
// (GPU only, on by default).
void setPresentWait(bool on);
// 119.88 Hz output on, or back to 59.94 Hz, while running (a game's stream
// only); false when the display can't switch or was refused.
bool setHighRefresh(bool on);
// Whether setHighRefresh() can work at all; known before init().
bool canSwitchRefresh();
// Bring-up check instead of init(): the GPU alone, then `frames` test frames
// against the refresh (autoplay "vktest"). False when it didn't come up.
bool bringUpTest(int frames);

// GPU only: the id of the last present(), and the last frame known to be on
// the screen (its id, and platform::nowUs() then). False on the CPU path.
uint64_t lastPresentId();
bool lastShown(uint64_t& id, uint64_t& atUs);

// The back buffer last drawn, untiled, as 8-bit RGB rows (diagnostics:
// what the TV shows), and its size. False without a display.
bool readBackRgb(std::vector<uint8_t>& rgb, int& width, int& height);

} // namespace xc::display
