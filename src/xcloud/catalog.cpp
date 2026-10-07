#include "xcloud/catalog.h"

#include "net/http.h"
#include "util/json.h"

namespace xc::xcloud {

namespace {

std::string imageUrl(const json::Value& v) {
    std::string url = v["URL"].str();
    if (url.rfind("//", 0) == 0) url = "https:" + url;
    return url;
}

std::string describe(const net::Response& r) {
    return r.status == 0 ? r.error : "HTTP " + std::to_string(r.status);
}

}  // namespace

bool fetchList(const std::string& siglId, const std::string& market, const std::string& language, ProductList& out,
               std::string& err) {
    net::Request req;
    req.url = "https://catalog.gamepass.com/sigls/v2?id=" + siglId + "&market=" + market + "&language=" + language;
    auto r = net::perform(req);
    auto j = json::parse(r.body);
    if (!r.ok() || !j || !j->isArray()) {
        err = "catalog list " + siglId + ": " + describe(r);
        return false;
    }
    out = {};
    // Element 0 describes the list; the rest are {"id": "<product id>"}.
    for (size_t i = 0; i < j->size(); ++i) {
        const auto& item = (*j)[i];
        if (item.has("siglId")) {
            out.title = item["title"].str();
        } else if (auto id = item["id"].str(); !id.empty()) {
            out.productIds.push_back(id);
        }
    }
    return true;
}

bool fetchProducts(const std::vector<std::string>& ids, const std::string& market, const std::string& language,
                   std::map<std::string, Product>& out, std::string& err, bool full) {
    const size_t kBatch = full ? 20 : 60;
    for (size_t i = 0; i < ids.size(); i += kBatch) {
        json::Value list = json::Value::array();
        for (size_t k = i; k < ids.size() && k < i + kBatch; ++k) list.push(ids[k]);
        json::Value body = json::Value::object();
        body.set("Products", list);

        net::Request req;
        req.method = "POST";
        req.url = "https://catalog.gamepass.com/v3/products?market=" + market + "&language=" + language +
                  "&hydration=" + (full ? "RemoteHighSapphire0" : "RemoteLowJade0");
        req.headers = {{"Content-Type", "application/json"},
                       {"ms-cv", "0.0"},
                       {"calling-app-name", "xcloud-ps5"},
                       {"calling-app-version", "0.1"}};
        req.body = body.dump();
        auto r = net::perform(req);
        auto j = json::parse(r.body);
        if (!r.ok() || !j) {
            err = "catalog products: " + describe(r);
            return false;
        }
        for (const auto& [id, p] : (*j)["Products"].members()) {
            Product prod;
            prod.productId = id;
            prod.xcloudTitleId = p["XCloudTitleId"].str();
            prod.title = p["ProductTitle"].str();
            prod.publisher = p["PublisherName"].str();
            prod.description = p["ProductDescriptionShort"].str();
            if (prod.description.empty()) prod.description = p["ProductDescription"].str();
            prod.tileUrl = imageUrl(p["Image_Tile"]);
            prod.posterUrl = imageUrl(p["Image_Poster"]);
            prod.heroUrl = imageUrl(p["Image_Hero"]);
            if (prod.heroUrl.empty()) prod.heroUrl = imageUrl(p["Image_TitledHero"]);
            for (const auto& c : p["LocalizedCategories"].items()) prod.categories.push_back(c.str());
            out[id] = std::move(prod);
        }
    }
    return true;
}

}  // namespace xc::xcloud
