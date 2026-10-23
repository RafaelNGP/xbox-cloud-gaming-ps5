// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "app/library.h"

#include "platform/platform.h"
#include "ui/strings.h"
#include "util/json.h"
#include "util/log.h"
#include "xcloud/catalog.h"
#include "xcloud/purchase.h"
#include "xcloud/social.h"
#include "xcloud/titlehub.h"

#include <algorithm>
#include <cstring>
#include <ctime>
#include <map>

namespace xc::app {

namespace {

constexpr int kCacheVersion = 2;

struct ListSpec {
    const char* sigl;
    size_t limit;
};

json::Value productToJson(const xcloud::Product& p) {
    json::Value v = json::Value::object();
    v.set("xt", p.xcloudTitleId);
    v.set("xbox", p.xboxTitleId);
    v.set("title", p.title);
    v.set("publisher", p.publisher);
    v.set("description", p.description);
    v.set("tile", p.tileUrl);
    v.set("poster", p.posterUrl);
    v.set("hero", p.heroUrl);
    json::Value cats = json::Value::array();
    for (const auto& c : p.categories) cats.push(c);
    v.set("categories", cats);
    if (p.detailed) {
        v.set("d", true);
        v.set("at", p.detailedAt);
        v.set("modes", static_cast<int64_t>(p.modes));
        if (p.plan) v.set("plan", static_cast<int64_t>(p.plan));
        json::Value langs = json::Value::object();
        for (const auto& [lang, bits] : p.languages) langs.set(lang, static_cast<int64_t>(bits));
        v.set("langs", langs);
    }
    if (!p.ratingAge.empty()) {
        v.set("rage", p.ratingAge);
        v.set("rsys", p.ratingSystem);
        json::Value rdesc = json::Value::array();
        for (const auto& d : p.ratingDescriptors) rdesc.push(d);
        v.set("rdesc", rdesc);
    }
    return v;
}

xcloud::Product productFromJson(const std::string& id, const json::Value& v) {
    xcloud::Product p;
    p.productId = id;
    p.xcloudTitleId = v["xt"].str();
    if (p.xcloudTitleId == "None") p.xcloudTitleId.clear();
    p.xboxTitleId = v["xbox"].str();
    p.title = v["title"].str();
    p.publisher = v["publisher"].str();
    p.description = v["description"].str();
    p.tileUrl = v["tile"].str();
    p.posterUrl = v["poster"].str();
    p.heroUrl = v["hero"].str();
    for (const auto& c : v["categories"].items()) p.categories.push_back(xcloud::categoryName(c.str()));
    p.detailed = v["d"].asBool(false);
    // Details from before their date was kept count from now (not all due at once).
    p.detailedAt = v["at"].asInt(static_cast<int64_t>(std::time(nullptr)));
    p.modes = static_cast<uint32_t>(v["modes"].asInt(0));
    p.plan = static_cast<uint8_t>(v["plan"].asInt(0));
    for (const auto& [lang, bits] : v["langs"].members()) p.languages[lang] = static_cast<uint8_t>(bits.asInt(0));
    p.ratingAge = v["rage"].str();
    p.ratingSystem = v["rsys"].str();
    for (const auto& d : v["rdesc"].items()) p.ratingDescriptors.push_back(d.str());
    return p;
}

json::Value itemsToJson(const std::vector<std::pair<std::string, std::string>>& items) {
    json::Value a = json::Value::array();
    for (const auto& [pid, tid] : items) {
        json::Value o = json::Value::object();
        o.set("p", pid);
        o.set("t", tid);
        a.push(o);
    }
    return a;
}

bool namesSeries(const std::string& title) {
    return title.find("Series X|S") != std::string::npos || title.find("Series X/S") != std::string::npos;
}

bool endsWith(const std::string& s, const char* tail) {
    size_t n = std::strlen(tail);
    return s.size() >= n && s.compare(s.size() - n, n, tail) == 0;
}

// Which version of a cross-gen pair (two products, one Xbox title id) a
// product is, from its cloud title id or its name: +1 Series X|S, -1 Xbox
// One, 0 can't tell. "HOGWARTSLEGACYXBOXONEVERSION", "...XBOXSERIESXSVERSION",
// "RUSTCONSOLEEDITIONXS" / "Rust Console Edition X|S", "... - Xbox Series X|S".
int pairVersion(const xcloud::Product& p) {
    const std::string& c = p.xcloudTitleId;
    if (c.find("XBOXONE") != std::string::npos) return -1;
    if (c.find("XBOXSERIES") != std::string::npos || c.find("SERIESXS") != std::string::npos || endsWith(c, "XS"))
        return 1;
    if (namesSeries(p.title) || endsWith(p.title, "X|S") || endsWith(p.title, "X/S")) return 1;
    return 0;
}

// Editions of one game ("Forza Horizon 5", "... Standard Edition") are
// separate products that start the same cloud title: keep one, the shortest
// name, where the first one was.
void dedupeByTitle(std::vector<ui::GameTile>& tiles) {
    std::map<std::string, size_t> first;
    std::vector<ui::GameTile> out;
    for (auto& t : tiles) {
        std::string key = t.titleId;
        if (t.titleId == "CALLOFDUTYHQ") {
            // Call of Duty HQ is a unified launcher hub containing multiple distinct games:
            // Modern Warfare 4, Warzone, Black Ops 7, Black Ops 6, etc.
            // Dedupe only genuine duplicate editions within each experience.
            std::string lower;
            lower.reserve(t.name.size());
            for (char c : t.name) lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
            if (lower.find("modern warfare 4") != std::string::npos ||
                lower.find("modern warfare® 4") != std::string::npos ||
                lower.find("modern warfare iv") != std::string::npos ||
                lower.find("call of duty 4") != std::string::npos) {
                key = "CALLOFDUTYHQ:MW4";
            } else if (lower.find("warzone") != std::string::npos) {
                key = "CALLOFDUTYHQ:WARZONE";
            } else if (lower.find("black ops 7") != std::string::npos) {
                key = "CALLOFDUTYHQ:BO7";
            } else if (lower.find("black ops 6") != std::string::npos) {
                key = "CALLOFDUTYHQ:BO6";
            } else if (lower == "call of duty" || lower == "call of duty®") {
                key = "CALLOFDUTYHQ:HUB";
            } else {
                key = "CALLOFDUTYHQ:" + t.productId;
            }
        }
        auto it = first.find(key);
        if (it == first.end()) {
            first[key] = out.size();
            out.push_back(std::move(t));
        } else {
            auto& exist = out[it->second];
            bool anyPlayable = exist.playable || t.playable;
            bool anyPurchasable = exist.purchasable || t.purchasable;
            bool anyFree = exist.freeInStore || t.freeInStore;
            if (t.name.size() < exist.name.size()) {
                if (t.tileUrl.empty() && !exist.tileUrl.empty()) t.tileUrl = exist.tileUrl;
                if (t.heroUrl.empty() && !exist.heroUrl.empty()) t.heroUrl = exist.heroUrl;
                if (t.description.empty() && !exist.description.empty()) t.description = exist.description;
                exist = std::move(t);
            }
            exist.playable = anyPlayable;
            exist.purchasable = anyPurchasable;
            exist.freeInStore = anyFree;
        }
    }
    tiles = std::move(out);
}

bool isStoreBigId(const std::string& s) {
    if (s.size() != 12) return false;
    for (char c : s) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) return false;
    }
    return true;
}

struct KnownCloudGame {
    const char* productId;
    const char* cloudTitleId;
    const char* xboxTitleId;
    const char* fallbackTitle;
    const char* publisher;
    const char* tileUrl;
    const char* posterUrl;
    const char* heroUrl;
    const char* category;
    const char* description;
    const char* ratingAge;
    const char* ratingSystem;
    const char* ratingDescriptors;
    bool isFree;
};

