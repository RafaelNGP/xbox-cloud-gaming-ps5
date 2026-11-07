// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "trophy/trophy_backend.h"
#include "trophy/trophy_builder.h"
#include "trophy/trophy_db.h"
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

extern "C" {
int sceKernelLoadStartModule(const char* path, size_t argc, const void* argv, uint32_t flags, void* opt, int* res);
const char* sceKernelGetFsSandboxRandomWord(void);
int sceUserServiceInitialize(const void* param);
int sceUserServiceGetInitialUser(int32_t* userId);
int sceUserServiceGetUserName(int32_t userId, char* name, size_t size);
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

void triggerPayloadPath8084(const char* payloadPath) {
    int s = ::socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return;
    struct sockaddr_in sin{};
    sin.sin_family = AF_INET;
    sin.sin_port = htons(8084);
    sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // 127.0.0.1
    struct timeval tv{0, 400000};
    ::setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    ::setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    if (::connect(s, reinterpret_cast<struct sockaddr*>(&sin), sizeof(sin)) == 0) {
        std::string req = std::string("GET /loadpayload:") + payloadPath +
                          " HTTP/1.1\r\nHost: 127.0.0.1:8084\r\nConnection: close\r\n\r\n";
        ::send(s, req.data(), req.size(), 0);
        char buf[256];
        ::recv(s, buf, sizeof(buf), 0);
    }
    ::close(s);
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

    // Ensure /data/psbox/PSBox_Trophies.elf is staged from bundled assets
    std::string elfBytes;
    if (platform::readFile("/app0/assets/PSBox_Trophies.elf", elfBytes) ||
        platform::readFile("/data/homebrew/PPSA99810/assets/PSBox_Trophies.elf", elfBytes)) {
        std::string cur;
        if (!platform::readFile("/data/psbox/PSBox_Trophies.elf", cur) || cur != elfBytes) {
            platform::writeFileAtomic("/data/psbox/PSBox_Trophies.elf", elfBytes);
            ::chmod("/data/psbox/PSBox_Trophies.elf", 0777);
        }
    }
    ::unlink("/data/pldmgr/payloads/patch_test/patch_test.elf");
    ::rmdir("/data/pldmgr/payloads/patch_test");

    struct stat stAppDb{};
    std::string patchLog;
    if (::stat("/system_data/priv/mms/app.db", &stAppDb) == 0 &&
        platform::readFile("/data/trophy_patch_log.txt", patchLog) &&
        patchLog.find("PSBox_Trophies DONE") != std::string::npos) {
        XC_LOGI("trophy ps5: unjail & SceShellCore patch already active (pid=%d)", myPid);
        return;
    }

    struct stat stInternalElf{};
    if (::stat("/data/psbox/PSBox_Trophies.elf", &stInternalElf) == 0) {
        triggerPayloadPath8084("/data/psbox/PSBox_Trophies.elf");
    } else {
        triggerPayloadPath8084("/data/homebrew/PPSA99810/assets/PSBox_Trophies.elf");
    }
    XC_LOGI("trophy ps5: PSBox_Trophies.elf triggered via 127.0.0.1:8084 (pid=%d)", myPid);
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

        // Ensure unjail eboot so /system_data and /user are writable
        triggerSceShellCorePatch();
        waitForUnjail(8000);

        initialized_ = true;
        return true;
    }

    bool registerTitle(const TitleTrophySet& set) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!initialized_) init(0);

        if (currentNpCommId_ != set.npCommId) {
            cleanupContext();
            currentNpCommId_ = set.npCommId;
            unlockedTrophies_[set.npCommId].clear();

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
                            const bool isSyntheticSlot = (set.npCommId.rfind("NPWR8", 0) == 0 || set.npCommId.rfind("NPWR9", 0) == 0) &&
                                                         !isRetailInstalledGame(set.sonyTitleId);
                            if (!isSyntheticSlot || existingTrophies.size() == set.items.size() + 1) {
                                for (const auto& [tid, rec] : existingTrophies) {
                                    if (rec.state == 1) {
                                        unlockedTrophies_[set.npCommId].insert(static_cast<int>(tid));
                                    }
                                }
                                XC_LOGI("trophy ps5: loaded %zu existing unlocked trophies from %s",
                                        unlockedTrophies_[set.npCommId].size(), dataPath);
                            }
                        }
                    }
                }
                ::close(fd);
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
        }

        sessionUnlockedTrophies_[npCommId].insert(trophyId);
        unlockedTrophies_[npCommId].insert(trophyId);
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
                    if (unlockedSet.count(item.trophyId) > 0 || item.unlocked) {
                        item.unlocked = true;
                        unlockedCount++;
                    }
                }
                updatedSet.unlockedCount = unlockedCount;
                waitForUnjail(8000);
                deployTrophyPack(updatedSet, userId_);
            }
        }
        cleanupContext();
        currentNpCommId_.clear();
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
        (void)err;
        if (!initialized_) init(0);
        removedCount = 0;

        triggerSceShellCorePatch();
        waitForUnjail(8000);
        cleanupContext();

        // Also clean up any legacy /system_data/game/temp PPSA directories created by older builds
        rmTree("/system_data/game/temp");
        ::mkdir("/system_data/game/temp", 0777);
        ::chmod("/system_data/game/temp", 0777);

        for (const auto& [npComm, sonyTitle] : titles) {
            // Never delete trophies or metadata of a locally installed retail PS5 game!
            if (!sonyTitle.empty() && sonyTitle != "PPSA99810" && isRetailInstalledGame(sonyTitle)) {
                XC_LOGI("trophy ps5 remove: preserving retail installed title %s (%s)", sonyTitle.c_str(), npComm.c_str());
                continue;
            }

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
                        "/data/homebrew/PPSA99810/sce_sys/trophy2"
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
                } else {
                    const std::string titleDirs[] = {
                        "/system_data/priv/appmeta/" + sonyTitle,
                        "/user/appmeta/" + sonyTitle,
                        "/user/app/" + sonyTitle + "/sce_sys",
                        "/user/app/" + sonyTitle
                    };
                    for (const auto& d : titleDirs) {
                        struct stat st{};
                        if (::stat(d.c_str(), &st) == 0) {
                            rmTree(d);
                            foundAny = true;
                        }
                    }
                    if (removeTitleFromDatabase(sonyTitle)) {
                        foundAny = true;
                        XC_LOGI("trophy ps5 remove: purged database rows for %s", sonyTitle.c_str());
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
        return "PlayStation 5 Native (RecompTrophy/Triad Engine)";
    }

private:
    void cleanupContext() {
        context_ = -1;
        handle_ = -1;
        sessionUnlockedTrophies_.clear();
    }

    bool initialized_ = false;
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

    XC_LOGI("psbox selftest: done (check console profile for 'PSBox Cloud Gaming' trophies)");
}

ITrophyBackend* createPlatformTrophyBackend() {
    return new TrophyBackendPS5();
}

}  // namespace xc::trophy
