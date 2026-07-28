// Copyright (c) 2026 Matt Jones. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

// test_two_process_interop.cpp — the live two-process SocketCAN self-interop
// test harness. ROADMAP.md's "Interop testing" section, deliverable 1:
// two real, independent OS processes of `can_interop_peer`
// (can_interop_peer.cpp — real can::socketcan::Bus machinery, no test-only
// shortcuts) bound to the same real vcan0 SocketCAN interface, one sending
// real CAN frames via the RELAY-conformant can::IBus API, the other
// receiving and verifying field-exact correctness (ID, DLC, data, FD/BRS
// flags) — genuine kernel-level CAN traffic, not a mock. Mirrors
// rust-DDS's tests/rtps_two_process_interop.rs.
//
// Requires: a real vcan0 interface already up (see .github/workflows/ci.yml's
// `can-interop` job, or manually:
//   sudo modprobe vcan
//   sudo ip link add dev vcan0 type vcan
//   sudo ip link set up vcan0
// ), and CPPCAN_INTEROP_TESTS=ON — this whole binary is not even built
// otherwise (see CMakeLists.txt / interop/CMakeLists.txt), so it never runs
// as part of the default cross-platform `build-and-test` CI job.
//
// fusa:test REQ-SCAN-001 REQ-SCAN-003 REQ-SCAN-005 REQ-SCAN-006
// fusa:test REQ-SCAN-007 REQ-SCAN-008

#include "interop_common.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <cstdio>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using caninterop::FrameRecord;
using caninterop::Summary;

namespace {

#ifndef CPPCAN_INTEROP_PEER_BIN
#error "CPPCAN_INTEROP_PEER_BIN must be defined by CMake (interop/CMakeLists.txt)"
#endif

struct PeerResult {
    std::vector<FrameRecord> frames;
    Summary                  summary;
    bool                     summary_seen{false};
};

// Runs can_interop_peer with `args`, capturing stdout line-by-line via
// popen(3) (this is test-support code, not the library itself — no need to
// match cli/'s no-libc-shellout convention). Every "kind":"frame" line
// becomes a FrameRecord in order; the final "kind":"summary" line becomes
// PeerResult::summary.
PeerResult run_peer(const std::vector<std::string>& args) {
    std::ostringstream cmd;
    cmd << "\"" << CPPCAN_INTEROP_PEER_BIN << "\"";
    for (const auto& a : args) cmd << " " << a;
    cmd << " 2>&1"; // fold stderr in so a crash/usage error is visible in output

    FILE* pipe = popen(cmd.str().c_str(), "r");
    if (!pipe) throw std::runtime_error("popen failed for: " + cmd.str());

    PeerResult result;
    std::array<char, 4096> buf{};
    std::string pending;
    while (std::fgets(buf.data(), static_cast<int>(buf.size()), pipe) != nullptr) {
        pending += buf.data();
        std::size_t nl;
        while ((nl = pending.find('\n')) != std::string::npos) {
            std::string line = pending.substr(0, nl);
            pending.erase(0, nl + 1);

            FrameRecord fr;
            Summary sm;
            if (caninterop::parse_summary_json_line(line, sm)) {
                result.summary = sm;
                result.summary_seen = true;
            } else if (caninterop::parse_frame_json_line(line, fr)) {
                result.frames.push_back(fr);
            }
            // Anything else (e.g. a stray stderr line) is ignored — the
            // NDJSON contract only requires the "frame"/"summary" lines.
        }
    }
    pclose(pipe);

    if (!result.summary_seen) {
        throw std::runtime_error("can_interop_peer (cmd=" + cmd.str() +
                                  ") produced no \"kind\":\"summary\" line");
    }
    return result;
}

std::vector<uint8_t> expected_payload(unsigned seed, unsigned frame_index, unsigned len) {
    std::vector<uint8_t> data(len);
    for (unsigned j = 0; j < len; ++j) data[j] = static_cast<uint8_t>((seed + frame_index + j) & 0xFF);
    return data;
}

} // anonymous namespace

