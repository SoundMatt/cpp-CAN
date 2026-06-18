// Copyright (c) 2026 Matt Jones. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include <can/dbc/parser.hpp>
#include <algorithm>
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <string>

// fusa:req REQ-DBC-001 through REQ-DBC-007

namespace can::dbc {

// ── Signal::decode ────────────────────────────────────────────────────────────

static uint64_t extract_raw(const std::vector<uint8_t>& data,
                             int start_bit, int length, ByteOrder order)
{
    uint64_t raw = 0;
    if (order == ByteOrder::LittleEndian) {
        for (int i = 0; i < length; ++i) {
            int bit     = start_bit + i;
            int byte_i  = bit / 8;
            int bit_i   = bit % 8;
            if (byte_i >= static_cast<int>(data.size())) break;
            if (data[byte_i] & (1 << bit_i)) raw |= static_cast<uint64_t>(1) << i;
        }
    } else {
        int byte_i = start_bit / 8;
        int bit_i  = start_bit % 8;
        for (int i = 0; i < length; ++i) {
            if (byte_i < static_cast<int>(data.size()) && data[byte_i] & (1 << bit_i))
                raw |= static_cast<uint64_t>(1) << (length - 1 - i);
            if (bit_i == 0) { bit_i = 7; ++byte_i; }
            else              --bit_i;
        }
    }
    return raw;
}

double Signal::decode(const std::vector<uint8_t>& data) const {
    uint64_t raw = extract_raw(data, start_bit, length, byte_order);
    if (is_signed) {
        int64_t signed_val = static_cast<int64_t>(raw);
        if (raw & (static_cast<uint64_t>(1) << (length - 1)))
            signed_val = static_cast<int64_t>(raw) - (static_cast<int64_t>(1) << length);
        return static_cast<double>(signed_val) * factor + offset;
    }
    return static_cast<double>(raw) * factor + offset;
}

std::pair<std::string, bool> Signal::physical_to_label(double physical) const {
    if (values.empty()) return {"", false};
    int64_t raw = static_cast<int64_t>(std::round((physical - offset) / factor));
    auto it = values.find(raw);
    if (it == values.end()) return {"", false};
    return {it->second, true};
}

// ── DB ────────────────────────────────────────────────────────────────────────

const Message* DB::message(uint32_t id) const {
    auto it = messages.find(id);
    return it != messages.end() ? &it->second : nullptr;
}

std::unordered_map<std::string, double>
DB::decode(uint32_t id, const std::vector<uint8_t>& data) const {
    auto* msg = message(id);
    if (!msg) return {};
    std::unordered_map<std::string, double> result;
    for (const auto& [name, sig] : msg->signals)
        result[name] = sig.decode(data);
    return result;
}

// ── Parser helpers ────────────────────────────────────────────────────────────

static std::string trim(const std::string& s) {
    auto l = s.find_first_not_of(" \t\r\n");
    if (l == std::string::npos) return "";
    auto r = s.find_last_not_of(" \t\r\n");
    return s.substr(l, r - l + 1);
}

static Message parse_message_line(const std::string& line) {
    // BO_ <id> <name>: <dlc> <sender>
    std::istringstream ss(line.substr(4));
    Message msg;
    std::string name_colon, sender;
    ss >> msg.id >> name_colon >> msg.dlc >> msg.sender;
    if (name_colon.empty() || name_colon.back() != ':')
        throw std::runtime_error("dbc: malformed BO_ line: " + line);
    msg.name = name_colon.substr(0, name_colon.size() - 1);
    return msg;
}

static Signal parse_signal_line(const std::string& line) {
    // SG_ <name> : <start>|<len>@<order><sign> (<factor>,<offset>) [<min>|<max>] "<unit>" <rx>
    std::string rest = trim(line.substr(4));
    auto colon_pos = rest.find(" : ");
    if (colon_pos == std::string::npos)
        throw std::runtime_error("dbc: malformed SG_ line: " + line);

    Signal sig;
    sig.name = trim(rest.substr(0, colon_pos));
    rest = trim(rest.substr(colon_pos + 3));

    std::istringstream ss(rest);
    std::string bit_def, factor_offset, min_max, unit_q;
    ss >> bit_def >> factor_offset >> min_max >> unit_q;

    auto pipe = bit_def.find('|');
    auto at   = bit_def.find('@');
    if (pipe == std::string::npos || at == std::string::npos)
        throw std::runtime_error("dbc: malformed bit def: " + bit_def);

    sig.start_bit = std::stoi(bit_def.substr(0, pipe));
    sig.length    = std::stoi(bit_def.substr(pipe + 1, at - pipe - 1));
    char order_ch = bit_def[at + 1];
    char sign_ch  = bit_def.size() > at + 2 ? bit_def[at + 2] : '+';
    sig.byte_order = (order_ch == '0') ? ByteOrder::BigEndian : ByteOrder::LittleEndian;
    sig.is_signed  = (sign_ch == '-');

    // (<factor>,<offset>)
    std::string fo = factor_offset;
    fo.erase(std::remove(fo.begin(), fo.end(), '('), fo.end());
    fo.erase(std::remove(fo.begin(), fo.end(), ')'), fo.end());
    auto comma = fo.find(',');
    if (comma != std::string::npos) {
        sig.factor = std::stod(fo.substr(0, comma));
        sig.offset = std::stod(fo.substr(comma + 1));
    }

    // [min|max]
    std::string mm = min_max;
    mm.erase(std::remove(mm.begin(), mm.end(), '['), mm.end());
    mm.erase(std::remove(mm.begin(), mm.end(), ']'), mm.end());
    auto bar = mm.find('|');
    if (bar != std::string::npos) {
        sig.min = std::stod(mm.substr(0, bar));
        sig.max = std::stod(mm.substr(bar + 1));
    }

    // unit
    sig.unit = unit_q;
    if (!sig.unit.empty() && sig.unit.front() == '"') sig.unit = sig.unit.substr(1);
    if (!sig.unit.empty() && sig.unit.back()  == '"') sig.unit.pop_back();

    // receivers (remainder of line)
    std::string rx;
    while (ss >> rx) {
        std::istringstream rxs(rx);
        std::string r;
        while (std::getline(rxs, r, ','))
            if (!r.empty()) sig.receivers.push_back(r);
    }

    return sig;
}

// Tokenises a VAL_ line preserving quoted strings.
static std::vector<std::string> tokenise_val(const std::string& line) {
    std::vector<std::string> tokens;
    std::size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
        if (i >= line.size()) break;
        if (line[i] == '"') {
            ++i;
            std::size_t j = i;
            while (j < line.size() && line[j] != '"') ++j;
            tokens.push_back(line.substr(i, j - i));
            i = (j < line.size()) ? j + 1 : j;
        } else {
            std::size_t j = i;
            while (j < line.size() && line[j] != ' ' && line[j] != '\t') ++j;
            tokens.push_back(line.substr(i, j - i));
            i = j;
        }
    }
    return tokens;
}

