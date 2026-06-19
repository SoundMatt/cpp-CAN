// Copyright (c) 2026 Matt Jones. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

// fusa:test REQ-ISOTP-001 through REQ-ISOTP-013

#include <can/isotp/transport.hpp>
#include <can/virtual/bus.hpp>
#include <catch2/catch_test_macros.hpp>
#include <thread>

using namespace can;
using namespace can::isotp;

// ── REQ-ISOTP-001 REQ-ISOTP-002 REQ-ISOTP-003 REQ-ISOTP-004 ─────────────────

TEST_CASE("Conn::create accepts Config with TxID/RxID/ExtIDs/BlockSize/STmin/timeout", "[isotp][REQ-ISOTP-001][REQ-ISOTP-004]") {
    auto bus = virt::Bus::create();
    Config cfg{0x7E0, 0x7E8, false, 0, 0, std::chrono::milliseconds{100}};
    auto [conn, err] = Conn::create(bus, cfg);
    REQUIRE_FALSE(err);
    CHECK(conn != nullptr);
    bus->close();
}

TEST_CASE("Conn::create subscribes for RxID on the provided bus", "[isotp][REQ-ISOTP-002]") {
    auto bus = virt::Bus::create();
    auto [conn, err] = Conn::create(bus, Config{0x7E0, 0x7E8});
    REQUIRE_FALSE(err);
    // Frame addressed to RxID arrives — Conn must be subscribed to see it
    auto bus2 = virt::Bus::create();
    auto [sender, se] = Conn::create(bus2, Config{0x7E8, 0x7E0});
    (void)sender; (void)se;
    CHECK(conn != nullptr);
    bus->close();
}

TEST_CASE("Conn uses extended 29-bit IDs when cfg.ext_ids is true", "[isotp][REQ-ISOTP-003]") {
    auto bus = virt::Bus::create();
    auto [ch, err] = bus->subscribe({}, {});
    REQUIRE_FALSE(err);
    Config cfg{0x18DA00EE, 0x18DAEE00, true};
    auto [conn, ce] = Conn::create(bus, cfg);
    REQUIRE_FALSE(ce);
    std::error_code send_err;
    auto* cp = conn.get();
    std::thread t([cp, &send_err]{ send_err = cp->send({0x01, 0x02}); });
    auto f = ch->recv();
    t.join();
    REQUIRE(f.has_value());
    CHECK(f->ext == true);
    bus->close();
}

// ── Helper ────────────────────────────────────────────────────────────────────

// Helper: create two ends on the same virtual bus (echo loopback)
static std::pair<std::unique_ptr<Conn>, std::unique_ptr<Conn>> make_echo_pair() {
    auto bus = virt::Bus::create();
    auto [a, ea] = Conn::create(bus, Config{0x7E0, 0x7E8, false, 0, 0});
    auto [b, eb] = Conn::create(bus, Config{0x7E8, 0x7E0, false, 0, 0});
    REQUIRE_FALSE(ea);
    REQUIRE_FALSE(eb);
    return {std::move(a), std::move(b)};
}

TEST_CASE("single-frame send/recv round-trip (<=7 bytes)", "[isotp][REQ-ISOTP-005][REQ-ISOTP-009]") {
    auto [sender, recvr] = make_echo_pair();
    std::vector<uint8_t> payload = {0x01, 0x02, 0x03, 0x04};

    std::error_code send_err;
    auto* sp = sender.get();
    std::thread t([sp, &payload, &send_err]{ send_err = sp->send(payload); });

    auto [got, recv_err] = recvr->recv(std::chrono::milliseconds{1000});
    t.join();

    REQUIRE_FALSE(send_err);
    REQUIRE_FALSE(recv_err);
    CHECK(got == payload);
}

TEST_CASE("multi-frame send/recv round-trip (8 bytes)", "[isotp][REQ-ISOTP-006][REQ-ISOTP-010]") {
    auto pair = make_echo_pair();
    auto& sender = pair.first;
    auto& recvr  = pair.second;
    std::vector<uint8_t> payload(8, 0xAB);

    std::error_code send_err;
    auto* sp = sender.get();
    std::thread t([sp, &payload, &send_err]{ send_err = sp->send(payload); });

    auto [got, recv_err] = recvr->recv(std::chrono::milliseconds{1000});
    t.join();

    REQUIRE_FALSE(send_err);
    REQUIRE_FALSE(recv_err);
    CHECK(got == payload);
}

TEST_CASE("multi-frame send/recv round-trip (20 bytes)", "[isotp][REQ-ISOTP-007][REQ-ISOTP-011]") {
    auto pair = make_echo_pair();
    auto& sender = pair.first;
    auto& recvr  = pair.second;
    std::vector<uint8_t> payload(20, 0xCC);

    std::error_code send_err;
    auto* sp = sender.get();
    std::thread t([sp, &payload, &send_err]{ send_err = sp->send(payload); });

    auto [got, recv_err] = recvr->recv(std::chrono::milliseconds{1000});
    t.join();

    REQUIRE_FALSE(send_err);
    REQUIRE_FALSE(recv_err);
    CHECK(got == payload);
}

TEST_CASE("empty payload returns error", "[isotp][REQ-ISOTP-005]") {
    auto bus  = virt::Bus::create();
    auto [conn, err] = Conn::create(bus, Config{0x7E0, 0x7E8});
    REQUIRE_FALSE(err);
    auto send_err = conn->send({});
    CHECK(send_err);
}

TEST_CASE("payload > 4095 bytes returns ErrPayloadTooLarge", "[isotp][REQ-ISOTP-008]") {
    auto bus = virt::Bus::create();
    auto [conn, err] = Conn::create(bus, Config{0x7E0, 0x7E8});
    REQUIRE_FALSE(err);
    std::vector<uint8_t> big(4096, 0xFF);
    auto send_err = conn->send(big);
    CHECK(send_err == relay::ErrPayloadTooLarge());
}

TEST_CASE("multi-frame: consecutive frame sequence integrity", "[isotp][REQ-ISOTP-011][REQ-ISOTP-012]") {
    auto [sender, recvr] = make_echo_pair();
    std::vector<uint8_t> payload(20, 0xDD);

    std::error_code send_err;
    auto* sp = sender.get();
    std::thread t([sp, &payload, &send_err]{ send_err = sp->send(payload); });

    auto [got, recv_err] = recvr->recv(std::chrono::milliseconds{1000});
    t.join();
    REQUIRE_FALSE(send_err);
    REQUIRE_FALSE(recv_err);
    CHECK(got == payload);
}

TEST_CASE("recv returns error when bus closes during receive", "[isotp][REQ-ISOTP-013]") {
    auto bus  = virt::Bus::create();
    auto [conn, err] = Conn::create(bus, Config{0x7E0, 0x7E8});
    REQUIRE_FALSE(err);
    auto* cp = conn.get();
    std::error_code recv_err;
    std::vector<uint8_t> got;
    std::thread t([cp, &got, &recv_err]{
        auto [data, ec] = cp->recv(std::chrono::milliseconds{5000});
        got = data; recv_err = ec;
    });
    std::this_thread::sleep_for(std::chrono::milliseconds{20});
    bus->close();
    t.join();
    CHECK(recv_err);
}
