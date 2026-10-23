// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// Store prices for the games to buy as they come on screen, kept a day in
// <dataDir>/prices.json; the same thread fetches the description of an
// opened game that has none yet.
#include "app/app_state.h"
#include "app/settings.h"
#include "platform/toasts.h"
#include "ui/strings.h"
#include "util/json.h"
#include "util/log.h"
#include "xcloud/achievements.h"
#include "xcloud/catalog.h"
#include "xcloud/prices.h"

#include <algorithm>
#include <ctime>
#include <map>
#include <memory>
#include <set>

namespace xc::app {
namespace {

std::mutex g_priceMutex;
std::string g_priceMarket, g_priceLanguage;  // set by setPriceMarket() (empty: not yet)
std::string g_priceXblAuth, g_priceXuid;      // g_priceMutex
std::set<std::string> g_notifiedSale;
std::unique_ptr<xcloud::GssvClient> g_queueClient;  // g_priceMutex
platform::Thread g_priceThread;
std::atomic<bool> g_stopPrices{false};
std::atomic<bool> g_pricesEnded{false};  // the loop returned: joining won't wait
constexpr int64_t kPriceTtlSeconds = 24 * 3600;

std::string pricesPath() { return platform::dataDir() + "/prices.json"; }

ui::PriceInfo priceTexts(const xcloud::Price& p) {
    ui::PriceInfo info;
    info.now = p.list < 0.005 ? ui::tr(ui::Str::Free) : xcloud::formatPrice(p.list, p.currency);
    info.was = p.msrp > p.list + 0.005 ? xcloud::formatPrice(p.msrp, p.currency) : std::string();
    info.list = p.list;
    info.msrp = p.msrp;
    return info;
}

void priceLoop();

}  // namespace

void startPriceLoop() {
    platform::startThread(g_priceThread, [] {
        priceLoop();
        g_pricesEnded = true;
    });
}

bool stopPriceLoop(uint32_t ms) {
    g_stopPrices = true;
    if (!g_priceThread.joinable()) return true;
    // Its pauses check the flag every 300 ms; a request may take longer.
    for (uint64_t start = platform::nowMs(); !g_pricesEnded && platform::nowMs() - start < ms;) platform::sleepMs(20);
    if (!g_pricesEnded) return false;
    g_priceThread.join();
    return true;
}

void setPriceMarket(const std::string& market, const std::string& language) {
    std::lock_guard<std::mutex> lock(g_priceMutex);
    g_priceMarket = market;
    g_priceLanguage = language;
}

void setPriceAuth(const std::string& xblAuth, const std::string& xuid) {
    std::lock_guard<std::mutex> lock(g_priceMutex);
    g_priceXblAuth = xblAuth;
    g_priceXuid = xuid;
}

void setQueueClient(const xcloud::GssvClient* gssv) {
    std::lock_guard<std::mutex> lock(g_priceMutex);
    g_queueClient = gssv ? std::make_unique<xcloud::GssvClient>(*gssv) : nullptr;
}

namespace {

void priceLoop() {
    json::Value cache = json::Value::object();
    {
        std::string text;
        if (platform::readFile(pricesPath(), text))
            if (auto j = json::parse(text)) cache = *j;
    }
    // Fresh cached prices to the UI right away.
    int64_t now = static_cast<int64_t>(std::time(nullptr));
    std::map<std::string, ui::PriceInfo> shown;
    json::Value kept = json::Value::object();
    for (const auto& [id, v] : cache.members()) {
        if (now - v["t"].asInt() > kPriceTtlSeconds) continue;
        xcloud::Price p{v["list"].asNumber(), v["msrp"].asNumber(), v["cur"].str()};
        if (p.list < 0.005) continue;
        kept.set(id, v);
        shown[id] = priceTexts(p);
    }
    cache = kept;
    if (!shown.empty()) g_ui->setPrices(shown);
    uint64_t pauseMs = 0;  // after failed requests: 2 s, doubling to a minute
    while (!g_stopPrices) {
        for (uint64_t slept = 0; slept < pauseMs && !g_stopPrices; slept += 300) platform::sleepMs(300);
        std::string market, language;
        {
            std::lock_guard<std::mutex> lock(g_priceMutex);
            market = g_priceMarket;
            language = g_priceLanguage;
        }
        // The open page's description, for games that only have the light
        // catalog data (games to buy, search results).
        if (std::string id = market.empty() ? std::string() : g_ui->detailWanted(platform::nowMs()); !id.empty()) {
            std::map<std::string, xcloud::Product> full;
            std::string err;
            bool ok = xcloud::fetchProducts({id}, market, language, full, err, true) && full.count(id);
            if (ok) {
                const auto& p = full[id];
                g_ui->setDetailInfo(id, p.description, p.publisher, p.categories, p.heroUrl,
                                    p.ratingAge, p.ratingSystem, p.ratingDescriptors);
            }
            XC_LOGI("details of %s: %s", id.c_str(), ok ? "ok" : err.empty() ? "not in the catalog" : err.c_str());
        }
        // Focused game on Home whose hero art is missing.
        if (std::string id = market.empty() ? std::string() : g_ui->heroWanted(platform::nowMs()); !id.empty()) {
            std::map<std::string, xcloud::Product> full;
            std::string err;
            bool ok = xcloud::fetchProducts({id}, market, language, full, err, true) && full.count(id);
            if (ok) {
                const auto& p = full[id];
                g_ui->setDetailInfo(id, p.description, p.publisher, p.categories, p.heroUrl,
                                    p.ratingAge, p.ratingSystem, p.ratingDescriptors);
            }
            XC_LOGI("hero of %s: %s", id.c_str(), ok ? "ok" : err.empty() ? "not in the catalog" : err.c_str());
        }
        // The open page's queue estimate (a game the account can play).
        if (std::string title = g_ui->queueWanted(platform::nowMs()); !title.empty()) {
            std::unique_ptr<xcloud::GssvClient> client;
            {
                std::lock_guard<std::mutex> lock(g_priceMutex);
                if (g_queueClient) client = std::make_unique<xcloud::GssvClient>(*g_queueClient);
            }
            int seconds = client ? client->titleWaitSeconds(title) : -1;
            XC_LOGI("queue estimate for %s: %d s", title.c_str(), seconds);
            g_ui->setQueueTime(title, seconds);
        }
        // The open page's achievements (if it has an xboxTitleId).
        if (std::string achTitle = g_ui->achievementsWanted(platform::nowMs()); !achTitle.empty()) {
            std::string xblAuth, xuid, lang;
            {
                std::lock_guard<std::mutex> lock(g_priceMutex);
                xblAuth = g_priceXblAuth;
                xuid = g_priceXuid;
                lang = g_priceLanguage;
            }
            if (!xblAuth.empty()) {
                xcloud::TitleAchievements ach;
                std::string err;
                if (xcloud::fetchTitleAchievements(xblAuth, xuid, achTitle, ach, err, lang)) {
                    g_ui->setTitleAchievements(achTitle, ach);
                    XC_LOGI("achievements for %s: %d/%d (%d/%d G)", achTitle.c_str(), ach.unlockedCount,
                            ach.totalCount, ach.unlockedGamerscore, ach.totalGamerscore);
                } else {
                    XC_LOGW("achievements for %s: %s", achTitle.c_str(), err.c_str());
                }
            }
        }
        std::vector<std::string> ids = market.empty() ? std::vector<std::string>() : g_ui->pricesWanted(20, true);
        if (ids.empty()) {
            platform::sleepMs(300);
            continue;
        }
        std::map<std::string, xcloud::Price> got;
        std::string err;
        if (xcloud::fetchPrices(ids, market, language, got, err)) {
            pauseMs = 0;
        } else {
            // Those without a price are asked for again, after a pause (not
            // every game to buy marked asked in seconds while offline).
            std::vector<std::string> again;
            for (const auto& id : ids)
                if (!got.count(id)) again.push_back(id);
            g_ui->pricesFailed(again);
            pauseMs = pauseMs ? std::min<uint64_t>(pauseMs * 2, 60000) : 2000;
            XC_LOGW("%s (next try in %llu s)", err.c_str(), static_cast<unsigned long long>(pauseMs / 1000));
        }
        std::map<std::string, ui::PriceInfo> texts;
        now = static_cast<int64_t>(std::time(nullptr));
        for (const auto& [id, p] : got) {
            texts[id] = priceTexts(p);
            json::Value v = json::Value::object();
            v.set("list", p.list);
            v.set("msrp", p.msrp);
            v.set("cur", p.currency);
            v.set("t", now);
            cache.set(id, v);
            if (p.msrp > p.list + 0.005) {
                bool isWish = false;
                {
                    std::lock_guard<std::mutex> lock(g_settingsMutex);
                    isWish = std::find(g_settings.wishlist.begin(), g_settings.wishlist.end(), id) != g_settings.wishlist.end();
                }
                if (isWish && !g_notifiedSale.count(id)) {
                    g_notifiedSale.insert(id);
                    std::string name = g_ui->tileName(id);
                    if (!name.empty()) {
                        platform::notify(ui::trf(ui::Str::WishlistOnSale, name));
                    }
                }
            }
        }
        if (!texts.empty()) {
            g_ui->setPrices(texts);
            if (!platform::writeFileAtomic(pricesPath(), cache.dump())) XC_LOGW("prices: could not save the cache");
        }
    }
}

}  // namespace
}  // namespace xc::app