static const KnownCloudGame kKnownCloudGames[] = {
    {"9NCPSQQD1Z66", "CALLOFDUTYHQ", "2001700854", "Call of Duty®: Modern Warfare® 4", "Activision Publishing Inc.",
     "https://store-images.s-microsoft.com/image/apps.59792.13559635275456449.72d1efe7-e297-4b01-9ff5-81f9541bc9a1.f8f4e212-4f83-45fe-8db7-bbb19d5a9582",
     "https://store-images.s-microsoft.com/image/apps.4715.13559635275456449.72d1efe7-e297-4b01-9ff5-81f9541bc9a1.a25cb344-59ba-49dd-8858-37563c544636",
     "https://store-images.s-microsoft.com/image/apps.45467.13559635275456449.72d1efe7-e297-4b01-9ff5-81f9541bc9a1.98f47d6e-e5f3-4a70-8b61-43330deb2787",
     "Shooter",
     "War erupts on the Korean Peninsula as North Korea launches a full-scale invasion that threatens to destabilize global peace. Call of Duty®: Modern Warfare® 4 delivers an intense, gritty narrative campaign alongside signature multiplayer and cooperative operations.",
     "18", "ClassInd", "Violência Extrema, Drogas Lícitas, Linguagem Imprópria",
     false},
    {"9N201KQXS5BM", "CALLOFDUTYHQ", "2001700854", "Call of Duty®", "Activision Publishing Inc.",
     "https://store-images.s-microsoft.com/image/apps.16691.13966330883349940.9de723b2-8834-4acf-92cc-c6ec73db1644.50de713a-16e0-4383-b422-7052a1cb99f0",
     "https://store-images.s-microsoft.com/image/apps.30472.13966330883349940.9a0bbc3f-3231-4a17-ab16-55eb671f1755.d9aecc85-6637-4e56-a359-009944a02940",
     "https://store-images.s-microsoft.com/image/apps.45467.13966330883349940.05d0b25a-8298-42a1-adba-3a6c21a95a32.eb8f770e-9122-4ab6-ab50-372aa6cc912c",
     "Shooter",
     "The Call of Duty® experience supports Modern Warfare® 4, Black Ops 7 and Call of Duty®: Warzone™.",
     "18", "ClassInd", "Violência Extrema, Drogas Lícitas, Linguagem Imprópria",
     false},
    {"9NNFG8BQRCXL", "CALLOFDUTYHQ", "2001700854", "Call of Duty®: Warzone™", "Activision Publishing Inc.",
     "https://store-images.s-microsoft.com/image/apps.60826.13739535057760905.ee63b998-143e-4256-bd0e-3e78d667ab11.d72c1992-0383-4ff8-91c5-e310141d9a07",
     "https://store-images.s-microsoft.com/image/apps.60826.13739535057760905.ee63b998-143e-4256-bd0e-3e78d667ab11.d72c1992-0383-4ff8-91c5-e310141d9a07",
     "https://store-images.s-microsoft.com/image/apps.20908.13739535057760905.ee63b998-143e-4256-bd0e-3e78d667ab11.7315f232-c36d-4d7d-8581-edc82aebe238",
     "Shooter",
     "Welcome to Call of Duty®: Warzone™, the massive free-to-play combat arena. Experience thrilling modes and new gameplay features.",
     "18", "ClassInd", "Violência Extrema, Drogas Lícitas, Linguagem Imprópria",
     true},
    {"9N8KMNW6942X", "CALLOFDUTYHQ", "2001700854", "Call of Duty®: Black Ops 7", "Activision Publishing Inc.",
     nullptr, nullptr, nullptr, "Shooter",
     "Call of Duty®: Black Ops 7 expands the Cold War universe into unprecedented covert operations.",
     "18", "ClassInd", "Violência Extrema, Drogas Lícitas, Linguagem Imprópria",
     false},
    {"9N1N0NQNGK9W", "CALLOFDUTYBLACKOPS6", "1822992790", "Call of Duty®: Black Ops 6", "Activision Publishing Inc.",
     nullptr, nullptr, nullptr, "Shooter",
     "Call of Duty®: Black Ops 6 is a spy action thriller set in the early 90s, characterized by mind-bending narrative gameplay.",
     "18", "ClassInd", "Violência Extrema, Drogas Lícitas, Linguagem Imprópria",
     false},
    {"9P22THR57FV7", "CALLOFDUTYBLACKOPS6", "1822992790", "Call of Duty®: Black Ops 6 - Launcher", "Activision Publishing Inc.",
     nullptr, nullptr, nullptr, "Shooter", nullptr, "18", "ClassInd", nullptr, false},
    {"9NLZZ74TFD7P", "CALLOFDUTYMODERNWARFAREIII", "2003727579", "Call of Duty®: Modern Warfare® III", "Activision Publishing Inc.",
     nullptr, nullptr, nullptr, "Shooter", nullptr, "18", "ClassInd", nullptr, false},
    {"9PMFDKG5F9R7", "CALLOFDUTYMODERNWARFAREIII", "2003727579", "Call of Duty®: Modern Warfare® III", "Activision Publishing Inc.",
     nullptr, nullptr, nullptr, "Shooter", nullptr, "18", "ClassInd", nullptr, false},
    {"9NDKFJ6XDX2M", "CALLOFDUTYMODERNWARFAREIII", "2003727579", "Call of Duty®: Modern Warfare® III", "Activision Publishing Inc.",
     nullptr, nullptr, nullptr, "Shooter", nullptr, "18", "ClassInd", nullptr, false},
    {"9PNGFSX7S117", "CALLOFDUTYMODERNWARFAREII", "1959007243", "Call of Duty®: Modern Warfare® II", "Activision Publishing Inc.",
     nullptr, nullptr, nullptr, "Shooter", nullptr, "18", "ClassInd", nullptr, false},
    {"9N872NBW5XG5", "CALLOFDUTYMODERNWARFAREII", "1959007243", "Call of Duty®: Modern Warfare® II", "Activision Publishing Inc.",
     nullptr, nullptr, nullptr, "Shooter", nullptr, "18", "ClassInd", nullptr, false},
    {"9P7RQ1QFFF8R", "CALLOFDUTYMODERNWARFAREII", "1959007243", "Call of Duty®: Modern Warfare® II", "Activision Publishing Inc.",
     nullptr, nullptr, nullptr, "Shooter", nullptr, "18", "ClassInd", nullptr, false},
    {"9NVQBQ3F6W9W", "CALLOFDUTYMODERNWARFARE", "609700427", "Call of Duty®: Modern Warfare®", "Activision Publishing Inc.",
     nullptr, nullptr, nullptr, "Shooter", nullptr, "18", "ClassInd", nullptr, false},
    {"C5DTJ99626K3", "CALLOFDUTYMODERNWARFARE", "609700427", "Call of Duty®: Modern Warfare®", "Activision Publishing Inc.",
     nullptr, nullptr, nullptr, "Shooter", nullptr, "18", "ClassInd", nullptr, false},
    {"9P7PFH4RMFZD", "CALLOFDUTYBLACKOPSCOLDWAR", "1746206771", "Call of Duty®: Black Ops Cold War", "Activision Publishing Inc.",
     nullptr, nullptr, nullptr, "Shooter", nullptr, "18", "ClassInd", nullptr, false},
    {"9NPLRJ9CC870", "CALLOFDUTYBLACKOPSCOLDWARXBOXSERIESXS", "1746206771", "Call of Duty®: Black Ops Cold War - Xbox Series X|S", "Activision Publishing Inc.",
     nullptr, nullptr, nullptr, "Shooter", nullptr, "18", "ClassInd", nullptr, false},
    {"9PHP6HF58MB5", "CALLOFDUTYVANGUARD", "1917992764", "Call of Duty®: Vanguard", "Activision Publishing Inc.",
     nullptr, nullptr, nullptr, "Shooter", nullptr, "18", "ClassInd", nullptr, false},
    {"9MWHTW1SF7RW", "CALLOFDUTYVANGUARDXBOXSERIESXS", "1917992764", "Call of Duty®: Vanguard - Xbox Series X|S", "Activision Publishing Inc.",
     nullptr, nullptr, nullptr, "Shooter", nullptr, "18", "ClassInd", nullptr, false},
};

static void seedKnownGames(std::map<std::string, std::string>& cloudTitleOf,
                           std::map<std::string, std::string>& xboxTitleOf,
                           std::set<std::string>& freeInStore,
                           std::set<std::string>& gamePass,
                           std::vector<std::string>& allGames,
                           std::map<std::string, xcloud::Product>& products) {
    for (const auto& g : kKnownCloudGames) {
        cloudTitleOf[g.productId] = g.cloudTitleId;
        if (g.xboxTitleId && g.xboxTitleId[0]) xboxTitleOf[g.productId] = g.xboxTitleId;
        if (g.isFree) freeInStore.insert(g.productId);
        if (std::string_view(g.productId) == "9NCPSQQD1Z66") {
            gamePass.insert(g.productId);
        }
        if (std::find(allGames.begin(), allGames.end(), g.productId) == allGames.end()) {
            allGames.push_back(g.productId);
        }
        if (!products.count(g.productId)) {
            xcloud::Product p;
            p.productId = g.productId;
            p.xcloudTitleId = g.cloudTitleId;
            p.xboxTitleId = g.xboxTitleId ? g.xboxTitleId : "";
            p.title = g.fallbackTitle;
            p.publisher = g.publisher;
            p.tileUrl = g.tileUrl ? g.tileUrl : "";
            p.posterUrl = g.posterUrl ? g.posterUrl : "";
            p.heroUrl = g.heroUrl ? g.heroUrl : "";
            p.description = g.description ? g.description : "";
            p.ratingAge = g.ratingAge ? g.ratingAge : "";
            p.ratingSystem = g.ratingSystem ? g.ratingSystem : "";
            if (g.ratingDescriptors && g.ratingDescriptors[0]) {
                p.ratingDescriptors.push_back(g.ratingDescriptors);
            }
            if (g.category && g.category[0]) p.categories.push_back(g.category);
            p.detailed = true;
            p.plan = xcloud::kPlanUltimate;
            products[g.productId] = std::move(p);
        } else {
            auto& p = products[g.productId];
            if (p.xcloudTitleId.empty() || p.xcloudTitleId == "None") p.xcloudTitleId = g.cloudTitleId;
            if (p.description.empty() && g.description) p.description = g.description;
            if (p.tileUrl.empty() && g.tileUrl) p.tileUrl = g.tileUrl;
            if (p.posterUrl.empty() && g.posterUrl) p.posterUrl = g.posterUrl;
            if (p.heroUrl.empty() && g.heroUrl) p.heroUrl = g.heroUrl;
            if (p.ratingAge.empty() && g.ratingAge) p.ratingAge = g.ratingAge;
            if (p.ratingSystem.empty() && g.ratingSystem) p.ratingSystem = g.ratingSystem;
        }
    }
}

}  // namespace

