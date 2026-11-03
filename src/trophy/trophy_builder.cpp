// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "trophy/trophy_builder.h"
#include "platform/platform.h"
#include "util/json.h"
#include "util/log.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <mbedtls/sha1.h>
#include <sys/stat.h>
#include <unistd.h>


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

bool writeRawFile(const std::string& path, const void* data, size_t size) {
    int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) {
        XC_LOGW("trophy builder: failed to open '%s' for write: errno=%d", path.c_str(), errno);
        return false;
    }
    ssize_t written = ::write(fd, data, size);
    ::close(fd);
    ::chmod(path.c_str(), 0666);
    return (written == static_cast<ssize_t>(size));
}

void ensureDir(const std::string& dir) {
    std::string s;
    for (char c : dir) {
        s += c;
        if (c == '/' && s.size() > 1) {
            ::mkdir(s.c_str(), 0777);
            ::chmod(s.c_str(), 0777);
        }
    }
    ::mkdir(dir.c_str(), 0777);
    ::chmod(dir.c_str(), 0777);
}

}  // namespace

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
    if (!data || size < 0x200) return false;
    if (std::memcmp(data, "T2PD", 4) != 0) return false;

    for (int i = 0; i < 9; ++i) {
        size_t descOffset = 0x70 + (i * 32);
        if (descOffset + 32 > size) break;
        uint32_t tag = (static_cast<uint32_t>(data[descOffset + 0]) << 24) |
                       (static_cast<uint32_t>(data[descOffset + 1]) << 16) |
                       (static_cast<uint32_t>(data[descOffset + 2]) << 8)  |
                       (static_cast<uint32_t>(data[descOffset + 3]));
        uint32_t itemLen = (static_cast<uint32_t>(data[descOffset + 4]) << 24) |
                           (static_cast<uint32_t>(data[descOffset + 5]) << 16) |
                           (static_cast<uint32_t>(data[descOffset + 6]) << 8)  |
                           (static_cast<uint32_t>(data[descOffset + 7]));
        uint32_t count = (static_cast<uint32_t>(data[descOffset + 12]) << 24) |
                         (static_cast<uint32_t>(data[descOffset + 13]) << 16) |
                         (static_cast<uint32_t>(data[descOffset + 14]) << 8)  |
                         (static_cast<uint32_t>(data[descOffset + 15]));
        uint64_t relOff = (static_cast<uint64_t>(data[descOffset + 16]) << 56) |
                          (static_cast<uint64_t>(data[descOffset + 17]) << 48) |
                          (static_cast<uint64_t>(data[descOffset + 18]) << 40) |
                          (static_cast<uint64_t>(data[descOffset + 19]) << 32) |
                          (static_cast<uint64_t>(data[descOffset + 20]) << 24) |
                          (static_cast<uint64_t>(data[descOffset + 21]) << 16) |
                          (static_cast<uint64_t>(data[descOffset + 22]) << 8)  |
                          (static_cast<uint64_t>(data[descOffset + 23]));

        if (tag == 0x0500) {
            size_t itemBase = 0x60 + relOff;
            size_t itemTotalSize = 0x10 + itemLen; // 0x10 header + 0xc0 payload = 208 bytes
            for (uint32_t t = 0; t < count; ++t) {
                size_t entryPos = itemBase + (t * itemTotalSize);
                if (entryPos + 40 > size) break;
                uint32_t tid = (static_cast<uint32_t>(data[entryPos + 16]) << 24) |
                               (static_cast<uint32_t>(data[entryPos + 17]) << 16) |
                               (static_cast<uint32_t>(data[entryPos + 18]) << 8)  |
                               (static_cast<uint32_t>(data[entryPos + 19]));
                uint32_t grade = (static_cast<uint32_t>(data[entryPos + 20]) << 24) |
                                 (static_cast<uint32_t>(data[entryPos + 21]) << 16) |
                                 (static_cast<uint32_t>(data[entryPos + 22]) << 8)  |
                                 (static_cast<uint32_t>(data[entryPos + 23]));
                uint32_t state = (static_cast<uint32_t>(data[entryPos + 24]) << 24) |
                                 (static_cast<uint32_t>(data[entryPos + 25]) << 16) |
                                 (static_cast<uint32_t>(data[entryPos + 26]) << 8)  |
                                 (static_cast<uint32_t>(data[entryPos + 27]));
                uint64_t ts = (static_cast<uint64_t>(data[entryPos + 32]) << 56) |
                              (static_cast<uint64_t>(data[entryPos + 33]) << 48) |
                              (static_cast<uint64_t>(data[entryPos + 34]) << 40) |
                              (static_cast<uint64_t>(data[entryPos + 35]) << 32) |
                              (static_cast<uint64_t>(data[entryPos + 36]) << 24) |
                              (static_cast<uint64_t>(data[entryPos + 37]) << 16) |
                              (static_cast<uint64_t>(data[entryPos + 38]) << 8)  |
                              (static_cast<uint64_t>(data[entryPos + 39]));
                outTrophies[tid] = {tid, grade, state, ts};
            }
            return true;
        }
    }
    return false;
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
    platMetaEn.set("name", "Platinum: " + set.titleName);
    platMetaEn.set("detail", "Earn all trophies");
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
    platMetaPt.set("name", "Platina: " + set.titleName);
    platMetaPt.set("detail", "Obtenha todos os troféus");
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
        const uint8_t* data;
        size_t size;
    };
    std::vector<Entry> fileEntries;

    fileEntries.push_back({"icon0_en-US.png", kDefaultIconPng, sizeof(kDefaultIconPng)});
    fileEntries.push_back({"trop0000.png", kDefaultTropPng, sizeof(kDefaultTropPng)});

    for (const auto& item : set.items) {
        char nameBuf[32];
        snprintf(nameBuf, sizeof(nameBuf), "trop%04d.png", item.trophyId);
        fileEntries.push_back({nameBuf, kDefaultTropPng, sizeof(kDefaultTropPng)});
    }

    fileEntries.push_back({"tropconf.json", reinterpret_cast<const uint8_t*>(tropconfStr.data()), tropconfStr.size()});
    fileEntries.push_back({"tropmeta_en-US.json", reinterpret_cast<const uint8_t*>(tropmetaEnStr.data()), tropmetaEnStr.size()});
    fileEntries.push_back({"tropmeta_pt-BR.json", reinterpret_cast<const uint8_t*>(tropmetaPtStr.data()), tropmetaPtStr.size()});

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

    // Load existing unlocked trophies to preserve them 100% (append-only)
    std::map<uint32_t, ExistingTrophyRecord> existingTrophies;
    if (existingDat && !existingDat->empty()) {
        parseTrpTitleDat(existingDat->data(), existingDat->size(), existingTrophies);
    } else {
        char existingPath[256];
        snprintf(existingPath, sizeof(existingPath),
                 "/user/home/%08x/trophy2/nobackup/data/%s/TRPTITLE.DAT",
                 userId, set.npCommId.c_str());
        int eFd = ::open(existingPath, O_RDONLY);
        if (eFd >= 0) {
            struct stat st{};
            if (::fstat(eFd, &st) == 0 && st.st_size > 0) {
                std::vector<uint8_t> diskBuf(st.st_size);
                ssize_t n = ::read(eFd, diskBuf.data(), diskBuf.size());
                if (n > 0) {
                    parseTrpTitleDat(diskBuf.data(), static_cast<size_t>(n), existingTrophies);
                }
            }
            ::close(eFd);
        }
    }

    // If ucpSha1 is not provided, compute it via buildUcp
    std::vector<uint8_t> computedSha1;
    if (!ucpSha1) {
        std::vector<uint8_t> tempUcp;
        if (buildUcp(set, tempUcp, &computedSha1)) {
            ucpSha1 = computedSha1.data();
        }
    }

    // Total count including Platinum (Trophy 0)
    const uint32_t N = static_cast<uint32_t>(set.items.size() + 1);

    const uint32_t TAG1_LEN = 0x30;
    const uint32_t TAG2_LEN = 0x60;
    const uint32_t TAG100_LEN = 0x40;
    const uint32_t TAG300_LEN = 0x480;
    const uint32_t TAG400_LEN = 0x90;
    const uint32_t TAG500_ITEM_LEN = 0xc0;
    const uint32_t TAG600_LEN = 0x1f0;
    const uint32_t TAG700_LEN = 0xb0;
    const uint32_t TAG800_ITEM_LEN = 0x50;

    const uint64_t tag1_rel = 0x150;
    const uint64_t tag2_rel = tag1_rel + 0x10 + TAG1_LEN; // 0x190
    const uint64_t tag100_rel = tag2_rel + 0x10 + TAG2_LEN; // 0x200
    const uint64_t tag300_rel = tag100_rel + 0x10 + TAG100_LEN; // 0x250
    const uint64_t tag400_rel = tag300_rel + 0x10 + TAG300_LEN; // 0x6e0
    const uint64_t tag500_rel = tag400_rel + 0x10 + TAG400_LEN; // 0x780
    const uint64_t tag600_rel = tag500_rel + static_cast<uint64_t>(N) * (0x10 + TAG500_ITEM_LEN); // 0x780 + N * 0xd0
    const uint64_t tag700_rel = tag600_rel + 0x10 + TAG600_LEN;
    const uint64_t tag800_rel = tag700_rel + 0x10 + TAG700_LEN;

    const uint64_t tag800_end = 0x60 + tag800_rel + static_cast<uint64_t>(N) * (0x10 + TAG800_ITEM_LEN);
    const uint64_t total_size = tag800_end + 0x40; // 0x20 pad + 0x20 signature

    outDat.assign(total_size, 0);
    uint8_t* p = outDat.data();

    // 1. T2PD Header
    std::memcpy(p + 0x00, "T2PD", 4);
    writeBe32(p + 0x04, 0x00010000);
    writeBe32(p + 0x08, 0);
    writeBe32(p + 0x0c, 0x0420);
    writeBe64(p + 0x20, 9);

    // 2. T2TD Header
    std::memcpy(p + 0x40, "T2TD", 4);
    writeBe32(p + 0x44, 0x00010000);
    writeBe32(p + 0x48, 9);

    // 3. Descriptors
    struct Desc {
        uint32_t tag;
        uint32_t len;
        uint32_t count;
        uint64_t rel_off;
    };
    Desc descs[9] = {
        {0x0001, TAG1_LEN, 1, tag1_rel},
        {0x0002, TAG2_LEN, 1, tag2_rel},
        {0x0100, TAG100_LEN, 1, tag100_rel},
        {0x0300, TAG300_LEN, 1, tag300_rel},
        {0x0400, TAG400_LEN, 1, tag400_rel},
        {0x0500, TAG500_ITEM_LEN, N, tag500_rel},
        {0x0600, TAG600_LEN, 1, tag600_rel},
        {0x0700, TAG700_LEN, 1, tag700_rel},
        {0x0800, TAG800_ITEM_LEN, N, tag800_rel},
    };
    for (int i = 0; i < 9; ++i) {
        uint8_t* d = p + 0x70 + (i * 32);
        writeBe32(d + 0, descs[i].tag);
        writeBe32(d + 4, descs[i].len);
        writeBe32(d + 8, 1); // unk = 1
        writeBe32(d + 12, descs[i].count);
        writeBe64(d + 16, descs[i].rel_off);
        writeBe64(d + 24, 0);
    }

    // 4. Tag 1 at 0x190
    writeBe32(p + 0x190, 0x0001);
    writeBe32(p + 0x194, TAG1_LEN);
    std::memcpy(p + 0x1a0, kTag1PayloadTemplate, sizeof(kTag1PayloadTemplate));

    // 5. Tag 2 at 0x1d0
    writeBe32(p + 0x1d0, 0x0002);
    writeBe32(p + 0x1d4, TAG2_LEN);
    std::memcpy(p + 0x1e0, kTag2PayloadTemplate, sizeof(kTag2PayloadTemplate));
    writeBe32(p + 0x1e0 + 0x28, userId);

    // 6. Tag 0x0100 at 0x240
    writeBe32(p + 0x240, 0x0100);
    writeBe32(p + 0x244, TAG100_LEN);
    std::memcpy(p + 0x250, kTag100PayloadTemplate, sizeof(kTag100PayloadTemplate));
    if (ucpSha1) {
        std::memcpy(p + 0x250, ucpSha1, 20);
    }

    // 7. Tag 0x0300 at 0x290
    writeBe32(p + 0x290, 0x0300);
    writeBe32(p + 0x294, TAG300_LEN);
    std::memcpy(p + 0x2a0, kTag300PayloadTemplate, sizeof(kTag300PayloadTemplate));
    std::memcpy(p + 0x2a0 + 0x000, set.npCommId.c_str(), std::min(set.npCommId.size(), size_t(16)));
    std::memcpy(p + 0x2a0 + 0x0b0, set.npCommId.c_str(), std::min(set.npCommId.size(), size_t(16)));
    writeBe32(p + 0x2a0 + 0xd8, 780785);

    // Bitmasks for Tag 0x0300
    uint8_t total_mask[32] = {0};
    uint8_t unlocked_mask[32] = {0};
    uint8_t bronze_mask[32] = {0};
    uint8_t silver_mask[32] = {0};
    uint8_t gold_mask[32] = {0};

    total_mask[0] |= 1; // Plat

    struct FinalTrophyState {
        bool unlocked = false;
        uint64_t timestamp = 0;
    };
    std::vector<FinalTrophyState> finalStates(set.items.size());
    size_t unlockedNonPlatCount = 0;

    for (size_t idx = 0; idx < set.items.size(); ++idx) {
        uint32_t tid = static_cast<uint32_t>(idx + 1);
        uint32_t bIdx = tid / 8;
        uint32_t bit = tid % 8;
        if (bIdx < 32) {
            total_mask[bIdx] |= (1 << bit);
            if (set.items[idx].grade == TrophyGrade::Bronze) {
                bronze_mask[bIdx] |= (1 << bit);
            } else if (set.items[idx].grade == TrophyGrade::Silver) {
                silver_mask[bIdx] |= (1 << bit);
            } else if (set.items[idx].grade == TrophyGrade::Gold) {
                gold_mask[bIdx] |= (1 << bit);
            }
        }

        bool isUnlocked = false;
        uint64_t trophyTs = 0x00000000ffffffffULL;

        auto eIt = existingTrophies.find(tid);
        if (eIt != existingTrophies.end() && eIt->second.state == 1) {
            // 100% PRESERVE existing unlocked state and original timestamp from console!
            isUnlocked = true;
            trophyTs = eIt->second.timestamp;
        } else if (set.items[idx].unlocked) {
            // Newly unlocked from Xbox Live!
            isUnlocked = true;
            uint64_t nowUs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
            trophyTs = (nowUs > 0) ? nowUs : (1730400000000000ULL + tid * 1000000ULL);
        }

        finalStates[idx] = {isUnlocked, trophyTs};
        if (isUnlocked) {
            unlockedNonPlatCount++;
            if (bIdx < 32) {
                unlocked_mask[bIdx] |= (1 << bit);
            }
        }
    }

    // Trophy 0: Platinum
    bool platUnlocked = false;
    uint64_t platTs = 0xffffffffffffffffULL;
    auto pIt = existingTrophies.find(0);
    if (pIt != existingTrophies.end() && pIt->second.state == 1) {
        platUnlocked = true;
        platTs = pIt->second.timestamp;
    } else if (unlockedNonPlatCount == set.items.size() && set.items.size() > 0) {
        platUnlocked = true;
        platTs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    }

    if (platUnlocked) {
        unlocked_mask[0] |= 1;
    }

    std::memcpy(p + 0x2a0 + 0x0f0, total_mask, 32);
    std::memcpy(p + 0x2a0 + 0x170, unlocked_mask, 32);
    std::memcpy(p + 0x2a0 + 0x290, bronze_mask, 32);
    std::memcpy(p + 0x2a0 + 0x310, silver_mask, 32);
    std::memcpy(p + 0x2a0 + 0x390, gold_mask, 32);

    // 8. Tag 0x0400 at 0x740
    writeBe32(p + 0x740, 0x0400);
    writeBe32(p + 0x744, TAG400_LEN);
    std::memcpy(p + 0x750 + 0x10, total_mask, 32);

    // 9. Tag 0x0500 at 0x7e0 (N items * 208 bytes)
    const uint64_t base500 = 0x7e0;
    // Trophy 0: Platinum
    writeBe32(p + base500 + 0, 0x0500);
    writeBe32(p + base500 + 4, TAG500_ITEM_LEN);
    writeBe32(p + base500 + 16, 0); // tid = 0
    writeBe32(p + base500 + 20, 1); // grade = 1 (Plat)
    writeBe32(p + base500 + 24, platUnlocked ? 1 : 0);
    writeBe64(p + base500 + 32, platTs);

    uint8_t total_mask_no_plat[32];
    std::memcpy(total_mask_no_plat, total_mask, 32);
    total_mask_no_plat[0] &= ~1;
    std::memcpy(p + base500 + 0x50, total_mask_no_plat, 32);

    // Trophies 1..N-1
    for (size_t idx = 0; idx < set.items.size(); ++idx) {
        uint32_t tid = static_cast<uint32_t>(idx + 1);
        uint64_t pos = base500 + (static_cast<uint64_t>(tid) * 208);
        const auto& item = set.items[idx];
        const auto& st = finalStates[idx];
        uint32_t gVal = 4;
        if (item.grade == TrophyGrade::Platinum) gVal = 1;
        else if (item.grade == TrophyGrade::Gold) gVal = 2;
        else if (item.grade == TrophyGrade::Silver) gVal = 3;

        writeBe32(p + pos + 0, 0x0500);
        writeBe32(p + pos + 4, TAG500_ITEM_LEN);
        writeBe32(p + pos + 16, tid);
        writeBe32(p + pos + 20, gVal);
        writeBe32(p + pos + 24, st.unlocked ? 1 : 0);
        writeBe64(p + pos + 32, st.timestamp);

        // Template flags
        writeBe32(p + pos + 16 + 0x20, 6);
        writeBe32(p + pos + 16 + 0x24, 4);
        writeBe32(p + pos + 16 + 0x28, 2);
        writeBe32(p + pos + 16 + 0x2c, 0);
        writeBe32(p + pos + 16 + 0x30, 0x55000000);
        writeBe32(p + pos + 16 + 0x34, 8);
        writeBe32(p + pos + 16 + 0x38, 0);
        writeBe32(p + pos + 16 + 0x3c, 1);
    }

    // 10. Tag 0x0600
    uint64_t pos600 = 0x60 + tag600_rel;
    writeBe32(p + pos600 + 0, 0x0600);
    writeBe32(p + pos600 + 4, TAG600_LEN);
    std::memcpy(p + pos600 + 16, kTag600PayloadTemplate, sizeof(kTag600PayloadTemplate));

    // 11. Tag 0x0700
    uint64_t pos700 = 0x60 + tag700_rel;
    writeBe32(p + pos700 + 0, 0x0700);
    writeBe32(p + pos700 + 4, TAG700_LEN);

    // 12. Tag 0x0800
    uint64_t pos800 = 0x60 + tag800_rel;
    for (uint32_t tid = 0; tid < N; ++tid) {
        uint64_t entryPos = pos800 + (static_cast<uint64_t>(tid) * 96);
        writeBe32(p + entryPos + 0, 0x0800);
        writeBe32(p + entryPos + 4, TAG800_ITEM_LEN);
        writeBe32(p + entryPos + 16, tid);
    }

    // 13. Trailer signature
    std::memcpy(p + total_size - 32, kTrailerSignatureTemplate, 32);

    XC_LOGI("trophy builder: built TRPTITLE.DAT for %s (%s, user=0x%08x): %zu bytes, %u trophies (%zu unlocked)",
            set.titleName.c_str(), set.npCommId.c_str(), userId, outDat.size(), N,
            unlockedNonPlatCount + (platUnlocked ? 1 : 0));
    return true;
}

