// Copyright (c) 2026 Matt Jones. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

// test_cangen_candump_interop.cpp — third-party-peer interop: Linux's real
// kernel SocketCAN subsystem plus can-utils (candump/cangen) as the
// independent validator, per ROADMAP.md's "Interop testing" section,
// deliverable 2. CAN doesn't need a third-party network *stack* the way DDS
// needs CycloneDDS as a second RTPS implementation — the OS kernel's own
// vcan netdevice plus can-utils (an entirely separate codebase from
// cpp-CAN, maintained upstream at github.com/linux-can/can-utils) already
// is a real, independent oracle for "did the right bytes cross the wire".
//
// Two directions:
//   1. cangen (fixed -I/-L/-D args, so its output is fully deterministic)
//      injects real frames onto vcan0; can::socketcan::Bus's receive path
//      decodes them and this test asserts the decode is field-exact against
//      exactly what cangen was told to send.
//   2. can::socketcan::Bus's send path transmits a known Frame;
//      `candump -L vcan0` (an entirely separate process/codebase) captures
//      it off the wire and this test asserts the captured line's ID/data
//      hex matches the expected CAN wire encoding byte-for-byte.
//
// Requires CPPCAN_INTEROP_TESTS=ON (Linux + vcan0 + can-utils installed) —
// see interop/CMakeLists.txt and .github/workflows/ci.yml's `can-interop`
// job. Not part of the default `build-and-test` matrix.
//
// fusa:test REQ-SCAN-003 REQ-SCAN-005 REQ-SCAN-007

#include <can/socketcan/bus.hpp>
#include "interop_common.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <iomanip>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using caninterop::exec_capture;
using caninterop::hex_encode;

namespace {

// 3-hex-digit, zero-padded, uppercase standard (SFF) CAN ID, matching
// can-utils' own `put_sff_id()` log-format convention exactly (lib.c) —
// used to build the exact substring we expect to find in `candump -L`
// output, and to pass as cangen's `-I` argument.
std::string sff_id_hex(uint32_t id) {
    std::ostringstream o;
    o << std::hex << std::uppercase << std::setw(3) << std::setfill('0') << (id & 0x7FF);
    return o.str();
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Direction 1/2: cangen (independent third-party sender) -> cpp-CAN's own
// SocketCAN receive path.
// ---------------------------------------------------------------------------

//fusa:test REQ-SCAN-005 REQ-SCAN-007
TEST_CASE("cpp-CAN's SocketCAN receiver decodes cangen-injected frames field-exact",
          "[can-interop][cangen][REQ-SCAN-007]") {
    const uint32_t kId    = 0x2A5;
    const unsigned kCount = 5;
    // Fixed (not random) cangen args: -I/-L/-D all pinned, so "what cangen
    // actually sent" is fully known up front rather than needing a second
    // capture process to discover it after the fact.
    const std::string kDataHex = "0102030405060708"; // 8 bytes
    const std::vector<uint8_t> kExpectedData = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};

    auto [bus, ec] = can::socketcan::Bus::create("vcan0");
    REQUIRE_FALSE(ec);
    auto [ch, sub_ec] = bus->subscribe({can::Filter{kId, can::kCANMaxExtID}});
    REQUIRE_FALSE(sub_ec);

    // cangen exits on its own after -n frames (bounded further by the
    // exec_capture timeout as a hard backstop against any upstream
    // can-utils hang). argv vector, no shell — see exec_capture's doc
    // comment in interop_common.hpp.
    auto cangen_result = exec_capture(
        {"cangen", "-I", sff_id_hex(kId), "-L", "8", "-D", kDataHex,
         "-g", "5", "-n", std::to_string(kCount), "vcan0"},
        std::chrono::seconds(20));
    std::string cangen_output = cangen_result.output;
    INFO("cangen output: " << cangen_output);
    INFO("cangen timed out: " << (cangen_result.timed_out ? "true" : "false"));

    std::vector<can::Frame> received;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (received.size() < kCount && std::chrono::steady_clock::now() < deadline) {
        auto got = ch->try_recv();
        if (!got.has_value()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        received.push_back(*got);
    }
    bus->close();

    REQUIRE(received.size() == kCount);
    for (const auto& f : received) {
        CHECK(f.id == kId);
        CHECK_FALSE(f.ext);
        CHECK_FALSE(f.rtr);
        CHECK_FALSE(f.fd);
        CHECK(f.data == kExpectedData);
    }
}

// ---------------------------------------------------------------------------
// Direction 2/2: cpp-CAN's own SocketCAN send path -> candump -L (independent
// third-party capture).
// ---------------------------------------------------------------------------

//fusa:test REQ-SCAN-003
TEST_CASE("candump -L captures cpp-CAN-sent frames with the exact expected wire encoding",
          "[can-interop][candump][REQ-SCAN-003]") {
    const uint32_t kId = 0x1AB;
    const std::vector<uint8_t> kData = {0xDE, 0xAD, 0xBE, 0xEF, 0x11, 0x22, 0x33, 0x44};

    // Start candump first (it must already be listening before cpp-CAN
    // sends), capturing its log-format stdout in a background thread; -n 1
    // makes it exit on its own the moment it has the frame this test cares
    // about, bounded by `timeout` as a hard backstop.
    std::string candump_output;
    std::thread candump_thread([&] {
        auto result = exec_capture({"candump", "-L", "-n", "1", "vcan0"}, std::chrono::seconds(15));
        candump_output = result.output;
    });
    // Joins candump_thread on scope exit even if a REQUIRE below fails
    // first — std::thread's destructor calls std::terminate() on a
    // still-joinable thread, which a bare exception unwind would hit
    // otherwise (the happy path still joins explicitly below).
    caninterop::ThreadJoinGuard candump_join_guard(candump_thread);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    auto [bus, ec] = can::socketcan::Bus::create("vcan0");
    REQUIRE_FALSE(ec);
    can::Frame f{};
    f.id   = kId;
    f.data = kData;
    auto send_ec = bus->send(f);
    bus->close();
    REQUIRE_FALSE(send_ec);

    candump_thread.join();
    INFO("candump output: " << candump_output);

    // The RTPS-analogue "byte-exact wire encoding" check: can-utils' own
    // log-format encoder (lib.c's snprintf_canframe(), an entirely separate
    // codebase from this crate's can::socketcan::Bus::send()) must have
    // printed exactly "<3-hex-digit-SFF-ID>#<uppercase-hex-data>" for the
    // frame cpp-CAN transmitted.
    std::string expected = sff_id_hex(kId) + "#" + hex_encode(kData);
    CHECK(candump_output.find(expected) != std::string::npos);
}
