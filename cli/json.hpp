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
#include <istream>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cli {

namespace detail {

inline int b64val(unsigned char c) noexcept {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

inline std::string base64_encode(const std::vector<uint8_t>& data) {
    static const char kAlpha[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((data.size() + 2) / 3) * 4);
    for (std::size_t i = 0; i < data.size(); i += 3) {
        uint32_t b = static_cast<uint32_t>(data[i]) << 16;
        if (i + 1 < data.size()) b |= static_cast<uint32_t>(data[i+1]) << 8;
        if (i + 2 < data.size()) b |= static_cast<uint32_t>(data[i+2]);
        out += kAlpha[(b >> 18) & 0x3F];
        out += kAlpha[(b >> 12) & 0x3F];
        out += (i + 1 < data.size()) ? kAlpha[(b >> 6) & 0x3F] : '=';
        out += (i + 2 < data.size()) ? kAlpha[b & 0x3F]        : '=';
    }
    return out;
}

inline std::vector<uint8_t> base64_decode(const std::string& s) {
    std::vector<uint8_t> out;
    out.reserve(s.size() * 3 / 4);
    uint32_t accum = 0;
    int bits = 0;
    for (unsigned char c : s) {
        if (c == '=') break;
        int v = b64val(c);
        if (v < 0) continue;
        accum = (accum << 6) | static_cast<uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<uint8_t>((accum >> bits) & 0xFF));
        }
    }
    return out;
}

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
    unsigned long long v = std::strtoull(s.c_str() + p, &end, 10);
    if (!end || end == s.c_str() + p) return false;
    if (v > 0xFFFFFFFFULL) return false;
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
    if (p >= s.size()) return false;
    if (s[p] == '"') {
        ++p;
        auto end = s.find('"', p);
        if (end == std::string::npos) return false;
        out = base64_decode(s.substr(p, end - p));
        return true;
    }
    if (s[p] != '[') return false;
    ++p;
    out.clear();
    while (p < s.size()) {
        while (p < s.size() && (s[p]==' ' || s[p]=='\t' || s[p]=='\n' || s[p]=='\r')) ++p;
        if (p >= s.size() || s[p] == ']') break;
        char* end = nullptr;
        unsigned long long b = std::strtoull(s.c_str() + p, &end, 10);
        if (!end || end == s.c_str() + p) break;
        if (b > 255ULL) break;
        out.push_back(static_cast<uint8_t>(b));
        p = static_cast<std::size_t>(end - s.c_str());
        while (p < s.size() && (s[p]==' ' || s[p]=='\t')) ++p;
        if (p < s.size() && s[p] == ',') ++p;
    }
    return true;
}

inline bool extract_string(const std::string& s, const std::string& key, std::string& out) {
    std::string k = "\"" + key + "\":";
    auto p = s.find(k);
    if (p == std::string::npos) return false;
    p += k.size();
    while (p < s.size() && (s[p] == ' ' || s[p] == '\t')) ++p;
    if (p >= s.size() || s[p] != '"') return false;
    ++p;
    auto end = s.find('"', p);
    if (end == std::string::npos) return false;
    out = s.substr(p, end - p);
    return true;
}

inline bool extract_u64(const std::string& s, const std::string& key, uint64_t& out) {
    std::string k = "\"" + key + "\":";
    auto p = s.find(k);
    if (p == std::string::npos) return false;
    p += k.size();
    while (p < s.size() && (s[p] == ' ' || s[p] == '\t')) ++p;
    char* end = nullptr;
    unsigned long long v = std::strtoull(s.c_str() + p, &end, 10);
    if (!end || end == s.c_str() + p) return false;
    out = static_cast<uint64_t>(v);
    return true;
}

