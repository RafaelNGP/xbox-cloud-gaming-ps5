// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// The in-app notices' queue (platform/toasts.h).
#include "platform/toasts.h"

#include <algorithm>

namespace xc::platform {

namespace {
std::atomic<bool> g_enabled{false};
}  // namespace

void ToastQueue::push(const std::string& text, uint64_t nowMs) {
    if (text.empty()) return;
    std::lock_guard<std::mutex> lock(mutex_);
    std::erase_if(toasts_, [&](const Toast& t) { return t.text == text; });
    if (toasts_.size() >= kMaxQueued) toasts_.erase(toasts_.begin());
    toasts_.push_back({text, false, 0});
    ++version_;
    update(nowMs);
}

void ToastQueue::update(uint64_t nowMs) {
    size_t before = toasts_.size();
    std::erase_if(toasts_, [&](const Toast& t) { return t.shown && nowMs - t.shownAt >= kShowMs; });
    if (toasts_.size() != before) ++version_;
    for (size_t i = 0; i < toasts_.size() && i < kMaxShown; ++i) {
        if (toasts_[i].shown) continue;
        toasts_[i].shown = true;
        toasts_[i].shownAt = nowMs;
        ++version_;
    }
}

std::vector<std::string> ToastQueue::visible(uint64_t nowMs) {
    std::lock_guard<std::mutex> lock(mutex_);
    update(nowMs);
    std::vector<std::string> out;
    for (const auto& t : toasts_)
        if (t.shown) out.push_back(t.text);
    return out;
}

uint64_t ToastQueue::version(uint64_t nowMs) {
    std::lock_guard<std::mutex> lock(mutex_);
    update(nowMs);
    return version_;
}

void ToastQueue::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!toasts_.empty()) ++version_;
    toasts_.clear();
}

ToastQueue& toasts() {
    static ToastQueue queue;
    return queue;
}

void enableToasts(bool on) { g_enabled = on; }
bool toastsEnabled() { return g_enabled; }

}  // namespace xc::platform
