// Copyright (c) 2026 Matt Jones. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

// fusa:test REQ-CAN-001 through REQ-CAN-014

#include <can/can.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace can;

// ── validate_frame ────────────────────────────────────────────────────────────

TEST_CASE("validate_frame: valid standard frame", "[can][REQ-CAN-009]") {
    REQUIRE_NOTHROW(validate_frame(Frame{0x100, false, false, false, false, {1, 2, 3}}));
}

TEST_CASE("validate_frame: valid extended frame", "[can][REQ-CAN-010]") {
    REQUIRE_NOTHROW(validate_frame(Frame{0x1FFFFFFF, true, false, false, false, {0xFF}}));
}

TEST_CASE("validate_frame: valid CAN FD frame", "[can][REQ-CAN-011]") {
    std::vector<uint8_t> data(64, 0xAB);
    REQUIRE_NOTHROW(validate_frame(Frame{0x100, false, false, true, false, data}));
}

TEST_CASE("validate_frame: valid RTR frame", "[can][REQ-CAN-012]") {
    REQUIRE_NOTHROW(validate_frame(Frame{0x200, false, true, false, false, {}}));
}

TEST_CASE("validate_frame: RTR + FD is invalid", "[can][REQ-CAN-013]") {
    REQUIRE_THROWS_AS(validate_frame(Frame{0x200, false, true, true, false, {}}),
                      ErrInvalidFrame);
}

TEST_CASE("validate_frame: standard ID too large", "[can][REQ-CAN-009]") {
    REQUIRE_THROWS_AS(validate_frame(Frame{0x800, false, false, false, false, {}}),
                      ErrInvalidFrame);
}

TEST_CASE("validate_frame: extended ID too large", "[can][REQ-CAN-010]") {
    REQUIRE_THROWS_AS(validate_frame(Frame{0x20000000, true, false, false, false, {}}),
                      ErrInvalidFrame);
}

TEST_CASE("validate_frame: RTR with data is invalid", "[can][REQ-CAN-012]") {
    REQUIRE_THROWS_AS(validate_frame(Frame{0x100, false, true, false, false, {1}}),
                      ErrInvalidFrame);
}

TEST_CASE("validate_frame: standard CAN data too long", "[can][REQ-CAN-011]") {
    std::vector<uint8_t> data(9, 0);
    REQUIRE_THROWS_AS(validate_frame(Frame{0x100, false, false, false, false, data}),
                      ErrInvalidFrame);
}

TEST_CASE("validate_frame: FD data too long", "[can][REQ-CAN-011]") {
    std::vector<uint8_t> data(65, 0);
    REQUIRE_THROWS_AS(validate_frame(Frame{0x100, false, false, true, false, data}),
                      ErrInvalidFrame);
}

TEST_CASE("validate_frame: BRS without FD is invalid", "[can][REQ-CAN-014]") {
    REQUIRE_THROWS_AS(validate_frame(Frame{0x100, false, false, false, true, {1}}),
                      ErrInvalidFrame);
}

// ── Filter::matches ───────────────────────────────────────────────────────────

TEST_CASE("Filter::matches: zero filter passes all", "[can][REQ-CAN-003]") {
    Filter f{};
    CHECK(f.matches(Frame{0x123, false, false, false, false, {}}));
    CHECK(f.matches(Frame{0x000, false, false, false, false, {}}));
    CHECK(f.matches(Frame{0x7FF, false, false, false, false, {}}));
}

TEST_CASE("Filter::matches: exact match", "[can][REQ-CAN-004]") {
    Filter f{0x100, 0x7FF};
    CHECK(f.matches(Frame{0x100, false, false, false, false, {}}));
    CHECK_FALSE(f.matches(Frame{0x101, false, false, false, false, {}}));
}

TEST_CASE("Filter::matches: masked range", "[can][REQ-CAN-004]") {
    Filter f{0x100, 0x700};
    CHECK(f.matches(Frame{0x1FF, false, false, false, false, {}}));
    CHECK_FALSE(f.matches(Frame{0x200, false, false, false, false, {}}));
}

// ── max_data_len ──────────────────────────────────────────────────────────────

TEST_CASE("max_data_len", "[can][REQ-CAN-002]") {
    CHECK(max_data_len(false) == 8);
    CHECK(max_data_len(true)  == 64);
}

// ── Error sentinels ───────────────────────────────────────────────────────────

TEST_CASE("ErrClosed alias matches relay::ErrClosed", "[can][REQ-CAN-008]") {
    CHECK(ErrClosed() == relay::ErrClosed());
}

TEST_CASE("ErrNotConnected alias matches relay::ErrNotConnected", "[can][REQ-CAN-008]") {
    CHECK(ErrNotConnected() == relay::ErrNotConnected());
}

TEST_CASE("ErrTimeout alias matches relay::ErrTimeout", "[can][REQ-CAN-008]") {
    CHECK(ErrTimeout() == relay::ErrTimeout());
}

TEST_CASE("ErrPayloadTooLarge alias matches relay::ErrPayloadTooLarge", "[can][REQ-CAN-008]") {
    CHECK(ErrPayloadTooLarge() == relay::ErrPayloadTooLarge());
}
