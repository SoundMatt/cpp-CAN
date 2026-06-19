// Copyright (c) 2026 Matt Jones. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

// fusa:test REQ-CAN-001 REQ-CAN-001B REQ-CAN-001C REQ-CAN-002 REQ-CAN-003 REQ-CAN-004 REQ-CAN-005 REQ-CAN-006 REQ-CAN-007 REQ-CAN-008 REQ-CAN-009 REQ-CAN-010 REQ-CAN-011 REQ-CAN-012 REQ-CAN-013 REQ-CAN-014 REQ-CAN-015

#include <can/can.hpp>
#include <catch2/catch_test_macros.hpp>
#include <type_traits>

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

TEST_CASE("validate_frame: valid CAN XL frame", "[can][REQ-CAN-009]") {
    Frame xl{};
    xl.id  = 0x123;
    xl.xl  = true;
    xl.sdt = 5;
    xl.data = std::vector<uint8_t>(64, 0xAB);
    REQUIRE_NOTHROW(validate_frame(xl));
}

TEST_CASE("validate_frame: FD and XL mutually exclusive", "[can][REQ-CAN-009]") {
    Frame f{};
    f.fd = true; f.xl = true;
    f.data = {1};
    REQUIRE_THROWS_AS(validate_frame(f), ErrInvalidFrame);
}

TEST_CASE("validate_frame: XL with extended ID is invalid", "[can][REQ-CAN-009]") {
    Frame f{};
    f.xl = true; f.ext = true;
    f.data = {1};
    REQUIRE_THROWS_AS(validate_frame(f), ErrInvalidFrame);
}

TEST_CASE("validate_frame: XL with RTR is invalid", "[can][REQ-CAN-009]") {
    Frame f{};
    f.xl = true; f.rtr = true;
    f.data = {1};
    REQUIRE_THROWS_AS(validate_frame(f), ErrInvalidFrame);
}

TEST_CASE("validate_frame: XL with BRS is invalid", "[can][REQ-CAN-009]") {
    Frame f{};
    f.xl = true; f.brs = true;
    f.data = {1};
    REQUIRE_THROWS_AS(validate_frame(f), ErrInvalidFrame);
}

TEST_CASE("validate_frame: XL priority ID exceeds 11 bits", "[can][REQ-CAN-009]") {
    Frame f{};
    f.id = 0x800; f.xl = true;
    f.data = {1};
    REQUIRE_THROWS_AS(validate_frame(f), ErrInvalidFrame);
}

TEST_CASE("validate_frame: XL empty data is invalid", "[can][REQ-CAN-009]") {
    Frame f{};
    f.xl = true; f.data = {};
    REQUIRE_THROWS_AS(validate_frame(f), ErrInvalidFrame);
}

TEST_CASE("validate_frame: XL data exceeds 2048 bytes", "[can][REQ-CAN-009]") {
    Frame f{};
    f.xl = true; f.data = std::vector<uint8_t>(2049, 0);
    REQUIRE_THROWS_AS(validate_frame(f), ErrInvalidFrame);
}

TEST_CASE("validate_frame: ESI without FD is invalid", "[can][REQ-CAN-014]") {
    Frame f{};
    f.id = 0x100; f.esi = true;
    f.data = {1};
    REQUIRE_THROWS_AS(validate_frame(f), ErrInvalidFrame);
}

// ── Frame XL fields ── REQ-CAN-001C ──────────────────────────────────────────

TEST_CASE("Frame has CAN XL fields", "[can][REQ-CAN-001C]") {
    Frame f{};
    f.xl = true; f.sdt = 5; f.vcid = 2; f.af = 0xCAFE; f.sec = true; f.esi = true;
    CHECK(f.xl   == true);
    CHECK(f.sdt  == 5);
    CHECK(f.vcid == 2);
    CHECK(f.af   == 0xCAFEu);
    CHECK(f.sec  == true);
    CHECK(f.esi  == true);
}

