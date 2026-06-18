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

// Helper: create two ends on the same virtual bus (echo loopback)
static std::pair<std::unique_ptr<Conn>, std::unique_ptr<Conn>> make_echo_pair() {
    auto bus = virt::Bus::create();
    auto [a, ea] = Conn::create(bus, Config{0x7E0, 0x7E8, false, 0, 0});
    auto [b, eb] = Conn::create(bus, Config{0x7E8, 0x7E0, false, 0, 0});
    REQUIRE_FALSE(ea);
    REQUIRE_FALSE(eb);
    return {std::move(a), std::move(b)};
}

TEST_CASE("single-frame send/recv round-trip (≤7 bytes)", "[isotp][REQ-ISOTP-005][REQ-ISOTP-009]") {
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
