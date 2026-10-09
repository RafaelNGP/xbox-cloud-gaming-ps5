// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "app/updater.h"

#include "net/http.h"
#include "platform/platform.h"
#include "util/json.h"
#include "util/log.h"

#include <mbedtls/pk.h>
#include <mbedtls/sha256.h>
#if defined(MBEDTLS_PSA_CRYPTO_C)
#include <psa/crypto.h>
#endif

// stb_image's inflate, for the deflated entries (its own static copy).
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#endif
#include "stb_image.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <set>
#include <sys/stat.h>
#include <unistd.h>

namespace xc::app {

const char* const kReleaseKey =
    "-----BEGIN PUBLIC KEY-----\n"
    "MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEGyPKcF2L65xh03F1ZRzfhv48LOWS\n"
    "OBKQ55SVLod7HOZZiViUTbY47qETC8l9A1ez1RflBTH94WILhlHhC4xTeQ==\n"
    "-----END PUBLIC KEY-----\n";

namespace {

constexpr size_t kMaxUnpacked = 256u << 20;  // far above a release (~50 MB)

uint32_t le16(const std::string& s, size_t at) {
    return static_cast<uint8_t>(s[at]) | static_cast<uint8_t>(s[at + 1]) << 8;
}
uint32_t le32(const std::string& s, size_t at) {
    return le16(s, at) | le16(s, at + 2) << 16;
}

bool isDir(const std::string& path) {
    struct stat st {};
    return ::stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}
bool exists(const std::string& path) {
    struct stat st {};
    return ::stat(path.c_str(), &st) == 0;
}

// mkdir -p of the folder holding `file`.
bool makeParents(const std::string& file) {
    for (size_t at = file.find('/', 1); at != std::string::npos; at = file.find('/', at + 1)) {
        std::string d = file.substr(0, at);
        if (!isDir(d) && ::mkdir(d.c_str(), 0777) != 0 && errno != EEXIST) return false;
    }
    return true;
}

void removeTree(const std::string& path) {
    if (DIR* d = ::opendir(path.c_str())) {
        while (dirent* e = ::readdir(d)) {
            std::string name = e->d_name;
            if (name == "." || name == "..") continue;
            std::string p = path + "/" + name;
            if (isDir(p))
                removeTree(p);
            else
                ::unlink(p.c_str());
        }
        ::closedir(d);
        ::rmdir(path.c_str());
    }
}

// Every file under `root`, relative to it.
void listFiles(const std::string& root, const std::string& rel, std::vector<std::string>& out) {
    DIR* d = ::opendir((root + (rel.empty() ? "" : "/" + rel)).c_str());
    if (!d) return;
    while (dirent* e = ::readdir(d)) {
        std::string name = e->d_name;
        if (name == "." || name == "..") continue;
        std::string r = rel.empty() ? name : rel + "/" + name;
        if (isDir(root + "/" + r))
            listFiles(root, r, out);
        else
            out.push_back(r);
    }
    ::closedir(d);
}

bool download(const std::string& url, long size, std::string& out, const std::function<void(double)>& progress,
              std::string& err) {
    net::Request req;
    req.url = url;
    req.followRedirects = true;  // GitHub hands the files out from another host
    req.timeoutMs = 30000;
    req.headers = {{"Accept", "application/octet-stream"}, {"User-Agent", "PSBox-Cloud-Gaming"}};
    out.clear();
    if (size > 0) out.reserve(static_cast<size_t>(size));
    req.onBody = [&](const char* data, size_t len) {
        if (out.size() + len > kMaxUnpacked) return false;
        out.append(data, len);
        return true;
    };
    if (progress)
        req.onProgress = [&](size_t got, long total) {
            if (total <= 0) total = size;
            progress(total > 0 ? std::min(1.0, static_cast<double>(got) / static_cast<double>(total)) : -1);
        };
    auto r = net::perform(req);
    if (!r.ok()) {
        err = "download " + url + ": " + (r.status ? "HTTP " + std::to_string(r.status) : r.error);
        return false;
    }
    if (size > 0 && static_cast<long>(out.size()) != size) {
        err = "download " + url + ": " + std::to_string(out.size()) + " bytes, " + std::to_string(size) + " expected";
        return false;
    }
    return true;
}

// The files in place go back where they were (an update cut short).
bool rollBack(const std::string& dir) {
    std::vector<std::string> old;
    listFiles(dir + "/update.old", "", old);
    for (const auto& rel : old)
        if (!makeParents(dir + "/" + rel) || ::rename((dir + "/update.old/" + rel).c_str(), (dir + "/" + rel).c_str()) != 0)
            XC_LOGE("update: could not put back %s (errno %d)", rel.c_str(), errno);
    return !old.empty();
}

}  // namespace

bool verifySignature(const std::string& data, const std::string& sigDer, const char* publicKeyPem, std::string& err) {
#if defined(MBEDTLS_PSA_CRYPTO_C)
    psa_crypto_init();  // once per process; the TLS setup did it already in the app
#endif
    unsigned char hash[32];
    mbedtls_sha256(reinterpret_cast<const unsigned char*>(data.data()), data.size(), hash, 0);
    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);
    int rc = mbedtls_pk_parse_public_key(&pk, reinterpret_cast<const unsigned char*>(publicKeyPem),
                                         std::strlen(publicKeyPem) + 1);
    if (rc == 0)
        rc = mbedtls_pk_verify(&pk, MBEDTLS_MD_SHA256, hash, sizeof hash,
                               reinterpret_cast<const unsigned char*>(sigDer.data()), sigDer.size());
    mbedtls_pk_free(&pk);
    if (rc != 0) {
        char buf[32];
        std::snprintf(buf, sizeof buf, "-0x%04x", static_cast<unsigned>(-rc));
        err = std::string("the signature doesn't match (") + buf + ")";
    }
    return rc == 0;
}

