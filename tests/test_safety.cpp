// Copyright (c) 2026 Matt Jones. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

// fusa:test REQ-SAFETY-001 through REQ-SAFETY-011

#include <can/safety/e2e.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace can::safety;

TEST_CASE("protect and unwrap round-trip", "[safety][REQ-SAFETY-005][REQ-SAFETY-007]") {
    Config cfg{0x0001, 0x0010};
    Protector protector{cfg};
    Receiver  receiver{cfg};

    std::vector<uint8_t> payload = {0x01, 0x02, 0x03, 0x04};
    auto protected_payload = protector.protect(payload);
    CHECK(protected_payload.size() == kHeaderSize + payload.size());

    auto result = receiver.unwrap(protected_payload);
    CHECK(result == payload);
}

TEST_CASE("protect increments sequence counter", "[safety][REQ-SAFETY-006]") {
    Config cfg{0x0001, 0x0010};
    Protector protector{cfg};

    auto p0 = protector.protect({0xAA});
    auto p1 = protector.protect({0xBB});

    // seq is at bytes 4–7, LE
    uint32_t s0 = static_cast<uint32_t>(p0[4])
                | static_cast<uint32_t>(p0[5]) << 8
                | static_cast<uint32_t>(p0[6]) << 16
                | static_cast<uint32_t>(p0[7]) << 24;
    uint32_t s1 = static_cast<uint32_t>(p1[4])
                | static_cast<uint32_t>(p1[5]) << 8
                | static_cast<uint32_t>(p1[6]) << 16
                | static_cast<uint32_t>(p1[7]) << 24;
    CHECK(s1 == s0 + 1);
}

TEST_CASE("unwrap: CRC mismatch throws E2EError", "[safety][REQ-SAFETY-008]") {
    Config cfg{0x0001, 0x0010};
    Protector protector{cfg};
    Receiver  receiver{cfg};

    auto protected_payload = protector.protect({0x01, 0x02});
    // Corrupt the CRC byte
    protected_payload[8] ^= 0xFF;
    REQUIRE_THROWS_AS(receiver.unwrap(protected_payload), E2EError);
    try { receiver.unwrap(protected_payload); }
    catch (const E2EError& e) { CHECK(e.kind() == E2EErrorKind::CRCMismatch); }
}

TEST_CASE("unwrap: header too short throws E2EError", "[safety][REQ-SAFETY-007]") {
    Config cfg{};
    Receiver receiver{cfg};
    REQUIRE_THROWS_AS(receiver.unwrap({0x01, 0x02, 0x03}), E2EError);
    try { receiver.unwrap({1, 2, 3}); }
    catch (const E2EError& e) { CHECK(e.kind() == E2EErrorKind::HeaderTooShort); }
}

TEST_CASE("unwrap: sequence gap throws E2EError", "[safety][REQ-SAFETY-009]") {
    Config cfg{0x0001, 0x0010};
    Protector protector{cfg};
    Receiver  receiver{cfg};

    auto p0 = protector.protect({0xAA});
    auto p1 = protector.protect({0xBB});  // seq=1 — skip
    auto p2 = protector.protect({0xCC});  // seq=2

    receiver.unwrap(p0);  // seq=0 OK (first)
    // Now expect seq=1. Skip to seq=2 → gap
    REQUIRE_THROWS_AS(receiver.unwrap(p2), E2EError);
    try { receiver.unwrap(p2); }
    catch (const E2EError& e) { CHECK(e.kind() == E2EErrorKind::SequenceGap); }
    (void)p1;
}

TEST_CASE("multiple sequential unwraps succeed", "[safety][REQ-SAFETY-010]") {
    Config cfg{0x0001, 0x0010};
    Protector protector{cfg};
    Receiver  receiver{cfg};

    for (int i = 0; i < 10; ++i) {
        std::vector<uint8_t> payload = {static_cast<uint8_t>(i)};
        auto p = protector.protect(payload);
        REQUIRE_NOTHROW(receiver.unwrap(p));
    }
}

TEST_CASE("crc16: known vector", "[safety]") {
    // "123456789" → 0x29B1 for CRC-16/CCITT-FALSE
    const uint8_t data[] = {'1','2','3','4','5','6','7','8','9'};
    CHECK(crc16(data, 9) == 0x29B1);
}
