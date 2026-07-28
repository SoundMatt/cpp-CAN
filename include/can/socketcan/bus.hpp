// Copyright (c) 2026 Matt Jones. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

// socketcan/bus.hpp — Linux SocketCAN CAN bus implementation.
// Works with hardware CAN interfaces (can0, can1, ...) and the Linux
// virtual CAN driver (vcan0, ...). CAN FD frames are supported when the
// underlying interface is FD-capable. Mirrors go-CAN's socketcan package
// (github.com/SoundMatt/go-CAN/socketcan).
//
// Requires Linux kernel >= 3.6 with CONFIG_CAN_RAW=y or =m. This header is
// only compiled on Linux (see CMakeLists.txt) — the kernel uapi headers it
// depends on (<linux/can.h> etc.) do not exist on other platforms.
//
// CAN XL is not implemented by this transport (Frame::xl frames are
// rejected by send()): CAN_RAW support for CANXL_XLF is a very recent
// kernel feature not guaranteed present on common CI/production kernels,
// and go-CAN's own socketcan package — the reference this file mirrors —
// does not support it either. Classic CAN and CAN FD are fully supported.
//
// fusa:req REQ-SCAN-001
// fusa:req REQ-SCAN-002
// fusa:req REQ-SCAN-003
// fusa:req REQ-SCAN-004
// fusa:req REQ-SCAN-005
// fusa:req REQ-SCAN-006
// fusa:req REQ-SCAN-007
// fusa:req REQ-SCAN-008

#pragma once

#include <can/can.hpp>
#include <can/channel.hpp>
#include <atomic>
#include <memory>
#include <shared_mutex>
#include <string>
#include <thread>
#include <vector>

namespace can::socketcan {

// Bus is a Linux SocketCAN bus implementation supporting classic CAN and
// CAN FD, bound to a real network interface (hardware or vcan).
//
// Multiple threads may call send() and subscribe() concurrently. A single
// background thread performs blocking reads from the underlying CAN_RAW
// socket and fans received frames out to matching subscriber channels —
// the same Chan<Frame>/Filter/back-pressure model as can::virt::Bus, but
// backed by genuine kernel CAN traffic instead of an in-process broadcast.
//
// fusa:req REQ-SCAN-001 REQ-SCAN-005 REQ-SCAN-006
class Bus : public IBus {
public:
    // Opens a raw CAN_RAW socket and binds it to `iface` (e.g. "vcan0",
    // "can0"). CAN FD frame reception is enabled automatically (best
    // effort — silently ignored on classic-only interfaces, matching
    // go-CAN's New()). Returns ErrNotConnected if the socket, interface
    // lookup, or bind fails.
    // fusa:req REQ-SCAN-001 REQ-SCAN-002
    static std::pair<std::shared_ptr<Bus>, std::error_code> create(const std::string& iface);

    ~Bus() override;

    // Non-copyable, non-movable — always heap-allocated via create().
    Bus(const Bus&)            = delete;
    Bus& operator=(const Bus&) = delete;

    // IBus
    // fusa:req REQ-SCAN-003 REQ-SCAN-004
    std::error_code send(Frame frame) override;
    // fusa:req REQ-SCAN-005 REQ-SCAN-007 REQ-SCAN-008
    std::pair<std::shared_ptr<Chan<Frame>>, std::error_code>
        subscribe(std::vector<Filter> filters = {},
                  std::vector<relay::SubscriberOption> opts = {}) override;
    // fusa:req REQ-SCAN-006
    std::error_code close() override;

    // The interface name this bus is bound to (e.g. "vcan0").
    const std::string& iface() const noexcept { return iface_; }

private:
    Bus() = default;
    void read_loop();

    int         fd_{-1};
    std::string iface_;

    mutable std::shared_mutex mu_;
    struct Subscription {
        std::vector<Filter>          filters;
        std::shared_ptr<Chan<Frame>> ch;
    };
    std::vector<Subscription> subs_;
    bool                      closed_{false};

    std::thread       reader_;
    std::atomic<bool> stop_{false};
};

} // namespace can::socketcan
