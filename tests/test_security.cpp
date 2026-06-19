// Copyright (c) 2026 Matt Jones. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

// fusa:test REQ-SEC-001 REQ-SEC-002 REQ-SEC-003 REQ-SEC-004 REQ-SEC-005
// fusa:test REQ-SEC-006 REQ-SEC-007 REQ-SEC-008 REQ-SEC-009 REQ-SEC-010
// fusa:test REQ-SEC-011 REQ-SEC-012 REQ-SEC-013 REQ-SEC-014 REQ-SEC-015

#include <can/can.hpp>
#include <can/channel.hpp>
#include <can/dbc/parser.hpp>
#include <can/isotp/transport.hpp>
#include <sstream>
#include <can/relay.hpp>
#include <can/safety/e2e.hpp>
#include <can/virtual/bus.hpp>
#include <catch2/catch_test_macros.hpp>
#include <thread>

using namespace can;

// ── REQ-SEC-001: standard ID overflow rejected ────────────────────────────────

TEST_CASE("SEC: standard ID > 0x7FF is rejected", "[security][REQ-SEC-001]") {
    REQUIRE_THROWS_AS(validate_frame(Frame{0x800, false, false, false, false, {}}),
                      ErrInvalidFrame);
}

TEST_CASE("SEC: standard ID == 0x7FF is accepted", "[security][REQ-SEC-001]") {
    REQUIRE_NOTHROW(validate_frame(Frame{0x7FF, false, false, false, false, {}}));
}

// ── REQ-SEC-002: payload length overflow rejected ─────────────────────────────

TEST_CASE("SEC: standard CAN data > 8 bytes is rejected", "[security][REQ-SEC-002]") {
    Frame f{0x100, false, false, false, false, std::vector<uint8_t>(9, 0xAA)};
    REQUIRE_THROWS_AS(validate_frame(f), ErrInvalidFrame);
}

TEST_CASE("SEC: CAN FD data > 64 bytes is rejected", "[security][REQ-SEC-002]") {
    Frame f{0x100, false, false, true, false, std::vector<uint8_t>(65, 0xBB)};
    REQUIRE_THROWS_AS(validate_frame(f), ErrInvalidFrame);
}

TEST_CASE("SEC: CAN FD data == 64 bytes is accepted", "[security][REQ-SEC-002]") {
    Frame f{0x100, false, false, true, false, std::vector<uint8_t>(64, 0xCC)};
    REQUIRE_NOTHROW(validate_frame(f));
}

// ── REQ-SEC-003: RTR+FD rejected (undefined combination) ─────────────────────

TEST_CASE("SEC: RTR+FD combination is rejected", "[security][REQ-SEC-003]") {
    Frame f{0x100, false, true, true, false, {}};
    REQUIRE_THROWS_AS(validate_frame(f), ErrInvalidFrame);
}

// ── REQ-SEC-004: E2E integrity header prepended ───────────────────────────────

TEST_CASE("SEC: E2E protect prepends 10-byte header", "[security][REQ-SEC-004]") {
    safety::Config cfg{0xABCD, 0x01};
    safety::Protector p(cfg);
    std::vector<uint8_t> payload = {0x11, 0x22, 0x33};
    auto out = p.protect(payload);
    REQUIRE(out.size() == payload.size() + 10);
}

// ── REQ-SEC-005: CRC mismatch is rejected ────────────────────────────────────

TEST_CASE("SEC: corrupted E2E payload is rejected with CRCMismatch", "[security][REQ-SEC-005]") {
    safety::Config cfg{0xABCD, 0x01};
    safety::Protector protector(cfg);
    safety::Receiver  receiver(cfg);

    auto frame = protector.protect({0xDE, 0xAD, 0xBE, 0xEF});
    frame[10] ^= 0xFF;  // corrupt first payload byte (offset past 10-byte header)
    REQUIRE_THROWS_AS(receiver.unwrap(frame), safety::E2EError);
}

// ── REQ-SEC-006: replay / reorder detection ───────────────────────────────────

TEST_CASE("SEC: replayed frame is rejected with SequenceGap", "[security][REQ-SEC-006]") {
    safety::Config cfg{0x1234, 0x02};
    safety::Protector protector(cfg);
    safety::Receiver  receiver(cfg);

    auto f1 = protector.protect({0x01});
    auto f2 = protector.protect({0x02});
    receiver.unwrap(f1);  // seq 1 accepted
    receiver.unwrap(f2);  // seq 2 accepted
    // Send f1 again (seq 1 replayed after seq 2)
    REQUIRE_THROWS_AS(receiver.unwrap(f1), safety::E2EError);
}

// ── REQ-SEC-007: subscription allowlist filtering ─────────────────────────────

TEST_CASE("SEC: non-matching frame not delivered to filtered subscriber", "[security][REQ-SEC-007]") {
    auto bus = virt::Bus::create();
    // Subscribe to 0x100 only
    auto [ch, err] = bus->subscribe({Filter{0x100, 0x7FF}}, {});
    REQUIRE_FALSE(err);

    // Send 0x200 — should NOT arrive
    REQUIRE_FALSE(bus->send(Frame{0x200, false, false, false, false, {0x01}}));
    auto got = ch->try_recv();
    CHECK_FALSE(got.has_value());

    // Send 0x100 — SHOULD arrive
    REQUIRE_FALSE(bus->send(Frame{0x100, false, false, false, false, {0x02}}));
    auto got2 = ch->recv();
    REQUIRE(got2.has_value());
    CHECK(got2->id == 0x100u);
    bus->close();
}

