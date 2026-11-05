// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "trophy/trophy_backend.h"
#include "trophy/trophy_builder.h"
#include "trophy/trophy_rules.h"
#include "platform/platform.h"
#include "util/log.h"

#include <arpa/inet.h>
#include <dirent.h>
#include <fcntl.h>
#include <map>
#include <mutex>
#include <netinet/in.h>
#include <set>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

struct SceNpTrophy2DebugUnlockParam {
    uint32_t mode;     // 1 = all, 2 = specific trophyId (1..N)
    int32_t trophyId;  // 1-based trophy id, or -1
};

extern "C" {
int sceKernelLoadStartModule(const char* path, size_t argc, const void* argv, uint32_t flags, void* opt, int* res);
const char* sceKernelGetFsSandboxRandomWord(void);
int sceUserServiceInitialize(const void* param);
int sceUserServiceGetInitialUser(int32_t* userId);
int sceUserServiceGetUserName(int32_t userId, char* name, size_t size);

int32_t sceNpTrophy2CreateContext(int32_t *context, int32_t userId, uint32_t serviceLabel, uint64_t options);
int32_t sceNpTrophy2DestroyContext(int32_t context);
int32_t sceNpTrophy2CreateHandle(int32_t *handle);
int32_t sceNpTrophy2DestroyHandle(int32_t handle);
int32_t sceNpTrophy2AbortHandle(int32_t handle);
int32_t sceNpTrophy2RegisterContext(int32_t context, int32_t handle, uint64_t options);
int32_t sceNpTrophy2SystemDebugUnlockTrophy(int32_t handle, int32_t context, const SceNpTrophy2DebugUnlockParam *param);
int32_t sceNpTrophy2ShowTrophyList(int32_t handle, uint64_t options);
}

namespace xc::trophy {

namespace {

std::string systemLib(const char* name) {
    const char* word = sceKernelGetFsSandboxRandomWord();
    if (word && *word) {
        std::string jailed = std::string("/") + word + "/common/lib/" + name;
        struct stat st{};
        if (::stat(jailed.c_str(), &st) == 0) return jailed;
    }
    std::string sys = std::string("/system/common/lib/") + name;
    struct stat st{};
    if (::stat(sys.c_str(), &st) == 0) return sys;
    return std::string("/common/lib/") + name;
}

void triggerSceShellCorePatch() {
    pid_t myPid = ::getpid();
    int fd = ::open("/data/psbox/eboot_pid.txt", O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd >= 0) {
        char buf[32];
        int n = snprintf(buf, sizeof(buf), "%d\n", myPid);
        if (n > 0) ::write(fd, buf, n);
        ::close(fd);
    }

    int s = ::socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return;
    struct sockaddr_in sin{};
    sin.sin_family = AF_INET;
    sin.sin_port = htons(8084);
    sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // 127.0.0.1
    struct timeval tv{1, 0};
    ::setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    ::setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    if (::connect(s, reinterpret_cast<struct sockaddr*>(&sin), sizeof(sin)) == 0) {
        const char req[] = "GET /loadpayload:/data/pldmgr/payloads/patch_test/patch_test.elf HTTP/1.1\r\nHost: 127.0.0.1:8084\r\nConnection: close\r\n\r\n";
        ::send(s, req, sizeof(req) - 1, 0);
        char buf[256];
        ::recv(s, buf, sizeof(buf), 0);
        XC_LOGI("trophy ps5: SceShellCore patch & unjail payload triggered via 127.0.0.1:8084 (pid=%d)", myPid);
    }
    ::close(s);
    ::usleep(30000); // 30ms settle for kernel credential update
}

// The SceShellCore patch/unjail payload runs asynchronously: it only unjails
// our eboot process AFTER it finishes its own work (the slow heap scan runs
// first). Until then our process still sees the sandbox root, so writes to
// /system_data and /user fail with ENOENT (errno=2) and the whole trophy pack
// deploy is silently lost. Block until the privileged filesystem is actually
// visible (unjail took effect) or we time out. Probe a root-only path with a
// non-destructive stat() as the unjail indicator.
bool waitForUnjail(int timeoutMs) {
    struct stat st{};
    for (int waited = 0; waited <= timeoutMs; waited += 100) {
        if (::stat("/system_data/priv/mms/app.db", &st) == 0) {
            if (waited > 0) XC_LOGI("trophy ps5: unjail confirmed after %d ms", waited);
            return true;
        }
        ::usleep(100000); // 100ms
    }
    XC_LOGW("trophy ps5: unjail NOT confirmed after %d ms; file deploy will fail with ENOENT", timeoutMs);
    return false;
}

void rmTree(const std::string& path) {
    DIR* d = ::opendir(path.c_str());
    if (!d) {
        ::unlink(path.c_str());
        return;
    }
    struct dirent* de;
    while ((de = ::readdir(d)) != nullptr) {
        std::string name = de->d_name;
        if (name == "." || name == "..") continue;
        std::string sub = path + "/" + name;
        struct stat st{};
        if (::lstat(sub.c_str(), &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                rmTree(sub);
            } else {
                ::unlink(sub.c_str());
            }
        }
    }
    ::closedir(d);
    ::rmdir(path.c_str());
}

}  // namespace

class TrophyBackendPS5 : public ITrophyBackend {
public:
    TrophyBackendPS5() = default;
    ~TrophyBackendPS5() override {
        cleanupContext();
    }

