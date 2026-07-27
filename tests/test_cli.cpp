// Copyright (c) 2026 Matt Jones. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

// fusa:test REQ-CLI-001 REQ-CLI-002 REQ-CLI-003 REQ-CLI-004 REQ-CLI-005 REQ-CLI-006

#include <can/can.hpp>
#include <can/virtual/bus.hpp>
#include <catch2/catch_test_macros.hpp>
#include <json.hpp>
#include <sstream>

using namespace cli;

// ── version / capabilities / status ──────────────────────────────────────────

TEST_CASE("version_json matches RELAY spec 12.1 schema", "[cli][REQ-CLI-001]") {
    std::string j = cli::version_json();
    CHECK(j.find("\"spec_version\":\"1.11\"") != std::string::npos);
    CHECK(j.find("\"tool\":\"cpp-CAN\"")     != std::string::npos);
    CHECK(j.find("\"protocol\":\"CAN\"")     != std::string::npos);
    CHECK(j.find("\"protocol_int\":1")       != std::string::npos);
    CHECK(j.find("\"language\":\"cpp\"")     != std::string::npos);
    CHECK(j.find("\"runtime\":\"c++17\"")    != std::string::npos);
    CHECK(j.find("\"version\":\"" + std::string(cli::kToolVersion) + "\"") != std::string::npos);
}

TEST_CASE("version_text is a human-readable summary, not JSON", "[cli][REQ-CLI-007]") {
    std::string t = cli::version_text();
    CHECK(t.find("cpp-CAN") != std::string::npos);
    CHECK(t.find(std::string(cli::kToolVersion)) != std::string::npos);
    CHECK(t.find("1.11") != std::string::npos);
    CHECK(t.find('{') == std::string::npos);
}

TEST_CASE("capabilities_json matches RELAY spec 12.2 schema", "[cli][REQ-CLI-002]") {
    std::string j = cli::capabilities_json();
    CHECK(j.find("\"spec_version\":\"1.11\"") != std::string::npos);
    CHECK(j.find("\"kind\":\"capabilities\"") != std::string::npos);
    CHECK(j.find("\"protocol\":\"CAN\"")     != std::string::npos);
    CHECK(j.find("\"protocol_int\":1")       != std::string::npos);
    CHECK(j.find("\"commands\"")            != std::string::npos);
    CHECK(j.find("\"transports\"")           != std::string::npos);
    CHECK(j.find("\"interfaces\"")           != std::string::npos);
    CHECK(j.find("\"features\"")             != std::string::npos);
    CHECK(j.find("\"adapt\":true")           != std::string::npos);
}

TEST_CASE("capabilities_json features are limited to spec-defined CAN values (RELAY spec 12.2)", "[cli][REQ-CLI-002]") {
    std::string j = cli::capabilities_json();
    CHECK(j.find("\"features\":[\"fd\",\"isotp\",\"j1939\"]") != std::string::npos);
    CHECK(j.find("\"dbc\"") == std::string::npos);
    CHECK(j.find("\"e2e\"") == std::string::npos);
}

TEST_CASE("status_json matches RELAY spec 12.3 schema", "[cli][REQ-CLI-003]") {
    std::string j = cli::status_json();
    CHECK(j.find("\"healthy\":true")  != std::string::npos);
    CHECK(j.find("\"connected\"")     != std::string::npos);
    CHECK(j.find("\"endpoint\"")      != std::string::npos);
    CHECK(j.find("\"tool\"")          != std::string::npos);
}

TEST_CASE("status_text is a human-readable summary, not JSON", "[cli][REQ-CLI-007]") {
    std::string t = cli::status_text();
    CHECK(t.find("cpp-CAN") != std::string::npos);
    CHECK(t.find('{') == std::string::npos);
}

