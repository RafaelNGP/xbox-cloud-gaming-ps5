// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// The picture on the GPU: RADV (PS5_Vulkan's Mesa port, linked into the
// eboot) presenting to the 3840x2160 display through VK_KHR_display.
//
// Each frame, the video picture (YUV 4:2:0, any size) or the menus' canvas
// (1920x1080 RGBA) is upscaled to the display with AMD FidelityFX FSR 1:
// EASU, then RCAS sharpening, with the in-game menu laid over. Same calls as
// display.h, which forwards to these once init() succeeded.
#pragma once

#include <cstdint>
#include <vector>

namespace xc::display::gpu {

// Instance, device, display surface, swapchain and the FSR pipelines. False,
// with the step that failed in the log, when the console won't give them:
// the CPU display is used instead.
bool init();
bool ready();

// Bring-up check: `frames` frames cleared to changing colours and presented,
// with how long they took against the display's refresh.
void probe(int frames);

// One frame, then present(). False (nothing drawn) when, without `wait`, no
// swapchain image is free yet.
bool drawYuv420(const uint8_t* y, const uint8_t* u, const uint8_t* v, int strideY, int strideU, int strideV,
                int width, int height, bool wait);
void drawRgba(const uint32_t* pixels);  // 1920x1080, 0xAABBGGRR
void present();

// As display::setOverlay / setSharpness.
void setOverlay(const uint32_t* pixels, int x, int y, int w, int h, uint8_t opacity);
void setSharpness(int amount);
// Smoothing of compression blocks in flat areas: 0 off, 1 low, 2 high.
void setDeband(int level);

// The image last presented, 8-bit RGB rows; false when there is none.
bool readBack(std::vector<uint8_t>& rgb, int& width, int& height);

}  // namespace xc::display::gpu
