// Copyright (c) 2026 Matt Jones. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

// fusa:test REQ-DBC-001 REQ-DBC-002 REQ-DBC-003 REQ-DBC-004 REQ-DBC-005 REQ-DBC-006 REQ-DBC-007

#include <can/dbc/parser.hpp>
#include <catch2/catch_test_macros.hpp>
#include <sstream>

using namespace can::dbc;

static const char* kSimpleDBC = R"(
BO_ 256 EngineData: 8 ECU
 SG_ EngineSpeed : 0|16@1+ (0.25,0) [0|16383.75] "rpm" Vector__XXX
 SG_ EngineTemp  : 16|8@1+ (1,-40) [-40|215] "degC" Vector__XXX

BO_ 512 WheelSpeed: 8 BCM
 SG_ WheelFL : 0|8@1+ (1,0) [0|255] "km/h" TCM
)";

static const char* kValDBC = R"(
BO_ 100 StatusMsg: 1 ECU
 SG_ GearState : 0|3@1+ (1,0) [0|7] "" ECU

VAL_ 100 GearState 0 "NEUTRAL" 1 "FIRST" 2 "SECOND" 3 "THIRD" ;
)";

TEST_CASE("parse: message and signal counts", "[dbc][REQ-DBC-001][REQ-DBC-004]") {
    std::istringstream ss(kSimpleDBC);
    auto db = parse(ss);

    REQUIRE(db != nullptr);
    CHECK(db->messages.size() == 2);
    CHECK(db->messages.count(256) == 1);
    CHECK(db->messages.count(512) == 1);
    CHECK(db->messages.at(256).signals.size() == 2);
    CHECK(db->messages.at(512).signals.size() == 1);
}

TEST_CASE("parse: BigEndian signal byte order is supported", "[dbc][REQ-DBC-002]") {
    const char* be_dbc = R"(
BO_ 300 BigTest: 8 ECU
 SG_ Sig1 : 7|8@0+ (1,0) [0|255] "" Vector
)";
    std::istringstream ss(be_dbc);
    auto db = parse(ss);
    const auto& sig = db->messages.at(300).signals.at("Sig1");
    CHECK(sig.byte_order == ByteOrder::BigEndian);
    // Big-endian: start_bit=7, length=8 — byte 0 completely
    std::vector<uint8_t> data = {0x42, 0, 0, 0, 0, 0, 0, 0};
    auto vals = db->decode(300, data);
    CHECK(vals.count("Sig1") == 1);
    CHECK(std::abs(vals.at("Sig1") - 0x42) < 0.001);
}

TEST_CASE("parse: signal fields", "[dbc][REQ-DBC-003]") {
    std::istringstream ss(kSimpleDBC);
    auto db = parse(ss);

    const auto& sig = db->messages.at(256).signals.at("EngineSpeed");
    CHECK(sig.start_bit  == 0);
    CHECK(sig.length     == 16);
    CHECK(sig.byte_order == ByteOrder::LittleEndian);
    CHECK_FALSE(sig.is_signed);
    CHECK(sig.factor == 0.25);
    CHECK(sig.offset == 0.0);
    CHECK(sig.unit   == "rpm");
}

TEST_CASE("decode: EngineSpeed at 4000 rpm", "[dbc][REQ-DBC-005]") {
    std::istringstream ss(kSimpleDBC);
    auto db = parse(ss);

    // 4000 rpm → raw = 4000 / 0.25 = 16000 = 0x3E80
    // little-endian → bytes[0]=0x80, bytes[1]=0x3E
    std::vector<uint8_t> data = {0x80, 0x3E, 0, 0, 0, 0, 0, 0};
    auto values = db->decode(256, data);

    CHECK(values.count("EngineSpeed") == 1);
    CHECK(std::abs(values.at("EngineSpeed") - 4000.0) < 0.001);
}

TEST_CASE("decode: unknown message ID returns empty map", "[dbc][REQ-DBC-007]") {
    std::istringstream ss(kSimpleDBC);
    auto db = parse(ss);
    auto values = db->decode(0xDEAD, {0, 0, 0, 0, 0, 0, 0, 0});
    CHECK(values.empty());
}

TEST_CASE("VAL_ table parse and physical_to_label", "[dbc][REQ-DBC-006]") {
    std::istringstream ss(kValDBC);
    auto db = parse(ss);

    const auto& sig = db->messages.at(100).signals.at("GearState");
    CHECK(sig.values.size() == 4);

    auto [label, ok] = sig.physical_to_label(1.0);
    CHECK(ok);
    CHECK(label == "FIRST");

    auto [label2, ok2] = sig.physical_to_label(99.0);
    CHECK_FALSE(ok2);
}

TEST_CASE("signed signal decoding with offset", "[dbc][REQ-DBC-005]") {
    std::istringstream ss(kSimpleDBC);
    auto db = parse(ss);

    // EngineTemp: factor=1, offset=-40, raw=0x50=80 → phys=80-40=40
    std::vector<uint8_t> data = {0, 0, 0x50, 0, 0, 0, 0, 0};
    auto values = db->decode(256, data);
    CHECK(std::abs(values.at("EngineTemp") - 40.0) < 0.001);
}
