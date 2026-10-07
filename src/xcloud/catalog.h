// Game Pass catalog (catalog.gamepass.com), the source xbox.com/play uses for
// its rows: lists ("sigls") of product ids, and product details with names,
// descriptions, art and the xCloud title id needed to start a session.
// Public endpoints: no authentication.
#pragma once

#include <map>
#include <string>
#include <vector>

namespace xc::xcloud {

struct Product {
    std::string productId;      // Store "big id"
    std::string xcloudTitleId;  // empty: not streamable
    std::string title;
    std::string publisher;
    std::string description;
    std::string tileUrl;    // square box art
    std::string posterUrl;  // 2:3
    std::string heroUrl;    // 16:9 background
    std::vector<std::string> categories;
};

// Lists shown on xbox.com/play.
namespace sigl {
constexpr const char* kRecentlyAdded = "06323672-b8c8-43cc-b0de-32d5a9834749";
constexpr const char* kMostPopular = "6a589fa0-d493-472b-8e20-3813699d7056";
constexpr const char* kLeavingSoon = "31ff2361-2772-4622-849b-f4f1abb4ad1b";
constexpr const char* kAllGames = "af206485-e87d-4624-9007-cb7f6d0cc42e";
}  // namespace sigl

struct ProductList {
    std::string title;  // localized row title
    std::vector<std::string> productIds;
};

bool fetchList(const std::string& siglId, const std::string& market, const std::string& language, ProductList& out,
               std::string& err);
// Details for `ids` (batched); products the catalog does not know are absent.
// `full` adds the hero art and description (about 5x the payload: ~10 KB
// per product instead of ~2 KB).
bool fetchProducts(const std::vector<std::string>& ids, const std::string& market, const std::string& language,
                   std::map<std::string, Product>& out, std::string& err, bool full = true);

}  // namespace xc::xcloud
