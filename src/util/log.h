// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#pragma once

#include <cstdarg>
#include <string>

namespace xc::log {

enum class Level { Debug, Info, Warn, Error };

void setLevel(Level level);
// Also appends every line to this file (created if missing). nullptr disables.
// Returns true on success (or if path is nullptr), false if fopen failed.
bool setFile(const char* path);

// Returns recent log lines kept in memory (up to 64 KiB).
std::string recentLogs();

// Flushes any buffered repeated log notifications.
void flush();

void write(Level level, const char* fmt, ...) __attribute__((format(printf, 2, 3)));

}  // namespace xc::log

#define XC_LOGD(...) ::xc::log::write(::xc::log::Level::Debug, __VA_ARGS__)
#define XC_LOGI(...) ::xc::log::write(::xc::log::Level::Info, __VA_ARGS__)
#define XC_LOGW(...) ::xc::log::write(::xc::log::Level::Warn, __VA_ARGS__)
#define XC_LOGE(...) ::xc::log::write(::xc::log::Level::Error, __VA_ARGS__)
