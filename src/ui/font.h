// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// TrueType text through stb_truetype, with a glyph cache per pixel size.
// Coordinates are the top-left corner of the line box.
#pragma once

#include "ui/canvas.h"

#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace xc::ui {

class Font {
public:
    Font();
    ~Font();
    Font(const Font&) = delete;
    Font& operator=(const Font&) = delete;

    // False (logged) when the file can't be read or isn't a font. A font
    // that never loaded measures and draws as nothing (said once in the log)
    // instead of reading an empty font: the app still runs without its
    // assets folder.
    bool load(const std::string& path);
    bool loaded() const { return loaded_; }

    int lineHeight(int px) const;
    // The `y` to draw at so that capital letters sit centred in a box of
    // height `boxH` starting at `boxY` (badges, keys, buttons).
    int centeredY(int boxY, int boxH, int px) const;
    int measure(std::string_view text, int px) const;
    // Returns the x after the last glyph.
    int draw(Canvas& c, std::string_view text, int x, int y, int px, Color color) const;

    // Word-wraps to `maxWidth`; at most `maxLines` (the last gets "...").
    std::vector<std::string> wrap(std::string_view text, int px, int maxWidth, int maxLines) const;
    // Shortens with "..." to fit `maxWidth`.
    std::string fit(std::string_view text, int px, int maxWidth) const;

private:
    struct Glyph {
        int w = 0, h = 0, xoff = 0, yoff = 0, advance = 0;
        std::vector<uint8_t> bitmap;
    };
    const Glyph& glyph(uint32_t codepoint, int px) const;
    float scale(int px) const;
    // loaded_, else false and a line in the log (the first time).
    bool usable() const;

    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::vector<unsigned char> data_;
    bool loaded_ = false;
    mutable std::mutex mutex_;
    mutable std::unordered_map<uint64_t, Glyph> cache_;
};

// The three weights the UI uses.
struct Fonts {
    Font regular, semibold, bold;
    // The console's own icon font (keyboard and mouse, controller...), read
    // from the system at run time, never shipped; hasIcons false without it.
    Font icons;
    bool hasIcons = false;
    bool load(const std::string& dir);
    // The first of `paths` that loads; false when none does.
    bool loadIcons(const std::vector<std::string>& paths);
};

// Code points of the console's icon font.
constexpr const char* kIconKeyboardMouse = "\xEF\xA3\xBE";  // U+F8FE
constexpr const char* kIconGamepad = "\xEF\xA1\xB4";        // U+F874

// Next UTF-8 code point from `s` at `i` (advances `i`); U+FFFD on bad input.
uint32_t nextCodepoint(std::string_view s, size_t& i);

}  // namespace xc::ui
