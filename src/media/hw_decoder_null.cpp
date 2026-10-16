// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// No hardware decoder (anywhere but the PS5): init() fails, FFmpeg decodes.
#include "media/hw_decoder.h"

namespace xc::media {

HwDecoder::~HwDecoder() = default;
bool HwDecoder::init(int, int) { return false; }
bool HwDecoder::decode(const uint8_t*, size_t, uint64_t, Picture&) { return false; }

}  // namespace xc::media
