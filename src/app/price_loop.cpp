// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// Store prices for the games to buy as they come on screen, kept a day in
// <dataDir>/prices.json; the same thread fetches the description of an
// opened game that has none yet.
#include "app/ps5_app.h"
#include "platform/platform.h"
#include "ui/strings.h"
#include "util/json.h"
#include "util/log.h"
#include "xcloud/catalog.h"
#include "xcloud/prices.h"

#include <ctime>
#include <map>

namespace xc::app {
namespace {

std::mutex g_priceMutex;
std::string g_priceMarket, g_priceLanguage;  // set by setPriceMarket() (empty: not yet)
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
    while (!g_stopPrices) {
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
        std::vector<std::string> ids = market.empty() ? std::vector<std::string>() : g_ui->pricesWanted(20, true);
        if (ids.empty()) {
            platform::sleepMs(300);
            continue;
        }
        std::map<std::string, xcloud::Price> got;
        std::string err;
        if (!xcloud::fetchPrices(ids, market, language, got, err)) XC_LOGW("%s", err.c_str());
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
            platform::writeFileAtomic(pricesPath(), cache.dump());
        }
    }
}

}  // namespace
}  // namespace xc::app