// ── to_message / from_message round-trip ── REQ-CAN-007 REQ-CAN-015 ──────────

TEST_CASE("to_message / from_message: standard frame round-trip", "[can][REQ-CAN-015]") {
    Frame orig{0x123, true, false, false, false, {0xDE, 0xAD}};
    auto msg = to_message(orig);
    CHECK(msg.meta.at("can.ext") == "true");
    CHECK(msg.meta.at("can.fd")  == "false");
    auto f2 = from_message(msg);
    CHECK(f2.id   == 0x123u);
    CHECK(f2.ext  == true);
    CHECK(f2.data == std::vector<uint8_t>{0xDE, 0xAD});
}

TEST_CASE("to_message / from_message: CAN XL frame round-trip", "[can][REQ-CAN-015]") {
    Frame orig{};
    orig.id = 0x123; orig.xl = true; orig.esi = true;
    orig.sdt = 5; orig.vcid = 2; orig.af = 0xCAFE; orig.sec = true;
    orig.data = {0xDE, 0xAD, 0xBE, 0xEF};
    auto msg = to_message(orig);
    CHECK(msg.meta.at("can.xl")   == "true");
    CHECK(msg.meta.at("can.esi")  == "true");
    CHECK(msg.meta.at("can.sdt")  == "5");
    CHECK(msg.meta.at("can.vcid") == "2");
    CHECK(msg.meta.at("can.af")   == "51966");
    CHECK(msg.meta.at("can.sec")  == "true");
    auto f2 = from_message(msg);
    CHECK(f2.xl   == true);
    CHECK(f2.esi  == true);
    CHECK(f2.sdt  == 5);
    CHECK(f2.vcid == 2);
    CHECK(f2.af   == 0xCAFEu);
    CHECK(f2.sec  == true);
    CHECK(f2.data == std::vector<uint8_t>{0xDE, 0xAD, 0xBE, 0xEF});
}

TEST_CASE("to_message: XL fields absent when zero/false", "[can][REQ-CAN-015]") {
    Frame f{0x100, false, false, false, false, {1}};
    auto msg = to_message(f);
    CHECK(msg.meta.find("can.xl")   == msg.meta.end());
    CHECK(msg.meta.find("can.esi")  == msg.meta.end());
    CHECK(msg.meta.find("can.sdt")  == msg.meta.end());
    CHECK(msg.meta.find("can.vcid") == msg.meta.end());
    CHECK(msg.meta.find("can.af")   == msg.meta.end());
    CHECK(msg.meta.find("can.sec")  == msg.meta.end());
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

// ── Frame fields ── REQ-CAN-001 REQ-CAN-001B REQ-CAN-001C ────────────────────

TEST_CASE("Frame has ID and ext flag", "[can][REQ-CAN-001]") {
    Frame f{0x200, true, false, false, false, {}};
    CHECK(f.id  == 0x200u);
    CHECK(f.ext == true);
}

TEST_CASE("Frame has RTR, FD, BRS flags", "[can][REQ-CAN-001B]") {
    Frame f{0x100, false, true, true, true, {}};
    CHECK(f.rtr == true);
    CHECK(f.fd  == true);
    CHECK(f.brs == true);
}

TEST_CASE("Frame has data payload vector", "[can][REQ-CAN-001C]") {
    std::vector<uint8_t> payload = {0xAB, 0xCD};
    Frame f{0x100, false, false, false, false, payload};
    CHECK(f.data == payload);
}

// ── kSpecVersion ─────────────────────────────────────────────────────────────

TEST_CASE("kSpecVersion is 0.2", "[can][REQ-CAN-005]") {
    CHECK(std::string(kSpecVersion) == "0.2");
}

// ── IBus interface ────────────────────────────────────────────────────────────

TEST_CASE("IBus is abstract with virtual send/subscribe/close", "[can][REQ-CAN-006]") {
    CHECK(std::is_abstract<IBus>::value);
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
