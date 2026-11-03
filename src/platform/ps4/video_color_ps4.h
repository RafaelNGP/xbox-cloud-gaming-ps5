// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// PS4 only: the colour matrix and range of the stream (from its SPS, read by
// hw_decoder_ps4.cpp), used by display_ps4.cpp's YUV -> RGB conversion.
#pragma once

namespace xc::display::ps4 {

// BT.709 limited range until a stream says otherwise.
void setVideoColor(bool bt601, bool fullRange);

}  // namespace xc::display::ps4
