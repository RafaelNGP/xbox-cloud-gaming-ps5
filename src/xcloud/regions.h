// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// Which xCloud region to fall back to. Latency to a region can't be measured
// before a session runs there: the regions' API hosts share one gateway per
// continent (brs/eus/wus3 all resolve to the same address) and the login's
// networkTestHostname entries no longer resolve. So: round trips measured in
// past sessions where known, otherwise an estimate from the distance between
// the regions (light in fibre: about 1 ms of round trip per 100 km).
#pragma once

#include <map>
#include <string>
#include <vector>

namespace xc::xcloud {

// Great-circle distance between two Azure regions, km; -1 if either is unknown.
double regionDistanceKm(const std::string& a, const std::string& b);

// `candidates` other than `from`, by expected round trip, lowest first.
// `measured`: ms per region from past sessions.
std::vector<std::string> regionsByExpectedRtt(const std::string& from, const std::vector<std::string>& candidates,
                                              const std::map<std::string, int>& measured);

}  // namespace xc::xcloud
