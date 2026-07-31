// Copyright (c) 2026 Matt Jones. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

// fusa:test REQ-CAN-007 REQ-CAN-015 REQ-CAN-016

#include <can/can.hpp>
#include <can/virtual/bus.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace can;

TEST_CASE("to_message: fields are encoded correctly", "[relay_adapter][REQ-CAN-007][REQ-CAN-015]") {
    Frame f{0x7FF, false, false, true, true, {0x01, 0x02, 0x03}};
    relay::Message msg = to_message(f);

    CHECK(msg.protocol == relay::Protocol::CAN);
    CHECK(msg.id       == "2047");  // 0x7FF == 2047
    CHECK(msg.payload  == std::vector<uint8_t>{0x01, 0x02, 0x03});
    CHECK(msg.meta.at("can.fd")  == "true");
    CHECK(msg.meta.at("can.brs") == "true");
    CHECK(msg.meta.at("can.ext") == "false");
    CHECK(msg.meta.at("can.rtr") == "false");
    CHECK(msg.timestamp != std::chrono::system_clock::time_point{});
}

TEST_CASE("from_message: round-trip", "[relay_adapter][REQ-CAN-015]") {
    Frame orig{0x1FFFFFFF, true, false, true, true, {0xDE, 0xAD}};
    relay::Message msg = to_message(orig);
    Frame got = from_message(msg);

    CHECK(got.id  == orig.id);
    CHECK(got.ext == orig.ext);
    CHECK(got.fd  == orig.fd);
    CHECK(got.brs == orig.brs);
    CHECK(got.data == orig.data);
}

TEST_CASE("from_message: invalid ID throws ErrInvalidFrame", "[relay_adapter][REQ-CAN-015]") {
    relay::Message msg;
    msg.protocol = relay::Protocol::CAN;
    msg.id       = "not-a-number";
    REQUIRE_THROWS_AS(from_message(msg), ErrInvalidFrame);
}

// ── from_message validates the reconstructed Frame (REQ-CAN-015) ────────────
//
// from_message() previously only range-checked the ID before returning; it
// never called validate_frame(), so a structurally invalid Frame (e.g. a
// standard 11-bit ID with can.ext=false but a value that only fits in 29
// bits, or a non-canonical FD length) could be constructed and handed
// straight to any direct caller that doesn't separately call
// validate_frame() itself.

TEST_CASE("from_message: standard ID that overflows 11 bits throws ErrInvalidFrame",
          "[relay_adapter][REQ-CAN-015]") {
    relay::Message msg;
    msg.protocol = relay::Protocol::CAN;
    msg.id       = "2048";  // 0x800 — exceeds kCANMaxStdID (0x7FF) for ext=false
    REQUIRE_THROWS_AS(from_message(msg), ErrInvalidFrame);
}

TEST_CASE("from_message: non-canonical CAN FD length throws ErrInvalidFrame",
          "[relay_adapter][REQ-CAN-015]") {
    relay::Message msg;
    msg.protocol      = relay::Protocol::CAN;
    msg.id            = "256";
    msg.meta["can.fd"] = "true";
    msg.payload        = std::vector<uint8_t>(9, 0xAA);  // 9 is not a canonical FD DLC length
    REQUIRE_THROWS_AS(from_message(msg), ErrInvalidFrame);
}

TEST_CASE("adapt: send of a structurally invalid frame does not report payload_too_large",
          "[relay_adapter][REQ-CAN-016]") {
    auto bus  = virt::Bus::create();
    auto node = adapt(bus);

    relay::Message msg;
    msg.protocol      = relay::Protocol::CAN;
    msg.id            = "256";
    msg.meta["can.fd"] = "true";
    msg.payload        = std::vector<uint8_t>(9, 0xAA);  // non-canonical FD length

    auto err = node->send(relay::Context::background(), msg);
    REQUIRE(err);
    // Previously every ErrInvalidFrame reason was collapsed into
    // payload_too_large, which is misleading for a non-oversize structural
    // failure; it must now be reported distinctly.
    CHECK(err != relay::ErrPayloadTooLarge());
    node->close();
}

TEST_CASE("adapt: protocol() returns CAN", "[relay_adapter][REQ-CAN-016]") {
    auto bus  = virt::Bus::create();
    auto node = adapt(bus);
    CHECK(node->protocol() == relay::Protocol::CAN);
    node->close();
}

TEST_CASE("adapt: send/subscribe round-trip with seq numbering", "[relay_adapter][REQ-CAN-016]") {
    auto bus  = virt::Bus::create();
    auto node = adapt(bus);

    auto [ch, err] = node->subscribe({});
    REQUIRE_FALSE(err);

    relay::Message out_msg = to_message(Frame{0x100, false, false, false, false, {0xAB, 0xCD}});
    REQUIRE_FALSE(node->send(relay::Context::background(), out_msg));

    auto got = ch->recv();
    REQUIRE(got.has_value());
    CHECK(got->protocol == relay::Protocol::CAN);
    CHECK(got->id       == "256");   // 0x100 == 256
    CHECK(got->seq      == 1);

    node->close();
}

TEST_CASE("adapt: close propagates to bus", "[relay_adapter][REQ-CAN-016]") {
    auto bus  = virt::Bus::create();
    auto node = adapt(bus);
    REQUIRE_FALSE(node->close());
    // Bus should be closed now
    CHECK(bus->send(Frame{0x100, false, false, false, false, {}}) == relay::ErrClosed());
}
