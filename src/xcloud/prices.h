// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// Store prices (displaycatalog): the purchase offer of a product in the
// account's market. About 14 KB per product, so asked for the games on
// screen only.
#pragma once

#include <map>
#include <string>
#include <vector>

namespace xc::xcloud {

struct Price {
    double list = 0;   // what it costs now
    double msrp = 0;   // the regular price (higher while on sale)
    std::string currency;  // ISO code, "BRL"
};

// Fills `out` for the products that have a purchase offer; batched by 20.
bool fetchPrices(const std::vector<std::string>& productIds, const std::string& market, const std::string& language,
                 std::map<std::string, Price>& out, std::string& err);

// "R$ 78,82", "$59.99", "59,99 €"...
std::string formatPrice(double amount, const std::string& currency);

}  // namespace xc::xcloud
