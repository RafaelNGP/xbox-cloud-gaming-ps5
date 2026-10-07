// Software 2D drawing into a linear RGBA buffer, for the menus. Pixels are
// uint32_t 0xAABBGGRR (bytes R, G, B, A in memory), the scan-out format of
// the PS5 and of stb_image's RGBA output. The canvas itself is opaque.
#pragma once

#include <cstdint>
#include <vector>

namespace xc::ui {

using Color = uint32_t;

constexpr Color rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
    return (static_cast<uint32_t>(a) << 24) | (static_cast<uint32_t>(b) << 16) | (static_cast<uint32_t>(g) << 8) | r;
}
constexpr uint8_t alphaOf(Color c) { return static_cast<uint8_t>(c >> 24); }
constexpr Color withAlpha(Color c, uint8_t a) { return (c & 0x00FFFFFFu) | (static_cast<uint32_t>(a) << 24); }

struct Image {
    int w = 0, h = 0;
    std::vector<uint32_t> px;  // straight (not premultiplied) alpha
    size_t bytes() const { return px.size() * 4; }
};

struct Rect {
    int x = 0, y = 0, w = 0, h = 0;
};

class Canvas {
public:
    Canvas(int w, int h) : w_(w), h_(h), px_(static_cast<size_t>(w) * h, rgba(0, 0, 0)) { resetClip(); }

    int width() const { return w_; }
    int height() const { return h_; }
    const uint32_t* data() const { return px_.data(); }
    uint32_t* data() { return px_.data(); }

    void setClip(Rect r);
    void resetClip() { clip_ = {0, 0, w_, h_}; }

    void clear(Color c);
    // Alpha-blended, antialiased rounded corners when radius > 0.
    void fillRect(Rect r, Color c, int radius = 0);
    void strokeRect(Rect r, Color c, int thickness, int radius = 0);
    void gradientV(Rect r, Color top, Color bottom);
    void gradientH(Rect r, Color left, Color right);
    void fillCircle(float cx, float cy, float radius, Color c);
    // Ring segment; angles in radians, 0 = 3 o'clock, clockwise.
    void strokeArc(float cx, float cy, float radius, float thickness, float start, float sweep, Color c);
    void line(float x0, float y0, float x1, float y1, float thickness, Color c);
    // 1:1 copy (no scaling) with opacity and optional rounded corners;
    // radius < 0 clips to a circle (avatars).
    void drawImage(const Image& img, int x, int y, uint8_t opacity = 255, int radius = 0);
    // Blends a single colour through an 8-bit coverage mask (text, QR).
    void drawMask(const uint8_t* mask, int mw, int mh, int stride, int x, int y, Color c);

private:
    void blend(uint32_t& dst, Color src, uint32_t alpha /*0..255*/) const;
    // Coverage (0..255) of a rounded rectangle at pixel (px, py).
    static uint32_t cornerCoverage(Rect r, int radius, int px, int py);

    int w_, h_;
    std::vector<uint32_t> px_;
    Rect clip_;
};

}  // namespace xc::ui