std::string buildParamJson(const std::string& titleId, const std::string& titleName) {
    std::string contentId;
    std::string conceptId;
    if (titleId == "PPSA01922") {
        contentId = "UP1477-PPSA01922_00-FORTNITECORE0001";
        conceptId = "210118";
    } else {
        contentId = "UP0001-" + titleId + "_00-0000000000000000";
        conceptId = "10004542";
    }
    std::string name = titleName.empty() ? "Fortnite" : titleName;
    std::string escapedName;
    for (char c : name) {
        if (c == '"') escapedName += "\\\"";
        else escapedName += c;
    }
    char buf[2048];
    snprintf(buf, sizeof(buf),
        "{\n"
        "  \"ageLevel\": {\n"
        "    \"default\": 12\n"
        "  },\n"
        "  \"appType\": 1,\n"
        "  \"applicationCategoryType\": 0,\n"
        "  \"applicationDrmType\": \"standard\",\n"
        "  \"attribute\": 0,\n"
        "  \"attribute2\": 0,\n"
        "  \"attribute3\": 1835012,\n"
        "  \"conceptId\": \"%s\",\n"
        "  \"contentBadgeType\": 1,\n"
        "  \"contentId\": \"%s\",\n"
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
        "      \"titleName\": \"%s\"\n"
        "    },\n"
        "    \"pt-BR\": {\n"
        "      \"titleName\": \"%s\"\n"
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
        "  \"titleId\": \"%s\",\n"
        "  \"versionFileUri\": \"\"\n"
        "}\n",
        conceptId.c_str(), contentId.c_str(), escapedName.c_str(), escapedName.c_str(), titleId.c_str());
    return std::string(buf);
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

    // 3. Target appmeta dirs for homebrew runner
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

    // 4. Game Title appmeta dirs (PPSA01922 and set.sonyTitleId)
    std::vector<std::string> gameTitles;
    std::string targetTitle = set.sonyTitleId.empty() ? "PPSA01922" : set.sonyTitleId;
    if (!targetTitle.empty() && targetTitle != "PPSA99810") {
        gameTitles.push_back(targetTitle);
    }
    if (std::find(gameTitles.begin(), gameTitles.end(), "PPSA01922") == gameTitles.end()) {
        gameTitles.push_back("PPSA01922");
    }

    for (const auto& tid : gameTitles) {
        std::string pJson = buildParamJson(tid, (tid == targetTitle) ? set.titleName : "Fortnite");
        std::vector<uint8_t> npTitleBytes;
        buildNpTitleDat(tid, npTitleBytes);

        const std::string titleDirs[] = {
            "/system_data/priv/appmeta/" + tid,
            "/user/appmeta/" + tid,
            "/user/app/" + tid + "/sce_sys"
        };
        for (const auto& d : titleDirs) {
            ensureDir(d);
            // param.json
            writeRawFile(d + "/param.json", pJson.data(), pJson.size());
            // nptitle.dat (160 bytes, Magic NPTD, Title ID <tid>_00)
            writeRawFile(d + "/nptitle.dat", npTitleBytes.data(), npTitleBytes.size());
            // icon0.png
            std::string iconPath = d + "/icon0.png";
            struct stat st{};
            if (::stat(iconPath.c_str(), &st) != 0 || st.st_size == 0) {
                int srcFd = ::open("/tmp/fortnite_icon0.png", O_RDONLY);
                if (srcFd >= 0) {
                    std::vector<uint8_t> buf(200000);
                    ssize_t n = ::read(srcFd, buf.data(), buf.size());
                    ::close(srcFd);
                    if (n > 0) {
                        writeRawFile(iconPath, buf.data(), static_cast<size_t>(n));
                    } else {
                        writeRawFile(iconPath, kDefaultIconPng, sizeof(kDefaultIconPng));
                    }
                } else {
                    writeRawFile(iconPath, kDefaultIconPng, sizeof(kDefaultIconPng));
                }
            }
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
    }

    // 5. UDS conf dir: /user/np_uds/nobackup/conf/<NPWR>/uds.ucp
    std::string udsConfDir = "/user/np_uds/nobackup/conf/" + set.npCommId;
    ensureDir(udsConfDir);
    writeRawFile(udsConfDir + "/uds.ucp", udsBytes.data(), udsBytes.size());

    ::sync();
    ::usleep(100000); // 100ms settle for kernel filesystem caches
    XC_LOGI("trophy builder: deployed trophy pack & TRPTITLE.DAT for %s (%s, user=0x%08x) to all system dirs",
            set.titleName.c_str(), set.npCommId.c_str(), userId);
    return true;
}


}  // namespace xc::trophy
