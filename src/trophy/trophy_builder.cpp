// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "trophy/trophy_builder.h"
#include "trophy/trophy_db.h"
#include "platform/platform.h"
#include "util/json.h"
#include "util/log.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <mbedtls/sha1.h>
#include <mbedtls/sha256.h>
#include <sys/stat.h>
#include <unistd.h>

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "stb_image.h"

#define STB_IMAGE_RESIZE_STATIC
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize2.h"

#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

namespace xc::trophy {

namespace {

#include "trophy/icon_arrays.inc"
#include "trophy/trptitle_template.inc"

const uint8_t kNpBindTemplate[532] = {
    0xd2, 0x94, 0xa0, 0x18, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x14,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x10, 0x00, 0x0c, 0x4e, 0x50, 0x57, 0x52, 0x38, 0x30, 0x30, 0x30, 0x31, 0x5f, 0x30, 0x30,
    0x00, 0x11, 0x00, 0x0c, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x12, 0x00, 0xb0, 0x27, 0xde, 0x68, 0x6e, 0xfd, 0x10, 0x2e, 0x33, 0x07, 0x99, 0x2e, 0x24,
    0x9f, 0x19, 0x19, 0xd1, 0x05, 0xf4, 0xa3, 0x88, 0x17, 0x1e, 0x6e, 0x15, 0xd0, 0xfa, 0x4b, 0x5d,
    0x97, 0xab, 0x11, 0xb2, 0xa0, 0xb0, 0xbb, 0x43, 0xb0, 0xe5, 0x68, 0x20, 0x5c, 0x78, 0xdd, 0x8a,
    0xfc, 0x89, 0x04, 0x6b, 0xe2, 0x67, 0xa6, 0x62, 0x2f, 0x58, 0x30, 0x1c, 0x77, 0x75, 0xea, 0x50,
    0x3e, 0xf2, 0xdf, 0xac, 0xcd, 0x89, 0xe1, 0x6d, 0x68, 0x24, 0xe9, 0x09, 0x93, 0x4e, 0xc7, 0xf9,
    0x7e, 0x03, 0x5c, 0x01, 0x56, 0x5e, 0xe1, 0xbf, 0x20, 0x3e, 0xe1, 0x39, 0xf0, 0x49, 0x9e, 0xbe,
    0x78, 0x7e, 0xd3, 0xfa, 0xe2, 0x7a, 0xcb, 0x41, 0xbd, 0xd9, 0xd7, 0xf6, 0x5f, 0x15, 0x7a, 0x64,
    0xe9, 0x95, 0x43, 0x1f, 0xc8, 0x7e, 0x01, 0x2d, 0xd3, 0x73, 0xb0, 0x04, 0xa2, 0x52, 0xb1, 0xb2,
    0x43, 0xf7, 0x96, 0x56, 0x7e, 0x30, 0x88, 0x98, 0x57, 0x0d, 0x65, 0x32, 0x19, 0x31, 0x8a, 0x1c,
    0xed, 0xae, 0xed, 0xe4, 0x3a, 0x77, 0xfb, 0x3f, 0xfb, 0xd5, 0xad, 0x4b, 0x0a, 0x0f, 0xaa, 0x28,
    0x19, 0x92, 0x14, 0x56, 0x70, 0xd6, 0x59, 0x6f, 0xad, 0xad, 0x24, 0xa4, 0xad, 0xe9, 0x92, 0x27,
    0x96, 0xdc, 0x6c, 0x5a, 0x00, 0x13, 0x00, 0x10, 0x46, 0x39, 0xfe, 0x61, 0x98, 0x58, 0x41, 0x49,
    0x8b, 0x51, 0x82, 0x3b, 0x9f, 0x55, 0x6b, 0xdd, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x16, 0x98, 0xa7, 0x89, 0x3f, 0x3c, 0xff, 0x0b, 0xf5, 0x0b, 0x4b, 0xcf, 0xb8, 0x34, 0xcc, 0xf9,
    0x82, 0x17, 0x43, 0xa3
};

inline void writeBe32(uint8_t* p, uint32_t val) {
    p[0] = static_cast<uint8_t>((val >> 24) & 0xFF);
    p[1] = static_cast<uint8_t>((val >> 16) & 0xFF);
    p[2] = static_cast<uint8_t>((val >> 8) & 0xFF);
    p[3] = static_cast<uint8_t>(val & 0xFF);
}

inline void writeBe64(uint8_t* p, uint64_t val) {
    p[0] = static_cast<uint8_t>((val >> 56) & 0xFF);
    p[1] = static_cast<uint8_t>((val >> 48) & 0xFF);
    p[2] = static_cast<uint8_t>((val >> 40) & 0xFF);
    p[3] = static_cast<uint8_t>((val >> 32) & 0xFF);
    p[4] = static_cast<uint8_t>((val >> 24) & 0xFF);
    p[5] = static_cast<uint8_t>((val >> 16) & 0xFF);
    p[6] = static_cast<uint8_t>((val >> 8) & 0xFF);
    p[7] = static_cast<uint8_t>(val & 0xFF);
}

inline uint32_t readBe32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | p[3];
}

inline uint64_t readBe64(const uint8_t* p) {
    return (static_cast<uint64_t>(readBe32(p)) << 32) | readBe32(p + 4);
}

constexpr size_t TRP_HEAD = 0x40;
constexpr size_t TRP_CHUNK = 0x400;
constexpr size_t TRP_STRIDE = 0x420;
constexpr size_t TRP_MAX = 256 * 1024;

bool readTrophyStream(const uint8_t* raw, size_t rawSize, std::vector<uint8_t>& stream) {
    stream.clear();
    if (!raw || rawSize < TRP_HEAD || (rawSize - TRP_HEAD) % TRP_STRIDE != 0 || std::memcmp(raw, "T2PD", 4) != 0) {
        return false;
    }
    stream.assign(raw, raw + TRP_HEAD);
    for (size_t o = TRP_HEAD; o + TRP_STRIDE <= rawSize; o += TRP_STRIDE) {
        stream.insert(stream.end(), raw + o, raw + o + TRP_CHUNK);
    }
    return true;
}

bool writeTrophyFile(const uint8_t* stream, size_t streamSize, std::vector<uint8_t>& outDat) {
    outDat.clear();
    if (!stream || streamSize < TRP_HEAD || std::memcmp(stream, "T2PD", 4) != 0) {
        return false;
    }
    outDat.assign(stream, stream + TRP_HEAD);
    for (size_t o = TRP_HEAD; o + TRP_CHUNK <= streamSize; o += TRP_CHUNK) {
        const uint8_t* chunk = stream + o;
        outDat.insert(outDat.end(), chunk, chunk + TRP_CHUNK);
        bool any = false;
        for (size_t i = 0; i < TRP_CHUNK && !any; ++i) {
            any = (chunk[i] != 0);
        }
        if (any) {
            uint8_t hash[32];
            mbedtls_sha256(chunk, TRP_CHUNK, hash, 0);
            outDat.insert(outDat.end(), hash, hash + 32);
        } else {
            outDat.insert(outDat.end(), 32, 0);
        }
    }
    return true;
}

bool dirRecord(const uint8_t* s, size_t sSize, uint32_t rtype, uint32_t* rsize, uint32_t* rcount, size_t* roffset) {
    if (sSize < 0x70 || std::memcmp(s + 0x40, "T2TD", 4) != 0) return false;
    uint32_t n = readBe32(s + 0x48);
    for (uint32_t i = 0; i < n && i < 16; ++i) {
        size_t e = 0x70 + i * 0x20;
        if (e + 0x20 > sSize) break;
        if (readBe32(s + e) == rtype) {
            if (rsize) *rsize = readBe32(s + e + 4);
            if (rcount) *rcount = readBe32(s + e + 12);
            if (roffset) *roffset = 0x40 + static_cast<size_t>(readBe64(s + e + 16));
            return true;
        }
    }
    return false;
}

size_t findRecord(const uint8_t* s, size_t sSize, uint32_t kind, uint32_t recSize, size_t fromOff = 0x190) {
    for (size_t o = fromOff; o + 0x10 <= sSize; o += 16) {
        if (readBe32(s + o) == kind && readBe32(s + o + 4) == recSize) return o;
    }
    return 0;
}

void putMask(uint8_t* p, const std::vector<uint8_t>& ids) {
    std::memset(p, 0, 16);
    for (uint8_t tid : ids) {
        if (tid < 128) {
            p[tid / 8] |= static_cast<uint8_t>(1u << (tid % 8));
        }
    }
}

bool unlockInStream(uint8_t* s, size_t sSize, uint32_t tid, uint64_t whenTicks) {
    size_t h = findRecord(s, sSize, 0x2, 0x60);
    size_t r6 = findRecord(s, sSize, 0x600, 0x1f0);
    size_t r7 = findRecord(s, sSize, 0x700, 0xb0);
    if (!h || !r6 || !r7 || tid >= 128) return false;

    size_t o = 0;
    for (size_t at = findRecord(s, sSize, 0x800, 0x50); at != 0; at = findRecord(s, sSize, 0x800, 0x50, at + 0x60)) {
        if (readBe32(s + at + 0x10) == tid) {
            o = at;
            break;
        }
    }
    if (!o || o + 0x50 > sSize || readBe32(s + o + 0x14) == 0x11) return false;

    static const uint8_t kTrailer[16] = {0x55, 0, 0, 0, 0, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 1};
    writeBe32(s + o + 0x14, 0x11);
    writeBe32(s + o + 0x18, 0x2000);
    writeBe64(s + o + 0x20, whenTicks);
    writeBe64(s + o + 0x30, whenTicks);
    std::memcpy(s + o + 0x40, kTrailer, 16);

    const size_t bitmaps[] = {r6 + 0x80, r6 + 0x180, r7 + 0x40};
    for (int i = 0; i < 3; ++i) s[bitmaps[i] + tid / 8] |= static_cast<uint8_t>(1u << (tid % 8));

    uint64_t last = readBe64(s + r6 + 0x40);
    if (whenTicks > last) last = whenTicks;
    const size_t lasts[] = {r6 + 0x40, r6 + 0x70, r7 + 0x20};
    for (int i = 0; i < 3; ++i) writeBe64(s + lasts[i], last);

    if (whenTicks > readBe64(s + h + 0x20)) {
        writeBe64(s + h + 0x20, whenTicks);
        writeBe64(s + h + 0x28, 0);
    }
    writeBe64(s + h + 0x18, readBe64(s + h + 0x18) + 2);
    writeBe64(s + r6 + 0x10, readBe64(s + r6 + 0x10) + 1);
    return true;
}

bool writeRawFile(const std::string& path, const void* data, size_t size, mode_t mode = 0666) {
    int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, mode);
    if (fd < 0) {
        XC_LOGW("trophy builder: failed to open '%s' for write: errno=%d", path.c_str(), errno);
        return false;
    }
    ssize_t written = ::write(fd, data, size);
    ::close(fd);
    ::chmod(path.c_str(), mode);
    return (written == static_cast<ssize_t>(size));
}