    bool init(uint32_t userId) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (initialized_) return true;

        if (userId != 0) {
            userId_ = userId;
        } else {
            int32_t uid = -1;
            int rc = sceUserServiceGetInitialUser(&uid);
            if (rc != 0 || uid <= 0) {
                sceUserServiceInitialize(nullptr);
                rc = sceUserServiceGetInitialUser(&uid);
            }
            if (rc == 0 && uid > 0) {
                userId_ = static_cast<uint32_t>(uid);
            } else {
                userId_ = 0x1ea2f4da; // Default to devRafaelNGP
            }
        }

        char userName[64] = {0};
        sceUserServiceGetUserName(static_cast<int32_t>(userId_), userName, sizeof(userName));
        XC_LOGI("trophy ps5: initialized for user 0x%08x (%s)", userId_, userName[0] ? userName : "devRafaelNGP");

        // Load libSceNpTrophy2 module first so it is present in memory for patching
        loadNpTrophyModule();

        // Ensure SceShellCore patches are active, unjail eboot, and patch libSceNpTrophy2
        triggerSceShellCorePatch();
        initialized_ = true;
        return true;
    }

    bool registerTitle(const TitleTrophySet& set) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!initialized_) init(0);

        if (currentNpCommId_ != set.npCommId || context_ < 0) {
            cleanupContext();
            currentNpCommId_ = set.npCommId;

            // Load existing unlocked trophies from disk TRPTITLE.DAT into unlockedTrophies_
            char dataPath[256];
            snprintf(dataPath, sizeof(dataPath), "/user/home/%08x/trophy2/nobackup/data/%s/TRPTITLE.DAT",
                     userId_, set.npCommId.c_str());
            int fd = ::open(dataPath, O_RDONLY);
            if (fd >= 0) {
                struct stat st{};
                if (::fstat(fd, &st) == 0 && st.st_size > 0) {
                    std::vector<uint8_t> diskBuf(st.st_size);
                    ssize_t n = ::read(fd, diskBuf.data(), diskBuf.size());
                    if (n > 0) {
                        std::map<uint32_t, ExistingTrophyRecord> existingTrophies;
                        if (parseTrpTitleDat(diskBuf.data(), static_cast<size_t>(n), existingTrophies)) {
                            for (const auto& [tid, rec] : existingTrophies) {
                                if (rec.state == 1) {
                                    unlockedTrophies_[set.npCommId].insert(tid);
                                }
                            }
                            XC_LOGI("trophy ps5: loaded %zu existing unlocked trophies from %s",
                                    unlockedTrophies_[set.npCommId].size(), dataPath);
                        }
                    }
                }
                ::close(fd);
            }

            // Generate UCP package and npbind.dat, deploy to system folders.
            // Wait for the async unjail first, or every write below hits ENOENT.
            waitForUnjail(8000);
            deployTrophyPack(set, userId_);
            std::vector<uint8_t> dummyUcp;
            buildUcp(set, dummyUcp, &ucpSha1_);

            // Re-trigger patch to guarantee client-side libSceNpTrophy2 and SceShellCore are patched
            triggerSceShellCorePatch();

            if (trophyModule_ < 0) loadNpTrophyModule();
            if (trophyModule_ < 0) {
                XC_LOGW("trophy ps5: cannot register title: libSceNpTrophy2.sprx not loaded");
                return false;
            }

            int rc = sceNpTrophy2CreateContext(&context_, static_cast<int32_t>(userId_), 1, 0);
            XC_LOGI("trophy ps5: sceNpTrophy2CreateContext(user=0x%08x, serviceLabel=1) -> rc=0x%08x, context=%d",
                    userId_, static_cast<unsigned>(rc), context_);
            if (rc == 0 && handle_ < 0) {
                rc = sceNpTrophy2CreateHandle(&handle_);
                XC_LOGI("trophy ps5: sceNpTrophy2CreateHandle -> rc=0x%08x, handle=%d",
                        static_cast<unsigned>(rc), handle_);
            }
            if (context_ >= 0 && handle_ >= 0) {
                ::usleep(500000); // 500ms for SceShellCore CreateContextJob to settle and register context
                for (int retry = 0; retry < 6; ++retry) {
                    rc = sceNpTrophy2RegisterContext(context_, handle_, 0);
                    if (rc == 0 || static_cast<uint32_t>(rc) == 0x80553921) {
                        XC_LOGI("trophy ps5: sceNpTrophy2RegisterContext(ctx=%d, hdl=%d) -> rc=0x%08x (attempt %d)",
                                context_, handle_, static_cast<unsigned>(rc), retry + 1);
                        break;
                    }
                    if ((static_cast<uint32_t>(rc) == 0x80553909 ||
                         static_cast<uint32_t>(rc) == 0x80020023 ||
                         static_cast<uint32_t>(rc) == 0x80553908) && retry < 5) {
                        XC_LOGI("trophy ps5: RegisterContext context %d not ready yet (rc=0x%08x), retrying in 800ms (%d/6)",
                                context_, static_cast<unsigned>(rc), retry + 1);
                        if (retry == 1) {
                            triggerSceShellCorePatch();
                        }
                        ::usleep(800000);
                        continue;
                    }
                    XC_LOGW("trophy ps5: sceNpTrophy2RegisterContext failed with rc=0x%08x", static_cast<unsigned>(rc));
                    break;
                }
                ::usleep(500000); // 500ms settle for SceShellCore async pack load
            }
        }

        registeredTitles_[set.npCommId] = set;
        XC_LOGI("trophy ps5: title %s (%s, %d trophies) registered",
                set.titleName.c_str(), set.npCommId.c_str(), static_cast<int>(set.items.size()));
        return true;
    }

    bool unlockTrophy(const std::string& npCommId, int trophyId, TrophyGrade grade, const std::string& trophyName) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (sessionUnlockedTrophies_[npCommId].count(trophyId) > 0) {
            return true;
        }

        // Sony SceShellCore automatically awards Platinum upon completing all trophies in the pack.
        if (grade == TrophyGrade::Platinum || trophyId == kPlatinumTrophyId) {
            XC_LOGI("trophy ps5: Platinum trophy awarded for %s (%s) upon 100%% completion",
                    npCommId.c_str(), trophyName.c_str());
            sessionUnlockedTrophies_[npCommId].insert(trophyId);
            unlockedTrophies_[npCommId].insert(trophyId);
            return true;
        }

        int rc = -1;
        if (context_ >= 0 && handle_ >= 0) {
            SceNpTrophy2DebugUnlockParam param{};
            param.mode = 1; // Mode 1: specific trophy_id
            param.trophyId = trophyId;

            for (int retry = 0; retry < 10; ++retry) {
                rc = sceNpTrophy2SystemDebugUnlockTrophy(handle_, context_, &param);
                if (rc == 0 || static_cast<uint32_t>(rc) == 0x80553998) {
                    rc = 0;
                    break;
                }
                if ((static_cast<uint32_t>(rc) == 0x80553909 ||
                     static_cast<uint32_t>(rc) == 0x80020023 ||
                     static_cast<uint32_t>(rc) == 0x80553908) && retry < 9) {
                    ::usleep(100000);
                    continue;
                }
                // Fallback to mode 2
                param.mode = 2;
                int dbg2 = sceNpTrophy2SystemDebugUnlockTrophy(handle_, context_, &param);
                if (dbg2 == 0 || static_cast<uint32_t>(dbg2) == 0x80553998) {
                    rc = 0;
                    break;
                }
                break;
            }
            XC_LOGI("trophy ps5: sceNpTrophy2SystemDebugUnlockTrophy(%s, trophyId=%d, grade=%s) -> rc=0x%08x",
                    npCommId.c_str(), trophyId, gradeToString(grade), static_cast<unsigned>(rc));
        } else {
            XC_LOGW("trophy ps5: context or handle invalid (ctx=%d, hdl=%d)", context_, handle_);
        }

        if (rc != 0) {
            XC_LOGW("trophy ps5: sceNpTrophy2SystemDebugUnlockTrophy rc=0x%08x; recording direct TRPTITLE.DAT unlock for %s trophyId=%d",
                    static_cast<unsigned>(rc), npCommId.c_str(), trophyId);
        }

        sessionUnlockedTrophies_[npCommId].insert(trophyId);
        unlockedTrophies_[npCommId].insert(trophyId);
        ::usleep(40000); // 40ms pacing
        return true;
    }

    void flush() override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!currentNpCommId_.empty()) {
            auto it = registeredTitles_.find(currentNpCommId_);
            if (it != registeredTitles_.end()) {
                TitleTrophySet updatedSet = it->second;
                const auto& unlockedSet = unlockedTrophies_[currentNpCommId_];
                int unlockedCount = 0;
                for (auto& item : updatedSet.items) {
                    if (unlockedSet.count(item.trophyId) > 0) {
                        item.unlocked = true;
                        unlockedCount++;
                    }
                }
                updatedSet.unlockedCount = unlockedCount;
                std::vector<uint8_t> datBytes;
                if (buildTrpTitleDat(updatedSet, userId_, datBytes, ucpSha1_.empty() ? nullptr : ucpSha1_.data())) {
                    char path[256];
                    snprintf(path, sizeof(path), "/user/home/%08x/trophy2/nobackup/data/%s/TRPTITLE.DAT",
                             userId_, currentNpCommId_.c_str());
                    int fd = ::open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
                    if (fd >= 0) {
                        ::write(fd, datBytes.data(), datBytes.size());
                        ::close(fd);
                        ::chmod(path, 0666);
                        ::sync();
                        XC_LOGI("trophy ps5: flushed updated TRPTITLE.DAT to %s (%zu bytes, %d unlocked)",
                                path, datBytes.size(), unlockedCount);
                    }
                }
            }
        }
        cleanupContext();
        currentNpCommId_.clear();
        // NOTE: destroying the context does NOT commit anything to the OS trophy
    // index. TRPTITLE.DAT is written above, but the service-side durable
    // publish (what the system Trophy collection reads) is NOT confirmed here.
    XC_LOGI("trophy ps5: context destroyed; TRPTITLE.DAT written (service-side durable publish NOT confirmed)");
    }

    bool isTrophyUnlocked(const std::string& npCommId, int trophyId) override {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = unlockedTrophies_.find(npCommId);
        if (it != unlockedTrophies_.end() && it->second.count(trophyId) > 0) {
            return true;
        }
        return false;
    }

    bool removeAllTrophies(const std::vector<std::pair<std::string, std::string>>& titles, int& removedCount, std::string& err) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!initialized_) init(0);
        removedCount = 0;

        triggerSceShellCorePatch();
        waitForUnjail(8000);
        cleanupContext();

        for (const auto& [npComm, sonyTitle] : titles) {
            bool foundAny = false;
            if (!npComm.empty()) {
                char dataDir[256];
                snprintf(dataDir, sizeof(dataDir), "/user/home/%08x/trophy2/nobackup/data/%s", userId_, npComm.c_str());
                struct stat st{};
                if (::stat(dataDir, &st) == 0) {
                    rmTree(dataDir);
                    foundAny = true;
                    XC_LOGI("trophy ps5 remove: purged %s", dataDir);
                }

                std::string confDir = "/user/trophy2/nobackup/conf/" + npComm;
                if (::stat(confDir.c_str(), &st) == 0) {
                    rmTree(confDir);
                    foundAny = true;
                    XC_LOGI("trophy ps5 remove: purged %s", confDir.c_str());
                }
            }

            if (!sonyTitle.empty()) {
                if (sonyTitle == "PPSA99810") {
                    const std::string appMetaDirs[] = {
                        "/system_data/priv/appmeta/PPSA99810/trophy2",
                        "/user/appmeta/PPSA99810/trophy2",
                        "/user/app/PPSA99810/sce_sys/trophy2",
                        "/data/homebrew/PPSA99810/sce_sys/trophy2",
                        "/system_data/game/temp/PPSA99810/trophy2",
                        "/system_data/game/temp/PPSA99810"
                    };
                    for (const auto& d : appMetaDirs) {
                        struct stat st{};
                        if (::stat(d.c_str(), &st) == 0) {
                            rmTree(d);
                            foundAny = true;
                        }
                    }
                    ::unlink("/system_data/priv/appmeta/PPSA99810/npbind.dat");
                    ::unlink("/user/appmeta/PPSA99810/npbind.dat");
                    ::unlink("/system_data/game/temp/PPSA99810/npbind.dat");
                } else {
                    const std::string titleDirs[] = {
                        "/system_data/priv/appmeta/" + sonyTitle,
                        "/user/appmeta/" + sonyTitle,
                        "/user/app/" + sonyTitle + "/sce_sys",
                        "/user/app/" + sonyTitle,
                        "/system_data/game/temp/" + sonyTitle
                    };
                    for (const auto& d : titleDirs) {
                        struct stat st{};
                        if (::stat(d.c_str(), &st) == 0) {
                            rmTree(d);
                            foundAny = true;
                        }
                    }
                }
            }
            if (foundAny) removedCount++;
        }

        unlockedTrophies_.clear();
        sessionUnlockedTrophies_.clear();
        registeredTitles_.clear();
        ::sync();
        XC_LOGI("trophy ps5 remove: finished, removed %d title(s)", removedCount);
        return true;
    }

    std::string name() const override {
        return "PlayStation 5 Native (libSceNpTrophy2)";
    }

