// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// H.264 on the PS5's hardware video decoder (libSceVideodec2, the system's
// own: RADV offers no Vulkan Video on the console). PS5 build only.
// Setup after BlackBearReloaded's ProsperoLight (GPL-3.0-or-later).
#pragma once

#include <cstddef>
#include <cstdint>

namespace xc::media {

class HwDecoder {
public:
    // A decoded picture, as the decoder left it in its frame buffer.
    struct Picture {
        const uint8_t* data = nullptr;
        size_t size = 0;
        uint32_t width = 0, height = 0, pitch = 0, pitchBytes = 0, format = 0;
        uint64_t pts = 0;
        bool error = false;
    };

    ~HwDecoder();
    // Loads the module and makes a decoder for pictures up to the size.
    bool init(int maxWidth, int maxHeight);
    bool ready() const { return decoder_ != nullptr; }
    // One access unit in; true with `out` when a picture came out.
    bool decode(const uint8_t* data, size_t len, uint64_t pts, Picture& out);

private:
    void* decoder_ = nullptr;
    void* computeQueue_ = nullptr;
    uint8_t* frameBuffer_ = nullptr;
    size_t frameBufferSize_ = 0;
};

}  // namespace xc::media
