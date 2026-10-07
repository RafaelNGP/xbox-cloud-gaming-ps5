// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "xcloud/prices.h"

#include "net/http.h"
#include "util/json.h"

#include <cmath>
#include <cstdio>

namespace xc::xcloud {

bool fetchPrices(const std::vector<std::string>& productIds, const std::string& market, const std::string& language,
                 std::map<std::string, Price>& out, std::string& err) {
    constexpr size_t kBatch = 20;
    for (size_t i = 0; i < productIds.size(); i += kBatch) {
        std::string ids;
        for (size_t k = i; k < productIds.size() && k < i + kBatch; ++k) ids += (ids.empty() ? "" : ",") + productIds[k];
        net::Request req;
        req.url = "https://displaycatalog.mp.microsoft.com/v7.0/products?bigIds=" + ids + "&market=" + market +
                  "&languages=" + language + "&fieldsTemplate=browse";
        auto r = net::perform(req);
        auto j = json::parse(r.body);
        if (!r.ok() || !j) {
            err = "prices: " + (r.status ? "HTTP " + std::to_string(r.status) : r.error);
            return false;
        }
        for (const auto& p : (*j)["Products"].items()) {
            // The first availability that can be bought, among the SKUs.
            for (const auto& dsa : p["DisplaySkuAvailabilities"].items()) {
                bool found = false;
                for (const auto& av : dsa["Availabilities"].items()) {
                    bool purchase = false;
                    for (const auto& a : av["Actions"].items()) purchase |= a.str() == "Purchase";
                    const auto& price = av["OrderManagementData"]["Price"];
                    if (!purchase || !price.isObject()) continue;
                    Price pr;
                    pr.list = price["ListPrice"].asNumber();
                    pr.msrp = price["MSRP"].asNumber();
                    pr.currency = price["CurrencyCode"].str();
                    out[p["ProductId"].str()] = pr;
                    found = true;
                    break;
                }
                if (found) break;
            }
        }
    }
    return true;
}

std::string formatPrice(double amount, const std::string& currency) {
    long cents = std::lround(amount * 100);
    long whole = cents / 100, frac = cents % 100;
    // Thousands with `sep`, cents after `dec`.
    auto number = [&](char sep, char dec) {
        std::string digits = std::to_string(whole), grouped;
        for (size_t i = 0; i < digits.size(); ++i) {
            if (i && (digits.size() - i) % 3 == 0) grouped += sep;
            grouped += digits[i];
        }
        char tail[8];
        std::snprintf(tail, sizeof tail, "%c%02ld", dec, frac);
        return grouped + tail;
    };
    if (currency == "BRL") return "R$ " + number('.', ',');
    if (currency == "USD") return "$" + number(',', '.');
    if (currency == "CAD") return "CA$" + number(',', '.');
    if (currency == "MXN") return "MX$" + number(',', '.');
    if (currency == "GBP") return "\xC2\xA3" + number(',', '.');
    if (currency == "EUR") return number('.', ',') + " \xE2\x82\xAC";
    if (currency == "JPY") return "\xC2\xA5" + std::to_string(std::lround(amount));
    return currency + " " + number(',', '.');
}

}  // namespace xc::xcloud
