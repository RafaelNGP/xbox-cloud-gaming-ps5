// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "xcloud/catalog.h"

#include <ctime>

#include "net/http.h"
#include "util/json.h"

namespace xc::xcloud {

namespace {

std::string imageUrl(const json::Value& v) {
    std::string url = v["URL"].str();
    if (url.rfind("//", 0) == 0) url = "https:" + url;
    return url;
}

std::string describe(const net::Response& r) {
    return r.status == 0 ? r.error : "HTTP " + std::to_string(r.status);
}

}  // namespace

bool fetchList(const std::string& siglId, const std::string& market, const std::string& language, ProductList& out,
               std::string& err) {
    net::Request req;
    req.url = "https://catalog.gamepass.com/sigls/v2?id=" + siglId + "&market=" + market + "&language=" + language;
    auto r = net::perform(req);
    auto j = json::parse(r.body);
    if (!r.ok() || !j || !j->isArray()) {
        err = "catalog list " + siglId + ": " + describe(r);
        return false;
    }
    out = {};
    // Element 0 describes the list; the rest are {"id": "<product id>"}.
    for (size_t i = 0; i < j->size(); ++i) {
        const auto& item = (*j)[i];
        if (item.has("siglId")) {
            out.title = item["title"].str();
        } else if (auto id = item["id"].str(); !id.empty()) {
            out.productIds.push_back(id);
        }
    }
    return true;
}

bool fetchProducts(const std::vector<std::string>& ids, const std::string& market, const std::string& language,
                   std::map<std::string, Product>& out, std::string& err, bool full) {
    const size_t kBatch = full ? 20 : 60;
    for (size_t i = 0; i < ids.size(); i += kBatch) {
        json::Value list = json::Value::array();
        for (size_t k = i; k < ids.size() && k < i + kBatch; ++k) list.push(ids[k]);
        json::Value body = json::Value::object();
        body.set("Products", list);

        net::Request req;
        req.method = "POST";
        req.url = "https://catalog.gamepass.com/v3/products?market=" + market + "&language=" + language +
                  "&hydration=" + (full ? "RemoteHighSapphire0" : "RemoteLowJade0");
        req.headers = {{"Content-Type", "application/json"},
                       {"ms-cv", "0.0"},
                       {"calling-app-name", "xcloud-ps5"},
                       {"calling-app-version", "0.1"}};
        req.body = body.dump();
        auto r = net::perform(req);
        auto j = json::parse(r.body);
        if (!r.ok() || !j) {
            err = "catalog products: " + describe(r);
            return false;
        }
        for (const auto& [id, p] : (*j)["Products"].members()) {
            Product prod;
            prod.productId = id;
            prod.xcloudTitleId = p["XCloudTitleId"].str();
            prod.title = p["ProductTitle"].str();
            prod.xboxTitleId = p["XboxTitleId"].str();
            if (prod.xboxTitleId.empty()) prod.xboxTitleId = p["ChildXboxTitleIds"][0].str();
            prod.publisher = p["PublisherName"].str();
            prod.description = p["ProductDescriptionShort"].str();
            if (prod.description.empty()) prod.description = p["ProductDescription"].str();
            prod.tileUrl = imageUrl(p["Image_Tile"]);
            prod.posterUrl = imageUrl(p["Image_Poster"]);
            prod.heroUrl = imageUrl(p["Image_Hero"]);
            if (prod.heroUrl.empty()) prod.heroUrl = imageUrl(p["Image_TitledHero"]);
            for (const auto& c : p["LocalizedCategories"].items()) prod.categories.push_back(categoryName(c.str()));
            if (full) {
                // How it can be played, from the store's attributes.
                prod.detailed = true;
                prod.detailedAt = static_cast<int64_t>(std::time(nullptr));
                for (const auto& a : p["Attributes"].items()) {
                    std::string n = a["Name"].str();
                    if (n == "SinglePlayer") prod.modes |= kModeSingle;
                    if (n == "XblOnlineMultiPlayer" || n == "XblCrossPlatformMultiPlayer" || n == "XboxLiveCrossGenMP")
                        prod.modes |= kModeOnlineMulti;
                    if (n == "XblOnlineCoop" || n == "XblCrossPlatformCoop") prod.modes |= kModeOnlineCoop;
                    if (n == "XblLocalMultiPlayer" || n == "XblLocalCoop" || n == "SharedSplitScreen") prod.modes |= kModeLocal;
                }
                // What is translated, per language: "pt-BR" and "pt-PT" both count as "pt".
                for (const auto& [locale, support] : p["LanguageSupport"].members()) {
                    std::string lang = locale.substr(0, locale.find('-'));
                    for (const char* kept : kKeptLanguages) {
                        if (lang != kept) continue;
                        uint8_t bits = 0;
                        if (support["InterfaceLanguageSupport"].asInt(0)) bits |= kLangInterface;
                        if (support["SubtitlesLanguageSupport"].asInt(0)) bits |= kLangSubtitles;
                        if (support["GamePlayAudioLanguageSupport"].asInt(0)) bits |= kLangAudio;
                        prod.languages[lang] |= bits;
                    }
                }
            }
            out[id] = std::move(prod);
        }
    }
    return true;
}

std::string categoryName(const std::string& storeName) {
    static const char* const kMoba[] = {"Multi-Player Online Battle Arena", "Multi-player Online Battle Arena",
                                        "Arena de batalha online para vários jogadores",
                                        "Campo de batalla en línea para varios jugadores",
                                        "Arène de combat multijoueur en ligne", "Multiplayer-Onlinekampfarena",
                                        "Arena per combattimenti online multiplayer"};
    for (const char* name : kMoba)
        if (storeName == name) return "MOBA";
    return storeName;
}

}  // namespace xc::xcloud
