// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "ui/accent_color.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace xc::ui {

bool dominantColor(const Image& img, Color& out) {
    if (img.w <= 0 || img.h <= 0 || img.px.empty()) return false;
    // Hues in 24 bins, each pixel weighted by how saturated and bright it is:
    // a poster's big dark background counts for little, its colour for a lot.
    constexpr int kBins = 24;
    std::array<double, kBins> weight{};
    std::array<std::array<double, 3>, kBins> sum{};
    const int step = std::max(1, std::min(img.w, img.h) / 48);
    for (int y = 0; y < img.h; y += step)
        for (int x = 0; x < img.w; x += step) {
            uint32_t p = img.px[static_cast<size_t>(y) * img.w + x];
            if ((p >> 24) < 128) continue;  // transparent
            double r = (p & 0xFF) / 255.0, g = ((p >> 8) & 0xFF) / 255.0, b = ((p >> 16) & 0xFF) / 255.0;
            double mx = std::max({r, g, b}), mn = std::min({r, g, b});
            double sat = mx > 0 ? (mx - mn) / mx : 0;
            if (sat < 0.25 || mx < 0.2) continue;
            double hue;
            double d = mx - mn;
            if (mx == r) hue = std::fmod((g - b) / d + 6.0, 6.0);
            else if (mx == g) hue = (b - r) / d + 2.0;
            else hue = (r - g) / d + 4.0;
            int bin = std::min(kBins - 1, static_cast<int>(hue / 6.0 * kBins));
            double w = sat * sat * mx;
            weight[bin] += w;
            sum[bin][0] += r * w;
            sum[bin][1] += g * w;
            sum[bin][2] += b * w;
        }
    int best = static_cast<int>(std::max_element(weight.begin(), weight.end()) - weight.begin());
    // Too little colour overall (a black and white poster): no answer.
    double total = 0;
    for (double w : weight) total += w;
    int samples = ((img.w + step - 1) / step) * ((img.h + step - 1) / step);
    if (weight[best] <= 0 || total < samples * 0.01) return false;
    double r = sum[best][0] / weight[best], g = sum[best][1] / weight[best], b = sum[best][2] / weight[best];
    // An LED shows hue, not shade: strongest channel at full, and the weakest
    // pulled down so it doesn't wash out to white.
    double mx = std::max({r, g, b}), mn = std::min({r, g, b});
    auto shape = [&](double c) {
        double t = mx > mn ? (c - mn) / (mx - mn) : 1.0;  // 0 for the weakest, 1 for the strongest
        return static_cast<uint8_t>(std::lround(255.0 * (0.15 + 0.85 * t)));
    };
    out = rgba(shape(r), shape(g), shape(b));
    return true;
}

}  // namespace xc::ui
