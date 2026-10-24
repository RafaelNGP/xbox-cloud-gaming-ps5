// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#pragma once

#include "app/library.h"
#include "xcloud/catalog.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace xc::app {

constexpr int kCacheVersion = 2;

struct ListSpec {
    const char* sigl;
    size_t limit;
};

inline bool namesSeries(const std::string& title) {
    return title.find("Series X|S") != std::string::npos || title.find("Series X/S") != std::string::npos;
}

inline bool endsWith(const std::string& s, const char* tail) {
    size_t n = std::strlen(tail);
    return s.size() >= n && s.compare(s.size() - n, n, tail) == 0;
}

inline int pairVersion(const xcloud::Product& p) {
    const std::string& c = p.xcloudTitleId;
    if (c.find("XBOXONE") != std::string::npos) return -1;
    if (c.find("XBOXSERIES") != std::string::npos || c.find("SERIESXS") != std::string::npos || endsWith(c, "XS"))
        return 1;
    if (namesSeries(p.title) || endsWith(p.title, "X|S") || endsWith(p.title, "X/S")) return 1;
    if (p.title.find("Xbox One") != std::string::npos || p.title.find("Xbox 360") != std::string::npos) return -1;
    return 0;
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

inline const KnownCloudGame kKnownCloudGames[] = {
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
     "https://store-images.s-microsoft.com/image/apps.21616.14207097610490811.5b338e31-5c12-4d95-9a2a-2f16ceb01377.5c60a78b-cc32-4777-9141-90ebd76e88ad",
     "https://store-images.s-microsoft.com/image/apps.28966.14207097610490811.5b338e31-5c12-4d95-9a2a-2f16ceb01377.acc0294d-61eb-49e2-a059-758d5b1227d4",
     "https://store-images.s-microsoft.com/image/apps.5449.14207097610490811.5b338e31-5c12-4d95-9a2a-2f16ceb01377.54d31648-9587-49a6-b312-7df6ce023530",
     "Shooter",
     "Call of Duty®: Black Ops 6 is a spy action thriller set in the early 90s, characterized by mind-bending narrative gameplay.",
     "18", "ClassInd", "Violência Extrema, Drogas Lícitas, Linguagem Imprópria",
     false},
    {"9P22THR57FV7", "CALLOFDUTYBLACKOPS6", "1822992790", "Call of Duty®: Black Ops 6 - Launcher", "Activision Publishing Inc.",
     nullptr, nullptr, nullptr, "Shooter", nullptr, "18", "ClassInd", nullptr, false},
    {"9NLZZ74TFD7P", "CALLOFDUTYMODERNWARFAREIII", "2003727579", "Call of Duty®: Modern Warfare® III", "Activision Publishing Inc.",
     "https://store-images.s-microsoft.com/image/apps.44737.13702346117700120.96dcc05d-7589-4e95-abbc-4fbc3399d7ff.76cfb4be-3069-4cd2-a883-70654bf6898e",
     "https://store-images.s-microsoft.com/image/apps.35931.13702346117700120.96dcc05d-7589-4e95-abbc-4fbc3399d7ff.bf621533-dad7-4478-8719-e8ff3da2223e",
     "https://store-images.s-microsoft.com/image/apps.44508.13702346117700120.96dcc05d-7589-4e95-abbc-4fbc3399d7ff.aca3a7f0-6dab-4d13-be0a-c87a416eac22",
     "Shooter",
     "In the direct sequel to the record-breaking Call of Duty®: Modern Warfare® II, Captain Price and Task Force 141 face off against the ultimate threat.",
     "18", "ClassInd", "Violência Extrema, Drogas Lícitas, Linguagem Imprópria", false},
    {"9PMFDKG5F9R7", "CALLOFDUTYMODERNWARFAREIII", "2003727579", "Call of Duty®: Modern Warfare® III", "Activision Publishing Inc.",
     nullptr, nullptr, nullptr, "Shooter", nullptr, "18", "ClassInd", nullptr, false},
    {"9NDKFJ6XDX2M", "CALLOFDUTYMODERNWARFAREIII", "2003727579", "Call of Duty®: Modern Warfare® III", "Activision Publishing Inc.",
     nullptr, nullptr, nullptr, "Shooter", nullptr, "18", "ClassInd", nullptr, false},
    {"9PNGFSX7S117", "CALLOFDUTYMODERNWARFAREII", "1959007243", "Call of Duty®: Modern Warfare® II", "Activision Publishing Inc.",
     "https://store-images.s-microsoft.com/image/apps.62866.14349375889218622.77ec9575-f44b-4013-97c6-f9cd0bbc53f5.b1790761-1d9e-4ffd-b146-3f68bdeb949e",
     "https://store-images.s-microsoft.com/image/apps.34778.14349375889218622.77ec9575-f44b-4013-97c6-f9cd0bbc53f5.fd60100a-bbb6-4eb3-a03c-6da8e90db089",
     "https://store-images.s-microsoft.com/image/apps.46802.14349375889218622.77ec9575-f44b-4013-97c6-f9cd0bbc53f5.041995ec-87c2-4c88-b7a6-98dbdbb5c975",
     "Shooter",
     "Call of Duty®: Modern Warfare® II drops players into an unprecedented global conflict that features the return of the iconic Operators of Task Force 141.",
     "18", "ClassInd", "Violência Extrema, Drogas Lícitas, Linguagem Imprópria", false},
    {"9N872NBW5XG5", "CALLOFDUTYMODERNWARFAREII", "1959007243", "Call of Duty®: Modern Warfare® II", "Activision Publishing Inc.",
     nullptr, nullptr, nullptr, "Shooter", nullptr, "18", "ClassInd", nullptr, false},
    {"9P7RQ1QFFF8R", "CALLOFDUTYMODERNWARFAREII", "1959007243", "Call of Duty®: Modern Warfare® II", "Activision Publishing Inc.",
     nullptr, nullptr, nullptr, "Shooter", nullptr, "18", "ClassInd", nullptr, false},
    {"9NVQBQ3F6W9W", "CALLOFDUTYMODERNWARFARE", "609700427", "Call of Duty®: Modern Warfare®", "Activision Publishing Inc.",
     "https://store-images.s-microsoft.com/image/apps.52064.14393996338935986.84c4ee65-c726-49ae-925f-3dca581be2df.3e540e94-9e5a-41f5-9b00-c8f5190819f7",
     "https://store-images.s-microsoft.com/image/apps.50528.14393996338935986.84c4ee65-c726-49ae-925f-3dca581be2df.31d66188-6781-49ce-a75a-b4247e73b89d",
     "https://store-images.s-microsoft.com/image/apps.1926.14393996338935986.84c4ee65-c726-49ae-925f-3dca581be2df.46cd9cf0-2829-4517-a140-3d163199497f",
     "Shooter",
     "Experience a visceral Campaign or assemble your team in the ultimate online playground with multiple Special Ops challenges and a mix of Multiplayer maps and modes.",
     "18", "ClassInd", "Violência Extrema, Drogas Lícitas, Linguagem Imprópria", false},
    {"C5DTJ99626K3", "CALLOFDUTYMODERNWARFARE", "609700427", "Call of Duty®: Modern Warfare®", "Activision Publishing Inc.",
     "https://store-images.s-microsoft.com/image/apps.52064.67185831113154542.823e899c-d262-40a0-91f6-eee04bdc3713.74be39c5-2d6e-4e6b-960b-7d8077da0142",
     "https://store-images.s-microsoft.com/image/apps.50528.67185831113154542.823e899c-d262-40a0-91f6-eee04bdc3713.dd7010c6-9641-4c5a-b8b5-7eb16ddc67ea",
     "https://store-images.s-microsoft.com/image/apps.1926.67185831113154542.823e899c-d262-40a0-91f6-eee04bdc3713.ca4b2ff5-8dc9-4774-a5f1-4e379a72cbda",
     "Shooter",
     "Experience a visceral Campaign or assemble your team in the ultimate online playground with multiple Special Ops challenges and a mix of Multiplayer maps and modes.",
     "18", "ClassInd", "Violência Extrema, Drogas Lícitas, Linguagem Imprópria", false},
    {"9P7PFH4RMFZD", "CALLOFDUTYBLACKOPSCOLDWAR", "1790482525", "Call of Duty®: Black Ops Cold War", "Activision Publishing Inc.",
     "https://store-images.s-microsoft.com/image/apps.16401.14107985044965209.e8fed65f-093d-40d1-849f-6c564d2ad876.13b9c6b6-dddd-4f67-a303-b98250929229",
     "https://store-images.s-microsoft.com/image/apps.25992.14107985044965209.e8fed65f-093d-40d1-849f-6c564d2ad876.89955624-8043-4f20-8620-4381e11a3546",
     "https://store-images.s-microsoft.com/image/apps.52829.14107985044965209.e8fed65f-093d-40d1-849f-6c564d2ad876.8fe14720-2b8f-4b9c-aebf-07f1610816d5",
     "Shooter",
     "The iconic Black Ops series is back with Call of Duty®: Black Ops Cold War - the direct sequel to the original and fan-favorite Call of Duty®: Black Ops.\n\nWelcome to the brink. Welcome to Call of Duty®: Black Ops Cold War.",
     "18", "ClassInd", "Violência Extrema, Drogas Lícitas, Linguagem Imprópria", false},
    {"9NPLRJ9CC870", "CALLOFDUTYBLACKOPSCOLDWARXBOXSERIESXS", "1790482525", "Call of Duty®: Black Ops Cold War - Xbox Series X|S", "Activision Publishing Inc.",
     "https://store-images.s-microsoft.com/image/apps.16401.13791444986287693.243003e9-8583-4df9-a0fc-2345d2f82ed4.e5e2cd4a-420a-41eb-bade-0eab002eab9c",
     "https://store-images.s-microsoft.com/image/apps.25992.13791444986287693.243003e9-8583-4df9-a0fc-2345d2f82ed4.b477eb21-1438-49b1-bf72-a36ed8d74bb8",
     "https://store-images.s-microsoft.com/image/apps.52829.13791444986287693.243003e9-8583-4df9-a0fc-2345d2f82ed4.92c561dc-948f-445a-ad29-5e287d884e0c",
     "Shooter",
     "The iconic Black Ops series is back with Call of Duty®: Black Ops Cold War - the direct sequel to the original and fan-favorite Call of Duty®: Black Ops.\n\nWelcome to the brink. Welcome to Call of Duty®: Black Ops Cold War.",
     "18", "ClassInd", "Violência Extrema, Drogas Lícitas, Linguagem Imprópria", false},
    {"9PHP6HF58MB5", "CALLOFDUTYVANGUARD", "1929807318", "Call of Duty®: Vanguard", "Activision Publishing Inc.",
     "https://store-images.s-microsoft.com/image/apps.25644.14231872788446538.01bda410-79a7-42cd-a67b-497b17d840f1.b577a06e-1952-4f78-9ca8-6e43b94f985b",
     "https://store-images.s-microsoft.com/image/apps.31712.14231872788446538.ec1df70e-1780-4ec2-90fc-de130713250f.37f86b3f-4921-4955-902e-3325f37a3e17",
     "https://store-images.s-microsoft.com/image/apps.27881.14231872788446538.ec1df70e-1780-4ec2-90fc-de130713250f.b54d334f-52d0-45e1-ac31-c86b1d4b6ad6",
     "Shooter",
     "Call of Duty®: Vanguard includes a deeply engaging single player Campaign, a massive Multiplayer offering featuring over 20 maps including 16 built for core Multiplayer and an exciting new Zombies experience developed by Treyarch.",
     "18", "ClassInd", "Violência Extrema, Drogas Lícitas, Linguagem Imprópria", false},
    {"9MWHTW1SF7RW", "CALLOFDUTYVANGUARDXBOXSERIESXS", "1929807318", "Call of Duty®: Vanguard - Xbox Series X|S", "Activision Publishing Inc.",
     "https://store-images.s-microsoft.com/image/apps.25644.13843233178803270.b12b929c-1b87-4fa1-900a-23a132dc9775.bdbacf51-1647-4c87-b9dc-6c455834d9bb",
     "https://store-images.s-microsoft.com/image/apps.31712.13843233178803270.d4ae9cce-02bd-4f56-8f61-6e02568a7dfd.401708cf-3756-4e90-b73d-bd1a6d4e1c09",
     "https://store-images.s-microsoft.com/image/apps.27881.13843233178803270.d4ae9cce-02bd-4f56-8f61-6e02568a7dfd.7b70ba8c-ba77-4fc3-af0d-cc1f87530bb0",
     "Shooter",
     "Call of Duty®: Vanguard includes a deeply engaging single player Campaign, a massive Multiplayer offering featuring over 20 maps including 16 built for core Multiplayer and an exciting new Zombies experience developed by Treyarch.",
     "18", "ClassInd", "Violência Extrema, Drogas Lícitas, Linguagem Imprópria", false},
};