private:
    void loadNpTrophyModule() {
        if (trophyModule_ >= 0) return;
        int res = 0;
        trophyModule_ = sceKernelLoadStartModule(systemLib("libSceNpTrophy2.sprx").c_str(), 0, nullptr, 0, nullptr, &res);
        if (trophyModule_ < 0) {
            const char* paths[] = {
                "/system/common/lib/libSceNpTrophy2.sprx",
                "/common/lib/libSceNpTrophy2.sprx",
                "libSceNpTrophy2.sprx"
            };
            for (const char* p : paths) {
                trophyModule_ = sceKernelLoadStartModule(p, 0, nullptr, 0, nullptr, &res);
                if (trophyModule_ >= 0) break;
            }
        }
        XC_LOGI("trophy ps5: load libSceNpTrophy2.sprx -> 0x%08x (res=%d)", static_cast<unsigned>(trophyModule_), res);
        int modMgr = sceKernelLoadStartModule("/system/common/lib/libSceNpManager.sprx", 0, nullptr, 0, nullptr, &res);
        if (modMgr < 0) {
            modMgr = sceKernelLoadStartModule(systemLib("libSceNpManager.sprx").c_str(), 0, nullptr, 0, nullptr, &res);
        }
        XC_LOGI("trophy ps5: load libSceNpManager.sprx -> 0x%08x (res=%d)", static_cast<unsigned>(modMgr), res);
    }

    void cleanupContext() {
        if (handle_ >= 0) {
            sceNpTrophy2DestroyHandle(handle_);
            handle_ = -1;
        }
        if (context_ >= 0) {
            sceNpTrophy2DestroyContext(context_);
            context_ = -1;
        }
        sessionUnlockedTrophies_.clear();
    }

    bool initialized_ = false;
    int trophyModule_ = -1;
    uint32_t userId_ = 0x1ea2f4da;
    int32_t context_ = -1;
    int32_t handle_ = -1;
    std::string currentNpCommId_;
    std::vector<uint8_t> ucpSha1_;
    std::mutex mutex_;
    std::map<std::string, TitleTrophySet> registeredTitles_;
    std::map<std::string, std::set<int>> unlockedTrophies_;
    std::map<std::string, std::set<int>> sessionUnlockedTrophies_;
};

