// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "trophy/trophy_backend.h"
#include "trophy/trophy_rules.h"
#include "util/log.h"

#include <map>
#include <mutex>
#include <set>

namespace xc::trophy {

class TrophyBackendNull : public ITrophyBackend {
public:
    TrophyBackendNull() = default;
    ~TrophyBackendNull() override = default;

    bool init(uint32_t userId) override {
        userId_ = userId;
        XC_LOGI("trophy null backend: initialized with user 0x%08x", userId);
        return true;
    }

    bool registerTitle(const TitleTrophySet& set) override {
        std::lock_guard<std::mutex> lock(mutex_);
        registeredTitles_[set.npCommId] = set;
        XC_LOGI("trophy null backend: registered title %s (%s, %d trophies)",
                set.titleName.c_str(), set.npCommId.c_str(), static_cast<int>(set.items.size()));
        return true;
    }

    bool unlockTrophy(const std::string& npCommId, int trophyId, TrophyGrade grade, const std::string& trophyName) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (unlockedTrophies_[npCommId].count(trophyId) > 0) {
            XC_LOGI("trophy null backend: trophy %d for %s already unlocked, skipping", trophyId, npCommId.c_str());
            return true;
        }
        unlockedTrophies_[npCommId].insert(trophyId);
        XC_LOGI("trophy null backend: UNLOCKED trophy %d (%s) for %s - '%s'",
                trophyId, gradeToString(grade), npCommId.c_str(), trophyName.c_str());
        return true;
    }

    bool isTrophyUnlocked(const std::string& npCommId, int trophyId) override {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = unlockedTrophies_.find(npCommId);
        if (it == unlockedTrophies_.end()) return false;
        return it->second.count(trophyId) > 0;
    }

    bool removeAllTrophies(const std::vector<std::pair<std::string, std::string>>& titles, int& removedCount, std::string& err) override {
        std::lock_guard<std::mutex> lock(mutex_);
        removedCount = static_cast<int>(registeredTitles_.size() > 0 ? registeredTitles_.size() : titles.size());
        registeredTitles_.clear();
        unlockedTrophies_.clear();
        XC_LOGI("trophy null backend: removeAllTrophies cleared %d titles", removedCount);
        return true;
    }

    std::string name() const override {
        return "Null / Mock Backend";
    }

private:
    uint32_t userId_ = 0;
    std::mutex mutex_;
    std::map<std::string, TitleTrophySet> registeredTitles_;
    std::map<std::string, std::set<int>> unlockedTrophies_;
};

ITrophyBackend* createPlatformTrophyBackend() {
    return new TrophyBackendNull();
}

void runPsboxNativeSelfTest() {
    XC_LOGI("psbox selftest: no-op on this platform (PS5-only)");
}

}  // namespace xc::trophy