// Parses a flat {"key":"value",...} object into a map. Values must be
// strings — sufficient for relay.Message's Meta field (§4.3).
inline bool extract_string_map(const std::string& s, const std::string& key,
                                std::unordered_map<std::string, std::string>& out) {
    std::string k = "\"" + key + "\":";
    auto p = s.find(k);
    if (p == std::string::npos) return false;
    p += k.size();
    while (p < s.size() && (s[p] == ' ' || s[p] == '\t')) ++p;
    if (p >= s.size() || s[p] != '{') return false;
    ++p;
    out.clear();
    while (p < s.size()) {
        while (p < s.size() && (s[p]==' '||s[p]=='\t'||s[p]=='\n'||s[p]=='\r')) ++p;
        if (p >= s.size() || s[p] == '}') break;
        if (s[p] != '"') break;
        ++p;
        auto kend = s.find('"', p);
        if (kend == std::string::npos) break;
        std::string mk = s.substr(p, kend - p);
        p = kend + 1;
        while (p < s.size() && (s[p]==' '||s[p]=='\t')) ++p;
        if (p >= s.size() || s[p] != ':') break;
        ++p;
        while (p < s.size() && (s[p]==' '||s[p]=='\t')) ++p;
        if (p >= s.size() || s[p] != '"') break;
        ++p;
        auto vend = s.find('"', p);
        if (vend == std::string::npos) break;
        out[mk] = s.substr(p, vend - p);
        p = vend + 1;
        while (p < s.size() && (s[p]==' '||s[p]=='\t')) ++p;
        if (p < s.size() && s[p] == ',') ++p;
    }
    return true;
}

} // namespace detail

// Single source of truth for the CLI's self-reported implementation version.
// Bump on every release alongside CMakeLists.txt's project() VERSION and
// CHANGELOG.md (see #18).
inline constexpr std::string_view kToolVersion = "0.2.4";

// fusa:req REQ-CLI-004
inline can::Frame parse_frame_json(const std::string& json) {
    can::Frame f{};
    if (!detail::extract_u32(json, "id", f.id))
        throw std::runtime_error("ErrInvalidInput: missing or invalid 'id'");
    detail::extract_bool(json, "ext",  f.ext);
    detail::extract_bool(json, "rtr",  f.rtr);
    detail::extract_bool(json, "fd",   f.fd);
    detail::extract_bool(json, "brs",  f.brs);
    detail::extract_bytes(json, "data", f.data);
    detail::extract_bool(json, "esi",  f.esi);
    detail::extract_bool(json, "xl",   f.xl);
    detail::extract_bool(json, "sec",  f.sec);
    uint32_t tmp{};
    if (detail::extract_u32(json, "sdt",  tmp)) f.sdt  = static_cast<uint8_t>(tmp);
    if (detail::extract_u32(json, "vcid", tmp)) f.vcid = static_cast<uint8_t>(tmp);
    if (detail::extract_u32(json, "af",   tmp)) f.af   = tmp;
    return f;
}

// fusa:req REQ-CLI-005
inline std::string message_to_json(const relay::Message& m) {
    std::ostringstream o;
    o << "{";
    o << "\"protocol\":" << static_cast<int>(m.protocol) << ",";
    o << "\"version\":{\"major\":" << m.version.major
      << ",\"minor\":"             << m.version.minor
      << ",\"patch\":"             << m.version.patch << "},";
    o << "\"id\":\"" << m.id << "\",";
    o << "\"payload\":\"" << detail::base64_encode(m.payload) << "\",";
    o << "\"timestamp\":\"1970-01-01T00:00:00Z\"";
    if (m.seq != 0) o << ",\"seq\":" << m.seq;
    if (!m.meta.empty()) {
        o << ",\"meta\":{";
        std::vector<std::pair<std::string,std::string>> sorted(m.meta.begin(), m.meta.end());
        std::sort(sorted.begin(), sorted.end());
        for (std::size_t i = 0; i < sorted.size(); ++i) {
            if (i) o << ",";
            o << "\"" << sorted[i].first << "\":\"" << sorted[i].second << "\"";
        }
        o << "}";
    }
    o << "}";
    return o.str();
}

// Parses one relay.Message JSON value (as emitted by message_to_json / the
// NDJSON stream consumed by `send --format json`, §11.2). Throws on a
// missing/invalid 'protocol' or 'id' field.
// fusa:req REQ-CLI-008
inline relay::Message parse_message_json(const std::string& json) {
    relay::Message m{};
    uint32_t proto{};
    if (!detail::extract_u32(json, "protocol", proto))
        throw std::runtime_error("ErrInvalidInput: missing or invalid 'protocol'");
    m.protocol = static_cast<relay::Protocol>(proto);
    if (!detail::extract_string(json, "id", m.id))
        throw std::runtime_error("ErrInvalidInput: missing 'id'");
    detail::extract_bytes(json, "payload", m.payload);
    uint64_t seq{};
    if (detail::extract_u64(json, "seq", seq)) m.seq = seq;
    detail::extract_string_map(json, "meta", m.meta);
    return m;
}