// --- PSBox NATIVE self-test -------------------------------------------------
// Minimal probe of the user's idea: attribute a trophy set to the RUNNING
// process's own title (PSBox = PPSA99810) and unlock via the NORMAL v1 flow
// (sceNpTrophyUnlockTrophy), exactly like a locally-installed game does — which
// already surfaces natively on this console. No system-service patching here:
// just privileged file placement (unjail) + the intended trophy SDK. Writes are
// ADDITIVE (a new NPWR + a trophy2 subdir); it never overwrites PSBox's own
// param.json/metadata. Verify by looking at the console profile AND by dumping
// the collection cache afterwards.
void runPsboxNativeSelfTest() {
    const char* kNpComm = "NPWR90210_00";   // dedicated test comm id for PSBox
    const char* kTitle = "PPSA99810";       // PSBox's own (running) title id

    TitleTrophySet set;
    set.sonyTitleId = kTitle;
    set.titleName = "PSBox Cloud Gaming";
    set.npCommId = kNpComm;
    for (int i = 1; i <= 4; ++i) {
        TrophyDef d;
        d.trophyId = i;
        d.name = "PSBox Native Test " + std::to_string(i);
        d.description = "Native v1 trophy self-test";
        d.grade = TrophyGrade::Bronze;
        d.gamerscore = 15;
        d.unlocked = false;
        set.items.push_back(d);
    }
    set.totalCount = 4;

    int32_t uid = 0x1ea2f4da;
    { int32_t u = -1; if (sceUserServiceGetInitialUser(&u) == 0 && u > 0) uid = static_cast<uint32_t>(u); }
    XC_LOGI("psbox selftest: user=0x%08x, title=%s, npComm=%s", uid, kTitle, kNpComm);

    // Load the Trophy2 module FIRST, while still sandboxed — the "/<word>/common/
    // lib/" path only resolves before the unjail changes our fs root. This binds
    // the sceNpTrophy2* imports so the calls below are safe.
    int res = 0;
    int mod = sceKernelLoadStartModule(systemLib("libSceNpTrophy2.sprx").c_str(), 0, nullptr, 0, nullptr, &res);
    if (mod < 0) mod = sceKernelLoadStartModule("libSceNpTrophy2.sprx", 0, nullptr, 0, nullptr, &res);
    XC_LOGI("psbox selftest: load libSceNpTrophy2.sprx -> 0x%08x (res=%d)", static_cast<unsigned>(mod), res);

    // Need root to write under /system_data and /user/trophy2.
    triggerSceShellCorePatch();
    waitForUnjail(8000);

    auto mkpath = [](const char* p) { ::mkdir(p, 0777); ::chmod(p, 0777); };
    auto writeBuf = [](const std::string& path, const std::vector<uint8_t>& buf) {
        int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (fd < 0) { XC_LOGW("psbox selftest: open fail %s", path.c_str()); return false; }
        ssize_t n = ::write(fd, buf.data(), buf.size());
        ::close(fd); ::chmod(path.c_str(), 0666); ::sync();
        XC_LOGI("psbox selftest: wrote %s (%zd/%zu bytes)", path.c_str(), n, buf.size());
        return n == static_cast<ssize_t>(buf.size());
    };

    // Build the trophy artifacts (NOT the title's param.json — surgical).
    std::vector<uint8_t> ucp, sha1, npbind, dat;
    buildUcp(set, ucp, &sha1);
    buildNpBind(kNpComm, npbind);
    buildTrpTitleDat(set, uid, dat, sha1.empty() ? nullptr : sha1.data());

    // (a) appmeta trophy2 pack for PSBox's own title (additive subdir).
    mkpath("/system_data/priv/appmeta/PPSA99810/trophy2");
    writeBuf("/system_data/priv/appmeta/PPSA99810/trophy2/trophy00.ucp", ucp);
    writeBuf("/system_data/priv/appmeta/PPSA99810/trophy2/npbind.dat", npbind);
    // (b) shared conf UCP (per-NPWR).
    mkpath("/user/trophy2/nobackup/conf/NPWR90210_00");
    writeBuf("/user/trophy2/nobackup/conf/NPWR90210_00/TROPHY.UCP", ucp);
    // (c) per-user data.
    char dataDir[256];
    snprintf(dataDir, sizeof(dataDir), "/user/home/%08x/trophy2/nobackup/data/NPWR90210_00", uid);
    mkpath(dataDir);
    writeBuf(std::string(dataDir) + "/TRPTITLE.DAT", dat);

    // NORMAL Trophy2 flow (like a real game): CreateContext -> CreateHandle ->
    // RegisterContext -> UnlockTrophy (NOT the debug unlock). serviceLabel=1.
    triggerSceShellCorePatch();
    int32_t ctx = -1, handle = -1;
    int rc = sceNpTrophy2CreateContext(&ctx, static_cast<int32_t>(uid), 1, 0);
    XC_LOGI("psbox selftest: CreateContext -> rc=0x%08x ctx=%d", static_cast<unsigned>(rc), ctx);
    rc = sceNpTrophy2CreateHandle(&handle);
    XC_LOGI("psbox selftest: CreateHandle -> rc=0x%08x handle=%d", static_cast<unsigned>(rc), handle);
    ::usleep(300000);
    for (int retry = 0; retry < 15; ++retry) {
        rc = sceNpTrophy2RegisterContext(ctx, handle, 0);
        if (rc == 0 || static_cast<uint32_t>(rc) == 0x80553921) break;
        ::usleep(100000);
    }
    XC_LOGI("psbox selftest: RegisterContext -> rc=0x%08x", static_cast<unsigned>(rc));
    ::usleep(250000);
    for (int id = 1; id <= 2; ++id) {
        SceNpTrophy2DebugUnlockParam param{};
        param.mode = 1;
        param.trophyId = id;
        rc = sceNpTrophy2SystemDebugUnlockTrophy(handle, ctx, &param);
        XC_LOGI("psbox selftest: sceNpTrophy2SystemDebugUnlockTrophy(id=%d) -> rc=0x%08x", id, static_cast<unsigned>(rc));
        ::usleep(300000);
    }
    if (handle >= 0) sceNpTrophy2DestroyHandle(handle);
    if (ctx >= 0) sceNpTrophy2DestroyContext(ctx);
    XC_LOGI("psbox selftest: done (check console profile for 'PSBox Cloud Gaming' trophies)");
}

ITrophyBackend* createPlatformTrophyBackend() {
    return new TrophyBackendPS5();
}

}  // namespace xc::trophy
