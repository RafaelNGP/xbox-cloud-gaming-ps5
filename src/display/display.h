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
// and it comes up, else the CPU path below.
bool init(bool preferGpu = true);
void shutdown();
// Held by whoever draws a frame (UI thread or video thread).
std::mutex& frameMutex();
void present();

// Copies a linear 1920x1080 RGBA (0xAABBGGRR) image to the back buffer
// (multi-threaded). Call present() afterwards.
void drawRgba(const uint32_t* pixels);

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

// GPU only: the id of the last present(), and the last frame known to be on
// the screen (its id, and platform::nowUs() then). False on the CPU path.
uint64_t lastPresentId();
bool lastShown(uint64_t& id, uint64_t& atUs);

// The back buffer last drawn, untiled, as 8-bit RGB rows (diagnostics:
// what the TV shows), and its size. False without a display.
bool readBackRgb(std::vector<uint8_t>& rgb, int& width, int& height);

} // namespace xc::display
