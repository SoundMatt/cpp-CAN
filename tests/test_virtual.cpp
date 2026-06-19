// Copyright (c) 2026 Matt Jones. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

// fusa:test REQ-VIRT-001 REQ-VIRT-002 REQ-VIRT-003 REQ-VIRT-004 REQ-VIRT-005 REQ-VIRT-006 REQ-VIRT-007 REQ-VIRT-008 REQ-VIRT-009
// fusa:test REQ-CAN-017 REQ-CAN-018
// fusa:test REQ-RELAY-023 REQ-RELAY-024 REQ-RELAY-025 REQ-RELAY-026 REQ-RELAY-027 REQ-RELAY-028 REQ-RELAY-029

#include <can/virtual/bus.hpp>
#include <catch2/catch_test_macros.hpp>
#include <thread>

using namespace can;
using namespace can::virt;

TEST_CASE("Bus::create returns non-null", "[virtual][REQ-VIRT-001]") {
    auto bus = Bus::create();
    REQUIRE(bus != nullptr);
    bus->close();
}

TEST_CASE("send and receive single frame", "[virtual][REQ-VIRT-001][REQ-VIRT-002]") {
    auto bus = Bus::create();
    auto [ch, err] = bus->subscribe({}, {});
    REQUIRE_FALSE(err);

    Frame f{0x100, false, false, false, false, {0xDE, 0xAD}};
    REQUIRE_FALSE(bus->send(f));

    auto got = ch->recv();
    REQUIRE(got.has_value());
    CHECK(got->id == 0x100);
    CHECK(got->data == std::vector<uint8_t>{0xDE, 0xAD});
    bus->close();
}

TEST_CASE("filter accepts matching frame", "[virtual][REQ-VIRT-003][REQ-VIRT-004]") {
    auto bus = Bus::create();
    auto [ch, err] = bus->subscribe({{0x200, 0x7FF}}, {});
    REQUIRE_FALSE(err);

    bus->send(Frame{0x100, false, false, false, false, {1}});
    bus->send(Frame{0x200, false, false, false, false, {2}});

    auto got = ch->recv();
    REQUIRE(got.has_value());
    CHECK(got->id == 0x200);

    // 0x100 should not arrive
    auto none = ch->try_recv();
    CHECK_FALSE(none.has_value());
    bus->close();
}

TEST_CASE("multiple subscribers each receive the frame", "[virtual][REQ-VIRT-002]") {
    auto bus = Bus::create();
    auto [ch1, e1] = bus->subscribe({}, {});
    auto [ch2, e2] = bus->subscribe({}, {});
    REQUIRE_FALSE(e1);
    REQUIRE_FALSE(e2);

    bus->send(Frame{0x300, false, false, false, false, {0x42}});

    auto g1 = ch1->recv();
    auto g2 = ch2->recv();
    REQUIRE(g1.has_value());
    REQUIRE(g2.has_value());
    CHECK(g1->id == 0x300);
    CHECK(g2->id == 0x300);
    bus->close();
}

TEST_CASE("send on closed bus returns ErrClosed", "[virtual][REQ-VIRT-006]") {
    auto bus = Bus::create();
    bus->close();
    auto err = bus->send(Frame{0x100, false, false, false, false, {}});
    CHECK(err == relay::ErrClosed());
}

TEST_CASE("subscribe on closed bus returns ErrClosed", "[virtual][REQ-VIRT-006]") {
    auto bus = Bus::create();
    bus->close();
    auto [ch, err] = bus->subscribe({}, {});
    CHECK(err == relay::ErrClosed());
    CHECK(ch == nullptr);
}

TEST_CASE("double close is idempotent", "[virtual][REQ-VIRT-005][REQ-VIRT-006]") {
    auto bus = Bus::create();
    REQUIRE_FALSE(bus->close());
    REQUIRE_FALSE(bus->close());
}

TEST_CASE("send invalid frame returns error", "[virtual][REQ-VIRT-006]") {
    auto bus = Bus::create();
    auto err = bus->send(Frame{0x800, false, false, false, false, {}});
    CHECK(err);  // standard ID too large
    bus->close();
}