bool updatablePath(const std::string& path) {
    if (path.empty() || path[0] == '/' || path.find('\\') != std::string::npos ||
        path.find(':') != std::string::npos || path.find('\0') != std::string::npos)
        return false;
    for (size_t start = 0;;) {
        size_t slash = path.find('/', start);
        std::string part = path.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
        if (part.empty() || part == "." || part == "..") return false;
        if (slash == std::string::npos) break;
        start = slash + 1;
    }
    static const std::set<std::string> kUsers = {"account.json", "settings.json", "library.json", "prices.json",
                                                 "xcloud.log",   "autoplay.txt",  "update.journal"};
    std::string top = path.substr(0, path.find('/'));
    return !kUsers.count(path) && top != "imgcache" && top.rfind("update.", 0) != 0 && path.find(".ppm") == std::string::npos;
}

bool unzip(const std::string& zip, const std::string& prefix, std::vector<ZipEntry>& out, std::string& err) {
    out.clear();
    // The end of central directory record: within the last 64 KiB + 22.
    size_t eocd = std::string::npos;
    for (size_t back = 22; back <= std::min<size_t>(zip.size(), 65557); ++back)
        if (le32(zip, zip.size() - back) == 0x06054b50) {
            eocd = zip.size() - back;
            break;
        }
    if (eocd == std::string::npos) {
        err = "not a zip archive";
        return false;
    }
    size_t count = le16(zip, eocd + 10), at = le32(zip, eocd + 16);
    size_t total = 0;
    for (size_t n = 0; n < count; ++n) {
        if (at + 46 > zip.size() || le32(zip, at) != 0x02014b50) {
            err = "damaged zip directory";
            return false;
        }
        uint32_t method = le16(zip, at + 10), packed = le32(zip, at + 20), size = le32(zip, at + 24);
        size_t nameLen = le16(zip, at + 28), extraLen = le16(zip, at + 30), commentLen = le16(zip, at + 32);
        size_t local = le32(zip, at + 42);
        if (at + 46 + nameLen > zip.size()) {
            err = "damaged zip directory";
            return false;
        }
        std::string name = zip.substr(at + 46, nameLen);
        at += 46 + nameLen + extraLen + commentLen;
        if (!name.empty() && name.back() == '/') continue;  // a folder
        if (name.rfind(prefix, 0) != 0 || !updatablePath(name.substr(prefix.size()))) {
            err = "unexpected file in the archive: " + name;
            return false;
        }
        if (local + 30 > zip.size() || le32(zip, local) != 0x04034b50) {
            err = "damaged zip entry " + name;
            return false;
        }
        size_t data = local + 30 + le16(zip, local + 26) + le16(zip, local + 28);
        total += size;
        if (data + packed > zip.size() || total > kMaxUnpacked) {
            err = "damaged zip entry " + name;
            return false;
        }
        ZipEntry e;
        e.path = name.substr(prefix.size());
        if (method == 0 && packed == size) {
            e.data = zip.substr(data, size);
        } else if (method == 8) {
            e.data.resize(size);
            int got = stbi_zlib_decode_noheader_buffer(e.data.data(), static_cast<int>(size), zip.data() + data,
                                                       static_cast<int>(packed));
            if (got != static_cast<int>(size)) {
                err = "could not unpack " + name;
                return false;
            }
        } else {
            err = "unsupported compression in " + name;
            return false;
        }
        out.push_back(std::move(e));
    }
    return true;
}