ui::GameTile Library::tile(const std::string& productId, const std::string& titleId) const {
    ui::GameTile t;
    t.productId = productId;
    t.titleId = titleId;
    static const xcloud::Product s_dummy{};
    auto it = products_.find(productId);
    if (it == products_.end() && !titleId.empty()) {
        for (auto pit = products_.begin(); pit != products_.end(); ++pit) {
            if (pit->second.xcloudTitleId == titleId) {
                it = pit;
                if (t.productId.empty()) t.productId = pit->first;
                break;
            }
        }
        if (it == products_.end()) {
            for (const auto& [cpid, ctid] : cloudTitleOf_) {
                if (ctid == titleId) {
                    it = products_.find(cpid);
                    if (it != products_.end()) {
                        if (t.productId.empty()) t.productId = cpid;
                        break;
                    }
                }
            }
        }
        if (it == products_.end()) {
            for (const auto& g : kKnownCloudGames) {
                if (g.cloudTitleId && titleId == g.cloudTitleId) {
                    if (t.productId.empty()) t.productId = g.productId;
                    if (t.name.empty() && g.fallbackTitle) t.name = g.fallbackTitle;
                    if (t.publisher.empty() && g.publisher) t.publisher = g.publisher;
                    if (t.tileUrl.empty() && g.tileUrl) t.tileUrl = g.tileUrl;
                    if (t.heroUrl.empty() && g.heroUrl) t.heroUrl = g.heroUrl;
                    if (t.description.empty() && g.description) t.description = g.description;
                    break;
                }
            }
        }
    }
    const xcloud::Product& p = (it != products_.end()) ? it->second : s_dummy;
    if (t.titleId.empty() || t.titleId == "None") {
        auto ct = cloudTitleOf_.find(productId);
        if (ct != cloudTitleOf_.end()) t.titleId = ct->second;
    }
    if (it != products_.end()) {
        if (t.titleId.empty() || t.titleId == "None") t.titleId = p.xcloudTitleId;
        if (t.titleId == "None") t.titleId.clear();
        if (t.titleId.empty() && !productId.empty()) t.titleId = productId;
        t.name = p.title;
        t.publisher = p.publisher;
        t.description = p.description;
        t.tileUrl = p.tileUrl.empty() ? p.posterUrl : p.tileUrl;
        t.heroUrl = p.heroUrl;
        t.categories = p.categories;
        t.detailed = p.detailed;
        t.modes = p.modes;
        t.ratingAge = p.ratingAge;
        t.ratingSystem = p.ratingSystem;
        t.ratingDescriptors = p.ratingDescriptors;
        if (auto lang = p.languages.find(language_.substr(0, language_.find('-'))); lang != p.languages.end())
            t.languages = lang->second;  // in the app's language
    } else {
        if (isStoreBigId(productId)) t.name.clear();
        else t.name = productId;
    }
    t.freeInStore = freeInStore_.count(productId) > 0;
    // Free-to-play games are open to every plan and to no plan, even those
    // also tagged with a Pass product id in the catalog.
    t.plan = (!t.freeInStore && gamePass_.count(productId)) ? p.plan : 0;
    uint8_t myPlan = xcloud::planTier(subscription_);
    if (ownershipKnown_) {
        t.playable = ownedTitles_.count(t.titleId) || ownedProducts_.count(t.productId);
        if (!t.playable && (t.titleId == "CALLOFDUTYHQ" || t.titleId.find("CALLOFDUTY") != std::string::npos)) {
            for (const auto& ot : ownedTitles_) {
                if (ot.find("CALLOFDUTY") != std::string::npos) {
                    t.playable = true;
                    break;
                }
            }
        }
        if (!t.playable && (gamePassMember_ || myPlan >= xcloud::kPlanEssential) && gamePass_.count(productId)) {
            if (myPlan == 0 || xcloud::planIncludes(myPlan, t.plan, t.freeInStore)) {
                t.playable = true;
            }
        }
    }
    // Added to Game Pass since the last start: the account's list from then
    // doesn't have it yet. Not locked until this start's list arrives, if the
    // account's plan includes it.
    if (!t.playable && !ownershipFresh_ && !knownGamePass_.empty() && gamePass_.count(productId) &&
        !knownGamePass_.count(productId) && (myPlan == 0 || xcloud::planIncludes(myPlan, t.plan, t.freeInStore)))
        t.playable = true;
    if (t.playable && !ownershipKnown_) {
        if (myPlan > 0 && !xcloud::planIncludes(myPlan, t.plan, t.freeInStore)) t.playable = false;
    }
    t.mouseKeyboard = mouseKeyboard_.count(productId) > 0;
    // A free-to-play game not on the account yet is got like one bought
    // (the store page's QR code), only free.
    t.purchasable = !t.playable && (purchasableSet_.count(productId) || t.freeInStore);
    if (siblingsFor_ != products_.size()) {
        seriesSiblings_.clear();
        sharedXbox_.clear();
        std::set<std::string> seen;
        for (const auto& [id, prod] : products_) {
            auto xs = xboxTitleOf_.find(id);
            const std::string& xbox = xs != xboxTitleOf_.end() ? xs->second : prod.xboxTitleId;
            if (!xbox.empty() && !seen.insert(xbox).second) sharedXbox_.insert(xbox);
        }
        // The pairs with a Series X|S side: the other side is the Xbox One one.
        for (const auto& [id, prod] : products_) {
            auto xs = xboxTitleOf_.find(id);
            const std::string& xbox = xs != xboxTitleOf_.end() ? xs->second : prod.xboxTitleId;
            if (namesSeries(prod.title) || (sharedXbox_.count(xbox) && pairVersion(prod) > 0)) seriesSiblings_.insert(xbox);
        }
        siblingsFor_ = products_.size();
    }
    auto x = xboxTitleOf_.find(productId);
    std::string xbox = x != xboxTitleOf_.end() ? x->second : p.xboxTitleId;
    t.xboxTitleId = xbox;
    auto pl = platform_.find(xbox);
    if (pl != platform_.end()) t.platform = pl->second;
    // Cross-gen pairs ("Call of Duty: Vanguard" and "... - Xbox Series X|S")
    // are two cloud titles sharing one Xbox title id: the name tells them apart.
    // The cloud title id names the version where the store's name doesn't
    // say it in English ("Hogwarts Legacy" / "... Versão Xbox One":
    // HOGWARTSLEGACYXBOXSERIESXSVERSION / ...XBOXONEVERSION).
    int version = sharedXbox_.count(xbox) ? pairVersion(p) : 0;
    if (version < 0) t.platform = xcloud::kPlatformOne;
    else if (version > 0) t.platform = xcloud::kPlatformSeries;
    else if (namesSeries(p.title)) t.platform = xcloud::kPlatformSeries;
    else if (t.platform == xcloud::kPlatformSeries && seriesSiblings_.count(xbox)) t.platform = xcloud::kPlatformOne;
    return t;
}

std::vector<ui::GameTile> Library::tiles(const std::vector<Item>& items) const {
    std::vector<ui::GameTile> out;
    for (const auto& [pid, tid] : items) {
        ui::GameTile t = tile(pid, tid);
        if (xcloud::isNonGameAddon(t.titleId, t.name)) continue;
        if (t.productId.empty() || t.titleId.empty() || t.name.empty()) continue;
        if (isStoreBigId(t.name) || (t.name == t.productId && isStoreBigId(t.productId))) continue;
        out.push_back(std::move(t));
    }
    dedupeByTitle(out);
    return out;
}