void ensureDir(const std::string& dir, mode_t mode = 0777) {
    std::string s;
    for (char c : dir) {
        s += c;
        if (c == '/' && s.size() > 1) {
            ::mkdir(s.c_str(), mode);
            ::chmod(s.c_str(), mode);
        }
    }
    ::mkdir(dir.c_str(), mode);
    ::chmod(dir.c_str(), mode);
}

}  // namespace

bool is512x512Png(const uint8_t* data, size_t size) {
    if (!data || size < 24) return false;
    static const uint8_t kPngSig[8] = {0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a};
    if (std::memcmp(data, kPngSig, 8) != 0) return false;
    if (std::memcmp(data + 12, "IHDR", 4) != 0) return false;
    uint32_t w = readBe32(data + 16);
    uint32_t h = readBe32(data + 20);
    return (w == 512 && h == 512);
}

bool convertTo512Png(const uint8_t* rawData, size_t rawSize, std::vector<uint8_t>& outPng) {
    outPng.clear();
    if (!rawData || rawSize == 0) return false;
    int w = 0, h = 0, n = 0;
    stbi_uc* pixels = stbi_load_from_memory(rawData, static_cast<int>(rawSize), &w, &h, &n, 4);
    if (!pixels || w <= 0 || h <= 0) {
        if (pixels) stbi_image_free(pixels);
        return false;
    }

    int side = std::min(w, h);
    int cx = (w - side) / 2;
    int cy = (h - side) / 2;
    const stbi_uc* srcStart = pixels + (static_cast<size_t>(cy) * w + cx) * 4;

    std::vector<uint8_t> resized(512 * 512 * 4);
    stbir_resize_uint8_srgb(srcStart, side, side, w * 4,
                            resized.data(), 512, 512, 512 * 4,
                            STBIR_RGBA);
    stbi_image_free(pixels);

    int pngLen = 0;
    unsigned char* pngData = stbi_write_png_to_mem(resized.data(), 512 * 4, 512, 512, 4, &pngLen);
    if (!pngData || pngLen <= 0) {
        if (pngData) free(pngData);
        return false;
    }

    outPng.assign(pngData, pngData + pngLen);
    free(pngData);
    return true;
}

const uint8_t kNpTitleTrailer[128] = {
    0x4d, 0x75, 0x39, 0xf0, 0xf9, 0x30, 0x94, 0x7a, 0x52, 0x04, 0x6a, 0xe7, 0x54, 0x08, 0x22, 0xb2,
    0x02, 0x20, 0xb1, 0x83, 0x3d, 0xec, 0x0f, 0xe2, 0x62, 0x17, 0xcb, 0x16, 0x33, 0x2e, 0xfa, 0x08,
    0x0e, 0xaa, 0x86, 0x5e, 0xe1, 0xf8, 0xd9, 0x31, 0xaa, 0x34, 0xba, 0x73, 0x41, 0xf6, 0xb9, 0xdd,
    0x74, 0xc5, 0xf2, 0x2d, 0x65, 0x68, 0xf0, 0xa9, 0x76, 0x83, 0x94, 0xc8, 0x80, 0x78, 0x65, 0x00,
    0xda, 0x1a, 0xd4, 0xfc, 0x51, 0x5d, 0x97, 0xf9, 0xec, 0xe2, 0x71, 0x9f, 0x4a, 0xc4, 0xb8, 0x85,
    0x91, 0xf6, 0xc1, 0x57, 0xe6, 0x28, 0xed, 0x31, 0x79, 0xd4, 0x1a, 0xb4, 0xbe, 0x83, 0x32, 0x2d,
    0xfb, 0x22, 0x9a, 0xc1, 0x50, 0x7d, 0x8a, 0x0b, 0x99, 0x8a, 0x92, 0x50, 0x6f, 0x1e, 0x5b, 0x49,
    0x22, 0xb2, 0x34, 0x70, 0xb9, 0x7c, 0x79, 0x9a, 0x00, 0x14, 0xf4, 0x3e, 0x89, 0xbe, 0x2a, 0x7a
};

