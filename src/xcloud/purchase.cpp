// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "xcloud/purchase.h"

#include "net/http.h"
#include "util/json.h"
#include "util/log.h"

#include <cmath>

namespace xc::xcloud {

bool fetchPurchaseOffer(const std::string& productId, const std::string& market, const std::string& language,
                        PurchaseOffer& out, std::string& err) {
    if (productId.empty()) {
        err = "empty product id";
        return false;
    }
    net::Request req;
    req.url = "https://displaycatalog.mp.microsoft.com/v7.0/products?bigIds=" + productId +
              "&market=" + (market.empty() ? "US" : market) +
              "&languages=" + (language.empty() ? "en-us" : language);
    auto r = net::perform(req);
    auto j = json::parse(r.body);
    if (!r.ok() || !j) {
        err = "catalog offer lookup failed: " + (r.status ? "HTTP " + std::to_string(r.status) : r.error);
        return false;
    }
    const auto& prods = (*j)["Products"];
    if (!prods.isArray() || prods.size() == 0) {
        err = "product not found in catalog: " + productId;
        return false;
    }
    const auto& p = prods[0];
    out.productId = productId;
    out.isFree = false;
    out.listPrice = 0.0;
    out.msrp = 0.0;
    out.skuId.clear();
    out.availabilityId.clear();

    for (const auto& dsa : p["DisplaySkuAvailabilities"].items()) {
        std::string skuId = dsa["Sku"]["SkuId"].str();
        for (const auto& av : dsa["Availabilities"].items()) {
            bool purchase = false;
            for (const auto& a : av["Actions"].items()) purchase |= (a.str() == "Purchase");
            const auto& price = av["OrderManagementData"]["Price"];
            if (!purchase || !price.isObject()) continue;

            out.skuId = skuId.empty() ? av["SkuId"].str() : skuId;
            out.availabilityId = av["AvailabilityId"].str();
            out.listPrice = price["ListPrice"].asNumber();
            out.msrp = price["MSRP"].asNumber();
            out.currency = price["CurrencyCode"].str();
            if (out.listPrice <= 0.005 && out.msrp <= 0.005) {
                out.isFree = true;
            }
            if (!out.availabilityId.empty()) {
                return true;
            }
        }
    }
    err = "no purchase availability found for " + productId;
    return false;
}

bool claimFreeProduct(const std::string& marketplaceAuthHeader, const PurchaseOffer& offer,
                      const std::string& market, std::string& err) {
    // Rigid safety check: NEVER allow purchases that cost money.
    if (!offer.isFree || offer.listPrice > 0.005 || offer.msrp > 0.005) {
        err = "safety block: product is not free (price > 0)";
        XC_LOGE("claimFreeProduct blocked: product %s is not free (list=%.2f, msrp=%.2f)",
                offer.productId.c_str(), offer.listPrice, offer.msrp);
        return false;
    }
    if (offer.productId.empty() || offer.skuId.empty() || offer.availabilityId.empty()) {
        err = "incomplete offer data (missing skuId or availabilityId)";
        return false;
    }
    if (marketplaceAuthHeader.empty()) {
        err = "missing marketplace authorization";
        return false;
    }

    json::Value item = json::Value::object();
    item.set("productId", offer.productId);
    item.set("skuId", offer.skuId);
    item.set("availabilityId", offer.availabilityId);
    item.set("quantity", int64_t(1));
    item.set("price", 0.0);
    item.set("currencyCode", offer.currency.empty() ? "USD" : offer.currency);

    json::Value items = json::Value::array();
    items.push(item);

    json::Value body = json::Value::object();
    body.set("orderItems", items);

    net::Request req;
    req.method = "POST";
    req.url = "https://purchase.mp.microsoft.com/v7.0/users/me/orders" +
              (market.empty() ? "" : "?market=" + market);
    req.headers = {
        {"Authorization", marketplaceAuthHeader},
        {"Content-Type", "application/json"},
        {"MS-CV", "PSBoxPurchase.1"}
    };
    req.body = body.dump();

    auto r = net::perform(req);
    if (!r.ok()) {
        auto j = json::parse(r.body);
        std::string detail;
        if (j) {
            if ((*j)["message"].isString()) detail = (*j)["message"].str();
            else if ((*j)["innererror"]["message"].isString()) detail = (*j)["innererror"]["message"].str();
        }
        err = detail.empty() ? ("HTTP " + std::to_string(r.status) + ": " + r.error) : detail;
        XC_LOGW("claimFreeProduct failed for %s: %s (status %d)", offer.productId.c_str(), err.c_str(), r.status);
        return false;
    }
    XC_LOGI("claimFreeProduct succeeded for %s (%s/%s)", offer.productId.c_str(), offer.skuId.c_str(), offer.availabilityId.c_str());
    return true;
}

}  // namespace xc::xcloud