std::vector<ui::GameRow> Library::rows() const {
    std::vector<ui::GameRow> rows;
    uint8_t myPlan = xcloud::planTier(subscription_);
    for (const auto& r : layout_) {
        ui::GameRow row;
        row.title = r.title;
        row.gamePassBadges = r.badges;
        row.isGrid = r.isGrid;
        for (const auto& [pid, tid] : r.items) {
            ui::GameTile t = tile(pid, tid);
            if (t.productId.empty() || t.titleId.empty() || t.name.empty() ||
                isStoreBigId(t.name) || (t.name == t.productId && isStoreBigId(t.productId)) ||
                xcloud::isNonGameAddon(t.titleId, t.name)) continue;
            // "Jump back in": Game Pass games and the account's own (bought,
            // free-to-play); not one it can't start any more.
            if (r.recent && !gamePass_.empty() && !gamePass_.count(pid) && !t.playable) continue;
            // Rows show only what the account's plan plays (free-to-play is
            // open to every plan).
            if (!t.playable && !t.freeInStore && !r.isGrid &&
                (myPlan > 0 && !xcloud::planIncludes(myPlan, t.plan, t.freeInStore)))
                continue;
            row.tiles.push_back(std::move(t));
        }
        dedupeByTitle(row.tiles);
        if (!row.tiles.empty()) rows.push_back(std::move(row));
    }

    // Insert "Friends playing now" dynamically if any friend is playing an accessible title
    if (!friendsTiles_.empty()) {
        ui::GameRow friendsRow;
        friendsRow.title = ui::tr(ui::Str::FriendsPlayingNow);
        friendsRow.gamePassBadges = false;
        friendsRow.isGrid = false;
        friendsRow.tiles = friendsTiles_;
        for (auto& t : friendsRow.tiles) {
            ui::GameTile fresh = tile(t.productId, t.titleId);
            if (!fresh.heroUrl.empty()) t.heroUrl = fresh.heroUrl;
            if (!fresh.description.empty() && t.description.empty()) t.description = fresh.description;
            if (!fresh.name.empty() && t.name.empty()) t.name = fresh.name;
            if (!fresh.tileUrl.empty() && t.tileUrl.empty()) t.tileUrl = fresh.tileUrl;
            if (!fresh.publisher.empty() && t.publisher.empty()) t.publisher = fresh.publisher;
            if (!fresh.categories.empty() && t.categories.empty()) t.categories = fresh.categories;
        }

        // Place right after "Jump back in" if present, otherwise at index 0
        size_t insertIdx = 0;
        if (!rows.empty() && rows[0].title == ui::tr(ui::Str::JumpBackIn)) {
            insertIdx = 1;
        }
        if (insertIdx < rows.size()) {
            rows.insert(rows.begin() + static_cast<long>(insertIdx), std::move(friendsRow));
        } else {
            rows.push_back(std::move(friendsRow));
        }
    }

    // Insert "Favorites" row at the top of the library if any are marked
    if (!favorites_.empty()) {
        ui::GameRow favRow;
        favRow.title = ui::tr(ui::Str::Favorites);
        favRow.gamePassBadges = false;
        favRow.isGrid = false;
        for (const auto& pid : favorites_) {
            ui::GameTile t = tile(pid);
            if (!t.productId.empty() && !t.titleId.empty() && !t.name.empty() &&
                !isStoreBigId(t.name) && !(t.name == t.productId && isStoreBigId(t.productId)) &&
                !xcloud::isNonGameAddon(t.titleId, t.name)) {
                favRow.tiles.push_back(std::move(t));
            }
        }
        if (!favRow.tiles.empty()) {
            rows.insert(rows.begin(), std::move(favRow));
        }
    }

    return rows;
}

std::vector<ui::GameTile> Library::owned() const { return tiles(owned_); }
std::vector<ui::GameTile> Library::purchasable() const { return tiles(purchasable_); }

std::vector<ui::GameTile> Library::gamePassSearchPool() const {
    std::vector<Item> items;
    for (const auto& id : allGames_) items.emplace_back(id, std::string());
    return tiles(items);
}

std::vector<ui::GameTile> Library::librarySearchPool() const {
    std::vector<Item> items = owned_;
    items.insert(items.end(), purchasable_.begin(), purchasable_.end());
    for (const auto& id : allGames_) items.emplace_back(id, std::string());
    return tiles(items);
}

void Library::sortOwned() {
    // Recently played first (newest first), then alphabetical.
    auto rank = [&](const std::string& pid) {
        auto it = std::find(recent_.begin(), recent_.end(), pid);
        return it == recent_.end() ? recent_.size() : static_cast<size_t>(it - recent_.begin());
    };
    auto name = [&](const std::string& pid) {
        auto it = products_.find(pid);
        return it == products_.end() ? std::string() : it->second.title;
    };
    std::stable_sort(owned_.begin(), owned_.end(), [&](const auto& a, const auto& b) {
        size_t ra = rank(a.first), rb = rank(b.first);
        if (ra != rb) return ra < rb;
        return name(a.first) < name(b.first);
    });
}

bool Library::loadCache(bool withLayout) {
    std::string text;
    if (cachePath_.empty() || !platform::readFile(cachePath_, text)) {
        std::string fallback;
        if (!cachePath_.empty()) {
            auto pos = cachePath_.rfind('/');
            if (pos != std::string::npos) {
                fallback = cachePath_.substr(0, pos + 1) + "library.json";
            }
        }
        if (fallback.empty() || !platform::readFile(fallback, text)) {
            fallback = platform::dataDir().empty() ? "library.json" : (platform::dataDir() + "/library.json");
            if (!platform::readFile(fallback, text)) return false;
        }
    }
    auto j = json::parse(text);
    if (!j) return false;
    int ver = (*j)["version"].asInt();
    if (ver != 2 && ver != kCacheVersion) return false;
    if (market_.empty()) market_ = (*j)["market"].str();
    else if ((*j)["market"].str() != market_) return false;
    if (language_.empty()) language_ = (*j)["language"].str();
    // Names and descriptions in another language (the language changed):
    // still shown, so the lists stay whole, and fetched again in this one.
    bool sameLanguage = (*j)["language"].str() == language_;
    for (const auto& [pid, title] : (*j)["cloudTitles"].members()) cloudTitleOf_[pid] = title.str();
    for (const auto& [id, v] : (*j)["products"].members())
        if (!products_.count(id)) {
            products_[id] = productFromJson(id, v);
            if (products_[id].xcloudTitleId.empty() || products_[id].xcloudTitleId == "None") {
                auto ct = cloudTitleOf_.find(id);
                if (ct != cloudTitleOf_.end()) products_[id].xcloudTitleId = ct->second;
            }
            if (!sameLanguage) otherLanguage_.insert(id);
        }
    for (const auto& [id, code] : (*j)["platforms"].members()) platform_[id] = code.str();
    for (const auto& [pid, xbox] : (*j)["xboxTitles"].members()) xboxTitleOf_[pid] = xbox.str();
    for (const auto& id : (*j)["freeInStore"].items()) freeInStore_.insert(id.str());
    for (const auto& id : (*j)["mouseKeyboard"].items()) mouseKeyboard_.insert(id.str());
    for (const auto& t : (*j)["ownedTitles"].items()) ownedTitles_.insert(t.str());
    gamePassMember_ = (*j)["gamePassMember"].asBool();
    if (subscription_.empty()) subscription_ = (*j)["subscription"].str();
    for (const auto& p : (*j)["ownedProducts"].items()) ownedProducts_.insert(p.str());
    bool corruptedGamePass = (*j)["gamePass"].items().size() > 1050;
    if (corruptedGamePass) {
        XC_LOGW("library: cache gamePass has %zu items (likely contaminated), ignoring cached gamePass",
                (*j)["gamePass"].items().size());
    } else {
        for (const auto& p : (*j)["gamePass"].items()) {
            knownGamePass_.insert(p.str());
            if (withLayout) {
                gamePass_.insert(p.str());
                allGames_.push_back(p.str());
            }
        }
    }
    if (withLayout) {
        seedKnownGames(cloudTitleOf_, xboxTitleOf_, freeInStore_, gamePass_, allGames_, products_);
    }
    owned_.clear();
    for (const auto& o : (*j)["owned"].items()) {
        if (xcloud::isNonGameAddon(o["t"].str())) continue;
        owned_.emplace_back(o["p"].str(), o["t"].str());
    }
    purchasable_.clear();
    for (const auto& o : (*j)["purchasable"].items()) {
        if (xcloud::isNonGameAddon(o["t"].str())) continue;
        purchasable_.emplace_back(o["p"].str(), o["t"].str());
        purchasableSet_.insert(o["p"].str());
    }
    // The home rows: only for the instant start. load() builds them fresh,
    // and adding these too showed every row twice (and the cache grew with
    // each start). A cache written that way shows each row once here, and
    // the next save keeps only the fresh rows.
    size_t layoutRows = 0;
    if (withLayout && layout_.empty()) {
        std::set<std::string> titles;
        for (const auto& rowObj : (*j)["layout"].items()) {
            RowIds r;
            r.title = rowObj["title"].str();
            r.badges = rowObj["badges"].asBool(true);
            r.isGrid = rowObj["isGrid"].asBool(false);
            r.recent = rowObj["recent"].asBool(false);
            for (const auto& it : rowObj["items"].items()) {
                r.items.emplace_back(it["p"].str(), it["t"].str());
            }
            if (!r.items.empty() && titles.insert(r.title).second) layout_.push_back(std::move(r));
        }
        layoutRows = layout_.size();
        bool hasMw4 = false;
        for (const auto& r : layout_) {
            for (const auto& it : r.items) {
                if (it.first == "9NCPSQQD1Z66") { hasMw4 = true; break; }
            }
            if (hasMw4) break;
        }
        if (!hasMw4) {
            for (auto& r : layout_) {
                if (!r.recent && !r.isGrid) {
                    size_t pos = std::min<size_t>(1, r.items.size());
                    r.items.insert(r.items.begin() + static_cast<long>(pos), {"9NCPSQQD1Z66", "CALLOFDUTYHQ"});
                    break;
                }
            }
            for (auto& r : layout_) {
                if (r.isGrid) {
                    r.items.insert(r.items.begin(), {"9NCPSQQD1Z66", "CALLOFDUTYHQ"});
                    break;
                }
            }
        }
    }
    size_t codCount = 0;
    for (const auto& o : owned_) {
        if (o.second.find("CALLOFDUTY") != std::string::npos) ++codCount;
    }
    bool corruptedOwned = (codCount > 20);
    if (corruptedOwned) {
        XC_LOGW("library: cache owned has %zu duplicate Call of Duty entries (v1.5.0 bug), invalidating cached ownership",
                codCount);
        owned_.clear();
        ownershipKnown_ = false;
    } else {
        ownershipKnown_ = !ownedTitles_.empty() || !ownedProducts_.empty() || !owned_.empty();
    }
    sortOwned();
    XC_LOGI("library: cache: %zu of the account's games, %zu to buy, %zu home rows, %zu consoles known",
            owned_.size(), purchasable_.size(), layoutRows, platform_.size());
    return true;
}

