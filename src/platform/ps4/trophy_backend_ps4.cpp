// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "trophy/trophy_backend.h"
#include "trophy/trophy_rules.h"
#include "platform/platform.h"
#include "util/log.h"

#include <map>
#include <mutex>
#include <set>

extern "C" {
int sceSysmoduleLoadModule(uint32_t id);
int sceUserServiceInitialize(const void* param);
int sceUserServiceGetInitialUser(int32_t* userId);
int sceUserServiceGetUserName(int32_t userId, char* name, size_t size);

int32_t sceNpTrophyCreateContext(int32_t *context, int32_t user, uint32_t unk, uint64_t unk2);
int32_t sceNpTrophyDestroyContext(int32_t context);
int32_t sceNpTrophyCreateHandle(int32_t *handle);
int32_t sceNpTrophyDestroyHandle(int32_t handle);
int32_t sceNpTrophyRegisterContext(int32_t context, int32_t handle, uint64_t unk);
int32_t sceNpTrophyUnlockTrophy(int32_t context, int32_t handle, int32_t TrophyID, int32_t *PLATID);
int32_t sceNpTrophyGetTrophyUnlockState(int32_t context, int32_t handle, uint32_t *flags, uint32_t *count);
}

namespace xc::trophy {

class TrophyBackendPS4 : public ITrophyBackend {
public:
    TrophyBackendPS4() = default;
    ~TrophyBackendPS4() override {
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
            }
        }

        char userName[64] = {0};
        sceUserServiceGetUserName(static_cast<int32_t>(userId_), userName, sizeof(userName));
        XC_LOGI("trophy ps4: initialized for user 0x%08x (%s)", userId_, userName[0] ? userName : "User");

        // Load NP Trophy system module (0x009a)
        int mod = sceSysmoduleLoadModule(0x009a);
        XC_LOGI("trophy ps4: sceSysmoduleLoadModule(0x009a) -> 0x%08x", static_cast<unsigned>(mod));
        initialized_ = true;
        return true;
    }

    bool registerTitle(const TitleTrophySet& set) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!initialized_) init(0);

        if (currentNpCommId_ != set.npCommId) {
            cleanupContext();
            currentNpCommId_ = set.npCommId;
        }

        registeredTitles_[set.npCommId] = set;

        if (context_ < 0) {
            int rc = sceNpTrophyCreateContext(&context_, static_cast<int32_t>(userId_), 0, 0);
            XC_LOGI("trophy ps4: sceNpTrophyCreateContext -> rc=0x%08x, context=%d", static_cast<unsigned>(rc), context_);
            if (rc == 0 && handle_ < 0) {
                rc = sceNpTrophyCreateHandle(&handle_);
                XC_LOGI("trophy ps4: sceNpTrophyCreateHandle -> rc=0x%08x, handle=%d", static_cast<unsigned>(rc), handle_);
            }
            if (context_ >= 0 && handle_ >= 0) {
                rc = sceNpTrophyRegisterContext(context_, handle_, 0);
                XC_LOGI("trophy ps4: sceNpTrophyRegisterContext -> rc=0x%08x", static_cast<unsigned>(rc));
            }
        }

        return true;
    }

    bool unlockTrophy(const std::string& npCommId, int trophyId, TrophyGrade grade, const std::string& trophyName) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (isTrophyUnlockedLocked(npCommId, trophyId)) return true;

        int32_t platId = -1;
        int rc = -1;
        if (context_ >= 0 && handle_ >= 0) {
            rc = sceNpTrophyUnlockTrophy(context_, handle_, trophyId, &platId);
            XC_LOGI("trophy ps4: sceNpTrophyUnlockTrophy(%s, id=%d) -> 0x%08x (plat=%d)",
                    npCommId.c_str(), trophyId, static_cast<unsigned>(rc), platId);
        }

        std::string toast = "🏆 " + std::string(gradeToString(grade)) + ": " + (trophyName.empty() ? ("Trophy #" + std::to_string(trophyId)) : trophyName);
        platform::notify(toast);

        unlockedTrophies_[npCommId].insert(trophyId);
        if (platId == kPlatinumTrophyId && trophyId != kPlatinumTrophyId) {
            unlockedTrophies_[npCommId].insert(kPlatinumTrophyId);
        }
        return true;
    }

    bool isTrophyUnlocked(const std::string& npCommId, int trophyId) override {
        std::lock_guard<std::mutex> lock(mutex_);
        return isTrophyUnlockedLocked(npCommId, trophyId);
    }

    std::string name() const override {
        return "PlayStation 4 Native (libSceNpTrophy)";
    }

private:
    bool isTrophyUnlockedLocked(const std::string& npCommId, int trophyId) const {
        auto it = unlockedTrophies_.find(npCommId);
        if (it == unlockedTrophies_.end()) return false;
        return it->second.count(trophyId) > 0;
    }

    void cleanupContext() {
        if (handle_ >= 0) {
            sceNpTrophyDestroyHandle(handle_);
            handle_ = -1;
        }
        if (context_ >= 0) {
            sceNpTrophyDestroyContext(context_);
            context_ = -1;
        }
    }

    bool initialized_ = false;
    uint32_t userId_ = 0x10000000;
    int32_t context_ = -1;
    int32_t handle_ = -1;
    std::string currentNpCommId_;
    std::mutex mutex_;
    std::map<std::string, TitleTrophySet> registeredTitles_;
    std::map<std::string, std::set<int>> unlockedTrophies_;
};

ITrophyBackend* createPlatformTrophyBackend() {
    return new TrophyBackendPS4();
}

}  // namespace xc::trophy
