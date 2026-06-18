// Copyright (c) 2026 Matt Jones. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

// fusa:test REQ-RELAY-001 through REQ-RELAY-028

#include <can/relay.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace relay;

// ── Protocol ──────────────────────────────────────────────────────────────────

TEST_CASE("to_string: all protocols", "[relay][REQ-RELAY-003]") {
    CHECK(to_string(Protocol::CAN)    == "CAN");
    CHECK(to_string(Protocol::DDS)    == "DDS");
    CHECK(to_string(Protocol::LIN)    == "LIN");
    CHECK(to_string(Protocol::MQTT)   == "MQTT");
    CHECK(to_string(Protocol::RCP)    == "RCP");
    CHECK(to_string(Protocol::SOMEIP) == "SOMEIP");
    CHECK(to_string(static_cast<Protocol>(99)) == "unknown");
}

TEST_CASE("try_parse_protocol: all protocols round-trip", "[relay][REQ-RELAY-059]") {
    Protocol p{};
    CHECK(try_parse_protocol("CAN",    p)); CHECK(p == Protocol::CAN);
    CHECK(try_parse_protocol("DDS",    p)); CHECK(p == Protocol::DDS);
    CHECK(try_parse_protocol("LIN",    p)); CHECK(p == Protocol::LIN);
    CHECK(try_parse_protocol("MQTT",   p)); CHECK(p == Protocol::MQTT);
    CHECK(try_parse_protocol("RCP",    p)); CHECK(p == Protocol::RCP);
    CHECK(try_parse_protocol("SOMEIP", p)); CHECK(p == Protocol::SOMEIP);
    CHECK(try_parse_protocol("SOME/IP",p)); CHECK(p == Protocol::SOMEIP);
}

TEST_CASE("try_parse_protocol: case-insensitive", "[relay][REQ-RELAY-059]") {
    Protocol p{};
    CHECK(try_parse_protocol("can", p)); CHECK(p == Protocol::CAN);
    CHECK(try_parse_protocol("Can", p)); CHECK(p == Protocol::CAN);
}

TEST_CASE("try_parse_protocol: unknown returns false", "[relay][REQ-RELAY-059]") {
    Protocol p{};
    CHECK_FALSE(try_parse_protocol("UNKNOWN", p));
    CHECK_FALSE(try_parse_protocol("",        p));
}

TEST_CASE("parse_protocol: throws on unknown", "[relay][REQ-RELAY-059]") {
    REQUIRE_THROWS_AS(parse_protocol("BAD"), std::invalid_argument);
}

// ── Version ───────────────────────────────────────────────────────────────────

TEST_CASE("Version::to_string", "[relay][REQ-RELAY-005]") {
    Version v{1, 2, 3};
    CHECK(v.to_string() == "1.2.3");
    CHECK((Version{0, 0, 0}).to_string() == "0.0.0");
}

TEST_CASE("Version equality", "[relay][REQ-RELAY-004]") {
    Version a{1, 2, 3}, b{1, 2, 3}, c{1, 2, 4};
    CHECK(a == b);
    CHECK_FALSE(a == c);
}

// ── Error codes ───────────────────────────────────────────────────────────────

TEST_CASE("error sentinels are distinct", "[relay][REQ-RELAY-008]") {
    CHECK(ErrClosed()          != ErrNotConnected());
    CHECK(ErrClosed()          != ErrTimeout());
    CHECK(ErrClosed()          != ErrPayloadTooLarge());
    CHECK(ErrNotConnected()    != ErrTimeout());
    CHECK(ErrNotConnected()    != ErrPayloadTooLarge());
    CHECK(ErrTimeout()         != ErrPayloadTooLarge());
}

TEST_CASE("error codes compare equal to themselves", "[relay][REQ-RELAY-008]") {
    CHECK(ErrClosed()          == ErrClosed());
    CHECK(ErrNotConnected()    == ErrNotConnected());
    CHECK(ErrTimeout()         == ErrTimeout());
    CHECK(ErrPayloadTooLarge() == ErrPayloadTooLarge());
}

TEST_CASE("error code messages are non-empty", "[relay][REQ-RELAY-008]") {
    CHECK_FALSE(ErrClosed().message().empty());
    CHECK_FALSE(ErrNotConnected().message().empty());
    CHECK_FALSE(ErrTimeout().message().empty());
    CHECK_FALSE(ErrPayloadTooLarge().message().empty());
}

TEST_CASE("ok error_code is false", "[relay]") {
    std::error_code ok{};
    CHECK_FALSE(ok);
    CHECK(ErrClosed());
}

// ── SubscriberConfig / options ────────────────────────────────────────────────

TEST_CASE("SubscriberConfig defaults", "[relay][REQ-RELAY-016]") {
    SubscriberConfig cfg{};
    CHECK(cfg.chan_depth  == 0);
    CHECK(cfg.back_pressure == BackPressurePolicy::DropNewest);
    CHECK(cfg.event_id   == 0);
    CHECK(cfg.topic_name.empty());
}

TEST_CASE("with_channel_depth option", "[relay][REQ-RELAY-017]") {
    auto cfg = apply_options({with_channel_depth(128)});
    CHECK(cfg.chan_depth == 128);
}

TEST_CASE("effective_depth uses default when chan_depth is 0", "[relay][REQ-RELAY-019]") {
    SubscriberConfig cfg{};
    CHECK(cfg.effective_depth(64) == 64);
    cfg.chan_depth = 32;
    CHECK(cfg.effective_depth(64) == 32);
}

TEST_CASE("with_back_pressure option", "[relay][REQ-RELAY-017]") {
    auto cfg = apply_options({with_back_pressure(BackPressurePolicy::Block)});
    CHECK(cfg.back_pressure == BackPressurePolicy::Block);
}

TEST_CASE("with_event_id option", "[relay][REQ-RELAY-051]") {
    auto cfg = apply_options({with_event_id(42)});
    CHECK(cfg.event_id == 42);
}

TEST_CASE("with_topic option", "[relay][REQ-RELAY-056]") {
    auto cfg = apply_options({with_topic("my.topic")});
    CHECK(cfg.topic_name == "my.topic");
}

// ── kSpecVersion ─────────────────────────────────────────────────────────────

TEST_CASE("kSpecVersion is non-empty", "[relay][REQ-RELAY-020]") {
    CHECK(std::string(kSpecVersion) == "0.2");
}