//fusa:test REQ-SCAN-001 REQ-SCAN-003 REQ-SCAN-005 REQ-SCAN-007
TEST_CASE("two real processes exchange classic CAN frames field-exact over vcan0",
          "[can-interop][two-process][REQ-SCAN-003][REQ-SCAN-007]") {
    const unsigned kCount = 5;
    const unsigned kId    = 0x321;
    const unsigned kSeed  = 7;
    const unsigned kLen   = 8;

    PeerResult reader_result;
    std::thread reader_thread([&] {
        reader_result = run_peer({
            "--role", "reader", "--iface", "vcan0",
            "--id", std::to_string(kId),
            "--count", std::to_string(kCount),
            "--timeout-secs", "15",
        });
    });
    // Joins reader_thread on scope exit even if a run_peer() call below
    // throws or a REQUIRE fails before the explicit join() further down —
    // std::thread's own destructor calls std::terminate() on a still-
    // joinable thread, which a bare exception unwind would otherwise hit.
    caninterop::ThreadJoinGuard reader_join_guard(reader_thread);

    // Give the reader a head start so it is already bound and subscribed
    // before the writer's first real frame hits the wire.
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    PeerResult writer_result = run_peer({
        "--role", "writer", "--iface", "vcan0",
        "--id", std::to_string(kId),
        "--count", std::to_string(kCount),
        "--seed", std::to_string(kSeed),
        "--data-len", std::to_string(kLen),
        "--gap-ms", "10",
    });
    reader_thread.join();

    INFO("writer error: " << writer_result.summary.error);
    REQUIRE(writer_result.summary.ok);
    REQUIRE(writer_result.summary.count == kCount);
    REQUIRE(writer_result.frames.size() == kCount);

    INFO("reader error: " << reader_result.summary.error);
    REQUIRE(reader_result.summary.ok);
    REQUIRE(reader_result.frames.size() == kCount);

    // Field-exact: every frame the reader actually decoded off the real
    // vcan0 socket must match, byte for byte, what the writer actually
    // wrote to its own real socket — not a value the test predicted in
    // isolation from either process.
    REQUIRE(reader_result.frames == writer_result.frames);

    // And both independently match what the writer's own CLI args say it
    // should have sent (belt and suspenders against a harness bug that
    // makes both processes agree on the same wrong thing).
    for (unsigned i = 0; i < kCount; ++i) {
        const auto& f = reader_result.frames[i];
        CHECK(f.id == kId);
        CHECK_FALSE(f.ext);
        CHECK_FALSE(f.rtr);
        CHECK_FALSE(f.fd);
        CHECK(f.data == expected_payload(kSeed, i, kLen));
    }
}

//fusa:test REQ-SCAN-002 REQ-SCAN-003 REQ-SCAN-005 REQ-SCAN-008
TEST_CASE("two real processes exchange CAN FD frames with BRS field-exact over vcan0",
          "[can-interop][two-process][REQ-SCAN-008]") {
    const unsigned kCount = 3;
    const unsigned kId    = 0x322;
    const unsigned kSeed  = 99;
    const unsigned kLen   = 32; // > 8 bytes: only representable as CAN FD

    PeerResult reader_result;
    std::thread reader_thread([&] {
        reader_result = run_peer({
            "--role", "reader", "--iface", "vcan0",
            "--id", std::to_string(kId),
            "--count", std::to_string(kCount),
            "--timeout-secs", "15",
        });
    });
    caninterop::ThreadJoinGuard reader_join_guard(reader_thread);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    PeerResult writer_result = run_peer({
        "--role", "writer", "--iface", "vcan0",
        "--id", std::to_string(kId),
        "--count", std::to_string(kCount),
        "--seed", std::to_string(kSeed),
        "--data-len", std::to_string(kLen),
        "--fd", "--brs",
        "--gap-ms", "10",
    });
    reader_thread.join();

    INFO("writer error: " << writer_result.summary.error);
    REQUIRE(writer_result.summary.ok);
    INFO("reader error: " << reader_result.summary.error);
    REQUIRE(reader_result.summary.ok);

    REQUIRE(reader_result.frames == writer_result.frames);
    for (unsigned i = 0; i < kCount; ++i) {
        const auto& f = reader_result.frames[i];
        CHECK(f.id == kId);
        CHECK(f.fd);
        CHECK(f.brs);
        CHECK(f.data == expected_payload(kSeed, i, kLen));
    }
}

//fusa:test REQ-SCAN-001 REQ-SCAN-005
TEST_CASE("reader reports failure, not a hang, when nobody sends anything",
          "[can-interop][two-process]") {
    // Sanity check on the harness itself, mirroring rust-DDS's
    // `peer_binary_reports_failure_when_no_peer_ever_appears`: a lone
    // reader waiting on an ID nobody publishes must report ok=false with
    // zero frames within its own bounded timeout, not hang.
    PeerResult result = run_peer({
        "--role", "reader", "--iface", "vcan0",
        "--id", "0x7AA",
        "--count", "1",
        "--timeout-secs", "2",
    });
    CHECK_FALSE(result.summary.ok);
    CHECK(result.frames.empty());
    CHECK(result.summary.count == 0);
}
