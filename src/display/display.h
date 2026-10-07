#pragma once

#include <cstdint>
#include <cstddef>
#include <mutex>
#include <string_view>

namespace xc::display {

constexpr unsigned kWidth = 1920;
constexpr unsigned kHeight = 1080;

// 0xAABBGGRR: the scan-out buffer is R, G, B, A in memory.
enum class Color : uint32_t {
    Black       = 0xFF000000,
    DarkSlate   = 0xFF201914,
    HeaderBar   = 0xFF30251E,
    CardBg      = 0xFF423228,
    CardHover   = 0xFF634C3D,
    CardSelected= 0xFF107C10, // Xbox Green
    White       = 0xFFFFFFFF,
    XboxGreen   = 0xFF107C10,
    LightGreen  = 0xFF71CC2E,
    GrayText    = 0xFFB8AAA0,
    LightGray   = 0xFFE0E0E0
};

class Canvas {
public:
    explicit Canvas(uint32_t* pixels) : pixels_(pixels) {}

    void clear(Color color = Color::DarkSlate);
    void fillRect(unsigned x, unsigned y, unsigned w, unsigned h, Color color);
    void drawRect(unsigned x, unsigned y, unsigned w, unsigned h, Color color, unsigned thickness = 2);
    void drawText(unsigned x, unsigned y, std::string_view text, unsigned scale = 2, Color color = Color::White);

    uint32_t* pixels() const { return pixels_; }

private:
    uint32_t* pixels_;
};

bool init();
void shutdown();
// Held by whoever draws a frame (UI thread or video thread).
std::mutex& frameMutex();
Canvas getBackBuffer();
void present();

// Converts a BT.709 limited-range YUV 4:2:0 picture to the back buffer,
// scaled to the full screen (multi-threaded). Call present() afterwards.
void drawYuv420(const uint8_t* y, const uint8_t* u, const uint8_t* v, int strideY, int strideU, int strideV,
                int width, int height);

} // namespace xc::display