bool buildNpTitleDat(const std::string& titleId, std::vector<uint8_t>& outNpTitle) {
    outNpTitle.assign(160, 0);
    std::memcpy(outNpTitle.data(), "NPTD", 4);
    writeBe32(outNpTitle.data() + 4, 0x80);
    std::string tidStr = titleId + "_00";
    if (tidStr.size() > 15) tidStr.resize(15);
    std::memcpy(outNpTitle.data() + 0x10, tidStr.c_str(), tidStr.size());
    std::memcpy(outNpTitle.data() + 0x20, kNpTitleTrailer, sizeof(kNpTitleTrailer));
    return true;
}

bool parseTrpTitleDat(const uint8_t* data, size_t size, std::map<uint32_t, ExistingTrophyRecord>& outTrophies) {
    outTrophies.clear();
    if (!data || size < 0x40) return false;

    std::vector<uint8_t> streamBuf;
    const uint8_t* s = nullptr;
    size_t sSize = 0;

    if (size >= TRP_HEAD && (size - TRP_HEAD) % TRP_STRIDE == 0 && std::memcmp(data, "T2PD", 4) == 0) {
        if (!readTrophyStream(data, size, streamBuf)) return false;
        s = streamBuf.data();
        sSize = streamBuf.size();
    } else if (size >= TRP_HEAD && std::memcmp(data, "T2PD", 4) == 0) {
        s = data;
        sSize = size;
    } else {
        return false;
    }

    for (size_t at = findRecord(s, sSize, 0x800, 0x50); at != 0; at = findRecord(s, sSize, 0x800, 0x50, at + 0x60)) {
        uint32_t tid = readBe32(s + at + 0x10);
        uint32_t flags = readBe32(s + at + 0x14);
        uint64_t ts = readBe64(s + at + 0x20);
        uint32_t state = (flags == 0x11 && ts != 0) ? 1 : 0;
        outTrophies[tid] = {tid, 0, state, ts};
    }
    return !outTrophies.empty();
}


bool buildNpBind(const std::string& npCommId, std::vector<uint8_t>& outNpBind) {
    outNpBind.assign(kNpBindTemplate, kNpBindTemplate + sizeof(kNpBindTemplate));
    if (npCommId.size() >= 12) {
        std::memcpy(outNpBind.data() + 0x84, npCommId.c_str(), 12);
    }
    // Tag 0x0011 is serviceLabel: '0' (0x30 0x00 ...), matching all official PS5 titles
    outNpBind[0x94] = '0';
    return true;
}

