// Copyright (c) 2026 Matt Jones. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

// can_interop_peer — standalone test-support binary for the live SocketCAN
// interop test harness (ROADMAP.md "Interop testing", deliverable 1).
//
// A real, independent OS process driven entirely by the production
// can::socketcan::Bus / can::IBus machinery — no test-only shortcuts. Two
// instances of this binary, run as `--role writer` and `--role reader`
// against the same real vcan0 (or hardware) interface, exercise genuine
// kernel-level CAN traffic between two separate processes: mirrors
// rust-DDS's `rtps-interop-peer` (src/bin/rtps_interop_peer.rs).
//
// Unlike DDS's RTPS peer, a CAN bus has no discovery handshake (SPDP/SEDP)
// to prove — SocketCAN's vcan/can netdevice IS the shared medium, so
// "genuine kernel traffic reached the other process" is proven directly by
// field-exact frame delivery, which is what this binary's writer/reader
// roles set up.
//
// Usage:
//   can-interop-peer --role writer --iface vcan0 --id 0x123 --count 5
//   can-interop-peer --role reader --iface vcan0 --id 0x123 --count 5
//
// Output: one NDJSON "kind":"frame" line per frame sent/received (writer:
// what it actually wrote to the socket; reader: what it actually decoded
// off the socket), followed by exactly one final "kind":"summary" line —
// the contract the interop tests parse (see interop_common.hpp).
//
// fusa:test REQ-SCAN-001 REQ-SCAN-003 REQ-SCAN-005 REQ-SCAN-006
// fusa:test REQ-SCAN-007 REQ-SCAN-008

#include <can/socketcan/bus.hpp>
#include "interop_common.hpp"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>

using caninterop::FrameRecord;
using caninterop::Summary;

namespace {

struct Args {
    std::string role;                 // "writer" | "reader"
    std::string iface       = "vcan0";
    uint32_t    id           = 0x123;
    bool        ext          = false;
    bool        fd           = false;
    bool        brs          = false;
    unsigned    count        = 5;
    unsigned    data_len     = 8;
    unsigned    seed         = 0;
    unsigned    gap_ms       = 5;
    unsigned    timeout_secs = 10;
    bool        filter       = true; // reader: only count frames matching --id
};

bool parse_args(int argc, char** argv, Args& a, std::string& err) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto next = [&](const char* flag) -> const char* {
            if (i + 1 >= argc) { err = std::string(flag) + " requires a value"; return nullptr; }
            return argv[++i];
        };
        if (arg == "--role") {
            const char* v = next("--role"); if (!v) return false;
            a.role = v;
        } else if (arg == "--iface") {
            const char* v = next("--iface"); if (!v) return false;
            a.iface = v;
        } else if (arg == "--id") {
            const char* v = next("--id"); if (!v) return false;
            a.id = static_cast<uint32_t>(std::strtoul(v, nullptr, 0));
        } else if (arg == "--ext") {
            a.ext = true;
        } else if (arg == "--fd") {
            a.fd = true;
        } else if (arg == "--brs") {
            a.brs = true;
        } else if (arg == "--count") {
            const char* v = next("--count"); if (!v) return false;
            a.count = static_cast<unsigned>(std::strtoul(v, nullptr, 10));
        } else if (arg == "--data-len") {
            const char* v = next("--data-len"); if (!v) return false;
            a.data_len = static_cast<unsigned>(std::strtoul(v, nullptr, 10));
        } else if (arg == "--seed") {
            const char* v = next("--seed"); if (!v) return false;
            a.seed = static_cast<unsigned>(std::strtoul(v, nullptr, 10));
        } else if (arg == "--gap-ms") {
            const char* v = next("--gap-ms"); if (!v) return false;
            a.gap_ms = static_cast<unsigned>(std::strtoul(v, nullptr, 10));
        } else if (arg == "--timeout-secs") {
            const char* v = next("--timeout-secs"); if (!v) return false;
            a.timeout_secs = static_cast<unsigned>(std::strtoul(v, nullptr, 10));
        } else if (arg == "--no-filter") {
            a.filter = false;
        } else {
            err = "unknown argument: " + arg;
            return false;
        }
    }
    if (a.role != "writer" && a.role != "reader") {
        err = "--role must be 'writer' or 'reader'";
        return false;
    }
    return true;
}

