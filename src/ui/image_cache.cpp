// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "ui/image_cache.h"

#include "net/http.h"
#include "util/log.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "stb_image.h"

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#define STB_IMAGE_RESIZE_STATIC
#include "stb_image_resize2.h"

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>

namespace xc::ui {

bool decodeImage(const std::string& bytes, Image& out) {
    int w, h, n;
    unsigned char* data = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(bytes.data()),
                                                static_cast<int>(bytes.size()), &w, &h, &n, 4);
    if (!data) return false;
    out.w = w;
    out.h = h;
    out.px.resize(static_cast<size_t>(w) * h);
    std::memcpy(out.px.data(), data, out.px.size() * 4);
    stbi_image_free(data);
    return true;
}

Image coverResize(const Image& src, int w, int h) {
    Image out;
    out.w = w;
    out.h = h;
    out.px.resize(static_cast<size_t>(w) * h);
    // Crop the source to the target aspect ratio, centred.
    double scale = std::max(static_cast<double>(w) / src.w, static_cast<double>(h) / src.h);
    int cw = std::min(src.w, static_cast<int>(w / scale + 0.5));
    int ch = std::min(src.h, static_cast<int>(h / scale + 0.5));
    int cx = (src.w - cw) / 2, cy = (src.h - ch) / 2;
    const auto* start = reinterpret_cast<const unsigned char*>(&src.px[static_cast<size_t>(cy) * src.w + cx]);
    stbir_resize_uint8_srgb(start, cw, ch, src.w * 4, reinterpret_cast<unsigned char*>(out.px.data()), w, h, w * 4,
                            STBIR_RGBA);
    return out;
}

namespace {

// store-images.s-microsoft.com resizes on request: fetch close to the size
// we draw instead of the multi-megapixel original.
std::string sizedUrl(std::string url, int w, int h) {
    if (url.rfind("//", 0) == 0) url = "https:" + url;
    if (url.find("store-images.s-microsoft.com") != std::string::npos) {
        url += url.find('?') == std::string::npos ? "?" : "&";
        url += "w=" + std::to_string(w) + "&h=" + std::to_string(h) + "&mode=scale&q=90&format=jpg";
    }
    return url;
}

}  // namespace

ImageCache::ImageCache(std::function<void()> onReady, size_t budgetBytes, std::string diskDir,
                       size_t diskBudgetBytes)
    : onReady_(std::move(onReady)), budget_(budgetBytes), diskDir_(std::move(diskDir)), diskBudget_(diskBudgetBytes) {
    if (!diskDir_.empty()) {
        ::mkdir(diskDir_.c_str(), 0755);
        trimDisk();
    }
    threads_.resize(3);
    for (auto& t : threads_) platform::startThread(t, [this] { loop(); }, 2u << 20);
}

ImageCache::~ImageCache() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
    }
    cv_.notify_all();
    for (auto& t : threads_) t.join();
}

std::shared_ptr<const Image> ImageCache::get(const std::string& url, int w, int h) {
    if (url.empty() || w <= 0 || h <= 0) return nullptr;
    std::string key = url + "#" + std::to_string(w) + "x" + std::to_string(h);
    std::lock_guard<std::mutex> lock(mutex_);
    Entry& e = entries_[key];
    e.lastUse = ++clock_;
    if (!e.image && !e.loading && !e.failed) {
        e.loading = true;
        jobs_.push_front({key, url, w, h});
        cv_.notify_one();
    }
    return e.image;
}

void ImageCache::evict() {
    // Caller holds mutex_.
    while (bytes_ > budget_) {
        auto victim = entries_.end();
        for (auto it = entries_.begin(); it != entries_.end(); ++it)
            if (it->second.image && (victim == entries_.end() || it->second.lastUse < victim->second.lastUse))
                victim = it;
        if (victim == entries_.end()) break;
        bytes_ -= victim->second.image->bytes();
        entries_.erase(victim);
    }
}

std::string ImageCache::diskPath(const std::string& url) const {
    uint64_t h = 1469598103934665603ull;  // FNV-1a of the sized URL
    for (unsigned char ch : url) h = (h ^ ch) * 1099511628211ull;
    char name[24];
    std::snprintf(name, sizeof name, "%016llx", static_cast<unsigned long long>(h));
    return diskDir_ + "/" + name;
}

void ImageCache::trimDisk() {
    struct File {
        std::string path;
        off_t size;
        time_t mtime;
    };
    std::vector<File> files;
    size_t total = 0;
    if (DIR* d = ::opendir(diskDir_.c_str())) {
        while (dirent* e = ::readdir(d)) {
            if (e->d_name[0] == '.') continue;
            std::string path = diskDir_ + "/" + e->d_name;
            struct stat st {};
            if (::stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) continue;
            files.push_back({path, st.st_size, st.st_mtime});
            total += static_cast<size_t>(st.st_size);
        }
        ::closedir(d);
    }
    if (total <= diskBudget_) {
        XC_LOGI("image cache: %zu files, %zu MB on disk", files.size(), total >> 20);
        return;
    }
    std::sort(files.begin(), files.end(), [](const File& a, const File& b) { return a.mtime < b.mtime; });
    size_t removed = 0;
    for (const auto& f : files) {
        if (total <= diskBudget_ * 3 / 4) break;  // some headroom
        if (::unlink(f.path.c_str()) == 0) {
            total -= static_cast<size_t>(f.size);
            ++removed;
        }
    }
    XC_LOGI("image cache: trimmed %zu files, %zu MB left", removed, total >> 20);
}

void ImageCache::loop() {
    for (;;) {
        Job job;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return stop_ || !jobs_.empty(); });
            if (stop_) return;
            job = std::move(jobs_.front());
            jobs_.pop_front();
        }
        net::Request req;
        req.url = sizedUrl(job.url, job.w, job.h);
        req.headers = {{"Accept", "image/*"}};
        Image decoded;
        std::shared_ptr<Image> result;
        std::string cached;
        std::string path = diskDir_.empty() ? std::string() : diskPath(req.url);
        net::Response r;
        if (!path.empty() && platform::readFile(path, cached) && decodeImage(cached, decoded)) {
            r.status = 200;  // from disk
        } else {
            r = net::perform(req);
            if (r.ok() && !path.empty() && decodeImage(r.body, decoded)) {
                platform::writeFileAtomic(path, r.body);
            } else if (r.ok()) {
                decodeImage(r.body, decoded);
            }
        }
        if (r.ok() && decoded.w > 0) {
            result = std::make_shared<Image>(decoded.w == job.w && decoded.h == job.h
                                                 ? std::move(decoded)
                                                 : coverResize(decoded, job.w, job.h));
        } else {
            XC_LOGW("image %s: %s", req.url.c_str(), r.status ? ("HTTP " + std::to_string(r.status)).c_str()
                                                              : r.error.c_str());
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            Entry& e = entries_[job.key];
            e.loading = false;
            if (result) {
                e.image = result;
                bytes_ += result->bytes();
                evict();
            } else {
                e.failed = true;
            }
        }
        if (result && onReady_) onReady_();
    }
}

}  // namespace xc::ui