bool buildUcp(const TitleTrophySet& set, std::vector<uint8_t>& outUcp, std::vector<uint8_t>* outSha1) {
    outUcp.clear();
    if (outSha1) outSha1->clear();

    // 1. Generate tropconf.json
    json::Value conf = json::Value::object();
    conf.set("schemaVersion", "1.00");
    conf.set("trophyDefinitionRevision", static_cast<int64_t>(780785));
    json::Value platforms = json::Value::array();
    platforms.push("PS5");
    conf.set("platform", std::move(platforms));
    conf.set("trophyNpCommId", set.npCommId);
    conf.set("udsNpCommId", set.npCommId);
    conf.set("trophySetVersion", "1.0");
    conf.set("defaultLanguage", "en-US");
    json::Value languages = json::Value::array();
    languages.push("en-US");
    languages.push("pt-BR");
    conf.set("languages", std::move(languages));

    json::Value confTrophies = json::Value::array();
    // Platinum trophy #0000
    json::Value platConf = json::Value::object();
    platConf.set("id", "0000");
    platConf.set("hidden", false);
    platConf.set("grade", "P");
    platConf.set("hasReward", false);
    confTrophies.push(std::move(platConf));

    for (const auto& item : set.items) {
        char idBuf[8];
        snprintf(idBuf, sizeof(idBuf), "%04d", item.trophyId);
        json::Value t = json::Value::object();
        t.set("id", std::string(idBuf));
        t.set("hidden", false);
        const char* gStr = "B";
        if (item.grade == TrophyGrade::Platinum) gStr = "P";
        else if (item.grade == TrophyGrade::Gold) gStr = "G";
        else if (item.grade == TrophyGrade::Silver) gStr = "S";
        t.set("grade", gStr);
        t.set("hasReward", false);
        t.set("platinumTrophyId", "0000");

        // Unlock condition matching PS5 Animal Well master structure
        json::Value cond = json::Value::object();
        cond.set("udsStatId", std::to_string(100 + item.trophyId));
        cond.set("comparator", "ge");
        cond.set("targetValue", "1");
        cond.set("progressive", false);
        json::Value locs = json::Value::array();
        json::Value loc = json::Value::object();
        loc.set("unlockType", "client");
        json::Value platList = json::Value::array();
        platList.push("PS5");
        loc.set("platform", std::move(platList));
        locs.push(std::move(loc));
        cond.set("locations", std::move(locs));
        t.set("unlockCondition", std::move(cond));

        confTrophies.push(std::move(t));
    }
    conf.set("trophies", std::move(confTrophies));
    std::string tropconfStr = conf.dump();

    // 2. Generate tropmeta_en-US.json
    json::Value metaEn = json::Value::object();
    metaEn.set("schemaVersion", "0.90");
    metaEn.set("trophyDefinitionRevision", static_cast<int64_t>(780785));
    metaEn.set("trophySetVersion", "1.0");
    metaEn.set("trophyNpCommId", set.npCommId);
    json::Value metaEnInner = json::Value::object();
    json::Value titleMetaEn = json::Value::object();
    titleMetaEn.set("name", set.titleName);
    metaEnInner.set("titleMetadata", std::move(titleMetaEn));

    json::Value tropMetaEn = json::Value::array();
    json::Value platMetaEn = json::Value::object();
    platMetaEn.set("id", "0000");
    if (set.npCommId == "NPWR20561_00") {
        platMetaEn.set("name", "Hero of the Storm");
        platMetaEn.set("detail", "Earn all other Fortnite trophies.");
    } else {
        platMetaEn.set("name", "Platinum: " + set.titleName);
        platMetaEn.set("detail", "Earn all trophies");
    }
    tropMetaEn.push(std::move(platMetaEn));

    for (const auto& item : set.items) {
        char idBuf[8];
        snprintf(idBuf, sizeof(idBuf), "%04d", item.trophyId);
        json::Value t = json::Value::object();
        t.set("id", std::string(idBuf));
        t.set("name", item.name);
        t.set("detail", item.description);
        tropMetaEn.push(std::move(t));
    }
    metaEnInner.set("trophyMetadata", std::move(tropMetaEn));
    metaEn.set("metadata", std::move(metaEnInner));
    std::string tropmetaEnStr = metaEn.dump();

    // 3. Generate tropmeta_pt-BR.json
    json::Value metaPt = json::Value::object();
    metaPt.set("schemaVersion", "0.90");
    metaPt.set("trophyDefinitionRevision", static_cast<int64_t>(780785));
    metaPt.set("trophySetVersion", "1.0");
    metaPt.set("trophyNpCommId", set.npCommId);
    json::Value metaPtInner = json::Value::object();
    json::Value titleMetaPt = json::Value::object();
    titleMetaPt.set("name", set.titleName);
    metaPtInner.set("titleMetadata", std::move(titleMetaPt));

    json::Value tropMetaPt = json::Value::array();
    json::Value platMetaPt = json::Value::object();
    platMetaPt.set("id", "0000");
    if (set.npCommId == "NPWR20561_00") {
        platMetaPt.set("name", "Herói da Tempestade");
        platMetaPt.set("detail", "Obtenha todos os outros troféus do Fortnite.");
    } else {
        platMetaPt.set("name", "Platina: " + set.titleName);
        platMetaPt.set("detail", "Obtenha todos os troféus");
    }
    tropMetaPt.push(std::move(platMetaPt));

    for (const auto& item : set.items) {
        char idBuf[8];
        snprintf(idBuf, sizeof(idBuf), "%04d", item.trophyId);
        json::Value t = json::Value::object();
        t.set("id", std::string(idBuf));
        t.set("name", item.name);
        t.set("detail", item.description);
        tropMetaPt.push(std::move(t));
    }
    metaPtInner.set("trophyMetadata", std::move(tropMetaPt));
    metaPt.set("metadata", std::move(metaPtInner));
    std::string tropmetaPtStr = metaPt.dump();

    // 4. Collect file entries
    struct Entry {
        std::string name;
        const uint8_t* data = nullptr;
        size_t size = 0;
        std::vector<uint8_t> owned;
    };
    std::vector<Entry> fileEntries;

    auto addEntry = [&](const std::string& name, std::vector<uint8_t>&& owned, const uint8_t* defData, size_t defSize) {
        Entry e;
        e.name = name;
        if (!owned.empty()) {
            e.owned = std::move(owned);
            e.data = e.owned.data();
            e.size = e.owned.size();
        } else {
            e.data = defData;
            e.size = defSize;
        }
        fileEntries.push_back(std::move(e));
    };

    // Load game icon (icon0_en-US.png)
    std::vector<uint8_t> icon0Png;
    std::string candidatePaths[] = {
        platform::dataDir() + "/trophies/icons/" + set.npCommId + "/icon0.png",
        "/data/psbox/trophies/icons/" + set.npCommId + "/icon0.png",
        "/system_data/priv/appmeta/" + set.sonyTitleId + "/icon0.png",
        "/user/appmeta/" + set.sonyTitleId + "/icon0.png",
        "/user/app/" + set.sonyTitleId + "/sce_sys/icon0.png"
    };
    for (const auto& path : candidatePaths) {
        std::string raw;
        if (platform::readFile(path, raw) && raw.size() > 100) {
            const auto* udata = reinterpret_cast<const uint8_t*>(raw.data());
            if (is512x512Png(udata, raw.size())) {
                icon0Png.assign(udata, udata + raw.size());
                break;
            } else if (convertTo512Png(udata, raw.size(), icon0Png)) {
                std::string cacheFile = platform::dataDir() + "/trophies/icons/" + set.npCommId + "/icon0.png";
                platform::writeFileAtomic(cacheFile, std::string(reinterpret_cast<const char*>(icon0Png.data()), icon0Png.size()));
                break;
            }
        }
    }
    addEntry("icon0_en-US.png", std::move(icon0Png), kDefaultIconPng, sizeof(kDefaultIconPng));

    // Load trop0000.png (Platinum)
    std::vector<uint8_t> trop0000Png;
    std::string platCandidatePaths[] = {
        platform::dataDir() + "/trophies/icons/" + set.npCommId + "/trop0000.png",
        "/data/psbox/trophies/icons/" + set.npCommId + "/trop0000.png"
    };
    for (const auto& path : platCandidatePaths) {
        std::string raw;
        if (platform::readFile(path, raw) && raw.size() > 100) {
            const auto* udata = reinterpret_cast<const uint8_t*>(raw.data());
            if (is512x512Png(udata, raw.size())) {
                trop0000Png.assign(udata, udata + raw.size());
                break;
            } else if (convertTo512Png(udata, raw.size(), trop0000Png)) {
                break;
            }
        }
    }
    addEntry("trop0000.png", std::move(trop0000Png), kDefaultTropPng, sizeof(kDefaultTropPng));

    // Load trop%04d.png for each trophy
    for (const auto& item : set.items) {
        char nameBuf[32];
        snprintf(nameBuf, sizeof(nameBuf), "trop%04d.png", item.trophyId);
        std::vector<uint8_t> tropPng;
        std::string itemPaths[] = {
            platform::dataDir() + "/trophies/icons/" + set.npCommId + "/" + nameBuf,
            "/data/psbox/trophies/icons/" + set.npCommId + "/" + nameBuf
        };
        for (const auto& path : itemPaths) {
            std::string raw;
            if (platform::readFile(path, raw) && raw.size() > 100) {
                const auto* udata = reinterpret_cast<const uint8_t*>(raw.data());
                if (is512x512Png(udata, raw.size())) {
                    tropPng.assign(udata, udata + raw.size());
                    break;
                } else if (convertTo512Png(udata, raw.size(), tropPng)) {
                    break;
                }
            }
        }
        addEntry(nameBuf, std::move(tropPng), kDefaultTropPng, sizeof(kDefaultTropPng));
    }

    addEntry("tropconf.json", {}, reinterpret_cast<const uint8_t*>(tropconfStr.data()), tropconfStr.size());
    addEntry("tropmeta_en-US.json", {}, reinterpret_cast<const uint8_t*>(tropmetaEnStr.data()), tropmetaEnStr.size());
    addEntry("tropmeta_pt-BR.json", {}, reinterpret_cast<const uint8_t*>(tropmetaPtStr.data()), tropmetaPtStr.size());

    const uint32_t numFiles = static_cast<uint32_t>(fileEntries.size());
    const uint64_t headerSize = 0x60;
    const uint64_t tableSize = static_cast<uint64_t>(numFiles) * 64;
    uint64_t curOffset = (headerSize + tableSize + 15) & ~15ULL;

    std::vector<uint8_t> fileTable(tableSize, 0);
    std::vector<uint8_t> bodies;

    for (uint32_t i = 0; i < numFiles; ++i) {
        const auto& fe = fileEntries[i];
        uint8_t* row = fileTable.data() + (i * 64);
        std::strncpy(reinterpret_cast<char*>(row), fe.name.c_str(), 31);
        writeBe64(row + 32, curOffset);
        writeBe64(row + 40, fe.size);

        // Append body with 16-byte alignment
        bodies.insert(bodies.end(), fe.data, fe.data + fe.size);
        size_t pad = (16 - (fe.size % 16)) % 16;
        bodies.insert(bodies.end(), pad, 0);
        curOffset += (fe.size + pad);
    }

    uint64_t totalSize = curOffset;

    outUcp.assign(totalSize, 0);
    // Magic: 0xb228c60a, Version: 1
    writeBe32(outUcp.data() + 0x00, 0xb228c60a);
    writeBe32(outUcp.data() + 0x04, 1);
    writeBe64(outUcp.data() + 0x08, totalSize);
    writeBe32(outUcp.data() + 0x10, numFiles);
    writeBe32(outUcp.data() + 0x14, 64); // entry size
    writeBe32(outUcp.data() + 0x18, 0);

    // Copy file table & bodies
    std::memcpy(outUcp.data() + headerSize, fileTable.data(), tableSize);
    std::memcpy(outUcp.data() + (headerSize + tableSize), bodies.data(), bodies.size());

    // Calculate genuine SHA-1 digest across the entire UCP buffer (with 0x1c..0x30 zeroed)
    uint8_t sha1Digest[20] = {0};
    mbedtls_sha1(outUcp.data(), outUcp.size(), sha1Digest);
    // Write genuine SHA-1 digest into UCP header at 0x1c
    std::memcpy(outUcp.data() + 0x1c, sha1Digest, 20);

    if (outSha1) {
        outSha1->assign(sha1Digest, sha1Digest + 20);
    }

    XC_LOGI("trophy builder: built TROPHY.UCP for %s (%s): %zu bytes, %u files, sha1=%02x%02x%02x%02x...",
            set.titleName.c_str(), set.npCommId.c_str(), outUcp.size(), numFiles,
            sha1Digest[0], sha1Digest[1], sha1Digest[2], sha1Digest[3]);
    return true;
}

