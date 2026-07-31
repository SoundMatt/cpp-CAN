// Copyright (c) 2026 Matt Jones. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

// fusa:test REQ-J1939-001 REQ-J1939-002 REQ-J1939-003 REQ-J1939-004 REQ-J1939-005 REQ-J1939-006

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

TEST_CASE("decode_id: Data Page bit is included in PGN", "[j1939][REQ-J1939-003]") {
    // DP=1, PF=0xFE, PS=0x00, Src=0x00 → PGN should have DP bit set
    uint32_t id = (6u << 26) | (1u << 24) | (0xFEu << 16) | (0x00u << 8) | 0x00u;
    auto [prio, pgn, src] = decode_id(id);
    // DP bit is encoded as (dp << 16) in PGN, so DP=1 sets bit 16 (0x10000)
    CHECK((static_cast<uint32_t>(pgn) & 0x10000u) != 0);  // DP bit present
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

TEST_CASE("J1939 Bus: PGN filter - non-matching PGN not delivered", "[j1939][REQ-J1939-006]") {
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

// ── BAM reassembly: unchecked size field (SAE J1939-21 TP.CM_BAM) ──────────────
//
// SAE J1939-21 TP.CM_BAM: total message size is 9-1785 bytes (9-bit field).
// subscribe_tp's BAM handler previously read `total` straight from the
// frame and used it unchecked to allocate a std::vector<uint8_t>(total),
// with no relationship enforced between the declared total and the
// declared num_packets — a spoofed announcement could claim a small
// num_packets (so the "completion" condition `received == num_packets`
// is trivially satisfiable from a couple of real Data Transfer frames)
// together with an out-of-protocol-range total, and still get delivered
// to subscribers as a mostly-zero-padded oversized message. This proves
// such an announcement is rejected outright (no session created, nothing
// delivered) rather than silently accepted and short-completed.

TEST_CASE("subscribe_tp: BAM announcing an out-of-range total is rejected, "
          "not silently short-completed",
          "[j1939][REQ-J1939-005][REQ-SEC-011]") {
    auto can_bus = can::virt::Bus::create();
    auto j_bus   = Bus::create(can_bus, 0x00);

    auto [tp_ch, tp_err] = j_bus->subscribe_tp({0x0FECA});
    REQUIRE_FALSE(tp_err);

    constexpr uint8_t kSpoofedSrc = 0x11;
    constexpr PGN      kTPCM      = static_cast<PGN>(0xEC00);
    constexpr PGN      kTPDT      = static_cast<PGN>(0xEB00);

    // TP.CM_BAM declaring total=2000 (over J1939-21's 1785-byte maximum)
    // but only num_packets=3 — internally inconsistent (2000 bytes would
    // require 286 packets of 7 bytes each), but a receiver that only
    // range-checks nothing at all will still allocate a 2000-byte buffer
    // and consider the transfer "complete" after 3 packets.
    uint32_t bam_id = encode_id(6, kTPCM, kSpoofedSrc) |
                       (static_cast<uint32_t>(kBroadcastAddr) << 8);
    std::vector<uint8_t> bogus_bam = {
        0x20,               // BAM control byte
        0xD0, 0x07,         // total = 2000 (0x07D0), little-endian
        0x03,               // num_packets = 3
        0xFF,
        static_cast<uint8_t>(0x0FECA),
        static_cast<uint8_t>(0x0FECA >> 8),
        static_cast<uint8_t>(0x0FECA >> 16),
    };
    REQUIRE_FALSE(can_bus->send(can::Frame{bam_id, true, false, false, false, bogus_bam}));

    // Matching Data Transfer frames for the (bogus) declared num_packets.
    uint32_t dt_id = encode_id(6, kTPDT, kSpoofedSrc) |
                      (static_cast<uint32_t>(kBroadcastAddr) << 8);
    for (uint8_t seq = 1; seq <= 3; ++seq) {
        std::vector<uint8_t> pkt(8, 0xAA);
        pkt[0] = seq;
        REQUIRE_FALSE(can_bus->send(can::Frame{dt_id, true, false, false, false, pkt}));
    }

    // A conformant receiver must reject the out-of-range total before
    // ever creating a session for it, so these three DT frames — despite
    // satisfying the announced (bogus) num_packets — must not produce a
    // delivered message.
    std::this_thread::sleep_for(std::chrono::milliseconds{50});
    CHECK(tp_ch->size() == 0);

    // A subsequent legitimate BAM transfer from the SAME source address
    // must still work — proves the rejected announcement didn't leave
    // corrupt/stuck session state behind.
    std::vector<uint8_t> good_payload(20, 0x77);
    Frame good{6, 0x0FECA, kSpoofedSrc, kBroadcastAddr, good_payload};
    std::thread t([&]{ j_bus->send(good); });
    auto got = tp_ch->recv();
    t.join();
    REQUIRE(got.has_value());
    CHECK(got->data == good_payload);

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
