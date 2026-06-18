// Copyright (c) 2026 Matt Jones. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

// fusa:test REQ-J1939-001 through REQ-J1939-006

#include <can/j1939/pgn.hpp>
#include <can/virtual/bus.hpp>
#include <catch2/catch_test_macros.hpp>
#include <thread>

using namespace can::j1939;

// ── decode_id / encode_id ─────────────────────────────────────────────────────

TEST_CASE("decode_id: broadcast PGN (PF >= 240)", "[j1939][REQ-J1939-001]") {
    // Priority=6, DP=0, PF=0xFE, PS=0xCA, Src=0x00
    // ID = (6<<26) | (0<<24) | (0xFE<<16) | (0xCA<<8) | 0x00
    uint32_t id = (6u << 26) | (0xFEu << 16) | (0xCAu << 8) | 0x00u;
    auto [prio, pgn, src] = decode_id(id);
    CHECK(prio == 6);
    CHECK(src  == 0x00);
    CHECK(pgn  == static_cast<PGN>(0xFECA));
    CHECK_FALSE(pgn_is_peer_to_peer(pgn));
}

TEST_CASE("decode_id: peer-to-peer PGN (PF < 240)", "[j1939][REQ-J1939-002]") {
    // Priority=6, DP=0, PF=0xEC, PS=0x00 (destination), Src=0x01
    uint32_t id = (6u << 26) | (0xECu << 16) | (0x00u << 8) | 0x01u;
    auto [prio, pgn, src] = decode_id(id);
    CHECK(prio == 6);
    CHECK(src  == 0x01);
    // PF < 240 → PS is not part of PGN
    CHECK(pgn_is_peer_to_peer(pgn));
}

TEST_CASE("encode_id / decode_id round-trip", "[j1939][REQ-J1939-004]") {
    Priority pri = 3;
    PGN      pgn = 0x0FECA;  // broadcast
    uint8_t  src = 0x42;

    uint32_t id = encode_id(pri, pgn, src);
    auto [d_prio, d_pgn, d_src] = decode_id(id);
    CHECK(d_prio == pri);
    CHECK(d_pgn  == pgn);
    CHECK(d_src  == src);
}

TEST_CASE("pgn_is_peer_to_peer: PF < 240 is P2P", "[j1939][REQ-J1939-002]") {
    CHECK(pgn_is_peer_to_peer(0xEC00));   // PF=0xEC=236 < 240
    CHECK_FALSE(pgn_is_peer_to_peer(0xFECA));  // PF=0xFE=254 >= 240
}

// ── Bus send / subscribe ──────────────────────────────────────────────────────

TEST_CASE("J1939 Bus: send and receive single-packet frame", "[j1939][REQ-J1939-005][REQ-J1939-006]") {
    auto can_bus = can::virt::Bus::create();
    auto j_bus   = Bus::create(can_bus, 0x00);

    auto [ch, err] = j_bus->subscribe({});
    REQUIRE_FALSE(err);

    Frame jf{6, 0x0FECA, 0x00, kBroadcastAddr, {0x01, 0x02, 0x03}};
    REQUIRE_FALSE(j_bus->send(jf));

    auto got = ch->recv();
    REQUIRE(got.has_value());
    CHECK(got->pgn  == 0x0FECAu);
    CHECK(got->src  == 0x00);
    CHECK(got->data == std::vector<uint8_t>{0x01, 0x02, 0x03});
    can_bus->close();
}

TEST_CASE("J1939 Bus: PGN filter — non-matching PGN not delivered", "[j1939][REQ-J1939-006]") {
    auto can_bus = can::virt::Bus::create();
    auto j_bus   = Bus::create(can_bus, 0x00);

    auto [ch, err] = j_bus->subscribe({0x0FECA});
    REQUIRE_FALSE(err);

    // Send a different PGN
    Frame jf{6, 0x0FEBC, 0x00, kBroadcastAddr, {0xAB}};
    j_bus->send(jf);

    // Give the thread a moment
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
    CHECK(ch->size() == 0);
    can_bus->close();
}

TEST_CASE("J1939 Transport Protocol: BAM send/receive", "[j1939][REQ-J1939-005][REQ-J1939-006]") {
    auto can_bus = can::virt::Bus::create();
    auto j_bus   = Bus::create(can_bus, 0x00);

    std::vector<uint8_t> payload(20, 0x55);
    Frame jf{6, 0x0FECA, 0x00, kBroadcastAddr, payload};

    auto [tp_ch, tp_err] = j_bus->subscribe_tp({0x0FECA});
    REQUIRE_FALSE(tp_err);

    std::thread t([&]{ j_bus->send(jf); });

    auto got = tp_ch->recv();
    t.join();
    REQUIRE(got.has_value());
    CHECK(got->data == payload);
    can_bus->close();
}