bool buildUdsUcp(const TitleTrophySet& set, std::vector<uint8_t>& outUcp) {
    if (set.npCommId.empty()) return false;

    // 1. enum_definition.json
    std::string enumStr = "{\"schemaVersion\":\"1.1\",\"npCommunicationId\":\"" + set.npCommId + "\",\"enumDefinitionRevision\":2011,\"enumArray\":[{\"enumId\":1,\"definitionGroup\":\"autoGenState\",\"sourceId\":\"activity_pattern1\",\"enumValueArray\":[{\"id\":1,\"name\":\"MAIN_ACTIVITY\",\"routes\":[{\"postEventInterface\":\"ps5SdkLibrary\",\"eventToStatsLocation\":\"ps5\"}]}]},{\"enumId\":2,\"definitionGroup\":\"autoGenState\",\"sourceId\":\"activity_pattern2\",\"enumValueArray\":[{\"id\":1,\"name\":\"MAIN_ACTIVITY\",\"routes\":[{\"postEventInterface\":\"ps5SdkLibrary\",\"eventToStatsLocation\":\"ps5\"}]}]},{\"enumId\":3,\"definitionGroup\":\"autoGenState\",\"sourceId\":\"actor\",\"enumValueArray\":[]},{\"enumId\":4,\"definitionGroup\":\"autoGenState\",\"sourceId\":\"mechanic\",\"enumValueArray\":[]},{\"enumId\":5,\"definitionGroup\":\"autoGenState\",\"sourceId\":\"zone\",\"enumValueArray\":[]}]}";

    // 2. events_definition.json
    std::string eventsStr = "{\"configurationId\":40142,\"events\":[{\"eventName\":\"_UnlockTrophy\",\"eventType\":\"custom\",\"definitionGroup\":\"trophy\",\"properties\":[{\"property\":\"$._trophy_id\",\"dataType\":\"int32\"}]},{\"eventName\":\"_UpdateTrophyProgress\",\"eventType\":\"custom\",\"definitionGroup\":\"trophy\",\"properties\":[{\"property\":\"$._trophy_id\",\"dataType\":\"int32\"},{\"property\":\"$._trophy_progress\",\"dataType\":\"int32\"}]},{\"eventName\":\"activityStart\",\"eventType\":\"activityStart\",\"definitionGroup\":\"custom\",\"properties\":[{\"property\":\"$.activityId\",\"dataType\":\"string\",\"mappedProperty\":\"activityId\"}]},{\"eventName\":\"activityEnd\",\"eventType\":\"activityEnd\",\"definitionGroup\":\"custom\",\"properties\":[{\"property\":\"$.activityId\",\"dataType\":\"string\",\"mappedProperty\":\"activityId\"}]},{\"eventName\":\"activityAvailabilityChange\",\"eventType\":\"activityAvailabilityChange\",\"definitionGroup\":\"custom\",\"properties\":[{\"property\":\"$.mode\",\"dataType\":\"string\",\"mappedProperty\":\"mode\"},{\"property\":\"$.availableActivities\",\"dataType\":\"array\",\"itemType\":\"string\",\"mappedProperty\":\"availableActivities\"},{\"property\":\"$.unavailableActivities\",\"dataType\":\"array\",\"itemType\":\"string\",\"mappedProperty\":\"unavailableActivities\"}]}],\"nextOffset\":0,\"previousOffset\":0,\"totalItemCount\":5}";

    // 3. stats_definition.json
    std::string statsDefStr = "{\"schemaVersion\":\"1.1\",\"npCommunicationId\":\"" + set.npCommId + "\",\"statsDefinitionRevision\":2011,\"statDefinitionArray\":[{\"statId\":1,\"definitionGroup\":\"autoGenState\",\"sourceId\":\"everPlayedActivities\",\"statName\":\"_autoGenState_everPlayedActivities\",\"dataType\":\"enumSet\",\"aggregation\":\"collection\",\"enumDefinition\":{\"sourceId\":\"activity_pattern1\",\"enumId\":1},\"initialValueArray\":[]}";

    for (const auto& item : set.items) {
        char buf[512];
        snprintf(buf, sizeof(buf),
                 ",{\"statId\":%u,\"definitionGroup\":\"autoGenTrophy\",\"sourceId\":\"%04u\",\"statName\":\"_autoGenTrophy_%04u\",\"routes\":[{\"postEventInterface\":\"ps5SdkLibrary\",\"eventToStatsLocation\":\"ps5\"}],\"dataType\":\"uint64\",\"aggregation\":\"count\",\"maxValue\":\"1\",\"minValue\":\"0\",\"initialValue\":\"0\"}",
                 100 + item.trophyId, item.trophyId, item.trophyId);
        statsDefStr += buf;
    }
    statsDefStr += "]}";

    // 4. stats_extraction.json
    std::string statsExtStr = "{\"schemaVersion\":\"1.1\",\"npCommunicationId\":\"" + set.npCommId + "\",\"statsExtractionRevision\":2011,\"statsExtractionRuleArray\":[{\"ruleId\":1,\"definitionGroup\":\"autoGenState\",\"sourceId\":\"mp_matchStart\",\"condition\":{\"eventName\":\"_matchStart\"},\"action\":{\"input\":\"$._activityId\",\"output\":{\"statId\":1,\"statName\":\"_autoGenState_everPlayedActivities\",\"sourceId\":\"everPlayedActivities\"}}}";

    uint32_t ruleId = 100;
    for (const auto& item : set.items) {
        ++ruleId;
        char buf[512];
        snprintf(buf, sizeof(buf),
                 ",{\"ruleId\":%u,\"definitionGroup\":\"autoGenTrophy\",\"sourceId\":\"%04u\",\"condition\":{\"eventName\":\"_UnlockTrophy\",\"property\":{\"path\":\"$._trophy_id\",\"comparator\":\"==\",\"value\":\"%u\"}},\"action\":{\"output\":{\"statId\":%u,\"statName\":\"_autoGenTrophy_%04u\",\"sourceId\":\"%04u\"}}}",
                 ruleId, item.trophyId, item.trophyId, 100 + item.trophyId, item.trophyId, item.trophyId);
        statsExtStr += buf;
    }
    statsExtStr += "]}";

    struct Entry {
        std::string name;
        const uint8_t* data;
        size_t size;
    };
    std::vector<Entry> entries = {
        {"enum_definition.json", reinterpret_cast<const uint8_t*>(enumStr.data()), enumStr.size()},
        {"events_definition.json", reinterpret_cast<const uint8_t*>(eventsStr.data()), eventsStr.size()},
        {"stats_definition.json", reinterpret_cast<const uint8_t*>(statsDefStr.data()), statsDefStr.size()},
        {"stats_extraction.json", reinterpret_cast<const uint8_t*>(statsExtStr.data()), statsExtStr.size()}
    };

    const uint32_t numFiles = static_cast<uint32_t>(entries.size());
    const uint64_t headerSize = 0x60;
    const uint64_t tableSize = static_cast<uint64_t>(numFiles) * 64;
    uint64_t curOffset = (headerSize + tableSize + 15) & ~15ULL;

    std::vector<uint8_t> fileTable(tableSize, 0);
    std::vector<uint8_t> bodies;

    for (uint32_t i = 0; i < numFiles; ++i) {
        const auto& fe = entries[i];
        uint8_t* row = fileTable.data() + (i * 64);
        std::strncpy(reinterpret_cast<char*>(row), fe.name.c_str(), 31);
        writeBe64(row + 32, curOffset);
        writeBe64(row + 40, fe.size);

        bodies.insert(bodies.end(), fe.data, fe.data + fe.size);
        size_t pad = (16 - (fe.size % 16)) % 16;
        bodies.insert(bodies.end(), pad, 0);
        curOffset += (fe.size + pad);
    }

    uint64_t totalSize = curOffset;
    outUcp.assign(totalSize, 0);
    writeBe32(outUcp.data() + 0x00, 0xb228c60a);
    writeBe32(outUcp.data() + 0x04, 1);
    writeBe64(outUcp.data() + 0x08, totalSize);
    writeBe32(outUcp.data() + 0x10, numFiles);
    writeBe32(outUcp.data() + 0x14, 64);
    writeBe32(outUcp.data() + 0x18, 0);

    std::memcpy(outUcp.data() + headerSize, fileTable.data(), tableSize);
    std::memcpy(outUcp.data() + (headerSize + tableSize), bodies.data(), bodies.size());

    uint8_t sha1Digest[20] = {0};
    mbedtls_sha1(outUcp.data(), outUcp.size(), sha1Digest);
    std::memcpy(outUcp.data() + 0x1c, sha1Digest, 20);

    XC_LOGI("trophy builder: built uds.ucp for %s: %zu bytes", set.npCommId.c_str(), outUcp.size());
    return true;
}

