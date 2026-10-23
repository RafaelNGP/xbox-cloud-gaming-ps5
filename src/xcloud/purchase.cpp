// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "xcloud/purchase.h"

#include "net/http.h"
#include "platform/platform.h"
#include "util/json.h"
#include "util/log.h"

#include <cmath>
#include <mutex>
#include <set>

namespace xc::xcloud {

namespace {
std::mutex s_unclaimableMutex;
std::set<std::string> s_unclaimable = {
    // Known non-standalone / token bundles that lack purchase availability
    "9PMFDKG5F9R7", "9N872NBW5XG5"
};
std::string s_unclaimablePath;
}  // namespace

void markProductUnclaimable(const std::string& productId) {
    if (productId.empty()) return;
    std::string path;
    {
        std::lock_guard<std::mutex> lock(s_unclaimableMutex);
        s_unclaimable.insert(productId);
        path = s_unclaimablePath;
    }
    if (!path.empty()) saveUnclaimableProducts(path);
}

bool isProductUnclaimable(const std::string& productId) {
    if (productId.empty()) return false;
    std::lock_guard<std::mutex> lock(s_unclaimableMutex);
    return s_unclaimable.count(productId) > 0;
}

void loadUnclaimableProducts(const std::string& path) {
    std::lock_guard<std::mutex> lock(s_unclaimableMutex);
    s_unclaimablePath = path;
    std::string text;
    if (platform::readFile(path, text)) {
        if (auto j = json::parse(text)) {
            for (const auto& item : (*j)["unclaimable"].items()) {
                if (item.isString() && !item.str().empty()) s_unclaimable.insert(item.str());
            }
        }
    }
}

void saveUnclaimableProducts(const std::string& path) {
    std::set<std::string> copy;
    {
        std::lock_guard<std::mutex> lock(s_unclaimableMutex);
        copy = s_unclaimable;
    }
    json::Value j = json::Value::object();
    json::Value arr = json::Value::array();
    for (const auto& id : copy) arr.push(id);
    j.set("unclaimable", arr);
    platform::writeFileAtomic(path, j.dump());
}

bool fetchPurchaseOffer(const std::string& productId, const std::string& market, const std::string& language,
                        PurchaseOffer& out, std::string& err) {
    if (productId.empty()) {
        err = "empty product id";
        return false;
    }
    if (isProductUnclaimable(productId)) {
        err = "product has no claimable availability: " + productId;
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

    PurchaseOffer fallbackFree{};
    PurchaseOffer fallbackAny{};

    for (const auto& dsa : p["DisplaySkuAvailabilities"].items()) {
        std::string skuId = dsa["Sku"]["SkuId"].str();
        for (const auto& av : dsa["Availabilities"].items()) {
            bool purchase = false;
            for (const auto& a : av["Actions"].items()) {
                const auto act = a.str();
                if (act == "Purchase") purchase = true;
            }
            const auto& price = av["OrderManagementData"]["Price"];
            if (!purchase || !price.isObject()) continue;
            bool remediation = av["RemediationRequired"].asBool();
            if (remediation) continue;

            PurchaseOffer cand;
            cand.productId = productId;
            cand.skuId = skuId.empty() ? av["SkuId"].str() : skuId;
            cand.availabilityId = av["AvailabilityId"].str();
            cand.listPrice = price["ListPrice"].asNumber();
            cand.msrp = price["MSRP"].asNumber();
            cand.currency = price["CurrencyCode"].str();
            cand.isFree = (cand.listPrice <= 0.005 && cand.msrp <= 0.005);
            if (cand.availabilityId.empty()) continue;

            if (cand.isFree) {
                out = cand;
                return true;
            }
            if (fallbackAny.availabilityId.empty()) {
                fallbackAny = cand;
            }
        }
    }
    if (!fallbackAny.availabilityId.empty()) {
        out = fallbackAny;
        return true;
    }
    markProductUnclaimable(productId);
    err = "no purchase availability found for " + productId;
    return false;
}

namespace {
std::string uuid4() {
    uint8_t b[16];
    platform::randomBytes(b, sizeof b);
    b[6] = static_cast<uint8_t>((b[6] & 0x0f) | 0x40);
    b[8] = static_cast<uint8_t>((b[8] & 0x3f) | 0x80);
    char s[37];
    std::snprintf(s, sizeof s, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x", b[0], b[1],
                  b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
    return s;
}

bool isAlreadyOwnedError(const net::Response& r) {
    if (r.body.find("UserAlreadyOwnsContent") != std::string::npos) return true;
    auto j = json::parse(r.body);
    if (!j) return false;
    return (*j)["innererror"]["code"].str() == "UserAlreadyOwnsContent" ||
           (*j)["code"].str() == "UserAlreadyOwnsContent";
}

std::string extractErrorDetail(const net::Response& r) {
    auto j = json::parse(r.body);
    if (!j) return r.error.empty() ? ("HTTP " + std::to_string(r.status)) : r.error;
    std::string innerCode = (*j)["innererror"]["code"].str();
    if ((*j)["innererror"]["details"].isArray() && (*j)["innererror"]["details"].size() > 0 &&
        (*j)["innererror"]["details"][0]["message"].isString()) {
        std::string msg = (*j)["innererror"]["details"][0]["message"].str();
        return innerCode.empty() ? msg : (innerCode + ": " + msg);
    }
    if ((*j)["innererror"]["message"].isString()) {
        std::string msg = (*j)["innererror"]["message"].str();
        return innerCode.empty() ? msg : (innerCode + ": " + msg);
    }
    if (!innerCode.empty()) {
        return innerCode;
    }
    if ((*j)["message"].isString()) {
        return (*j)["message"].str();
    }
    if ((*j)["code"].isString()) {
        return (*j)["code"].str();
    }
    if ((*j)["innererror"]["data"].isArray() && (*j)["innererror"]["data"].size() > 0 &&
        (*j)["innererror"]["data"][0].isString()) {
        return (*j)["innererror"]["data"][0].str();
    }
    return "HTTP " + std::to_string(r.status);
}
}  // namespace

bool claimFreeProduct(const std::string& marketplaceAuthHeader, const PurchaseOffer& offer,
                      const std::string& market, const std::string& language, std::string& err) {
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

    std::string orderId = uuid4();

    // 1. Create order in Microsoft Store
    json::Value item = json::Value::object();
    item.set("productId", offer.productId);
    item.set("skuId", offer.skuId);
    item.set("availabilityId", offer.availabilityId);
    item.set("quantity", int64_t(1));
    item.set("price", 0.0);
    item.set("currencyCode", offer.currency.empty() ? "USD" : offer.currency);

    json::Value items = json::Value::array();
    items.push(item);

    json::Value clientCtx = json::Value::object();
    clientCtx.set("client", "Xbox");

    json::Value createBody = json::Value::object();
    createBody.set("orderId", orderId);
    createBody.set("items", items);
    createBody.set("clientContext", clientCtx);
    createBody.set("language", language.empty() ? "en-US" : language);
    createBody.set("market", market.empty() ? "US" : market);

    net::Request req;
    req.method = "POST";
    req.url = "https://purchase.mp.microsoft.com/v7.0/users/me/orders" +
              (market.empty() ? "" : "?market=" + market);
    req.headers = {
        {"Authorization", marketplaceAuthHeader},
        {"Content-Type", "application/json"},
        {"MS-CV", "PSBoxPurchase.1"}
    };
    req.body = createBody.dump();

    auto r = net::perform(req);
    if (!r.ok()) {
        if (isAlreadyOwnedError(r)) {
            XC_LOGI("claimFreeProduct: account already owns %s (UserAlreadyOwnsContent on create)",
                    offer.productId.c_str());
            return true;
        }
        err = extractErrorDetail(r);
        if (err.find("CatalogAvailabilityDataNotFound") != std::string::npos ||
            err.find("purchaseable action") != std::string::npos ||
            err.find("Availability does not contain") != std::string::npos) {
            markProductUnclaimable(offer.productId);
        }
        XC_LOGW("claimFreeProduct: create order failed for %s: %s (status %d)",
                offer.productId.c_str(), err.c_str(), r.status);
        return false;
    }

    // 2. Finalize order (transition from Editing to Purchased)
    json::Value finalizeBody = json::Value::object();
    finalizeBody.set("orderState", "Purchased");
    finalizeBody.set("clientContext", clientCtx);

    net::Request finReq;
    finReq.method = "PUT";
    finReq.url = "https://purchase.mp.microsoft.com/v7.0/users/me/orders/" + orderId;
    finReq.headers = req.headers;
    finReq.body = finalizeBody.dump();

    auto finRes = net::perform(finReq);
    if (!finRes.ok()) {
        if (isAlreadyOwnedError(finRes)) {
            XC_LOGI("claimFreeProduct: account already owns %s (UserAlreadyOwnsContent on finalize)",
                    offer.productId.c_str());
            return true;
        }
        err = extractErrorDetail(finRes);
        if (err.find("CatalogAvailabilityDataNotFound") != std::string::npos ||
            err.find("purchaseable action") != std::string::npos ||
            err.find("Availability does not contain") != std::string::npos) {
            markProductUnclaimable(offer.productId);
        }
        XC_LOGW("claimFreeProduct: finalize order failed for %s (order %s): %s (status %d)",
                offer.productId.c_str(), orderId.c_str(), err.c_str(), finRes.status);
        return false;
    }

    XC_LOGI("claimFreeProduct succeeded for %s (%s/%s) order %s",
            offer.productId.c_str(), offer.skuId.c_str(), offer.availabilityId.c_str(), orderId.c_str());
    return true;
}

}  // namespace xc::xcloud