void Library::saveCache() const {
    if (cachePath_.empty()) return;
    json::Value root = json::Value::object();
    root.set("version", kCacheVersion);
    root.set("market", market_);
    root.set("language", language_);
    json::Value titles = json::Value::array(), prods = json::Value::array();
    for (const auto& t : ownedTitles_) titles.push(t);
    for (const auto& p : ownedProducts_) prods.push(p);
    root.set("ownedTitles", titles);
    root.set("gamePassMember", gamePassMember_);
    if (!subscription_.empty()) root.set("subscription", subscription_);
    root.set("ownedProducts", prods);
    root.set("owned", itemsToJson(owned_));
    json::Value gamePass = json::Value::array();
    for (const auto& id : allGames_) gamePass.push(id);
    root.set("gamePass", gamePass);  // the next start tells the games added since
    root.set("purchasable", itemsToJson(purchasable_));
    json::Value layoutArr = json::Value::array();
    std::set<std::string> layoutTitles;
    for (const auto& r : layout_) {
        if (!layoutTitles.insert(r.title).second) continue;  // each row once
        json::Value rowObj = json::Value::object();
        rowObj.set("title", r.title);
        rowObj.set("badges", r.badges);
        rowObj.set("isGrid", r.isGrid);
        rowObj.set("recent", r.recent);
        rowObj.set("items", itemsToJson(r.items));
        layoutArr.push(std::move(rowObj));
    }
    root.set("layout", layoutArr);
    // Details of everything on the home screen, in "Your games" and in the
    // Game Pass search: the next launch shows them, hero art and all, before
    // the network answers, and the search's filters (play modes, languages)
    // work without asking again.
    json::Value details = json::Value::object();
    auto keep = [&](const std::string& pid) {
        auto it = products_.find(pid);
        if (it != products_.end()) details.set(pid, productToJson(it->second));
    };
    for (const auto& r : layout_)
        for (const auto& item : r.items) keep(item.first);
    for (const auto* list : {&owned_, &purchasable_})
        for (const auto& [pid, tid] : *list) keep(pid);
    for (const auto& pid : allGames_) keep(pid);
    root.set("products", details);
    json::Value platforms = json::Value::object(), xboxTitles = json::Value::object(), cloudTitles = json::Value::object();
    for (const auto& [id, code] : platform_) platforms.set(id, code);
    for (const auto& [pid, xbox] : xboxTitleOf_) xboxTitles.set(pid, xbox);
    for (const auto& [pid, title] : cloudTitleOf_) cloudTitles.set(pid, title);
    root.set("platforms", platforms);
    root.set("xboxTitles", xboxTitles);
    root.set("cloudTitles", cloudTitles);
    json::Value free = json::Value::array();
    for (const auto& id : freeInStore_) free.push(id);
    root.set("freeInStore", free);
    json::Value mkb = json::Value::array();
    for (const auto& id : mouseKeyboard_) mkb.push(id);
    root.set("mouseKeyboard", mkb);
    if (!platform::writeFileAtomic(cachePath_, root.dump())) XC_LOGW("library: could not write %s", cachePath_.c_str());
}

void Library::markAsOwned(const std::string& productId, const std::string& titleId) {
    if (!productId.empty()) ownedProducts_.insert(productId);
    if (!titleId.empty()) ownedTitles_.insert(titleId);
    if (!productId.empty() && !products_.count(productId)) {
        xcloud::Product p;
        p.productId = productId;
        p.xcloudTitleId = titleId;
        p.title = productId;
        products_[productId] = p;
    }
    bool inOwned = false;
    for (const auto& item : owned_) {
        if (item.first == productId) { inOwned = true; break; }
    }
    if (!inOwned && !productId.empty()) {
        owned_.emplace_back(productId, titleId);
    }
    ownershipKnown_ = true;
    sortOwned();
    saveCache();
}

void Library::markUnclaimable(const std::string& productId) {
    if (productId.empty()) return;
    freeInStore_.erase(productId);
    xcloud::markProductUnclaimable(productId);
    saveCache();
}

size_t Library::syncDelta(xcloud::GssvClient& gssv, const Changed& changed, std::string& err) {
    market_ = gssv.session().market.empty() ? "US" : gssv.session().market;
    std::vector<xcloud::Title> titles;
    std::string listErr;
    if (!gssv.listTitles(titles, listErr, false)) {
        err = listErr;
        return 0;
    }
    for (const auto& t : titles) {
        if (!t.productId.empty()) {
            if (!t.titleId.empty() && t.titleId != "None") {
                cloudTitleOf_[t.productId] = t.titleId;
                auto it = products_.find(t.productId);
                if (it != products_.end() && (it->second.xcloudTitleId.empty() || it->second.xcloudTitleId == "None")) {
                    it->second.xcloudTitleId = t.titleId;
                }
            }
            if (!t.xboxTitleId.empty() && !xboxTitleOf_.count(t.productId)) {
                xboxTitleOf_[t.productId] = t.xboxTitleId;
            }
            if (t.isFreeInStore) {
                freeInStore_.insert(t.productId);
            }
            if (t.mouseKeyboard) {
                mouseKeyboard_.insert(t.productId);
            }
        }
    }

    std::vector<std::string> newProducts;
    for (const auto& t : titles) {
        if (!t.productId.empty() && !products_.count(t.productId)) {
            newProducts.push_back(t.productId);
        }
        for (const auto& alt : t.alternateProductIds) {
            if (!alt.empty() && !products_.count(alt)) {
                newProducts.push_back(alt);
            }
        }
    }
    xcloud::ProductList allSigl;
    std::string siglErr;
    if (xcloud::fetchList(xcloud::sigl::kAllGames, market_, language_, allSigl, siglErr)) {
        gamePass_.clear();
        gamePass_.insert(allSigl.productIds.begin(), allSigl.productIds.end());
        allGames_ = allSigl.productIds;
        seedKnownGames(cloudTitleOf_, xboxTitleOf_, freeInStore_, gamePass_, allGames_, products_);
        for (const auto& pid : allSigl.productIds) {
            if (!pid.empty() && !products_.count(pid)) {
                newProducts.push_back(pid);
            }
        }
    }
    std::sort(newProducts.begin(), newProducts.end());
    newProducts.erase(std::unique(newProducts.begin(), newProducts.end()), newProducts.end());

    size_t newCount = newProducts.size();
    if (newCount > 0) {
        std::string fetchErr;
        xcloud::fetchProducts(newProducts, market_, language_, products_, fetchErr, false);

        for (auto& row : layout_) {
            if (row.isGrid) {
                for (const auto& pid : newProducts) {
                    bool exists = false;
                    for (const auto& item : row.items) {
                        if (item.first == pid) { exists = true; break; }
                    }
                    if (!exists) {
                        std::string tid = cloudTitleOf_.count(pid) ? cloudTitleOf_[pid] : "";
                        row.items.emplace_back(pid, tid);
                    }
                }
            } else if (!row.recent && row.badges) {
                for (const auto& pid : newProducts) {
                    bool exists = false;
                    for (const auto& item : row.items) {
                        if (item.first == pid) { exists = true; break; }
                    }
                    if (!exists) {
                        std::string tid = cloudTitleOf_.count(pid) ? cloudTitleOf_[pid] : "";
                        row.items.insert(row.items.begin(), {pid, tid});
                    }
                }
            }
        }
    }
    loadOwned(gssv, changed, nullptr, &titles);
    loadCatalogNames(changed, nullptr);
    saveCache();
    changed();
    return newCount;
}