bool buildTrpTitleDat(const TitleTrophySet& set, uint32_t userId, std::vector<uint8_t>& outDat,
                      const uint8_t* ucpSha1,
                      const std::vector<uint8_t>* existingDat) {
    outDat.clear();
    if (set.npCommId.empty()) return false;

    // 1. Gather all unlocked trophies and timestamps
    std::map<uint32_t, uint64_t> unlockedTimes;
    for (const auto& item : set.items) {
        if (item.unlocked) {
            uint64_t ticks = (static_cast<uint64_t>(std::time(nullptr)) + 62135596800ULL) * 1000000ULL;
            unlockedTimes[item.trophyId] = ticks;
        }
    }

    // Also check existing disk file for previously unlocked trophies (append-only)
    std::map<uint32_t, ExistingTrophyRecord> diskTrophies;
    if (existingDat && !existingDat->empty()) {
        parseTrpTitleDat(existingDat->data(), existingDat->size(), diskTrophies);
    } else {
        char dataPath[256];
        snprintf(dataPath, sizeof(dataPath), "/user/home/%08x/trophy2/nobackup/data/%s/TRPTITLE.DAT",
                 userId, set.npCommId.c_str());
        int fd = ::open(dataPath, O_RDONLY);
        if (fd >= 0) {
            struct stat st{};
            if (::fstat(fd, &st) == 0 && st.st_size > 0) {
                std::vector<uint8_t> dBuf(st.st_size);
                ssize_t n = ::read(fd, dBuf.data(), dBuf.size());
                if (n > 0) parseTrpTitleDat(dBuf.data(), static_cast<size_t>(n), diskTrophies);
            }
            ::close(fd);
        }
    }
    for (const auto& [tid, rec] : diskTrophies) {
        if (rec.state == 1 && rec.timestamp > 0) {
            unlockedTimes[tid] = rec.timestamp;
        }
    }

    // 2. Compute or use UCP SHA-1
    std::vector<uint8_t> computedSha1;
    if (!ucpSha1) {
        std::vector<uint8_t> tempUcp;
        if (buildUcp(set, tempUcp, &computedSha1)) {
            ucpSha1 = computedSha1.data();
        }
    }

    // 3. Find template stream (search on-disk first, fallback to embedded)
    std::vector<uint8_t> tStream;
    char homeDirBuf[256];
    snprintf(homeDirBuf, sizeof(homeDirBuf), "/user/home/%08x/trophy2/nobackup/data", userId);
    DIR* dir = ::opendir(homeDirBuf);
    if (dir) {
        struct dirent* ent = nullptr;
        while ((ent = ::readdir(dir)) != nullptr) {
            if (std::strncmp(ent->d_name, "NPWR", 4) != 0 || set.npCommId == ent->d_name) continue;
            char tPath[512];
            snprintf(tPath, sizeof(tPath), "%s/%s/TRPTITLE.DAT", homeDirBuf, ent->d_name);
            int tFd = ::open(tPath, O_RDONLY);
            if (tFd >= 0) {
                struct stat st{};
                if (::fstat(tFd, &st) == 0 && st.st_size >= static_cast<off_t>(TRP_HEAD)) {
                    std::vector<uint8_t> tRaw(st.st_size);
                    ssize_t n = ::read(tFd, tRaw.data(), tRaw.size());
                    if (n > 0 && readTrophyStream(tRaw.data(), static_cast<size_t>(n), tStream)) {
                        XC_LOGI("trophy builder: using on-disk template from %s (%zu bytes)", ent->d_name, tStream.size());
                        ::close(tFd);
                        break;
                    }
                }
                ::close(tFd);
            }
        }
        ::closedir(dir);
    }

    if (tStream.empty()) {
        XC_LOGI("trophy builder: using embedded Animal Well template (%zu bytes)", sizeof(kAnimalWellTemplateStream));
        tStream.assign(kAnimalWellTemplateStream, kAnimalWellTemplateStream + sizeof(kAnimalWellTemplateStream));
    }

    // 4. Build trophy list for target title
    struct TItem { uint8_t id; uint8_t grade; uint8_t hidden; };
    std::vector<TItem> trophyItems;
    trophyItems.push_back({0, 1, 0}); // Platinum
    for (const auto& item : set.items) {
        uint8_t g = 4; // default Bronze
        if (item.grade == TrophyGrade::Platinum) g = 1;
        else if (item.grade == TrophyGrade::Gold) g = 2;
        else if (item.grade == TrophyGrade::Silver) g = 3;
        else if (item.grade == TrophyGrade::Bronze) g = 4;
        trophyItems.push_back({static_cast<uint8_t>(item.trophyId), g, 0});
    }

    const size_t N = trophyItems.size();
    std::vector<uint8_t> ids(N), grades(N), hidden(N);
    for (size_t i = 0; i < N; ++i) {
        ids[i] = trophyItems[i].id;
        grades[i] = trophyItems[i].grade;
        hidden[i] = trophyItems[i].hidden;
    }

    // 5. Construct progress stream s
    static const uint32_t kOrder[9] = {0x1, 0x2, 0x100, 0x300, 0x400, 0x500, 0x600, 0x700, 0x800};
    uint32_t sizes[9], counts[9];
    size_t offsets[9];
    for (int i = 0; i < 9; ++i) {
        if (!dirRecord(tStream.data(), tStream.size(), kOrder[i], &sizes[i], &counts[i], &offsets[i])) {
            XC_LOGW("trophy builder: template missing tag 0x%x", kOrder[i]);
            return false;
        }
    }

    size_t platRec = 0, otherRec = 0;
    for (uint32_t k = 0; k < counts[5]; ++k) {
        size_t r = offsets[5] + k * 0xd0;
        if (readBe32(tStream.data() + r + 0x14) == 1) {
            if (!platRec) platRec = r;
        } else {
            if (!otherRec) otherRec = r;
        }
    }
    if (!otherRec) return false;

    std::vector<uint8_t> s(TRP_MAX, 0);
    std::memcpy(s.data(), tStream.data(), 0x190);
    size_t pos = 0x190;

    size_t at[9];
    for (int i = 0; i < 9; ++i) {
        uint32_t c = (i == 5 || i == 8) ? static_cast<uint32_t>(N) : 1;
        uint8_t* e = s.data() + 0x70 + i * 0x20;
        writeBe32(e, kOrder[i]);
        writeBe32(e + 4, sizes[i]);
        writeBe32(e + 8, 1);
        writeBe32(e + 12, c);
        writeBe64(e + 16, pos - 0x40);
        at[i] = pos;
        pos += (sizes[i] + 0x10) * c;
    }

    size_t blocks = (pos - 0x40 + TRP_CHUNK - 1) / TRP_CHUNK;
    size_t streamSize = 0x40 + blocks * TRP_CHUNK;
    if (streamSize > TRP_MAX) return false;

    for (int i = 0; i < 5; ++i) {
        std::memcpy(s.data() + at[i], tStream.data() + offsets[i], sizes[i] + 0x10);
    }
    if (ucpSha1) {
        std::memcpy(s.data() + at[2] + 0x10, ucpSha1, 20);
    }

    uint8_t* r3 = s.data() + at[3];
    std::memset(r3 + 0x10, 0, 16);
    std::memcpy(r3 + 0x10, set.npCommId.c_str(), std::min(set.npCommId.size(), size_t(16)));
    std::memset(r3 + 0xc0, 0, 16);
    std::memcpy(r3 + 0xc0, set.npCommId.c_str(), std::min(set.npCommId.size(), size_t(16)));
    writeBe32(r3 + 0xe8, 1);
    putMask(r3 + 0x100, ids);

    std::vector<uint8_t> secret;
    for (size_t i = 0; i < N; ++i) if (hidden[i]) secret.push_back(ids[i]);
    putMask(r3 + 0x180, secret);

    for (int g = 1; g <= 4; ++g) {
        std::vector<uint8_t> ofGrade;
        for (size_t i = 0; i < N; ++i) if (grades[i] == g) ofGrade.push_back(ids[i]);
        putMask(r3 + 0x200 + (g - 1) * 0x80, ofGrade);
    }

    putMask(s.data() + at[4] + 0x20, ids);

    std::vector<uint8_t> others;
    for (size_t i = 0; i < N; ++i) if (grades[i] != 1) others.push_back(ids[i]);

    for (size_t i = 0; i < N; ++i) {
        uint8_t* r = s.data() + at[5] + i * 0xd0;
        size_t srcOff = (grades[i] == 1 && platRec) ? platRec : otherRec;
        std::memcpy(r, tStream.data() + srcOff, 0xd0);
        writeBe32(r + 0x10, ids[i]);
        writeBe32(r + 0x14, grades[i]);
        writeBe32(r + 0x18, hidden[i]);
        if (grades[i] == 1) putMask(r + 0x50, others);
    }

    std::memcpy(s.data() + at[6], tStream.data() + offsets[6], 0x200);
    std::memcpy(s.data() + at[7], tStream.data() + offsets[7], 0xc0);

    uint8_t* r6 = s.data() + at[6];
    uint8_t* r7 = s.data() + at[7];
    std::memset(r6 + 0x80, 0, 16);
    std::memset(r6 + 0x180, 0, 16);
    std::memset(r7 + 0x40, 0, 16);
    std::memset(r6 + 0x40, 0, 8);
    std::memset(r6 + 0x70, 0, 8);
    std::memset(r7 + 0x20, 0, 8);

    for (size_t i = 0; i < N; ++i) {
        uint8_t* r = s.data() + at[8] + i * 0x60;
        std::memset(r, 0, 0x60);
        writeBe32(r, 0x800);
        writeBe32(r + 4, 0x50);
        writeBe32(r + 0x10, ids[i]);
    }

    writeBe32(s.data() + 0x24, static_cast<uint32_t>(blocks));

    // 6. Apply unlocks
    int unlockedCount = 0;
    for (const auto& [tid, ticks] : unlockedTimes) {
        if (unlockInStream(s.data(), streamSize, tid, ticks)) {
            unlockedCount++;
        }
    }

    // 7. Pack into finalized disk file with SHA-256 blocks
    if (!writeTrophyFile(s.data(), streamSize, outDat)) {
        return false;
    }

    XC_LOGI("trophy builder: built valid TRPTITLE.DAT for %s (%s, user=0x%08x): %zu bytes, %zu trophies (%d unlocked)",
            set.titleName.c_str(), set.npCommId.c_str(), userId, outDat.size(), N, unlockedCount);
    return true;
}

