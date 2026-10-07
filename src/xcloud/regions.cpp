// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "xcloud/regions.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace xc::xcloud {

namespace {

struct Location {
    const char* name;
    double lat, lon;
};

// Approximate datacenter cities of the Azure regions xCloud uses.
constexpr Location kRegions[] = {
    {"AUSTRALIAEAST", -33.87, 151.21},  {"AUSTRALIASOUTHEAST", -37.81, 144.96},
    {"BRAZILSOUTH", -23.55, -46.63},    {"CANADACENTRAL", 43.65, -79.38},
    {"CENTRALINDIA", 18.52, 73.86},     {"CENTRALUS", 41.59, -93.60},
    {"CHILECENTRAL", -33.45, -70.67},   {"EASTASIA", 22.27, 114.19},
    {"EASTUS", 37.37, -79.82},          {"EASTUS2", 36.68, -78.39},
    {"FRANCECENTRAL", 46.30, 2.40},     {"GERMANYWESTCENTRAL", 50.11, 8.68},
    {"ITALYNORTH", 45.46, 9.19},        {"JAPANEAST", 35.68, 139.77},
    {"JAPANWEST", 34.69, 135.50},       {"KOREACENTRAL", 37.57, 126.98},
    {"MEXICOCENTRAL", 20.59, -100.39},  {"NORTHCENTRALUS", 41.88, -87.63},
    {"NORTHEUROPE", 53.35, -6.26},      {"POLANDCENTRAL", 52.23, 21.01},
    {"SOUTHCENTRALUS", 29.42, -98.49},  {"SOUTHEASTASIA", 1.28, 103.83},
    {"SOUTHINDIA", 12.98, 80.16},       {"SPAINCENTRAL", 40.42, -3.70},
    {"SWEDENCENTRAL", 60.67, 17.14},    {"UAENORTH", 25.27, 55.30},
    {"UKSOUTH", 51.51, -0.13},          {"WESTEUROPE", 52.37, 4.90},
    {"WESTUS", 37.78, -122.42},         {"WESTUS2", 47.23, -119.85},
    {"WESTUS3", 33.45, -112.07},
};

const Location* find(const std::string& name) {
    std::string upper = name;
    for (auto& ch : upper) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    for (const auto& r : kRegions)
        if (upper == r.name) return &r;
    return nullptr;
}

}  // namespace

double regionDistanceKm(const std::string& a, const std::string& b) {
    const Location *p = find(a), *q = find(b);
    if (!p || !q) return -1;
    constexpr double kRad = 3.14159265358979 / 180.0;
    double dlat = (q->lat - p->lat) * kRad, dlon = (q->lon - p->lon) * kRad;
    double h = std::sin(dlat / 2) * std::sin(dlat / 2) +
               std::cos(p->lat * kRad) * std::cos(q->lat * kRad) * std::sin(dlon / 2) * std::sin(dlon / 2);
    return 2 * 6371.0 * std::asin(std::min(1.0, std::sqrt(h)));
}

std::vector<std::string> regionsByExpectedRtt(const std::string& from, const std::vector<std::string>& candidates,
                                              const std::map<std::string, int>& measured) {
    auto fromIt = measured.find(from);
    double base = fromIt != measured.end() ? fromIt->second : 30.0;  // the default region: nearby
    std::vector<std::pair<double, std::string>> scored;
    for (const auto& c : candidates) {
        if (c == from) continue;
        auto m = measured.find(c);
        double km = regionDistanceKm(from, c);
        double expected = m != measured.end() ? m->second : km >= 0 ? base + km / 100.0 : 1e6;
        scored.emplace_back(expected, c);
    }
    std::stable_sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<std::string> out;
    for (const auto& s : scored) out.push_back(s.second);
    return out;
}

}  // namespace xc::xcloud
