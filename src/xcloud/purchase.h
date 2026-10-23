// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// Microsoft Store Free Game Acquisition / Entitlement Service
#pragma once

#include <string>

namespace xc::xcloud {

struct PurchaseOffer {
    std::string productId;
    std::string skuId;
    std::string availabilityId;
    double listPrice = 0.0;
    double msrp = 0.0;
    std::string currency;
    bool isFree = false;
};

// Queries displaycatalog without fieldsTemplate=browse to retrieve full SkuId and AvailabilityId.
bool fetchPurchaseOffer(const std::string& productId, const std::string& market, const std::string& language,
                        PurchaseOffer& out, std::string& err);

// Claims a 100% free product (price == 0.00) via https://purchase.mp.microsoft.com/v7.0/users/me/orders.
// Rejects immediately if offer.isFree is false or listPrice > 0.005.
bool claimFreeProduct(const std::string& marketplaceAuthHeader, const PurchaseOffer& offer,
                      const std::string& market, const std::string& language, std::string& err);

// Tracks products known to fail claim (bundles, token/code-only, or no valid purchase action)
void markProductUnclaimable(const std::string& productId);
bool isProductUnclaimable(const std::string& productId);
void loadUnclaimableProducts(const std::string& path);
void saveUnclaimableProducts(const std::string& path);

}  // namespace xc::xcloud