// Deterministic payload: data[j] = (seed + frame_index + j) & 0xFF — lets
// the test process independently recompute exactly what each frame's
// payload must be from the writer's own CLI args, without trusting the
// writer's self-report as the only source of truth.
std::vector<uint8_t> make_payload(unsigned seed, unsigned frame_index, unsigned len) {
    std::vector<uint8_t> data(len);
    for (unsigned j = 0; j < len; ++j) {
        data[j] = static_cast<uint8_t>((seed + frame_index + j) & 0xFF);
    }
    return data;
}

int run_writer(const Args& a) {
    auto [bus, ec] = can::socketcan::Bus::create(a.iface);
    if (ec) {
        std::cout << caninterop::to_json_line(
            Summary{"writer", false, a.iface, 0, "open failed: " + ec.message()}) << "\n";
        return 1;
    }

    unsigned sent = 0;
    for (unsigned i = 0; i < a.count; ++i) {
        can::Frame f{};
        f.id  = a.id;
        f.ext = a.ext;
        f.fd  = a.fd;
        f.brs = a.brs && a.fd;
        f.data = make_payload(a.seed, i, a.data_len);

        auto send_ec = bus->send(f);
        if (send_ec) {
            std::cout << caninterop::to_json_line(
                Summary{"writer", false, a.iface, sent, "send failed: " + send_ec.message()}) << "\n";
            bus->close();
            return 1;
        }
        std::cout << caninterop::to_json_line(FrameRecord::from_frame(f)) << "\n";
        std::cout.flush();
        ++sent;
        if (a.gap_ms > 0 && i + 1 < a.count) {
            std::this_thread::sleep_for(std::chrono::milliseconds(a.gap_ms));
        }
    }

    bus->close();
    std::cout << caninterop::to_json_line(Summary{"writer", sent == a.count, a.iface, sent, ""}) << "\n";
    return sent == a.count ? 0 : 1;
}

int run_reader(const Args& a) {
    auto [bus, ec] = can::socketcan::Bus::create(a.iface);
    if (ec) {
        std::cout << caninterop::to_json_line(
            Summary{"reader", false, a.iface, 0, "open failed: " + ec.message()}) << "\n";
        return 1;
    }

    std::vector<can::Filter> filters;
    if (a.filter) filters.push_back(can::Filter{a.id, can::kCANMaxExtID});

    auto [ch, sub_ec] = bus->subscribe(filters);
    if (sub_ec) {
        std::cout << caninterop::to_json_line(
            Summary{"reader", false, a.iface, 0, "subscribe failed: " + sub_ec.message()}) << "\n";
        bus->close();
        return 1;
    }

    unsigned received = 0;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(a.timeout_secs);
    while (received < a.count && std::chrono::steady_clock::now() < deadline) {
        auto got = ch->try_recv();
        if (!got.has_value()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        std::cout << caninterop::to_json_line(FrameRecord::from_frame(*got)) << "\n";
        std::cout.flush();
        ++received;
    }

    bus->close();
    bool ok = received == a.count;
    std::string error = ok ? "" : "timed out waiting for frames";
    std::cout << caninterop::to_json_line(Summary{"reader", ok, a.iface, received, error}) << "\n";
    return ok ? 0 : 1;
}

} // anonymous namespace

int main(int argc, char** argv) {
    Args a;
    std::string err;
    if (!parse_args(argc, argv, a, err)) {
        std::cerr << "can-interop-peer: " << err << "\n";
        std::cout << caninterop::to_json_line(Summary{"unknown", false, "", 0, err}) << "\n";
        return 2;
    }

    return a.role == "writer" ? run_writer(a) : run_reader(a);
}