bool Library::load(xcloud::GssvClient& gssv, const std::string& language, const Changed& changed, std::string& err,
                   bool forceRefresh) {
    market_ = gssv.session().market.empty() ? "US" : gssv.session().market;
    language_ = language;
    layout_.clear();
    products_.clear();
    allGames_.clear();
    gamePass_.clear();
    popular_.clear();
    recent_.clear();
    ownershipKnown_ = false;
    ownershipFresh_ = false;
    knownGamePass_.clear();
    ownedTitles_.clear();
    ownedProducts_.clear();
    owned_.clear();
    purchasable_.clear();
    purchasableSet_.clear();
    freeInStore_.clear();
    otherLanguage_.clear();
    progress_ = {};
    std::string lastErr;

    // The Game Pass catalog first: it decides what the other lists may show.
    xcloud::ProductList all;
    std::string e;
    if (xcloud::fetchList(xcloud::sigl::kAllGames, market_, language_, all, e)) {
        allGames_ = all.productIds;
        gamePass_.insert(all.productIds.begin(), all.productIds.end());
    } else {
        XC_LOGW("%s", e.c_str());
        lastErr = e;
    }
    seedKnownGames(cloudTitleOf_, xboxTitleOf_, freeInStore_, gamePass_, allGames_, products_);
    // The account's games from the previous run, until loadOwned() refreshes
    // them. Not its home rows: those are built below.
    loadCache(false);

    // "Jump back in": recently played games, Game Pass or the account's own
    // (rows() leaves out those it can't play; they also go first in "Your
    // games").
    std::vector<xcloud::Title> recent;
    if (gssv.listTitles(recent, lastErr, true)) {
        RowIds r{ui::tr(ui::Str::JumpBackIn), false, false, {}, true};
        std::vector<std::string> ids;
        for (const auto& t : recent) {
            if (t.productId.empty()) continue;
            recent_.push_back(t.productId);
            if (!t.xboxTitleId.empty()) xboxTitleOf_[t.productId] = t.xboxTitleId;
            // Its inputs come with it: the first cards show keyboard and
            // mouse without waiting for the whole list (or a cache that has it).
            if (t.mouseKeyboard) mouseKeyboard_.insert(t.productId);
            r.items.emplace_back(t.productId, t.titleId);
            if (!products_.count(t.productId)) ids.push_back(t.productId);
        }
        sortOwned();
        // The first cards are the first thing seen: full details (hero art,
        // description) for them in this same round trip.
        constexpr size_t kFirst = 8;
        std::vector<std::string> first, rest;
        for (const auto& item : r.items) {
            auto it = products_.find(item.first);
            bool complete = it != products_.end() && !it->second.heroUrl.empty();
            if (complete) continue;
            (first.size() < kFirst ? first : rest).push_back(item.first);
        }
        bool ok = xcloud::fetchProducts(first, market_, language_, products_, lastErr, true) &&
                  xcloud::fetchProducts(rest, market_, language_, products_, lastErr, false);
        if (ok) {
            layout_.push_back(std::move(r));
            changed();
        }
    }

    const ListSpec lists[] = {{xcloud::sigl::kRecentlyAdded, 40},
                              {xcloud::sigl::kMostPopular, 40},
                              {xcloud::sigl::kFreeToPlay, 40},
                              {xcloud::sigl::kLeavingSoon, 40},
                              {xcloud::sigl::kAllGames, 0}};
    uint8_t myPlan = xcloud::planTier(subscription_);
    for (const auto& spec : lists) {
        xcloud::ProductList list;
        bool isAllGames = std::string_view(spec.sigl) == xcloud::sigl::kAllGames;
        if (isAllGames && !allGames_.empty()) {
            list.title = all.title.empty() ? "All games" : all.title;
            list.productIds = allGames_;
        } else if (!xcloud::fetchList(spec.sigl, market_, language_, list, e)) {
            XC_LOGW("%s", e.c_str());
            lastErr = e;
            continue;
        }
        if (spec.sigl == xcloud::sigl::kRecentlyAdded) {
            const char* const kNewCod[] = {"9NCPSQQD1Z66", "9N201KQXS5BM"};
            for (const char* pid : kNewCod) {
                auto it = std::find(list.productIds.begin(), list.productIds.end(), pid);
                if (it != list.productIds.end()) list.productIds.erase(it);
                size_t insertAt = std::min<size_t>(1, list.productIds.size());
                list.productIds.insert(list.productIds.begin() + static_cast<long>(insertAt), pid);
            }
        }
        // "Most popular on cloud" also lists games to buy first (Cuphead...):
        // those go to "Your games" (in this order), not to the Game Pass tab.
        if (spec.sigl == xcloud::sigl::kMostPopular) {
            popular_ = list.productIds;
            // Position key cloud titles (Modern Warfare 4, Call of Duty HQ, Warzone)
            // within the visible most popular shelf
            const char* const kTopCod[] = {"9NCPSQQD1Z66", "9N201KQXS5BM", "9NNFG8BQRCXL"};
            for (const char* pid : kTopCod) {
                auto it = std::find(list.productIds.begin(), list.productIds.end(), pid);
                if (it != list.productIds.end()) list.productIds.erase(it);
                size_t insertAt = std::min<size_t>(2, list.productIds.size());
                list.productIds.insert(list.productIds.begin() + static_cast<long>(insertAt), pid);
            }
        }
        // Free-to-play: no Game Pass needed, so not filtered; each one the
        // account hasn't got yet shows as free to get (tile()).
        bool freeToPlay = spec.sigl == xcloud::sigl::kFreeToPlay;
        if (freeToPlay) {
            freeInStore_.insert(list.productIds.begin(), list.productIds.end());
            XC_LOGI("library: %s: %zu games", list.title.c_str(), list.productIds.size());
        }
        if (!gamePass_.empty() && !freeToPlay) {
            size_t before = list.productIds.size();
            list.productIds.erase(std::remove_if(list.productIds.begin(), list.productIds.end(),
                                                 [&](const std::string& id) {
                                                     if (!gamePass_.count(id)) return true;
                                                     if (myPlan > 0 && myPlan < xcloud::kPlanUltimate && !ownedProducts_.count(id) &&
                                                         !freeInStore_.count(id)) {
                                                         auto it = products_.find(id);
                                                         if (it != products_.end() &&
                                                             !xcloud::planIncludes(myPlan, it->second.plan, false))
                                                             return true;
                                                     }
                                                     return false;
                                                 }),
                                  list.productIds.end());
            if (list.productIds.size() != before)
                XC_LOGI("library: %s: %zu of %zu not in Game Pass/plan, left out", list.title.c_str(),
                        before - list.productIds.size(), before);
        }
        if (spec.limit > 0 && list.productIds.size() > spec.limit) list.productIds.resize(spec.limit);
        std::vector<std::string> missing;
        for (const auto& id : list.productIds) {
            auto it = products_.find(id);
            if (it == products_.end() || it->second.tileUrl.empty() || otherLanguage_.count(id))
                missing.push_back(id);
        }
        if (!missing.empty() && !xcloud::fetchProducts(missing, market_, language_, products_, e, false)) {
            XC_LOGW("%s", e.c_str());
            lastErr = e;
        }
        RowIds r{list.title, !freeToPlay, isAllGames, {}};
        for (const auto& id : list.productIds) r.items.emplace_back(id, std::string());
        layout_.push_back(std::move(r));
        changed();
    }
    if (rows().empty()) {
        err = lastErr.empty() ? "no games found" : lastErr;
        return false;
    }
    return true;
}

