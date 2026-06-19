// Copyright (c) 2026 Matt Jones. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

// cli/json.hpp — minimal JSON parse/serialize for RELAY convert command.
// Handles exactly the can.Frame input schema and relay.Message output schema.

#pragma once

#include <can/can.hpp>
#include <can/relay.hpp>
#include <algorithm>
#include <cstdlib>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace cli {

namespace detail {

inline bool extract_bool(const std::string& s, const std::string& key, bool& out) {
    std::string k = "\"" + key + "\":";
    auto p = s.find(k);
    if (p == std::string::npos) return false;
    p += k.size();
    while (p < s.size() && (s[p] == ' ' || s[p] == '\t')) ++p;
    if (s.size() - p >= 4 && s.substr(p, 4) == "true")  { out = true;  return true; }
    if (s.size() - p >= 5 && s.substr(p, 5) == "false") { out = false; return true; }
    return false;
}

inline bool extract_u32(const std::string& s, const std::string& key, uint32_t& out) {
    std::string k = "\"" + key + "\":";
    auto p = s.find(k);
    if (p == std::string::npos) return false;
    p += k.size();
    while (p < s.size() && (s[p] == ' ' || s[p] == '\t')) ++p;
    char* end = nullptr;
    unsigned long v = std::strtoul(s.c_str() + p, &end, 10);
    if (!end || end == s.c_str() + p) return false;
    out = static_cast<uint32_t>(v);
    return true;
}

inline bool extract_bytes(const std::string& s, const std::string& key,
                           std::vector<uint8_t>& out) {
    std::string k = "\"" + key + "\":";
    auto p = s.find(k);
    if (p == std::string::npos) return false;
    p += k.size();
    while (p < s.size() && (s[p] == ' ' || s[p] == '\t')) ++p;
    if (p >= s.size() || s[p] != '[') return false;
    ++p;
    out.clear();
    while (p < s.size()) {
        while (p < s.size() && (s[p]==' ' || s[p]=='\t' || s[p]=='\n' || s[p]=='\r')) ++p;
        if (p >= s.size() || s[p] == ']') break;
        char* end = nullptr;
        unsigned long b = std::strtoul(s.c_str() + p, &end, 10);
        if (!end || end == s.c_str() + p) break;
        out.push_back(static_cast<uint8_t>(b));
        p = static_cast<std::size_t>(end - s.c_str());
        while (p < s.size() && (s[p]==' ' || s[p]=='\t')) ++p;
        if (p < s.size() && s[p] == ',') ++p;
    }
    return true;
}

} // namespace detail

// fusa:req REQ-CLI-004
inline can::Frame parse_frame_json(const std::string& json) {
    can::Frame f{};
    if (!detail::extract_u32(json, "id", f.id))
        throw std::runtime_error("ErrInvalidInput: missing or invalid 'id'");
    detail::extract_bool(json, "ext", f.ext);
    detail::extract_bool(json, "rtr", f.rtr);
    detail::extract_bool(json, "fd",  f.fd);
    detail::extract_bool(json, "brs", f.brs);
    detail::extract_bytes(json, "data", f.data);
    return f;
}

// fusa:req REQ-CLI-005
inline std::string message_to_json(const relay::Message& m) {
    std::ostringstream o;
    o << "{";
    o << "\"protocol\":\"" << relay::to_string(m.protocol) << "\",";
    o << "\"version\":{\"major\":" << m.version.major
      << ",\"minor\":"             << m.version.minor
      << ",\"patch\":"             << m.version.patch << "},";
    o << "\"id\":\"" << m.id << "\",";
    o << "\"payload\":[";
    for (std::size_t i = 0; i < m.payload.size(); ++i) {
        if (i) o << ",";
        o << static_cast<int>(m.payload[i]);
    }
    o << "],";
    o << "\"timestamp\":\"1970-01-01T00:00:00Z\",";
    o << "\"seq\":" << m.seq << ",";
    o << "\"meta\":{";
    std::vector<std::pair<std::string,std::string>> sorted(m.meta.begin(), m.meta.end());
    std::sort(sorted.begin(), sorted.end());
    for (std::size_t i = 0; i < sorted.size(); ++i) {
        if (i) o << ",";
        o << "\"" << sorted[i].first << "\":\"" << sorted[i].second << "\"";
    }
    o << "}";
    o << "}";
    return o.str();
}

// fusa:req REQ-CLI-001
inline std::string version_json() {
    return "{"
           "\"spec_version\":\"0.2\","
           "\"tool\":\"cpp-CAN\","
           "\"version\":\"0.1.1\","
           "\"language\":\"cpp\","
           "\"runtime\":\"c++17\""
           "}";
}

// fusa:req REQ-CLI-002
inline std::string capabilities_json() {
    return "{"
           "\"spec_version\":\"0.2\","
           "\"tool\":\"cpp-CAN\","
           "\"version\":\"0.1.1\","
           "\"kind\":\"library\","
           "\"transports\":[\"CAN\"],"
           "\"interfaces\":[\"IBus\",\"INode\",\"ICaller\"],"
           "\"optional_interfaces\":[\"ILoaningBus\",\"IHealthProvider\",\"IMetricsProvider\",\"IDrainer\"],"
           "\"features\":[\"convert\",\"validate\",\"isotp\",\"j1939\",\"dbc\",\"e2e\"],"
           "\"adapt\":true"
           "}";
}

// fusa:req REQ-CLI-003
inline std::string status_json() {
    return "{"
           "\"tool\":\"cpp-CAN\","
           "\"version\":\"0.1.1\","
           "\"healthy\":true,"
           "\"connected\":false,"
           "\"endpoint\":\"\","
           "\"details\":\"\""
           "}";
}

} // namespace cli
