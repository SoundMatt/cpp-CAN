// Copyright (c) 2026 Matt Jones. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

// virtual/bus.hpp — In-process broadcast CAN bus.
// Zero OS dependencies; default transport for development and testing.
// Mirrors go-CAN/virtual package.
//
// fusa:req REQ-VIRT-001 through REQ-VIRT-005

#pragma once

#include <can/optional.hpp>
#include <atomic>
#include <memory>
#include <shared_mutex>
#include <vector>

namespace can::virt {

// Bus is an in-process CAN bus.
//
// All frames sent on the bus are broadcast to every subscriber whose
// filters match, including the sender — mirroring real CAN bus behaviour.
// Multiple threads may call send() and subscribe() concurrently.
//
// fusa:req REQ-VIRT-001
// fusa:req REQ-VIRT-002
// fusa:req REQ-VIRT-003
// fusa:req REQ-VIRT-004
// fusa:req REQ-VIRT-005
class Bus : public ILoaningBus,
            public IHealthProvider,
            public IMetricsProvider,
            public IDrainer
{
public:
    // Factory constructor — always heap-allocated.
    static std::shared_ptr<Bus> create();

    // IBus
    std::error_code send(Frame frame) override;
    std::pair<std::shared_ptr<Chan<Frame>>, std::error_code>
        subscribe(std::vector<Filter> filters = {},
                  std::vector<relay::SubscriberOption> opts = {}) override;
    std::error_code close() override;

    // ILoaningBus
    std::pair<std::unique_ptr<LoanedFrame>, std::error_code> loan() override;
    std::error_code send_loaned(std::unique_ptr<LoanedFrame> f) override;

    // IHealthProvider
    Health health() const override;

    // IMetricsProvider
    Metrics metrics() const override;

    // IDrainer — waits until all subscriber channels are empty or timeout elapses.
    std::error_code close_with_drain(std::chrono::milliseconds timeout) override;

private:
    Bus() = default;

    struct Subscription {
        std::vector<Filter>                 filters;
        std::shared_ptr<Chan<Frame>>        ch;
        relay::BackPressurePolicy           back_pressure;
    };

    mutable std::shared_mutex               mu_;
    std::vector<Subscription>               subs_;
    bool                                    closed_{false};

    std::atomic<uint64_t>                   write_count_{0};
    std::atomic<uint64_t>                   deliver_count_{0};
    std::atomic<uint64_t>                   drop_count_{0};
    std::atomic<uint64_t>                   bytes_written_{0};
    std::atomic<uint64_t>                   bytes_delivered_{0};
    std::atomic<uint64_t>                   error_count_{0};

    bool all_drained() const;
};

} // namespace can::virt