void Library::loadOwned(xcloud::GssvClient gssv, const Changed& changed, const std::atomic<bool>* stop,
                        const std::vector<xcloud::Title>* preloadedTitles) {
    progress_ = {true, 0, 0};  // the account's list: no measure until the names
    changed();
    std::string err;
    std::vector<xcloud::Title> fetchedTitles;
    const std::vector<xcloud::Title>* titlesPtr = preloadedTitles;
    if (!titlesPtr) {
        if (!gssv.listTitles(fetchedTitles, err, false)) {
            XC_LOGW("%s", err.c_str());  // keep what the cache said
            return;
        }
        titlesPtr = &fetchedTitles;
    }
    const auto& titles = *titlesPtr;
    if (stop && *stop) return;
    std::set<std::string> ownedTitles, ownedProducts;
    std::vector<Item> mine;
    std::set<std::string> seenMine;
    std::map<std::string, std::string> toBuy;  // productId -> titleId
    bool member = false;
    auto registerCloudTitle = [&](const std::string& pid, const std::string& tid, const std::string& xboxId, bool freeStore, bool mkb) {
        if (pid.empty()) return;
        if (!tid.empty() && tid != "None") {
            cloudTitleOf_[pid] = tid;
            auto it = products_.find(pid);
            if (it != products_.end() && (it->second.xcloudTitleId.empty() || it->second.xcloudTitleId == "None")) {
                it->second.xcloudTitleId = tid;
            }
        }
        if (!xboxId.empty() && !xboxTitleOf_.count(pid)) xboxTitleOf_[pid] = xboxId;
        if (freeStore) freeInStore_.insert(pid);
        if (mkb) mouseKeyboard_.insert(pid);
    };

    for (const auto& t : titles) {
        member |= t.hasEntitlement && t.viaSubscription;
        if (xcloud::isNonGameAddon(t.titleId)) continue;
        if (t.hasEntitlement) ownedTitles.insert(t.titleId);

        registerCloudTitle(t.productId, t.titleId, t.xboxTitleId, t.isFreeInStore, t.mouseKeyboard);
        for (const auto& alt : t.alternateProductIds) {
            registerCloudTitle(alt, t.titleId, t.xboxTitleId, t.isFreeInStore, t.mouseKeyboard);
        }

        if (t.hasEntitlement) {
            if (!t.productId.empty()) ownedProducts.insert(t.productId);
            for (const auto& alt : t.alternateProductIds) {
                if (!alt.empty()) ownedProducts.insert(alt);
            }
            // Game Pass games the account owns itself (free-to-play ones
            // like Fortnite, without the subscription) are its games too.
            // Also games outside the Game Pass catalog.
            if (!t.productId.empty()) {
                if (!gamePass_.count(t.productId) || !t.viaSubscription) {
                    if (seenMine.insert(t.productId).second) {
                        mine.emplace_back(t.productId, t.titleId);
                    }
                }
            }
        } else if (!t.productId.empty() && !gamePass_.count(t.productId)) {
            toBuy[t.productId] = t.titleId;
        }
    }

    bool hasCod = member || ownedTitles.count("CALLOFDUTYHQ") || ownedTitles.count("CALLOFDUTYBLACKOPS6") ||
                  ownedTitles.count("CALLOFDUTYMODERNWARFAREIII");
    if (hasCod) {
        ownedTitles.insert("CALLOFDUTYHQ");
        ownedTitles.insert("CALLOFDUTYBLACKOPS6");
        ownedTitles.insert("CALLOFDUTYMODERNWARFAREIII");
    }
    for (const auto& g : kKnownCloudGames) {
        registerCloudTitle(g.productId, g.cloudTitleId, g.xboxTitleId ? g.xboxTitleId : "", g.isFree, false);
        if (ownedTitles.count(g.cloudTitleId)) {
            ownedProducts.insert(g.productId);
            if (g.isFree || std::string_view(g.productId) == "9NCPSQQD1Z66") {
                if (seenMine.insert(g.productId).second) {
                    mine.emplace_back(g.productId, g.cloudTitleId);
                }
            }
        } else if (!gamePass_.count(g.productId)) {
            toBuy[g.productId] = g.cloudTitleId;
        }
    }
    // Games to buy: the most popular in the cloud first, then the rest.
    std::vector<Item> buy;
    for (const auto& id : popular_) {
        auto it = toBuy.find(id);
        if (it == toBuy.end()) continue;
        buy.emplace_back(it->first, it->second);
        toBuy.erase(it);
    }
    for (const auto& [pid, tid] : toBuy) buy.emplace_back(pid, tid);

    std::vector<std::string> missing;
    for (const auto& m : mine)
        if (!products_.count(m.first)) missing.push_back(m.first);
    for (const auto& [pid, _] : toBuy)
        if (!products_.count(pid)) missing.push_back(pid);
    if (!missing.empty() && !xcloud::fetchProducts(missing, market_, language_, products_, err, false))
        XC_LOGW("%s", err.c_str());
    if (stop && *stop) return;
    ownedTitles_ = std::move(ownedTitles);
    ownershipFresh_ = true;
    ownedProducts_ = std::move(ownedProducts);
    owned_ = std::move(mine);
    purchasable_ = std::move(buy);
    purchasableSet_.clear();
    for (const auto& p : purchasable_) purchasableSet_.insert(p.first);
    ownershipKnown_ = true;
    gamePassMember_ = member;
    sortOwned();
    saveCache();
    XC_LOGI("library: account can stream %zu titles; %zu outside Game Pass; %zu more to buy", ownedTitles_.size(),
            owned_.size(), purchasable_.size());
    changed();
}

void Library::loadCatalogNames(const Changed& changed, const std::atomic<bool>* stop) {
    // Names and art: the account's games and the games to buy first (their
    // lists are on screen), then the Game Pass catalog for its search. Also
    // anything still in another language.
    std::vector<std::string> missing;
    std::set<std::string> queued;
    auto want = [&](const std::string& id) {
        auto it = products_.find(id);
        bool needs = (it == products_.end()) ||
                     it->second.title.empty() ||
                     (it->second.xcloudTitleId.empty() || it->second.xcloudTitleId == "None") ||
                     otherLanguage_.count(id);
        if (needs && queued.insert(id).second) missing.push_back(id);
    };
    for (const auto* list : {&owned_, &purchasable_})
        for (const auto& item : *list) want(item.first);
    for (const auto& id : allGames_) want(id);
    progress_ = {true, 0, missing.size()};
    changed();
    constexpr size_t kBatch = 100;
    for (size_t i = 0; i < missing.size(); i += kBatch) {
        if (stop && *stop) break;
        std::vector<std::string> batch(missing.begin() + static_cast<long>(i),
                                       missing.begin() + static_cast<long>(std::min(missing.size(), i + kBatch)));
        std::map<std::string, xcloud::Product> fetched;
        std::string e;
        if (!xcloud::fetchProducts(batch, market_, language_, fetched, e, false)) {
            XC_LOGW("%s", e.c_str());
            break;
        }
        for (auto& [id, p] : fetched) {
            auto ct = cloudTitleOf_.find(id);
            if (p.xcloudTitleId.empty() || p.xcloudTitleId == "None") {
                if (ct != cloudTitleOf_.end()) p.xcloudTitleId = ct->second;
            }
            if (p.xcloudTitleId == "None") p.xcloudTitleId.clear();
            // Keep the hero art and description already known, until they
            // come in the new language (hydrate()).
            auto old = products_.find(id);
            if (old != products_.end()) {
                if (p.heroUrl.empty()) p.heroUrl = old->second.heroUrl;
                if (p.description.empty() && !otherLanguage_.count(id)) p.description = old->second.description;
                if (p.xcloudTitleId.empty()) p.xcloudTitleId = old->second.xcloudTitleId;
                if (!p.detailed && old->second.detailed) {  // modes, languages and plan don't change with the language
                    p.detailed = true;
                    p.detailedAt = old->second.detailedAt;
                    p.modes = old->second.modes;
                    p.plan = old->second.plan;
                    p.languages = old->second.languages;
                }
            }
            products_[id] = std::move(p);
            otherLanguage_.erase(id);
        }
        progress_.done = std::min(missing.size(), i + kBatch);
        changed();  // each batch: the lists fill in as names arrive
    }
    progress_ = {};
    saveCache();
    XC_LOGI("library: search covers %zu Game Pass games and %zu of yours or to buy", gamePassSearchPool().size(),
            librarySearchPool().size());
    changed();
}

bool Library::fetchFull(const std::vector<std::string>& ids, const Changed& changed, const std::atomic<bool>* stop,
                        size_t firstBatch) {
    bool ok = true;
    int batches = 0;
    std::vector<std::string> order;
    std::set<std::string> queued;
    for (const auto& id : ids) {
        auto it = products_.find(id);
        if ((it == products_.end() || !it->second.detailed) && queued.insert(id).second)
            order.push_back(id);
    }
    // Batches of 20 (~10 KB per product: bigger ones made the catalog time
    // out, 504, and weren't faster).
    for (size_t i = 0, n = firstBatch; i < order.size(); i += n, n = 20) {
        if (stop && *stop) return true;
        std::vector<std::string> batch(order.begin() + static_cast<long>(i),
                                       order.begin() + static_cast<long>(std::min(order.size(), i + n)));
        std::map<std::string, xcloud::Product> full;
        std::string e;
        // A batch that fails (a timeout now and then) is asked again; one
        // that keeps failing is left for next time, and the rest carries on.
        bool got = false;
        for (int attempt = 0; attempt < 3 && !got; ++attempt) {
            if (attempt) platform::sleepMs(1000);
            got = xcloud::fetchProducts(batch, market_, language_, full, e, true);
            if (!got) XC_LOGW("%s%s", e.c_str(), attempt < 2 ? " (asking again)" : " (skipped)");
        }
        if (!got) {
            ok = false;
            continue;
        }
        for (const auto& id : batch) {
            // Not in the catalog's details: nothing more to know, not asked again.
            auto known = products_.find(id);
            if (!full.count(id) && known != products_.end()) known->second.detailed = true;
        }
        for (auto& [id, p] : full) products_[id] = std::move(p);
        changed();
        if (++batches % 4 == 0) saveCache();  // a long fetch keeps what it got if the app closes
    }
    return ok;
}

void Library::fetchPlatformsFor(const std::vector<std::string>& ids, const std::string& xblAuth) {
    if (xblAuth.empty()) return;
    std::set<std::string> want;
    for (const auto& pid : ids) {
        auto x = xboxTitleOf_.find(pid);
        std::string xbox = x != xboxTitleOf_.end() ? x->second : std::string();
        if (xbox.empty()) {
            auto p = products_.find(pid);
            if (p != products_.end()) xbox = p->second.xboxTitleId;
        }
        if (!xbox.empty() && !platform_.count(xbox)) want.insert(xbox);
    }
    if (want.empty()) return;
    std::vector<std::string> list(want.begin(), want.end());
    std::string err;
    size_t before = platform_.size();
    if (!xcloud::fetchPlatforms(xblAuth, list, platform_, err)) XC_LOGW("%s", err.c_str());
    XC_LOGI("library: consoles of %zu more games", platform_.size() - before);
}

