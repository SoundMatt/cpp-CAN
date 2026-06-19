// Copyright (c) 2026 Matt Jones. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

// dbc/parser.hpp — DBC file parser and signal decoder.
// A DBC file describes the messages and signals on a CAN network.
//
// fusa:req REQ-DBC-001 through REQ-DBC-007

#pragma once

#include <cstdint>
#include <istream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace can::dbc {

// fusa:req REQ-DBC-002
enum class ByteOrder {
    LittleEndian,  // Intel format — LSB at start_bit
    BigEndian,     // Motorola format — MSB at start_bit
};

// fusa:req REQ-DBC-003
struct Signal {
    std::string name;
    int         start_bit{};
    int         length{};
    ByteOrder   byte_order{ByteOrder::LittleEndian};
    bool        is_signed{};
    double      factor{1.0};
    double      offset{};
    double      min{};
    double      max{};
    std::string unit;
    std::vector<std::string> receivers;
    std::map<int64_t, std::string> values;  // VAL_ table

    // Decodes the physical value from a CAN frame payload.
    // fusa:req REQ-DBC-005
    double decode(const std::vector<uint8_t>& data) const;

    // Returns the value-table label for a physical value, or ("", false).
    // fusa:req REQ-DBC-006
    std::pair<std::string, bool> physical_to_label(double physical) const;
};

// fusa:req REQ-DBC-004
struct Message {
    uint32_t    id{};
    std::string name;
    int         dlc{};
    std::string sender;
    std::unordered_map<std::string, Signal> signals;
};

// Parsed DBC database.
// fusa:req REQ-DBC-001
class DB {
public:
    // Decodes all signal values from data for the given message ID.
    // Returns empty map if message ID is not in the database.
    // fusa:req REQ-DBC-007
    std::unordered_map<std::string, double>
        decode(uint32_t id, const std::vector<uint8_t>& data) const;

    const Message* message(uint32_t id) const;

    std::unordered_map<uint32_t, Message> messages;
};

// Parses a DBC file from the given stream.
// Throws std::runtime_error on parse failure.
// fusa:req REQ-DBC-001 REQ-DBC-002 REQ-DBC-003 REQ-DBC-004
std::unique_ptr<DB> parse(std::istream& r);

} // namespace can::dbc
