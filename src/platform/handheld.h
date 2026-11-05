// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// Whether the machine is a handheld PC (platform::isHandheld()): the Steam
// Deck by its DMI strings. Pure, so the tests can check it.
#pragma once

#include <string_view>

namespace xc::platform {

// /sys/class/dmi/id/sys_vendor and product_name, as read (with their newline):
// Valve's "Jupiter" (the LCD Deck) or "Galileo" (the OLED one).
inline bool steamDeckDmi(std::string_view vendor, std::string_view product) {
    auto trim = [](std::string_view s) {
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.remove_suffix(1);
        return s;
    };
    vendor = trim(vendor);
    product = trim(product);
    return vendor == "Valve" && (product == "Jupiter" || product == "Galileo");
}

}  // namespace xc::platform