void Library::loadFirstScreen(const std::string& xblAuth, const Changed& changed, const std::atomic<bool>* stop) {
    uint64_t t0 = platform::nowMs();
    // What is on screen at once: the first cards of each row (the focused
    // first row first), then the start of "Your games".
    constexpr size_t kPerRow = 8, kOwned = 12;
    std::vector<std::string> ids;
    for (const auto& r : layout_)
        for (size_t i = 0; i < r.items.size() && i < kPerRow; ++i) ids.push_back(r.items[i].first);
    for (size_t i = 0; i < owned_.size() && i < kOwned; ++i) ids.push_back(owned_[i].first);
    for (size_t i = 0; i < purchasable_.size() && i < kOwned; ++i) ids.push_back(purchasable_[i].first);
    // Badges for every card of the rows (one titlehub call), then the art.
    std::vector<std::string> badges = ids;
    for (const auto& r : layout_)
        for (const auto& item : r.items) badges.push_back(item.first);
    std::vector<std::string> firstRows(ids.begin(), ids.begin() + static_cast<long>(std::min<size_t>(ids.size(), 20)));
    fetchFull(firstRows, changed, stop);
    fetchPlatformsFor(badges, xblAuth);
    changed();
    fetchFull(ids, changed, stop);
    saveCache();
    XC_LOGI("library: first screen ready in %llu ms", static_cast<unsigned long long>(platform::nowMs() - t0));
}

void Library::loadPlatforms(const std::string& xblAuth, const Changed& changed, const std::atomic<bool>* stop) {
    if (stop && *stop) return;
    std::vector<std::string> ids;
    for (const auto& r : layout_)
        for (const auto& item : r.items) ids.push_back(item.first);
    for (const auto* list : {&owned_, &purchasable_})
        for (const auto& item : *list) ids.push_back(item.first);
    ids.insert(ids.end(), allGames_.begin(), allGames_.end());
    fetchPlatformsFor(ids, xblAuth);
    saveCache();
    changed();
}

void Library::hydrate(const Changed& changed, const std::atomic<bool>* stop) {
    // On-screen order, so what is shown first gets its art first: the rows,
    // then the account's games.
    std::vector<std::string> ids;
    for (const auto& r : layout_)
        for (const auto& item : r.items) ids.push_back(item.first);
    for (const auto& item : owned_) ids.push_back(item.first);
    // Then every game the search goes through: its filters (play mode,
    // language) need the full details.
    for (const auto& item : purchasable_) ids.push_back(item.first);
    ids.insert(ids.end(), allGames_.begin(), allGames_.end());
    uint64_t t0 = platform::nowMs();
    fetchFull(ids, changed, stop);
    // The store changes descriptions and details now and then: the oldest
    // few (over 30 days) are fetched again on each start, never all at once.
    constexpr int64_t kRefreshAge = 30 * 24 * 3600;
    constexpr size_t kRefreshPerStart = 40;
    int64_t now = static_cast<int64_t>(std::time(nullptr));
    std::vector<std::pair<int64_t, std::string>> old;
    for (const auto& [id, p] : products_)
        if (p.detailed && p.detailedAt && now - p.detailedAt > kRefreshAge) old.push_back({p.detailedAt, id});
    std::sort(old.begin(), old.end());
    if (old.size() > kRefreshPerStart) old.resize(kRefreshPerStart);
    if (!old.empty() && !(stop && *stop)) {
        std::vector<std::string> refresh;
        for (const auto& [at, id] : old) refresh.push_back(id);
        size_t done = 0;
        for (size_t i = 0; i < refresh.size(); i += 20) {
            std::vector<std::string> batch(refresh.begin() + static_cast<long>(i),
                                           refresh.begin() + static_cast<long>(std::min(refresh.size(), i + 20)));
            std::map<std::string, xcloud::Product> fresh;
            std::string e;
            if (!xcloud::fetchProducts(batch, market_, language_, fresh, e, true)) {
                XC_LOGW("%s (refresh: next start)", e.c_str());
                break;
            }
            for (const auto& id : batch) {
                auto it = fresh.find(id);
                if (it != fresh.end()) products_[id] = std::move(it->second), ++done;
                else products_[id].detailedAt = now;  // gone from the catalog's details: not asked again for a while
            }
        }
        XC_LOGI("library: %zu of %zu old details refreshed (%zu over 30 days in all)", done, refresh.size(),
                static_cast<size_t>(std::count_if(products_.begin(), products_.end(), [&](const auto& kv) {
                    return kv.second.detailed && now - kv.second.detailedAt > kRefreshAge;
                })));
        changed();
    }
    saveCache();  // descriptions, hero art, modes and languages for next time
    size_t detailed = 0;
    for (const auto& [id, p] : products_) detailed += p.detailed;
    XC_LOGI("library: %zu rows, %zu of %zu products with details (%llu s)", layout_.size(), detailed, products_.size(),
            static_cast<unsigned long long>((platform::nowMs() - t0) / 1000));
}

void Library::loadFriends(const std::string& xblAuth, const Changed& changed, const std::atomic<bool>* stop) {
    if (xblAuth.empty() || (stop && *stop)) return;
    std::vector<xcloud::FriendPresence> presence;
    std::string err;
    if (!xcloud::fetchFriendsPresence(xblAuth, presence, err, language_)) return;
    if (stop && *stop) return;

    // Map Xbox Live Title ID / Cloud Title ID -> Store Product ID
    std::map<std::string, std::string> productByXbox;
    for (const auto& [pid, xid] : xboxTitleOf_) {
        if (!xid.empty()) productByXbox[xid] = pid;
    }
    for (const auto& [pid, prod] : products_) {
        productByXbox[pid] = pid;
        if (!prod.xboxTitleId.empty() && !productByXbox.count(prod.xboxTitleId)) {
            productByXbox[prod.xboxTitleId] = pid;
        }
        if (!prod.xcloudTitleId.empty() && !productByXbox.count(prod.xcloudTitleId)) {
            productByXbox[prod.xcloudTitleId] = pid;
        }
    }
    for (const auto& [pid, cid] : cloudTitleOf_) {
        if (!cid.empty() && !productByXbox.count(cid)) {
            productByXbox[cid] = pid;
        }
    }

    // Group friends playing accessible games
    std::map<std::string, std::vector<ui::FriendPlaying>> friendsByProduct;
    for (const auto& fp : presence) {
        if (fp.presenceState != "Online" || fp.titleId.empty()) continue;
        auto it = productByXbox.find(fp.titleId);
        if (it == productByXbox.end()) continue;
        const std::string& pid = it->second;

        // Check if game is playable on this account (Game Pass or owned)
        ui::GameTile t = tile(pid);
        if (!t.playable) continue;

        friendsByProduct[pid].push_back({fp.gamertag, fp.gamerpicUrl});
    }

    std::vector<ui::GameTile> tiles;
    std::vector<std::string> needDetails;
    for (auto& [pid, friendsList] : friendsByProduct) {
        ui::GameTile t = tile(pid);
        if (t.productId.empty() || !t.playable) continue;
        if (t.heroUrl.empty()) {
            auto pit = products_.find(pid);
            if (pit != products_.end() && !pit->second.heroUrl.empty()) {
                t.heroUrl = pit->second.heroUrl;
            } else {
                for (const auto& [otherPid, otherProd] : products_) {
                    if (!otherProd.heroUrl.empty() &&
                        ((!t.titleId.empty() && otherProd.xcloudTitleId == t.titleId) ||
                         (!t.xboxTitleId.empty() && otherProd.xboxTitleId == t.xboxTitleId))) {
                        t.heroUrl = otherProd.heroUrl;
                        if (t.description.empty()) t.description = otherProd.description;
                        break;
                    }
                }
            }
        }
        if (t.heroUrl.empty() && !pid.empty()) {
            needDetails.push_back(pid);
        }
        t.friends = std::move(friendsList);
        tiles.push_back(std::move(t));
    }

    friendsTiles_ = std::move(tiles);
    if (!friendsTiles_.empty()) {
        XC_LOGI("library: %zu friends playing %zu accessible games now", presence.size(), friendsTiles_.size());
        changed();
        if (!needDetails.empty()) {
            fetchFull(needDetails, changed, stop);
            for (auto& t : friendsTiles_) {
                if (t.heroUrl.empty()) {
                    auto fresh = tile(t.productId, t.titleId);
                    if (!fresh.heroUrl.empty()) t.heroUrl = fresh.heroUrl;
                    if (!fresh.description.empty() && t.description.empty()) t.description = fresh.description;
                }
            }
            changed();
        }
    }
}

}  // namespace xc::app
