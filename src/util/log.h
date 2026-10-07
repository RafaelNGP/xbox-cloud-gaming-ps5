#pragma once

#include <cstdarg>

namespace xc::log {

enum class Level { Debug, Info, Warn, Error };

void setLevel(Level level);
// Also appends every line to this file (created if missing). nullptr disables.
void setFile(const char* path);

void write(Level level, const char* fmt, ...) __attribute__((format(printf, 2, 3)));

}  // namespace xc::log

#define XC_LOGD(...) ::xc::log::write(::xc::log::Level::Debug, __VA_ARGS__)
#define XC_LOGI(...) ::xc::log::write(::xc::log::Level::Info, __VA_ARGS__)
#define XC_LOGW(...) ::xc::log::write(::xc::log::Level::Warn, __VA_ARGS__)
#define XC_LOGE(...) ::xc::log::write(::xc::log::Level::Error, __VA_ARGS__)