TEST_CASE("is_valid_output_format accepts only text/json", "[cli][REQ-CLI-007]") {
    CHECK(cli::is_valid_output_format("text"));
    CHECK(cli::is_valid_output_format("json"));
    CHECK_FALSE(cli::is_valid_output_format("xml"));
    CHECK_FALSE(cli::is_valid_output_format(""));
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

TEST_CASE("parse_frame_json: base64 data field", "[cli][REQ-CLI-004]") {
    auto f = parse_frame_json(R"({"id":291,"fd":true,"data":"3q2+7w=="})");
    CHECK(f.id == 291u);
    CHECK(f.fd == true);
    REQUIRE(f.data.size() == 4);
    CHECK(f.data[0] == 0xDE);
    CHECK(f.data[1] == 0xAD);
    CHECK(f.data[2] == 0xBE);
    CHECK(f.data[3] == 0xEF);
}

TEST_CASE("parse_frame_json: id above uint32 max throws", "[cli][REQ-CLI-006]") {
    CHECK_THROWS_AS(parse_frame_json(R"({"id":5000000000,"data":[]})"),
                    std::runtime_error);
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
    CHECK(j.find("\"protocol\":1") != std::string::npos);
    CHECK(j.find("\"id\":\"256\"")  != std::string::npos);
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

TEST_CASE("message_to_json: payload bytes encoded as base64", "[cli][REQ-CLI-005]") {
    can::Frame f{0x1, false, false, false, false, {171, 205}};
    auto msg = can::to_message(f);
    msg.version = {0, 2, 0}; msg.timestamp = {}; msg.seq = 0;

    std::string j = message_to_json(msg);
    CHECK(j.find("\"payload\":\"q80=\"") != std::string::npos);
}

TEST_CASE("message_to_json: empty payload encoded as empty base64 string", "[cli][REQ-CLI-005]") {
    can::Frame f{0x200, true, false, false, false, {}};
    auto msg = can::to_message(f);
    msg.version = {0, 2, 0}; msg.timestamp = {}; msg.seq = 0;

    CHECK(message_to_json(msg).find("\"payload\":\"\"") != std::string::npos);
}

TEST_CASE("message_to_json: zeroed timestamp", "[cli][REQ-CLI-005]") {
    can::Frame f{0x1, false, false, false, false, {}};
    auto msg = can::to_message(f);
    msg.version = {0, 2, 0}; msg.timestamp = {}; msg.seq = 0;

    std::string j = message_to_json(msg);
    CHECK(j.find("1970-01-01T00:00:00Z") != std::string::npos);
    CHECK(j.find("\"seq\"") == std::string::npos);
}

TEST_CASE("message_to_json: seq omitted when zero, present when nonzero", "[cli][REQ-CLI-005]") {
    can::Frame f{0x1, false, false, false, false, {}};
    auto msg = can::to_message(f);
    msg.version = {}; msg.timestamp = {};

    msg.seq = 0;
    CHECK(message_to_json(msg).find("\"seq\"") == std::string::npos);

    msg.seq = 42;
    CHECK(message_to_json(msg).find("\"seq\":42") != std::string::npos);
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

TEST_CASE("parse_frame_json: CAN XL fields", "[cli][REQ-CLI-004]") {
    auto f = parse_frame_json(
        R"({"id":291,"xl":true,"esi":true,"sdt":5,"vcid":2,"af":51966,"sec":true,"data":"3q2+7w=="})");
    CHECK(f.id   == 291u);
    CHECK(f.xl   == true);
    CHECK(f.esi  == true);
    CHECK(f.sdt  == 5);
    CHECK(f.vcid == 2);
    CHECK(f.af   == 51966u);
    CHECK(f.sec  == true);
    REQUIRE(f.data.size() == 4);
    CHECK(f.data[0] == 0xDE);
    CHECK(f.data[3] == 0xEF);
}

TEST_CASE("message_to_json: CAN XL meta fields emitted conditionally", "[cli][REQ-CLI-005]") {
    can::Frame f{};
    f.id = 291; f.xl = true; f.esi = true;
    f.sdt = 5; f.vcid = 2; f.af = 51966; f.sec = true;
    f.data = {0xDE, 0xAD, 0xBE, 0xEF};
    auto msg = can::to_message(f);
    msg.timestamp = {}; msg.seq = 0;
    std::string j = message_to_json(msg);
    CHECK(j.find("\"can.xl\":\"true\"")    != std::string::npos);
    CHECK(j.find("\"can.esi\":\"true\"")   != std::string::npos);
    CHECK(j.find("\"can.sdt\":\"5\"")      != std::string::npos);
    CHECK(j.find("\"can.vcid\":\"2\"")     != std::string::npos);
    CHECK(j.find("\"can.af\":\"51966\"")   != std::string::npos);
    CHECK(j.find("\"can.sec\":\"true\"")   != std::string::npos);
}

TEST_CASE("message_to_json: XL meta absent when zero/false", "[cli][REQ-CLI-005]") {
    can::Frame f{0x1, false, false, false, false, {1}};
    auto msg = can::to_message(f);
    msg.timestamp = {}; msg.seq = 0;
    std::string j = message_to_json(msg);
    CHECK(j.find("can.xl")   == std::string::npos);
    CHECK(j.find("can.esi")  == std::string::npos);
    CHECK(j.find("can.sdt")  == std::string::npos);
    CHECK(j.find("can.vcid") == std::string::npos);
    CHECK(j.find("can.af")   == std::string::npos);
    CHECK(j.find("can.sec")  == std::string::npos);
}

// ── parse_message_json / send_json_stream (§11.2 streaming JSON sink) ────────

TEST_CASE("parse_message_json: round-trips message_to_json output", "[cli][REQ-CLI-008]") {
    can::Frame f{0x100, false, false, false, false, {0xAB, 0xCD}};
    auto out_msg = can::to_message(f);
    out_msg.timestamp = {};
    out_msg.seq = 7;
    std::string j = message_to_json(out_msg);

    auto in_msg = parse_message_json(j);
    CHECK(in_msg.protocol == relay::Protocol::CAN);
    CHECK(in_msg.id       == "256");
    CHECK(in_msg.payload  == std::vector<uint8_t>{0xAB, 0xCD});
    CHECK(in_msg.seq      == 7);
    CHECK(in_msg.meta.at("can.ext") == "false");
}

TEST_CASE("parse_message_json: missing protocol throws", "[cli][REQ-CLI-008]") {
    REQUIRE_THROWS_AS(parse_message_json("{\"id\":\"1\",\"payload\":\"\"}"), std::runtime_error);
}

TEST_CASE("parse_message_json: missing id throws", "[cli][REQ-CLI-008]") {
    REQUIRE_THROWS_AS(parse_message_json("{\"protocol\":1,\"payload\":\"\"}"), std::runtime_error);
}

TEST_CASE("send_json_stream: publishes each NDJSON line to the bus", "[cli][REQ-CLI-008]") {
    auto bus       = can::virt::Bus::create();
    auto [ch, err] = bus->subscribe({});
    REQUIRE_FALSE(err);

    can::Frame f1{0x100, false, false, false, false, {0x01}};
    can::Frame f2{0x200, false, false, false, false, {0x02}};
    auto m1 = can::to_message(f1); m1.timestamp = {};
    auto m2 = can::to_message(f2); m2.timestamp = {};

    std::istringstream in(message_to_json(m1) + "\n" + message_to_json(m2) + "\n");
    std::ostringstream err_out;

    auto result = send_json_stream(in, err_out, bus);
    CHECK(result.exit_code == 0);
    CHECK(result.published == 2);
    CHECK(err_out.str().empty());

    auto got1 = ch->recv();
    REQUIRE(got1.has_value());
    CHECK(got1->id == 0x100);
    auto got2 = ch->recv();
    REQUIRE(got2.has_value());
    CHECK(got2->id == 0x200);

    bus->close();
}

TEST_CASE("send_json_stream: skips blank lines", "[cli][REQ-CLI-008]") {
    auto bus = can::virt::Bus::create();
    can::Frame f{0x42, false, false, false, false, {}};
    auto m = can::to_message(f); m.timestamp = {};

    std::istringstream in("\n" + message_to_json(m) + "\n\n");
    std::ostringstream err_out;
    auto result = send_json_stream(in, err_out, bus);
    CHECK(result.exit_code == 0);
    CHECK(result.published == 1);
    bus->close();
}

TEST_CASE("send_json_stream: invalid JSON line exits 1", "[cli][REQ-CLI-008]") {
    auto bus = can::virt::Bus::create();
    std::istringstream in("not json\n");
    std::ostringstream err_out;
    auto result = send_json_stream(in, err_out, bus);
    CHECK(result.exit_code == 1);
    CHECK(result.published == 0);
    CHECK_FALSE(err_out.str().empty());
    bus->close();
}

TEST_CASE("send_json_stream: non-CAN protocol exits 1", "[cli][REQ-CLI-008]") {
    auto bus = can::virt::Bus::create();
    std::istringstream in("{\"protocol\":2,\"id\":\"x\",\"payload\":\"\"}\n");
    std::ostringstream err_out;
    auto result = send_json_stream(in, err_out, bus);
    CHECK(result.exit_code == 1);
    CHECK(err_out.str().find("ErrUnsupportedProtocol") != std::string::npos);
    bus->close();
}

TEST_CASE("send_json_stream: invalid frame exits 1", "[cli][REQ-CLI-008]") {
    auto bus = can::virt::Bus::create();
    // id far exceeds the 29-bit extended-ID range.
    std::istringstream in("{\"protocol\":1,\"id\":\"999999999999\",\"payload\":\"\"}\n");
    std::ostringstream err_out;
    auto result = send_json_stream(in, err_out, bus);
    CHECK(result.exit_code == 1);
    bus->close();
}