TEST_CASE("subscriber channel closes when bus closes", "[virtual][REQ-VIRT-007]") {
    auto bus = Bus::create();
    auto [ch, err] = bus->subscribe({}, {});
    REQUIRE_FALSE(err);
    bus->close();
    auto got = ch->recv();
    CHECK_FALSE(got.has_value());
}

TEST_CASE("DropNewest back-pressure drops new frames when full", "[virtual][REQ-VIRT-004]") {
    auto bus = Bus::create();
    auto [ch, err] = bus->subscribe({}, {relay::with_channel_depth(2),
                                          relay::with_back_pressure(relay::BackPressurePolicy::DropNewest)});
    REQUIRE_FALSE(err);

    bus->send(Frame{0x001, false, false, false, false, {1}});
    bus->send(Frame{0x002, false, false, false, false, {2}});
    bus->send(Frame{0x003, false, false, false, false, {3}});  // dropped

    CHECK(ch->size() == 2);
    auto m1 = ch->recv(); CHECK(m1->id == 0x001);
    auto m2 = ch->recv(); CHECK(m2->id == 0x002);
    bus->close();
}

TEST_CASE("DropOldest back-pressure evicts oldest frame when full", "[virtual][REQ-VIRT-004]") {
    auto bus = Bus::create();
    auto [ch, err] = bus->subscribe({}, {relay::with_channel_depth(2),
                                          relay::with_back_pressure(relay::BackPressurePolicy::DropOldest)});
    REQUIRE_FALSE(err);

    bus->send(Frame{0x001, false, false, false, false, {1}});
    bus->send(Frame{0x002, false, false, false, false, {2}});
    bus->send(Frame{0x003, false, false, false, false, {3}});  // evicts 0x001

    CHECK(ch->size() == 2);
    auto m1 = ch->recv(); CHECK(m1->id == 0x002);
    auto m2 = ch->recv(); CHECK(m2->id == 0x003);
    bus->close();
}

TEST_CASE("close_with_drain returns ok when all items consumed", "[virtual][REQ-VIRT-009][REQ-RELAY-028]") {
    auto bus = Bus::create();
    auto [ch, err] = bus->subscribe({}, {});
    REQUIRE_FALSE(err);

    bus->send(Frame{0x100, false, false, false, false, {}});
    ch->recv();

    CHECK_FALSE(bus->close_with_drain(std::chrono::milliseconds{100}));
}

TEST_CASE("health reports OK when open, Down when closed", "[virtual][REQ-VIRT-009][REQ-RELAY-023][REQ-RELAY-024][REQ-RELAY-025]") {
    auto bus = Bus::create();
    CHECK(bus->health().status == HealthStatus::OK);
    bus->close();
    CHECK(bus->health().status == HealthStatus::Down);
}

TEST_CASE("metrics count sends and deliveries", "[virtual][REQ-VIRT-009][REQ-RELAY-026][REQ-RELAY-027][REQ-RELAY-029]") {
    auto bus = Bus::create();
    auto [ch, err] = bus->subscribe({}, {});
    REQUIRE_FALSE(err);

    bus->send(Frame{0x100, false, false, false, false, {1, 2}});
    ch->recv();

    auto m = bus->metrics();
    CHECK(m.write_count   == 1);
    CHECK(m.deliver_count == 1);
    CHECK(m.bytes_written == 2);
    bus->close();
}

TEST_CASE("loan / send_loaned round-trip", "[virtual][REQ-VIRT-008][REQ-CAN-017][REQ-CAN-018]") {
    auto bus = Bus::create();
    auto [ch, err] = bus->subscribe({}, {});
    REQUIRE_FALSE(err);

    auto [lf, lerr] = bus->loan();
    REQUIRE_FALSE(lerr);
    REQUIRE(lf != nullptr);

    lf->frame = Frame{0x400, false, false, false, false, {0xBB}};
    bus->send_loaned(std::move(lf));

    auto got = ch->recv();
    REQUIRE(got.has_value());
    CHECK(got->id == 0x400);
    bus->close();
}
