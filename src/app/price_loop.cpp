// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// Store prices for the games to buy as they come on screen, kept a day in
// <dataDir>/prices.json; the same thread fetches the description of an
// opened game that has none yet.
#include "app/app_state.h"
#include "platform/platform.h"
#include "ui/strings.h"
#include "util/json.h"
#include "util/log.h"
#include "xcloud/catalog.h"
#include "xcloud/prices.h"

#include <algorithm>
#include <ctime>
#include <map>
#include <memory>

namespace xc::app {
namespace {

std::mutex g_priceMutex;
std::string g_priceMarket, g_priceLanguage;  // set by setPriceMarket() (empty: not yet)
std::unique_ptr<xcloud::GssvClient> g_queueClient;  // g_priceMutex
platform::Thread g_priceThread;
std::atomic<bool> g_stopPrices{false};
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

void startPriceLoop() { platform::startThread(g_priceThread, priceLoop); }

void setPriceMarket(const std::string& market, const std::string& language) {
    std::lock_guard<std::mutex> lock(g_priceMutex);
    g_priceMarket = market;
    g_priceLanguage = language;
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
        kept.set(id, v);
        xcloud::Price p{v["list"].asNumber(), v["msrp"].asNumber(), v["cur"].str()};
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
                g_ui->setDetailInfo(id, p.description, p.publisher, p.categories, p.heroUrl);
            }
            XC_LOGI("details of %s: %s", id.c_str(), ok ? "ok" : err.empty() ? "not in the catalog" : err.c_str());
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
        }
        if (!texts.empty()) {
            g_ui->setPrices(texts);
            if (!platform::writeFileAtomic(pricesPath(), cache.dump())) XC_LOGW("prices: could not save the cache");
        }
    }
}

}  // namespace
}  // namespace xc::app
