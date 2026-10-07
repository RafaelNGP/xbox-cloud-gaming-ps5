// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#pragma once

#include <cstdint>
#include <cstddef>
#include <mutex>

namespace xc::display {

constexpr unsigned kWidth = 1920;
constexpr unsigned kHeight = 1080;

bool init();
void shutdown();
// Held by whoever draws a frame (UI thread or video thread).
std::mutex& frameMutex();
void present();

// Copies a linear 1920x1080 RGBA (0xAABBGGRR) image to the back buffer
// (multi-threaded). Call present() afterwards.
void drawRgba(const uint32_t* pixels);

// Converts a BT.709 limited-range YUV 4:2:0 picture to the back buffer,
// scaled to the full screen (multi-threaded). Call present() afterwards.
void drawYuv420(const uint8_t* y, const uint8_t* u, const uint8_t* v, int strideY, int strideU, int strideV,
                int width, int height);

} // namespace xc::display