// Reads relay.Message values as NDJSON from `in` (one per line, per §11.2's
// streaming JSON sink) and publishes each converted CAN frame onto `bus`
// until EOF. Extracted from cmd_send() so the pipeline (parse → convert →
// validate → publish) is directly unit-testable against any IBus, including
// can::virt::Bus.
// fusa:req REQ-CLI-008
struct SendStreamResult {
    int         exit_code{0};
    std::size_t published{0};
};

inline SendStreamResult send_json_stream(std::istream& in, std::ostream& err,
                                          const std::shared_ptr<can::IBus>& bus) {
    SendStreamResult result;
    std::string line;
    while (std::getline(in, line)) {
        if (line.find_first_not_of(" \t\r\n") == std::string::npos) continue;

        relay::Message msg;
        try {
            msg = parse_message_json(line);
        } catch (const std::exception& e) {
            err << e.what() << "\n";
            return {1, result.published};
        }
        if (msg.protocol != relay::Protocol::CAN) {
            err << "ErrUnsupportedProtocol\n";
            return {1, result.published};
        }

        can::Frame f;
        try {
            f = can::from_message(msg);
            can::validate_frame(f);
        } catch (const can::ErrInvalidFrame& e) {
            err << "ErrInvalidInput: " << e.what() << "\n";
            return {1, result.published};
        }

        if (auto ec = bus->send(std::move(f))) {
            err << ec.message() << "\n";
            return {1, result.published};
        }
        ++result.published;
    }
    return result;
}

// fusa:req REQ-CLI-001
inline std::string version_json() {
    std::ostringstream o;
    o << "{"
         "\"tool\":\"cpp-CAN\","
         "\"protocol\":\"CAN\","
         "\"protocol_int\":1,"
         "\"version\":\"" << kToolVersion << "\","
         "\"spec_version\":\"" << relay::kRelaySpecVersion << "\","
         "\"language\":\"cpp\","
         "\"runtime\":\"c++17\""
         "}";
    return o.str();
}

// fusa:req REQ-CLI-007
inline std::string version_text() {
    std::ostringstream o;
    o << "cpp-CAN " << kToolVersion
      << " (protocol=CAN spec=" << relay::kRelaySpecVersion
      << " language=cpp runtime=c++17)";
    return o.str();
}

// Recognized CAN feature strings per RELAY spec §12.2's per-protocol table.
// "dbc" and "e2e" were previously advertised here but are not spec-defined
// values for any protocol (see #19) — dropped.
// fusa:req REQ-CLI-002
inline std::string capabilities_json() {
    std::ostringstream o;
    o << "{"
         "\"kind\":\"capabilities\","
         "\"tool\":\"cpp-CAN\","
         "\"protocol\":\"CAN\","
         "\"protocol_int\":1,"
         "\"version\":\"" << kToolVersion << "\","
         "\"spec_version\":\"" << relay::kRelaySpecVersion << "\","
         "\"commands\":[\"version\",\"capabilities\",\"status\",\"convert\",\"send\"],"
         "\"transports\":[\"socketcan\",\"virtual\"],"
         "\"features\":[\"fd\",\"isotp\",\"j1939\"],"
         "\"interfaces\":[\"IBus\",\"INode\"],"
         "\"optional_interfaces\":[\"ILoaningBus\",\"IHealthProvider\",\"IMetricsProvider\",\"IDrainer\"],"
         "\"adapt\":true"
         "}";
    return o.str();
}

// fusa:req REQ-CLI-003
inline std::string status_json() {
    std::ostringstream o;
    o << "{"
         "\"protocol\":\"CAN\","
         "\"tool\":\"cpp-CAN\","
         "\"version\":\"" << kToolVersion << "\","
         "\"healthy\":true,"
         "\"connected\":false,"
         "\"endpoint\":\"\","
         "\"details\":{}"
         "}";
    return o.str();
}

// fusa:req REQ-CLI-007
inline std::string status_text() {
    std::ostringstream o;
    o << "cpp-CAN " << kToolVersion << ": healthy (connected=false)";
    return o.str();
}

// fusa:req REQ-CLI-007
inline bool is_valid_output_format(const std::string& fmt) {
    return fmt == "text" || fmt == "json";
}

} // namespace cli