static void parse_val_line(DB& db, const std::string& line) {
    std::string stripped = line;
    if (!stripped.empty() && stripped.back() == ';') stripped.pop_back();
    stripped = trim(stripped);

    auto tokens = tokenise_val(stripped);
    if (tokens.size() < 3) return;  // too short, skip silently

    uint32_t msg_id = static_cast<uint32_t>(std::stoull(tokens[1]));
    auto it = db.messages.find(msg_id);
    if (it == db.messages.end()) return;  // unknown message

    const std::string& sig_name = tokens[2];
    auto& signals = it->second.signals;
    auto sit = signals.find(sig_name);
    if (sit == signals.end()) return;  // unknown signal

    auto& sig = sit->second;
    for (std::size_t i = 3; i + 1 < tokens.size(); i += 2) {
        int64_t raw = std::stoll(tokens[i]);
        sig.values[raw] = tokens[i + 1];
    }
}

// ── parse() ───────────────────────────────────────────────────────────────────

std::unique_ptr<DB> parse(std::istream& r) {
    auto db = std::make_unique<DB>();
    Message* cur_msg = nullptr;
    std::string line;

    while (std::getline(r, line)) {
        line = trim(line);
        if (line.empty() || line.substr(0, 2) == "//") continue;

        if (line.substr(0, 4) == "BO_ ") {
            auto msg = parse_message_line(line);
            uint32_t id = msg.id;
            db->messages[id] = std::move(msg);
            cur_msg = &db->messages[id];
        } else if (line.substr(0, 4) == "SG_ " && cur_msg) {
            auto sig = parse_signal_line(line);
            std::string name = sig.name;
            cur_msg->signals[name] = std::move(sig);
        } else if (line.substr(0, 5) == "VAL_ ") {
            cur_msg = nullptr;
            parse_val_line(*db, line);
        } else if (line.substr(0, 4) != "SG_ ") {
            cur_msg = nullptr;
        }
    }
    return db;
}

} // namespace can::dbc
