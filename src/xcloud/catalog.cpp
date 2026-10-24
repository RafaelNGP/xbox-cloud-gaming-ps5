// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "xcloud/catalog.h"

#include <algorithm>
#include <cctype>
#include <cstring>
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

uint8_t parsePassPlan(const json::Value& passes) {
    if (passes.has("CFQ7TTC0K5DJ")) return kPlanEssential;
    if (passes.has("CFQ7TTC0P85B")) return kPlanPremium;
    return kPlanUltimate;
}

}  // namespace

uint8_t productPlanFromJson(const std::string& passMetadataJson) {
    auto j = json::parse(passMetadataJson);
    return j ? parsePassPlan(*j) : kPlanUltimate;
}

uint8_t planTier(const std::string& subscription) {
    if (subscription == "Essential") return kPlanEssential;
    if (subscription == "Premium") return kPlanPremium;
    if (subscription == "Ultimate") return kPlanUltimate;
    return kPlanNone;
}

bool planIncludes(uint8_t plan, uint8_t gamePlan, bool freeInStore) {
    if (freeInStore) return true;
    if (plan == kPlanNone) return false;
    if (gamePlan == kPlanNone) return true;
    return gamePlan <= plan;
}

bool fetchList(const std::string& siglId, const std::string& market, const std::string& language, ProductList& out,
               std::string& err) {
    net::Request req;
    req.url = "https://catalog.gamepass.com/sigls/v2?id=" + siglId + "&market=" + market + "&language=" + language;
    req.headers = {{"Cache-Control", "no-cache"},
                   {"Pragma", "no-cache"},
                   {"ms-cv", "0.0"},
                   {"calling-app-name", "xcloud-ps5"},
                   {"calling-app-version", "0.1"}};
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
                       {"Cache-Control", "no-cache"},
                       {"Pragma", "no-cache"},
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
            parseContentRatings(p, market, prod.ratingAge, prod.ratingSystem, prod.ratingDescriptors);

            bool hasConsole = false;
            bool hasPc = false;
            auto checkPlatformStr = [&](const std::string& s) {
                if (s == "PC" || s == "Windows.Desktop" || s == "Win32") hasPc = true;
                if (s == "XboxOne" || s == "XboxSeries" || s == "XboxSeriesX" || s == "XboxSeriesS" ||
                    s == "Console" || s == "Xbox360") hasConsole = true;
            };
            if (p.has("Platforms") && p["Platforms"].isArray()) {
                for (const auto& pl : p["Platforms"].items()) checkPlatformStr(pl.str());
            }
            if (p.has("AllowedPlatforms") && p["AllowedPlatforms"].isArray()) {
                for (const auto& pl : p["AllowedPlatforms"].items()) checkPlatformStr(pl.str());
            }
            if (p.has("AvailablePlatforms") && p["AvailablePlatforms"].isArray()) {
                for (const auto& pl : p["AvailablePlatforms"].items()) checkPlatformStr(pl.str());
            }
            if (p.has("SupportedPlatforms") && p["SupportedPlatforms"].isArray()) {
                for (const auto& pl : p["SupportedPlatforms"].items()) checkPlatformStr(pl.str());
            }

            bool catPcOnly = false;
            if (!prod.categories.empty()) {
                bool hasOnlyPc = true;
                for (const auto& cat : prod.categories) {
                    std::string c = cat;
                    for (char& ch : c) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                    if (c != "pc" && c != "pc games" && c != "jogos para pc" && c != "jogos de pc") {
                        hasOnlyPc = false;
                        break;
                    }
                }
                catPcOnly = hasOnlyPc;
            }

            if ((hasPc && !hasConsole) || catPcOnly || (isPcTitle(prod.title) && !hasConsole)) {
                prod.isPcOnly = true;
            }
            if (full) {
                // How it can be played, from the store's attributes, and the
                // lowest Game Pass plan that includes it.
                prod.detailed = true;
                prod.detailedAt = static_cast<int64_t>(std::time(nullptr));
                prod.plan = parsePassPlan(p["PassMetadataByPassProductId"]);
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
            if (auto existing = out.find(id); existing != out.end()) {
                if (prod.heroUrl.empty()) prod.heroUrl = existing->second.heroUrl;
                if (prod.description.empty()) prod.description = existing->second.description;
                if (prod.xcloudTitleId.empty()) prod.xcloudTitleId = existing->second.xcloudTitleId;
                if (prod.ratingAge.empty()) prod.ratingAge = existing->second.ratingAge;
                if (prod.ratingSystem.empty()) prod.ratingSystem = existing->second.ratingSystem;
                if (prod.ratingDescriptors.empty()) prod.ratingDescriptors = existing->second.ratingDescriptors;
                if (prod.publisher.empty()) prod.publisher = existing->second.publisher;
                if (prod.categories.empty()) prod.categories = existing->second.categories;
                if (prod.xboxTitleId.empty()) prod.xboxTitleId = existing->second.xboxTitleId;
                prod.isPcOnly = prod.isPcOnly || existing->second.isPcOnly;
                if (!prod.detailed && existing->second.detailed) {
                    prod.detailed = existing->second.detailed;
                    prod.detailedAt = existing->second.detailedAt;
                    prod.modes = existing->second.modes;
                    prod.plan = existing->second.plan;
                    prod.languages = existing->second.languages;
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

bool isNonGameAddon(const std::string& titleId, const std::string& titleName) {
    static const char* const kBadTitleIdTokens[] = {
        "ARTBOOK",
        "SOUNDTRACK",
        "DIGITALSTORYART",
        "INSTALLPACK",
        "DIGITALEXTRAS",
        "BONUSCONTENT",
    };
    for (const char* token : kBadTitleIdTokens) {
        if (titleId.find(token) != std::string::npos) return true;
    }
    if (!titleName.empty()) {
        std::string lower;
        lower.reserve(titleName.size());
        for (char c : titleName) lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));

        static const char* const kBadNameTokens[] = {
            "artbook",
            "soundtrack",
            "trilha sonora",
            "livro de arte",
            "história e arte",
            "historia e arte",
            "digital story & art",
            "install pack",
            "upgrade pack",
        };
        for (const char* token : kBadNameTokens) {
            if (lower.find(token) != std::string::npos) return true;
        }
    }
    return false;
}

static bool endsWithNoCase(const std::string& s, const char* tail) {
    size_t n = std::strlen(tail);
    if (s.size() < n) return false;
    for (size_t i = 0; i < n; ++i) {
        if (std::tolower(static_cast<unsigned char>(s[s.size() - n + i])) !=
            std::tolower(static_cast<unsigned char>(tail[i]))) {
            return false;
        }
    }
    return true;
}

bool isPcTitle(const std::string& title) {
    if (title.empty()) return false;
    static const char* const kPcTokens[] = {
        " (pc)", " - pc", " [pc]", " for pc", " pc",
        " (windows 10)", " - windows 10", " [windows 10]", " for windows 10",
        " (windows 11)", " - windows 11", " [windows 11]", " for windows 11",
        " (windows)", " - windows", " [windows]", " for windows",
        " (win 10)", " - win 10", " [win 10]",
        " windows 10 edition", " windows 11 edition", " windows edition",
        " pc edition", " (pc edition)", " [pc edition]",
    };
    for (const char* tok : kPcTokens) {
        if (endsWithNoCase(title, tok)) return true;
    }
    std::string lower;
    lower.reserve(title.size());
    for (char c : title) lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    if (lower.find("windows 10 edition") != std::string::npos ||
        lower.find("windows 11 edition") != std::string::npos ||
        lower.find("pc edition") != std::string::npos ||
        lower.find("(pc)") != std::string::npos ||
        lower.find("[pc]") != std::string::npos ||
        lower.find("for windows") != std::string::npos) {
        return true;
    }
    return false;
}

bool isPcProduct(const Product& p) {
    if (p.isPcOnly) return true;
    if (isPcTitle(p.title)) return true;
    if (!p.categories.empty()) {
        bool onlyPc = true;
        for (const auto& cat : p.categories) {
            std::string c = cat;
            for (char& ch : c) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            if (c != "pc" && c != "pc games" && c != "jogos para pc" && c != "jogos de pc") {
                onlyPc = false;
                break;
            }
        }
        if (onlyPc) return true;
    }
    return false;
}

std::string normalizeBaseTitle(const std::string& input) {
    if (input.empty()) return "";

    // 1. Lowercase, normalize dashes (en-dash, em-dash -> '-'), and remove UTF-8 symbols:
    // ® (0xC2 0xAE), © (0xC2 0xA9), ™ (0xE2 0x84 0xA2), smart quotes (‘ ’), apostrophes.
    std::string s;
    s.reserve(input.size());
    for (size_t i = 0; i < input.size(); ) {
        unsigned char c = static_cast<unsigned char>(input[i]);
        if (c == 0xC2 && i + 1 < input.size()) {
            unsigned char c2 = static_cast<unsigned char>(input[i + 1]);
            if (c2 == 0xAE || c2 == 0xA9) {
                i += 2;
                continue;
            }
        } else if (c == 0xE2 && i + 2 < input.size()) {
            unsigned char c2 = static_cast<unsigned char>(input[i + 1]);
            unsigned char c3 = static_cast<unsigned char>(input[i + 2]);
            if (c2 == 0x84 && c3 == 0xA2) {
                i += 3;
                continue;
            }
            if (c2 == 0x80 && (c3 == 0x93 || c3 == 0x94)) {
                // En-dash (–) or Em-dash (—) -> standard '-'
                s.push_back('-');
                i += 3;
                continue;
            }
            if (c2 == 0x80 && (c3 == 0x98 || c3 == 0x99)) {
                // Smart single quotes ‘ or ’ -> drop
                i += 3;
                continue;
            }
        }
        if (c == '\'' || c == '`') {
            ++i;
            continue;
        }
        s.push_back(static_cast<char>(std::tolower(c)));
        ++i;
    }

    auto trim = [](std::string& str) {
        while (!str.empty() && (std::isspace(static_cast<unsigned char>(str.front())) ||
                                str.front() == '-' || str.front() == ':' || str.front() == '/' ||
                                str.front() == '|')) {
            str.erase(str.begin());
        }
        while (!str.empty() && (std::isspace(static_cast<unsigned char>(str.back())) ||
                                str.back() == '-' || str.back() == ':' || str.back() == '/' ||
                                str.back() == '|')) {
            str.pop_back();
        }
    };

    trim(s);

    // 2. Strip platform and edition suffixes iteratively
    static const char* const kSuffixes[] = {
        " (xbox series x|s)",
        " (xbox series x/s)",
        " (xbox series x)",
        " (xbox series s)",
        " [xbox series x|s]",
        " [xbox series x/s]",
        " - xbox series x|s",
        " - xbox series x/s",
        " - xbox series x",
        " - xbox series s",
        " : xbox series x|s",
        " : xbox series x/s",
        " for xbox series x|s",
        " for xbox series x/s",
        " for xbox series x",
        " for xbox series s",
        " xbox series x|s version",
        " xbox series x/s version",
        " versão xbox series x|s",
        " versao xbox series x|s",
        " xbox series x|s",
        " xbox series x/s",
        " xbox series x",
        " xbox series s",
        " (xbox one)",
        " [xbox one]",
        " - xbox one",
        " : xbox one",
        " for xbox one",
        " xbox one version",
        " versão xbox one",
        " versao xbox one",
        " xbox one",
        " (xbox 360)",
        " [xbox 360]",
        " - xbox 360",
        " : xbox 360",
        " for xbox 360",
        " xbox 360",
        " (windows 11)",
        " - windows 11",
        " [windows 11]",
        " for windows 11",
        " windows 11",
        " (windows 10)",
        " - windows 10",
        " [windows 10]",
        " for windows 10",
        " windows 10 edition",
        " windows 10",
        " (windows)",
        " - windows",
        " [windows]",
        " for windows",
        " (win 10)",
        " - win 10",
        " [win 10]",
        " (pc)",
        " - pc",
        " [pc]",
        " pc edition",
        " digital deluxe edition",
        " digital deluxe",
        " deluxe edition",
        " deluxe version",
        " premium edition",
        " premium version",
        " definitive edition",
        " ultimate edition",
        " gold edition",
        " special edition",
        " game of the year edition",
        " goty edition",
        " anniversary edition",
        " enhanced edition",
        " standard edition",
        " standard version",
        " edição padrão",
        " edicao padrao",
        " cross-gen bundle",
        " cross gen bundle",
        " cross-gen edition",
        " cross gen edition",
        " pacote cross-gen",
        " bundle cross-gen",
        " remastered",
        " remaster",
        " remasterizado",
        " remasterizada",
        " edition",
        " edição",
        " edicao",
    };

    bool stripped = true;
    while (stripped) {
        stripped = false;
        trim(s);
        for (const char* suf : kSuffixes) {
            size_t len = std::strlen(suf);
            if (s.size() >= len && s.compare(s.size() - len, len, suf) == 0) {
                s.erase(s.size() - len);
                trim(s);
                stripped = true;
                break;
            }
        }
    }

    // 3. Normalize punctuation and spaces (apostrophes already handled, non-alphanumeric converted to space)
    std::string clean;
    clean.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (std::isalnum(static_cast<unsigned char>(c))) {
            clean.push_back(c);
        } else if (clean.empty() || clean.back() != ' ') {
            clean.push_back(' ');
        }
    }
    trim(clean);

    // 4. Special cases: "aliens dark descent" -> "alien dark descent"
    if (clean.rfind("aliens dark descent", 0) == 0) {
        clean = "alien dark descent" + clean.substr(19);
    }

    return clean;
}

void parseContentRatings(const json::Value& p, const std::string& market,
                         std::string& outAge, std::string& outSystem,
                         std::vector<std::string>& outDescriptors) {
    outAge.clear();
    outSystem.clear();
    outDescriptors.clear();

    struct Candidate {
        std::string system;
        std::string rawId;
        int age = -1;
        std::vector<std::string> descriptors;
    };
    std::vector<Candidate> candidates;

    // Microsoft Game Pass v3 catalog API returns singular "ContentRating" object in hydration
    const json::Value* singleRating = nullptr;
    if (p.has("ContentRating") && p["ContentRating"].isObject()) {
        singleRating = &p["ContentRating"];
    } else if (p.has("MarketProperties") && p["MarketProperties"].isArray() && p["MarketProperties"].size() > 0) {
        const auto& mp0 = p["MarketProperties"][0];
        if (mp0.has("ContentRating") && mp0["ContentRating"].isObject()) {
            singleRating = &mp0["ContentRating"];
        }
    } else if (p.has("LocalizedProperties") && p["LocalizedProperties"].isArray() && p["LocalizedProperties"].size() > 0) {
        const auto& lp0 = p["LocalizedProperties"][0];
        if (lp0.has("ContentRating") && lp0["ContentRating"].isObject()) {
            singleRating = &lp0["ContentRating"];
        }
    }

    if (singleRating) {
        Candidate c;
        c.system = (*singleRating)["RatingSystem"].str();
        if (c.system.empty()) c.system = (*singleRating)["ratingSystem"].str();
        if (c.system.empty()) c.system = (*singleRating)["RatingSystemId"].str();

        c.rawId = (*singleRating)["LongName"].str();
        if (c.rawId.empty()) c.rawId = (*singleRating)["RatingId"].str();
        if (c.rawId.empty()) c.rawId = (*singleRating)["ratingId"].str();
        if (c.rawId.empty()) c.rawId = (*singleRating)["Description"].str();

        if ((*singleRating).has("Age")) {
            c.age = static_cast<int>((*singleRating)["Age"].asInt(-1));
        }

        auto descVal = (*singleRating).has("LocalizedDescriptors") ? (*singleRating)["LocalizedDescriptors"]
                       : (*singleRating).has("RatingDescriptors") ? (*singleRating)["RatingDescriptors"]
                       : (*singleRating)["ratingDescriptors"];
        if (descVal.isArray()) {
            for (const auto& d : descVal.items()) {
                if (d.isString() && !d.str().empty()) c.descriptors.push_back(d.str());
            }
        }
        if ((*singleRating).has("LocalizedInteractiveElements") && (*singleRating)["LocalizedInteractiveElements"].isArray()) {
            for (const auto& d : (*singleRating)["LocalizedInteractiveElements"].items()) {
                if (d.isString() && !d.str().empty()) c.descriptors.push_back(d.str());
            }
        }
        if (!c.rawId.empty() || c.age >= 0) candidates.push_back(std::move(c));
    }

    // Also parse array format if present ("ContentRatings")
    const json::Value* ratingsArray = nullptr;
    if (p.has("ContentRatings") && p["ContentRatings"].isArray()) {
        ratingsArray = &p["ContentRatings"];
    } else if (p.has("MarketProperties") && p["MarketProperties"].isArray() && p["MarketProperties"].size() > 0) {
        const auto& mp0 = p["MarketProperties"][0];
        if (mp0.has("ContentRatings") && mp0["ContentRatings"].isArray()) {
            ratingsArray = &mp0["ContentRatings"];
        }
    } else if (p.has("LocalizedProperties") && p["LocalizedProperties"].isArray() && p["LocalizedProperties"].size() > 0) {
        const auto& lp0 = p["LocalizedProperties"][0];
        if (lp0.has("ContentRatings") && lp0["ContentRatings"].isArray()) {
            ratingsArray = &lp0["ContentRatings"];
        }
    }

    if (ratingsArray) {
        for (const auto& item : ratingsArray->items()) {
            Candidate c;
            c.system = item["RatingSystem"].str();
            if (c.system.empty()) c.system = item["ratingSystem"].str();
            if (c.system.empty()) c.system = item["RatingSystemId"].str();

            c.rawId = item["RatingId"].str();
            if (c.rawId.empty()) c.rawId = item["ratingId"].str();
            if (c.rawId.empty()) c.rawId = item["RatingName"].str();
            if (c.rawId.empty()) c.rawId = item["ratingName"].str();
            if (c.rawId.empty()) c.rawId = item["LongName"].str();
            if (item.has("Age")) c.age = static_cast<int>(item["Age"].asInt(-1));

            auto descVal = item.has("RatingDescriptors") ? item["RatingDescriptors"] : item["ratingDescriptors"];
            if (descVal.isArray()) {
                for (const auto& d : descVal.items()) {
                    if (d.isString()) {
                        if (!d.str().empty()) c.descriptors.push_back(d.str());
                    } else if (d.isObject()) {
                        std::string s = d["Name"].str();
                        if (s.empty()) s = d["Descriptor"].str();
                        if (!s.empty()) c.descriptors.push_back(s);
                    }
                }
            } else if (descVal.isString() && !descVal.str().empty()) {
                c.descriptors.push_back(descVal.str());
            } else if (item.has("RatingDescriptor") && item["RatingDescriptor"].isString()) {
                c.descriptors.push_back(item["RatingDescriptor"].str());
            }

            if (!c.rawId.empty() || c.age >= 0) candidates.push_back(std::move(c));
        }
    }

    if (candidates.empty()) return;

    std::string mkt = market;
    for (char& ch : mkt) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));

    const Candidate* selected = nullptr;
    auto matchSystem = [&](const std::string& sysName) -> const Candidate* {
        for (const auto& c : candidates) {
            std::string sysLower;
            for (char ch : c.system) sysLower += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            std::string targetLower;
            for (char ch : sysName) targetLower += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            if (sysLower.find(targetLower) != std::string::npos) return &c;
        }
        return nullptr;
    };

    if (mkt == "BR" || mkt == "PT") {
        selected = matchSystem("classind");
        if (!selected) selected = matchSystem("djctq");
    } else if (mkt == "US" || mkt == "CA") {
        selected = matchSystem("esrb");
    } else if (mkt == "DE") {
        selected = matchSystem("usk");
        if (!selected) selected = matchSystem("pegi");
    } else if (mkt == "GB" || mkt == "UK" || mkt == "FR" || mkt == "ES" || mkt == "IT") {
        selected = matchSystem("pegi");
    }

    if (!selected) {
        if (mkt == "BR") selected = matchSystem("classind");
        if (!selected) selected = matchSystem("esrb");
        if (!selected) selected = matchSystem("pegi");
        if (!selected) selected = &candidates[0];
    }

    outSystem = selected->system;
    outDescriptors = selected->descriptors;

    std::string raw = selected->rawId;
    std::string rawLower;
    for (char ch : raw) rawLower += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));

    std::string sysLower;
    for (char ch : selected->system) sysLower += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));

    int ageNum = selected->age;
    if (ageNum < 0) {
        try {
            ageNum = std::stoi(raw);
        } catch (...) {}
    }

    if (sysLower.find("classind") != std::string::npos || sysLower.find("djctq") != std::string::npos) {
        outSystem = "ClassInd";
        if (rawLower.find("livre") != std::string::npos || rawLower == "l" || rawLower == "0" || rawLower == "general" || ageNum == 0 || ageNum == 1) outAge = "Livre";
        else if (ageNum == 10 || rawLower.find("10") != std::string::npos || rawLower == "ten") outAge = "10+";
        else if (ageNum == 12 || rawLower.find("12") != std::string::npos || rawLower == "twelve") outAge = "12+";
        else if (ageNum == 14 || rawLower.find("14") != std::string::npos || rawLower == "fourteen") outAge = "14+";
        else if (ageNum == 16 || rawLower.find("16") != std::string::npos || rawLower == "sixteen") outAge = "16+";
        else if (ageNum >= 18 || rawLower.find("18") != std::string::npos || rawLower == "eighteen") outAge = "18+";
        else if (ageNum > 0) outAge = std::to_string(ageNum) + "+";
        else outAge = raw;
    } else if (sysLower.find("esrb") != std::string::npos) {
        outSystem = "ESRB";
        if (ageNum == 10 || rawLower.find("everyone 10") != std::string::npos || rawLower == "e10" || rawLower == "e10+" || rawLower == "everyone10plus") outAge = "E10+";
        else if (ageNum >= 17 || rawLower.find("mature") != std::string::npos || rawLower == "m" || rawLower == "mature 17+") outAge = "M";
        else if (ageNum == 13 || rawLower.find("teen") != std::string::npos || rawLower == "t") outAge = "T";
        else if ((ageNum > 0 && ageNum <= 6) || rawLower.find("everyone") != std::string::npos || rawLower == "e") outAge = "E";
        else if (rawLower.find("adult") != std::string::npos || rawLower == "ao") outAge = "AO";
        else if (rawLower.find("pending") != std::string::npos || rawLower == "rp") outAge = "RP";
        else if (ageNum > 0) outAge = std::to_string(ageNum) + "+";
        else outAge = raw;
    } else if (sysLower.find("pegi") != std::string::npos) {
        outSystem = "PEGI";
        if (ageNum == 3 || rawLower.find("3") != std::string::npos) outAge = "3+";
        else if (ageNum == 7 || rawLower.find("7") != std::string::npos) outAge = "7+";
        else if (ageNum == 12 || rawLower.find("12") != std::string::npos) outAge = "12+";
        else if (ageNum == 16 || rawLower.find("16") != std::string::npos) outAge = "16+";
        else if (ageNum >= 18 || rawLower.find("18") != std::string::npos) outAge = "18+";
        else if (ageNum > 0) outAge = std::to_string(ageNum) + "+";
        else outAge = raw;
    } else {
        outAge = raw.empty() && ageNum > 0 ? std::to_string(ageNum) + "+" : raw;
    }
}

}  // namespace xc::xcloud
