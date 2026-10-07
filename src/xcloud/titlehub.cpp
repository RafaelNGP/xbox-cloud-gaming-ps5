// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "xcloud/titlehub.h"

#include "net/http.h"
#include "util/json.h"

#include <algorithm>

namespace xc::xcloud {

bool fetchPlatforms(const std::string& auth, const std::vector<std::string>& xboxTitleIds,
                    std::map<std::string, std::string>& out, std::string& err) {
    constexpr size_t kBatch = 100;
    for (size_t i = 0; i < xboxTitleIds.size(); i += kBatch) {
        json::Value ids = json::Value::array();
        for (size_t k = i; k < xboxTitleIds.size() && k < i + kBatch; ++k) ids.push(xboxTitleIds[k]);
        json::Value body = json::Value::object();
        body.set("pfns", nullptr);
        body.set("titleIds", ids);
        net::Request req;
        req.method = "POST";
        req.url = "https://titlehub.xboxlive.com/titles/batch/decoration/detail";
        req.headers = {{"Authorization", auth},
                       {"x-xbl-contract-version", "2"},
                       {"Content-Type", "application/json"},
                       {"Accept-Language", "en-US"}};
        req.body = body.dump();
        auto r = net::perform(req);
        auto j = json::parse(r.body);
        if (!r.ok() || !j) {
            err = "titlehub: " + (r.status ? "HTTP " + std::to_string(r.status) : r.error);
            return false;
        }
        for (const auto& t : (*j)["titles"].items()) {
            bool has360 = false, hasOne = false, hasSeries = false, optimized = false;
            for (const auto& d : t["devices"].items()) {
                has360 |= d.str() == "Xbox360";
                hasOne |= d.str() == "XboxOne";
                hasSeries |= d.str() == "XboxSeries";
            }
            for (const auto& a : t["detail"]["attributes"].items()) optimized |= a["name"].str() == "ConsoleGen9Optimized";
            const char* code = has360                          ? kPlatform360
                               : optimized || (hasSeries && !hasOne) ? kPlatformSeries
                               : hasOne || hasSeries                 ? kPlatformOne
                                                                     : nullptr;  // PC-only entries
            if (!code) continue;
            // The id asked for may be an older one titlehub maps to its
            // current id: record both.
            out[t["titleId"].str()] = code;
            if (!t["modernTitleId"].str().empty()) out[t["modernTitleId"].str()] = code;
        }
    }
    return true;
}

}  // namespace xc::xcloud