inline void seedKnownGames(std::map<std::string, std::string>& cloudTitleOf,
                           std::map<std::string, std::string>& xboxTitleOf,
                           std::set<std::string>& freeInStore,
                           std::set<std::string>& gamePass,
                           std::vector<std::string>& allGames,
                           std::map<std::string, xcloud::Product>& products) {
    for (const auto& g : kKnownCloudGames) {
        cloudTitleOf[g.productId] = g.cloudTitleId;
        if (g.xboxTitleId && g.xboxTitleId[0]) xboxTitleOf[g.productId] = g.xboxTitleId;
        if (g.isFree) freeInStore.insert(g.productId);
        bool isCodHub = (std::string_view(g.productId) == "9NCPSQQD1Z66" ||
                         std::string_view(g.productId) == "9N201KQXS5BM" ||
                         std::string_view(g.productId) == "9NNFG8BQRCXL");
        if (isCodHub) {
            gamePass.insert(g.productId);
            if (std::find(allGames.begin(), allGames.end(), g.productId) == allGames.end()) {
                allGames.push_back(g.productId);
            }
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
            p.detailed = (g.description != nullptr && g.tileUrl != nullptr && g.heroUrl != nullptr);
            p.plan = xcloud::kPlanUltimate;
            products[g.productId] = std::move(p);
        } else {
            auto& p = products[g.productId];
            if (p.xcloudTitleId.empty() || p.xcloudTitleId == "None") p.xcloudTitleId = g.cloudTitleId;
            if (p.title.empty() && g.fallbackTitle) p.title = g.fallbackTitle;
            if (p.description.empty() && g.description) p.description = g.description;
            if (p.tileUrl.empty() && g.tileUrl) p.tileUrl = g.tileUrl;
            if (p.posterUrl.empty() && g.posterUrl) p.posterUrl = g.posterUrl;
            if (p.heroUrl.empty() && g.heroUrl) p.heroUrl = g.heroUrl;
            if (p.publisher.empty() && g.publisher) p.publisher = g.publisher;
            if (p.ratingAge.empty() && g.ratingAge) p.ratingAge = g.ratingAge;
            if (p.ratingSystem.empty() && g.ratingSystem) p.ratingSystem = g.ratingSystem;
            if (p.categories.empty() && g.category) p.categories.push_back(g.category);
            if (!p.detailed && !p.description.empty() && !p.tileUrl.empty() && !p.heroUrl.empty())
                p.detailed = true;
        }
    }
}

}  // namespace xc::app