// ── REQ-SEC-008: channel capacity is bounded ──────────────────────────────────

TEST_CASE("SEC: channel rejects sends beyond capacity without blocking", "[security][REQ-SEC-008]") {
    Chan<int> ch(4);
    for (int i = 0; i < 4; ++i)
        CHECK(ch.try_send(i) == Chan<int>::SendResult::Ok);
    CHECK(ch.try_send(99) == Chan<int>::SendResult::Full);
}

// ── REQ-SEC-009: slow subscriber does not block other subscribers ─────────────

TEST_CASE("SEC: full DropNewest subscriber does not block second subscriber", "[security][REQ-SEC-009]") {
    auto bus = virt::Bus::create();

    // depth-1 DropNewest subscriber (will fill immediately)
    auto [slow_ch, e1] = bus->subscribe({}, {relay::with_channel_depth(1),
                                              relay::with_back_pressure(relay::BackPressurePolicy::DropNewest)});
    auto [fast_ch, e2] = bus->subscribe({}, {relay::with_channel_depth(64)});
    REQUIRE_FALSE(e1);
    REQUIRE_FALSE(e2);

    for (int i = 0; i < 10; ++i)
        bus->send(Frame{static_cast<uint32_t>(i), false, false, false, false, {}});

    // fast subscriber got all 10
    int count = 0;
    while (fast_ch->try_recv().has_value()) ++count;
    CHECK(count == 10);

    bus->close();
}

// ── REQ-SEC-010: ISO-TP 4096-byte payload rejected ───────────────────────────

TEST_CASE("SEC: ISO-TP payload >= 4096 bytes returns ErrPayloadTooLarge", "[security][REQ-SEC-010]") {
    auto bus = virt::Bus::create();
    auto [conn, err] = isotp::Conn::create(bus, isotp::Config{0x7E0, 0x7E8});
    REQUIRE_FALSE(err);
    std::vector<uint8_t> big(4096, 0xFF);
    CHECK(conn->send(big) == relay::ErrPayloadTooLarge());
    bus->close();
}

// ── REQ-SEC-011: ISO-TP sequence number integrity ─────────────────────────────

TEST_CASE("SEC: ISO-TP multi-frame data is fully and correctly reassembled", "[security][REQ-SEC-011]") {
    auto bus = virt::Bus::create();
    auto [sender, es] = isotp::Conn::create(bus, isotp::Config{0x7E0, 0x7E8, false, 0, 0});
    auto [recvr,  er] = isotp::Conn::create(bus, isotp::Config{0x7E8, 0x7E0, false, 0, 0});
    REQUIRE_FALSE(es);
    REQUIRE_FALSE(er);

    std::vector<uint8_t> payload(20, 0xAB);
    std::error_code send_err;
    auto* sp = sender.get();
    std::thread t([sp, &payload, &send_err]{ send_err = sp->send(payload); });

    auto [got, recv_err] = recvr->recv(std::chrono::milliseconds{1000});
    t.join();
    REQUIRE_FALSE(send_err);
    REQUIRE_FALSE(recv_err);
    CHECK(got == payload);
    bus->close();
}

// ── REQ-SEC-012: send on closed bus returns ErrClosed ────────────────────────

TEST_CASE("SEC: send on closed bus returns ErrClosed, no UAF", "[security][REQ-SEC-012]") {
    auto bus = virt::Bus::create();
    bus->close();
    auto err = bus->send(Frame{0x100, false, false, false, false, {0x01}});
    CHECK(err == relay::ErrClosed());
}

// ── REQ-SEC-013: try_send never blocks ───────────────────────────────────────

TEST_CASE("SEC: try_send returns Full instantly on full channel", "[security][REQ-SEC-013]") {
    Chan<int> ch(2);
    ch.try_send(1);
    ch.try_send(2);
    auto result = ch.try_send(3);
    CHECK(result == Chan<int>::SendResult::Full);
}

// ── REQ-SEC-014: E2E short-header rejection ───────────────────────────────────

TEST_CASE("SEC: E2E receiver rejects buffer shorter than 10 bytes", "[security][REQ-SEC-014]") {
    safety::Config cfg{0xDEAD, 0x03};
    safety::Receiver receiver(cfg);
    std::vector<uint8_t> short_buf(9, 0x00);
    REQUIRE_THROWS_AS(receiver.unwrap(short_buf), safety::E2EError);
}

// ── REQ-SEC-015: DBC unknown message returns empty, no crash ─────────────────

TEST_CASE("SEC: DBC decode of unknown message ID returns empty map", "[security][REQ-SEC-015]") {
    std::istringstream dbc("BO_ 100 Engine: 8 Vector\n SG_ Speed : 0|8@1+ (1,0) [0|0] \"\" Vector\n");
    auto db = dbc::parse(dbc);
    auto result = db->decode(0xDEAD, {0x00, 0x01, 0x02});
    CHECK(result.empty());
}