bool installRelease(const Release& release, const std::string& dir, const std::string& currentVersion,
                    const UpdateProgress& progress, std::string& err) {
    auto report = [&](UpdateStep step, double f) {
        if (progress) progress(step, f);
    };
    if (!isNewerVersion(release.tag, currentVersion)) {
        err = release.tag + " is not newer than " + currentVersion;
        return false;
    }
    if (release.zipUrl.empty() || release.sigUrl.empty()) {
        err = release.tag + " has no signed package";
        return false;
    }
    report(UpdateStep::Downloading, 0);
    std::string sig, zip;
    if (!download(release.sigUrl, -1, sig, nullptr, err)) return false;
    if (!download(release.zipUrl, release.zipSize, zip, [&](double f) { report(UpdateStep::Downloading, f); }, err))
        return false;
    XC_LOGI("update: %s downloaded, %zu bytes", release.tag.c_str(), zip.size());

    report(UpdateStep::Verifying, -1);
    if (!verifySignature(zip, sig, kReleaseKey, err)) return false;
    std::vector<ZipEntry> files;
    if (!unzip(zip, "PPSA99810/", files, err)) return false;
    zip.clear();
    zip.shrink_to_fit();
    // The package must be that version: an older signed one can't be
    // offered again under a new tag.
    std::string wanted = contentVersionOf(release.tag), packaged;
    bool hasEboot = false;
    for (const auto& f : files) {
        hasEboot |= f.path == "eboot.bin";
        if (f.path == "sce_sys/param.json")
            if (auto j = json::parse(f.data)) packaged = (*j)["contentVersion"].str();
    }
    if (!hasEboot || wanted.empty() || packaged != wanted) {
        err = "the package is not " + release.tag + " (contentVersion " + (packaged.empty() ? "?" : packaged) + ")";
        return false;
    }
    XC_LOGI("update: signature ok, %zu files, contentVersion %s", files.size(), packaged.c_str());

    report(UpdateStep::Installing, -1);
    recoverUpdate(dir);  // leftovers of an earlier try
    const std::string staged = dir + "/update.new", old = dir + "/update.old";
    for (const auto& f : files) {
        std::string p = staged + "/" + f.path;
        // As the console's FTP servers leave a package's files: without the
        // execute bit, eboot.bin doesn't start (the restart then fails).
        if (!makeParents(p) || !platform::writeFileAtomic(p, f.data) || ::chmod(p.c_str(), 0777) != 0) {
            err = "could not write " + p;
            removeTree(staged);
            return false;
        }
    }
    // eboot.bin last: until then, a failure leaves the old app whole.
    std::stable_sort(files.begin(), files.end(),
                     [](const ZipEntry& a, const ZipEntry& b) { return a.path != "eboot.bin" && b.path == "eboot.bin"; });
    if (!platform::writeFileAtomic(dir + "/update.journal", "swapping\n")) {
        err = "could not write the update journal";
        removeTree(staged);
        return false;
    }
    for (const auto& f : files) {
        std::string target = dir + "/" + f.path;
        bool moved = !exists(target) || (makeParents(old + "/" + f.path) &&
                                         ::rename(target.c_str(), (old + "/" + f.path).c_str()) == 0);
        if (!moved || !makeParents(target) || ::rename((staged + "/" + f.path).c_str(), target.c_str()) != 0) {
            err = "could not put " + f.path + " in place (errno " + std::to_string(errno) + ")";
            rollBack(dir);
            ::unlink((dir + "/update.journal").c_str());
            removeTree(staged);
            removeTree(old);
            return false;
        }
    }
    ::unlink((dir + "/update.journal").c_str());
    removeTree(staged);
    XC_LOGI("update: %s installed", release.tag.c_str());
    return true;
}

bool recoverUpdate(const std::string& dir) {
    bool rolledBack = false;
    if (exists(dir + "/update.journal")) {
        XC_LOGW("update: the last one was cut short; the old files go back");
        rolledBack = rollBack(dir);
        ::unlink((dir + "/update.journal").c_str());
    }
    removeTree(dir + "/update.new");
    removeTree(dir + "/update.old");
    return rolledBack;
}

}  // namespace xc::app
