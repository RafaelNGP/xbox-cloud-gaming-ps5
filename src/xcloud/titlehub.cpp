// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "xcloud/titlehub.h"

#include "net/http.h"
#include "util/json.h"

#include <algorithm>

namespace xc::xcloud {

namespace {

// One request. A single id titlehub doesn't know fails the whole batch with
// HTTP 404: then the batch is split until the bad id is alone (and dropped).
bool fetchBatch(const std::string& auth, const std::vector<std::string>& ids, std::map<std::string, std::string>& out,
                std::string& err) {
    json::Value list = json::Value::array();
    for (const auto& id : ids) list.push(id);
    json::Value body = json::Value::object();
    body.set("pfns", nullptr);
    body.set("titleIds", list);
    net::Request req;
    req.method = "POST";
    req.url = "https://titlehub.xboxlive.com/titles/batch/decoration/detail";
    req.headers = {{"Authorization", auth},
                   {"x-xbl-contract-version", "2"},
                   {"Content-Type", "application/json"},
                   {"Accept-Language", "en-US"}};
    req.body = body.dump();
    auto r = net::perform(req);
    if (r.status == 404 || r.status == 400) {
        if (ids.size() == 1) {  // that id is unknown: remembered as such, no badge
            out[ids[0]] = "";
            return true;
        }
        size_t half = ids.size() / 2;
        std::vector<std::string> a(ids.begin(), ids.begin() + static_cast<long>(half));
        std::vector<std::string> b(ids.begin() + static_cast<long>(half), ids.end());
        return fetchBatch(auth, a, out, err) && fetchBatch(auth, b, out, err);
    }
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
        // No console in "devices": the id is the PC / Play Anywhere entry
        // (ARK, #IDARB). Every cloud title is a console game, and one that
        // isn't marked for Series X|S is the Xbox One version.
        const char* code = has360                                ? kPlatform360
                           : optimized || (hasSeries && !hasOne) ? kPlatformSeries
                                                                 : kPlatformOne;
        // The id asked for may be an older one titlehub maps to its current
        // id: record both.
        out[t["titleId"].str()] = code;
        if (!t["modernTitleId"].str().empty()) out[t["modernTitleId"].str()] = code;
    }
    return true;
}

}  // namespace

bool fetchPlatforms(const std::string& auth, const std::vector<std::string>& xboxTitleIds,
                    std::map<std::string, std::string>& out, std::string& err) {
    constexpr size_t kBatch = 100;
    for (size_t i = 0; i < xboxTitleIds.size(); i += kBatch) {
        std::vector<std::string> batch(xboxTitleIds.begin() + static_cast<long>(i),
                                       xboxTitleIds.begin() + static_cast<long>(std::min(xboxTitleIds.size(), i + kBatch)));
        if (!fetchBatch(auth, batch, out, err)) return false;
    }
    return true;
}

}  // namespace xc::xcloud