std::string buildParamJson(const std::string& titleId, const std::string& titleName) {
    TitleDbInfo dbInfo = makeTitleDbInfo(titleId, titleName);
    std::string contentId = dbInfo.contentId;
    std::string conceptId = std::to_string(dbInfo.conceptId);
    std::string name = titleName.empty() ? ("Game " + titleId) : titleName;
    std::string escapedName;
    for (char c : name) {
        if (c == '"') escapedName += "\\\"";
        else escapedName += c;
    }
    return "{\n"
        "  \"ageLevel\": {\n"
        "    \"default\": 12\n"
        "  },\n"
        "  \"appType\": 1,\n"
        "  \"applicationCategoryType\": 0,\n"
        "  \"applicationDrmType\": \"standard\",\n"
        "  \"attribute\": 0,\n"
        "  \"attribute2\": 0,\n"
        "  \"attribute3\": 1835012,\n"
        "  \"conceptId\": \"" + conceptId + "\",\n"
        "  \"contentBadgeType\": 1,\n"
        "  \"contentId\": \"" + contentId + "\",\n"
        "  \"contentVersion\": \"01.000.000\",\n"
        "  \"downloadDataSize\": 0,\n"
        "  \"gameIntent\": {\n"
        "    \"permittedIntents\": [\n"
        "      {\n"
        "        \"intentType\": \"launchActivity\"\n"
        "      }\n"
        "    ]\n"
        "  },\n"
        "  \"localizedParameters\": {\n"
        "    \"defaultLanguage\": \"en-US\",\n"
        "    \"en-US\": {\n"
        "      \"titleName\": \"" + escapedName + "\"\n"
        "    },\n"
        "    \"pt-BR\": {\n"
        "      \"titleName\": \"" + escapedName + "\"\n"
        "    }\n"
        "  },\n"
        "  \"masterVersion\": \"01.00\",\n"
        "  \"pubtools\": {\n"
        "    \"creationDate\": \"2026-10-31 00:00:00\",\n"
        "    \"loudnessSnd0\": \"-33.02\",\n"
        "    \"toolVersion\": \"3.13\"\n"
        "  },\n"
        "  \"requiredSystemSoftwareVersion\": \"0x1220000000000000\",\n"
        "  \"sdkVersion\": \"0x1000000000000000\",\n"
        "  \"titleId\": \"" + titleId + "\",\n"
        "  \"versionFileUri\": \"\"\n"
        "}\n";
}

