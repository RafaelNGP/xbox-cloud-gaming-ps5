// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PSBox Cloud Gaming contributors
#include "ui/font.h"

#include "platform/platform.h"
#include "util/log.h"

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb_truetype.h"

#include <cmath>

namespace xc::ui {

struct Font::Impl {
    stbtt_fontinfo info{};
    int ascent = 0, descent = 0, lineGap = 0;
};

Font::Font() : impl_(std::make_unique<Impl>()) {}
Font::~Font() = default;

bool Font::load(const std::string& path) {
    std::string bytes;
    if (!platform::readFile(path, bytes)) {
        XC_LOGE("font %s: cannot read", path.c_str());
        return false;
    }
    data_.assign(bytes.begin(), bytes.end());
    if (!stbtt_InitFont(&impl_->info, data_.data(), stbtt_GetFontOffsetForIndex(data_.data(), 0))) {
        XC_LOGE("font %s: not a TrueType font", path.c_str());
        return false;
    }
    stbtt_GetFontVMetrics(&impl_->info, &impl_->ascent, &impl_->descent, &impl_->lineGap);
    return true;
}

float Font::scale(int px) const { return stbtt_ScaleForPixelHeight(&impl_->info, static_cast<float>(px)); }

int Font::lineHeight(int px) const {
    return static_cast<int>(std::ceil((impl_->ascent - impl_->descent) * scale(px)));
}

uint32_t nextCodepoint(std::string_view s, size_t& i) {
    auto c = static_cast<unsigned char>(s[i++]);
    if (c < 0x80) return c;
    int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : -1;
    if (extra < 0) return 0xFFFD;
    uint32_t cp = c & (0x3F >> extra);
    for (int k = 0; k < extra; ++k) {
        if (i >= s.size() || (static_cast<unsigned char>(s[i]) & 0xC0) != 0x80) return 0xFFFD;
        cp = (cp << 6) | (static_cast<unsigned char>(s[i++]) & 0x3F);
    }
    return cp;
}

const Font::Glyph& Font::glyph(uint32_t cp, int px) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto key = std::make_pair(cp, px);
    auto it = cache_.find(key);
    if (it != cache_.end()) return it->second;
    Glyph g;
    // Characters the font lacks are skipped instead of drawn as boxes.
    if (cp != ' ' && stbtt_FindGlyphIndex(&impl_->info, static_cast<int>(cp)) == 0)
        return cache_.emplace(key, std::move(g)).first->second;
    float s = scale(px);
    int advance, lsb;
    stbtt_GetCodepointHMetrics(&impl_->info, static_cast<int>(cp), &advance, &lsb);
    g.advance = static_cast<int>(std::lround(advance * s));
    unsigned char* bmp =
        stbtt_GetCodepointBitmap(&impl_->info, 0, s, static_cast<int>(cp), &g.w, &g.h, &g.xoff, &g.yoff);
    if (bmp) {
        g.bitmap.assign(bmp, bmp + static_cast<size_t>(g.w) * g.h);
        stbtt_FreeBitmap(bmp, nullptr);
    }
    return cache_.emplace(key, std::move(g)).first->second;
}

int Font::measure(std::string_view text, int px) const {
    int w = 0;
    for (size_t i = 0; i < text.size();) w += glyph(nextCodepoint(text, i), px).advance;
    return w;
}

int Font::draw(Canvas& c, std::string_view text, int x, int y, int px, Color color) const {
    int baseline = y + static_cast<int>(std::lround(impl_->ascent * scale(px)));
    for (size_t i = 0; i < text.size();) {
        const Glyph& g = glyph(nextCodepoint(text, i), px);
        if (!g.bitmap.empty()) c.drawMask(g.bitmap.data(), g.w, g.h, g.w, x + g.xoff, baseline + g.yoff, color);
        x += g.advance;
    }
    return x;
}

std::string Font::fit(std::string_view text, int px, int maxWidth) const {
    if (measure(text, px) <= maxWidth) return std::string(text);
    const char* dots = "...";
    int budget = maxWidth - measure(dots, px);
    std::string out;
    int w = 0;
    for (size_t i = 0; i < text.size();) {
        size_t start = i;
        int adv = glyph(nextCodepoint(text, i), px).advance;
        if (w + adv > budget) break;
        w += adv;
        out.append(text.substr(start, i - start));
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out + dots;
}

std::vector<std::string> Font::wrap(std::string_view input, int px, int maxWidth, int maxLines) const {
    // Store descriptions use \r\n and the odd tab.
    std::string cleaned;
    cleaned.reserve(input.size());
    for (char ch : input) {
        if (ch == '\r') continue;
        cleaned += ch == '\t' ? ' ' : ch;
    }
    std::string_view text = cleaned;
    std::vector<std::string> lines;
    std::string current;
    size_t i = 0;
    auto flush = [&] {
        while (!current.empty() && current.back() == ' ') current.pop_back();
        lines.push_back(current);
        current.clear();
    };
    while (i < text.size()) {
        if (text[i] == '\n') {
            ++i;
            if (!current.empty()) flush();
            if (static_cast<int>(lines.size()) >= maxLines) break;
            continue;
        }
        size_t end = text.find_first_of(" \n", i);
        if (end == std::string_view::npos) end = text.size();
        std::string word(text.substr(i, end - i));
        std::string candidate = current.empty() ? word : current + " " + word;
        if (!current.empty() && measure(candidate, px) > maxWidth) {
            flush();
            if (static_cast<int>(lines.size()) >= maxLines) break;
            current = word;
        } else {
            current = candidate;
        }
        i = end < text.size() && text[end] == ' ' ? end + 1 : end;
    }
    if (!current.empty() && static_cast<int>(lines.size()) < maxLines) flush();
    bool truncated = i < text.size();
    if (truncated && !lines.empty()) lines.back() = fit(lines.back() + " ...", px, maxWidth);
    for (auto& l : lines) l = fit(l, px, maxWidth);
    return lines;
}

bool Fonts::load(const std::string& dir) {
    return regular.load(dir + "/Inter-Regular.ttf") && semibold.load(dir + "/Inter-SemiBold.ttf") &&
           bold.load(dir + "/Inter-Bold.ttf");
}

}  // namespace xc::ui
