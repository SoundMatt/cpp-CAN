// Copyright (c) 2026 Matt Jones. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

// fusa:test REQ-CLI-001 REQ-CLI-002 REQ-CLI-003 REQ-CLI-004 REQ-CLI-005 REQ-CLI-006

#include <can/can.hpp>
#include <catch2/catch_test_macros.hpp>
#include <json.hpp>

using namespace cli;

// ── version / capabilities / status ──────────────────────────────────────────

TEST_CASE("version_json matches RELAY spec 12.1 schema", "[cli][REQ-CLI-001]") {
    std::string j = cli::version_json();
    CHECK(j.find("\"spec_version\":\"0.2\"") != std::string::npos);
    CHECK(j.find("\"tool\":\"cpp-CAN\"")     != std::string::npos);
    CHECK(j.find("\"language\":\"cpp\"")     != std::string::npos);
    CHECK(j.find("\"runtime\":\"c++17\"")    != std::string::npos);
    CHECK(j.find("\"version\":\"0.1.1\"")    != std::string::npos);
}

TEST_CASE("capabilities_json matches RELAY spec 12.2 schema", "[cli][REQ-CLI-002]") {
    std::string j = cli::capabilities_json();
    CHECK(j.find("\"spec_version\":\"0.2\"") != std::string::npos);
    CHECK(j.find("\"kind\":\"capabilities\"") != std::string::npos);
    CHECK(j.find("\"commands\"")            != std::string::npos);
    CHECK(j.find("\"transports\"")           != std::string::npos);
    CHECK(j.find("\"interfaces\"")           != std::string::npos);
    CHECK(j.find("\"features\"")             != std::string::npos);
    CHECK(j.find("\"adapt\":true")           != std::string::npos);
}

TEST_CASE("status_json matches RELAY spec 12.3 schema", "[cli][REQ-CLI-003]") {
    std::string j = cli::status_json();
    CHECK(j.find("\"healthy\":true")  != std::string::npos);
    CHECK(j.find("\"connected\"")     != std::string::npos);
    CHECK(j.find("\"endpoint\"")      != std::string::npos);
    CHECK(j.find("\"tool\"")          != std::string::npos);
}

// ── parse_frame_json ──────────────────────────────────────────────────────────

TEST_CASE("parse_frame_json: standard frame fields", "[cli][REQ-CLI-004]") {
    auto f = parse_frame_json(
        R"({"id":256,"ext":false,"rtr":false,"fd":false,"brs":false,"data":[1,2,3]})");
    CHECK(f.id   == 256u);
    CHECK(f.ext  == false);
    CHECK(f.rtr  == false);
    CHECK(f.fd   == false);
    CHECK(f.brs  == false);
    CHECK(f.data == std::vector<uint8_t>{1, 2, 3});
}

TEST_CASE("parse_frame_json: extended id and FD flags", "[cli][REQ-CLI-004]") {
    auto f = parse_frame_json(
        R"({"id":536870911,"ext":true,"rtr":false,"fd":true,"brs":true,"data":[]})");
    CHECK(f.id  == 0x1FFFFFFFu);
    CHECK(f.ext == true);
    CHECK(f.fd  == true);
    CHECK(f.brs == true);
    CHECK(f.data.empty());
}

TEST_CASE("parse_frame_json: RTR frame with no data", "[cli][REQ-CLI-004]") {
    auto f = parse_frame_json(
        R"({"id":100,"ext":false,"rtr":true,"fd":false,"brs":false,"data":[]})");
    CHECK(f.rtr == true);
    CHECK(f.data.empty());
}

TEST_CASE("parse_frame_json: full payload array", "[cli][REQ-CLI-004]") {
    auto f = parse_frame_json(
        R"({"id":1,"ext":false,"rtr":false,"fd":false,"brs":false,"data":[0,255,128,64]})");
    REQUIRE(f.data.size() == 4);
    CHECK(f.data[0] == 0);
    CHECK(f.data[1] == 255);
    CHECK(f.data[2] == 128);
    CHECK(f.data[3] == 64);
}

TEST_CASE("parse_frame_json: missing id throws", "[cli][REQ-CLI-006]") {
    CHECK_THROWS_AS(parse_frame_json(R"({"ext":false,"data":[]})"),
                    std::runtime_error);
}

// ── message_to_json ───────────────────────────────────────────────────────────

TEST_CASE("message_to_json: protocol and id fields", "[cli][REQ-CLI-005]") {
    can::Frame f{0x100, false, false, false, false, {0xAB, 0xCD}};
    auto msg      = can::to_message(f);
    msg.version   = {0, 2, 0};
    msg.timestamp = {};
    msg.seq       = 0;

    std::string j = message_to_json(msg);
    CHECK(j.find("\"protocol\":\"CAN\"") != std::string::npos);
    CHECK(j.find("\"id\":\"256\"")       != std::string::npos);
}

TEST_CASE("message_to_json: version fields", "[cli][REQ-CLI-005]") {
    can::Frame f{0x1, false, false, false, false, {}};
    auto msg    = can::to_message(f);
    msg.version = {0, 2, 0};
    msg.timestamp = {}; msg.seq = 0;

    std::string j = message_to_json(msg);
    CHECK(j.find("\"major\":0") != std::string::npos);
    CHECK(j.find("\"minor\":2") != std::string::npos);
    CHECK(j.find("\"patch\":0") != std::string::npos);
}

TEST_CASE("message_to_json: payload bytes", "[cli][REQ-CLI-005]") {
    can::Frame f{0x1, false, false, false, false, {171, 205}};
    auto msg = can::to_message(f);
    msg.version = {0, 2, 0}; msg.timestamp = {}; msg.seq = 0;

    std::string j = message_to_json(msg);
    CHECK(j.find("171") != std::string::npos);
    CHECK(j.find("205") != std::string::npos);
}

TEST_CASE("message_to_json: empty payload", "[cli][REQ-CLI-005]") {
    can::Frame f{0x200, true, false, false, false, {}};
    auto msg = can::to_message(f);
    msg.version = {0, 2, 0}; msg.timestamp = {}; msg.seq = 0;

    CHECK(message_to_json(msg).find("\"payload\":[]") != std::string::npos);
}

TEST_CASE("message_to_json: zeroed timestamp", "[cli][REQ-CLI-005]") {
    can::Frame f{0x1, false, false, false, false, {}};
    auto msg = can::to_message(f);
    msg.version = {0, 2, 0}; msg.timestamp = {}; msg.seq = 0;

    CHECK(message_to_json(msg).find("1970-01-01T00:00:00Z") != std::string::npos);
}

TEST_CASE("message_to_json: meta fields are sorted", "[cli][REQ-CLI-005]") {
    can::Frame f{0x1, true, true, true, true, {}};
    auto msg = can::to_message(f);
    msg.version = {0, 2, 0}; msg.timestamp = {}; msg.seq = 0;

    std::string j = message_to_json(msg);
    auto brs = j.find("can.brs");
    auto ext = j.find("can.ext");
    auto fd  = j.find("can.fd");
    auto rtr = j.find("can.rtr");
    CHECK(brs < ext);
    CHECK(ext < fd);
    CHECK(fd  < rtr);
}