bool deployTrophyPack(const TitleTrophySet& set, uint32_t userId) {
    if (set.npCommId.empty()) return false;

    std::vector<uint8_t> ucpBytes;
    std::vector<uint8_t> ucpSha1;
    if (!buildUcp(set, ucpBytes, &ucpSha1)) return false;

    std::vector<uint8_t> npBindBytes;
    if (!buildNpBind(set.npCommId, npBindBytes)) return false;

    std::vector<uint8_t> udsBytes;
    buildUdsUcp(set, udsBytes);

    // 1. Conf dir: /user/trophy2/nobackup/conf/<NPWR>/TROPHY.UCP
    std::string confDir = "/user/trophy2/nobackup/conf/" + set.npCommId;
    ensureDir(confDir);
    writeRawFile(confDir + "/TROPHY.UCP", ucpBytes.data(), ucpBytes.size());

    // 2. Data dir: /user/home/<userId>/trophy2/nobackup/data/<NPWR>/TRPTITLE.DAT
    // buildTrpTitleDat automatically merges existing disk trophies and timestamps (append-only)
    std::vector<uint8_t> datBytes;
    if (buildTrpTitleDat(set, userId, datBytes, ucpSha1.data())) {
        char dataDirBuf[256];
        snprintf(dataDirBuf, sizeof(dataDirBuf), "/user/home/%08x/trophy2/nobackup/data/%s",
                 userId, set.npCommId.c_str());
        std::string dataDir = dataDirBuf;
        ensureDir(dataDir);
        writeRawFile(dataDir + "/TRPTITLE.DAT", datBytes.data(), datBytes.size());
    }

    // 3. Target appmeta dirs for homebrew runner (only if verifying PSBox's own title)
    if (set.sonyTitleId == "PPSA99810") {
        const std::string appMetaDirs[] = {
            "/system_data/priv/appmeta/PPSA99810/trophy2",
            "/user/appmeta/PPSA99810/trophy2",
            "/user/app/PPSA99810/sce_sys/trophy2",
            "/data/homebrew/PPSA99810/sce_sys/trophy2"
        };

        for (const auto& dir : appMetaDirs) {
            ensureDir(dir);
            writeRawFile(dir + "/trophy00.ucp", ucpBytes.data(), ucpBytes.size());
            writeRawFile(dir + "/npbind.dat", npBindBytes.data(), npBindBytes.size());
        }

        // Canonical bare npbind.dat for PPSA99810
        writeRawFile("/system_data/priv/appmeta/PPSA99810/npbind.dat", npBindBytes.data(), npBindBytes.size());
        writeRawFile("/user/appmeta/PPSA99810/npbind.dat", npBindBytes.data(), npBindBytes.size());

        // Cronos /system_data/game/temp dirs for PPSA99810 (PS5 10.00+ and 13.60)
        ensureDir("/system_data/game/temp/PPSA99810");
        ensureDir("/system_data/game/temp/PPSA99810/trophy2");
        writeRawFile("/system_data/game/temp/PPSA99810/npbind.dat", npBindBytes.data(), npBindBytes.size());
        writeRawFile("/system_data/game/temp/PPSA99810/trophy2/npbind.dat", npBindBytes.data(), npBindBytes.size());
        writeRawFile("/system_data/game/temp/PPSA99810/trophy2/trophy00.ucp", ucpBytes.data(), ucpBytes.size());
    }

    // 4. Game Title appmeta dirs
    std::string targetTitle = set.sonyTitleId;
    if (!targetTitle.empty() && targetTitle != "PPSA99810") {
        std::string pJson = buildParamJson(targetTitle, set.titleName);
        std::vector<uint8_t> npTitleBytes;
        buildNpTitleDat(targetTitle, npTitleBytes);

        // Load 512x512 icon0 from cache if present across candidate paths
        std::string cachedIconPaths[] = {
            platform::dataDir() + "/trophies/icons/" + set.npCommId + "/icon0.png",
            "/data/psbox/trophies/icons/" + set.npCommId + "/icon0.png",
            "/data/homebrew/PPSA99810/trophies/icons/" + set.npCommId + "/icon0.png",
            "/system_data/priv/appmeta/" + targetTitle + "/icon0.png",
            "/user/appmeta/" + targetTitle + "/icon0.png",
            "/user/app/" + targetTitle + "/sce_sys/icon0.png"
        };
        std::string iconData;
        bool hasCachedIcon = false;
        for (const auto& cip : cachedIconPaths) {
            if (platform::readFile(cip, iconData) && !iconData.empty()) {
                hasCachedIcon = true;
                break;
            }
        }

        const std::string titleDirs[] = {
            "/system_data/priv/appmeta/" + targetTitle,
            "/user/appmeta/" + targetTitle,
            "/user/app/" + targetTitle + "/sce_sys"
        };
        for (const auto& d : titleDirs) {
            ensureDir(d);
            // param.json
            writeRawFile(d + "/param.json", pJson.data(), pJson.size());
            // nptitle.dat (160 bytes, Magic NPTD, Title ID <targetTitle>_00)
            writeRawFile(d + "/nptitle.dat", npTitleBytes.data(), npTitleBytes.size());
            // icon0.png and pic0.png
            std::string iconPath = d + "/icon0.png";
            std::string picPath = d + "/pic0.png";
            if (hasCachedIcon) {
                writeRawFile(iconPath, iconData.data(), iconData.size());
                writeRawFile(picPath, iconData.data(), iconData.size());
            } else {
                struct stat st{};
                if (::stat(iconPath.c_str(), &st) != 0 || st.st_size == 0) {
                    writeRawFile(iconPath, kDefaultIconPng, sizeof(kDefaultIconPng));
                }
                if (::stat(picPath.c_str(), &st) != 0 || st.st_size == 0) {
                    writeRawFile(picPath, kDefaultIconPng, sizeof(kDefaultIconPng));
                }
            }
            // Canonical bare npbind.dat (legacy path)
            writeRawFile(d + "/npbind.dat", npBindBytes.data(), npBindBytes.size());

            // trophy2 subdirectory: trophy00.ucp and npbind.dat
            std::string t2 = d + "/trophy2";
            ensureDir(t2);
            writeRawFile(t2 + "/trophy00.ucp", ucpBytes.data(), ucpBytes.size());
            writeRawFile(t2 + "/npbind.dat", npBindBytes.data(), npBindBytes.size());

            // uds subdirectory: uds00.ucp and npbind.dat
            std::string uds = d + "/uds";
            ensureDir(uds);
            writeRawFile(uds + "/uds00.ucp", udsBytes.data(), udsBytes.size());
            writeRawFile(uds + "/npbind.dat", npBindBytes.data(), npBindBytes.size());
        }

        // Cronos /system_data/game/temp dirs for this title (PS5 10.00+ and 13.60)
        ensureDir("/system_data/game/temp/" + targetTitle);
        ensureDir("/system_data/game/temp/" + targetTitle + "/trophy2");
        writeRawFile("/system_data/game/temp/" + targetTitle + "/npbind.dat", npBindBytes.data(), npBindBytes.size());
        writeRawFile("/system_data/game/temp/" + targetTitle + "/trophy2/npbind.dat", npBindBytes.data(), npBindBytes.size());
        writeRawFile("/system_data/game/temp/" + targetTitle + "/trophy2/trophy00.ucp", ucpBytes.data(), ucpBytes.size());

        ensureDir("/user/app/" + targetTitle);
    }

    // 5. UDS conf dir: /user/np_uds/nobackup/conf/<NPWR>/uds.ucp
    std::string udsConfDir = "/user/np_uds/nobackup/conf/" + set.npCommId;
    ensureDir(udsConfDir);
    writeRawFile(udsConfDir + "/uds.ucp", udsBytes.data(), udsBytes.size());

    // 6. Register title into PS5 system databases (app.db and appinfo.db) autonomously
    if (!targetTitle.empty() && targetTitle != "PPSA99810") {
        TitleDbInfo dbInfo = makeTitleDbInfo(targetTitle, set.titleName);
        registerTitleInDatabase(dbInfo);
    }

    ::sync();
    ::usleep(100000); // 100ms settle for kernel filesystem caches
    XC_LOGI("trophy builder: deployed trophy pack & TRPTITLE.DAT for %s (%s, user=0x%08x) to all system dirs",
            set.titleName.c_str(), set.npCommId.c_str(), userId);
    return true;
}

}  // namespace xc::trophy

