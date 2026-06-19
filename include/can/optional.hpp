// Copyright (c) 2026 Matt Jones. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

// optional.hpp — Optional capability interfaces for CAN bus implementations.
// Mirrors can_optional.go from go-CAN.
//
// fusa:req REQ-CAN-017 REQ-CAN-018

#pragma once

#include "can.hpp"
#include <chrono>

namespace can {

// ── LoanedFrame ───────────────────────────────────────────────────────────────

// A pre-allocated frame obtained from ILoaningBus::loan().
// Call release() when done to return the buffer to the pool.
struct LoanedFrame {
    Frame              frame;
    std::function<void()> release_fn;

    LoanedFrame() = default;
    LoanedFrame(Frame f, std::function<void()> fn)
        : frame(std::move(f)), release_fn(std::move(fn)) {}

    ~LoanedFrame() { release(); }

    void release() {
        if (release_fn) {
            release_fn();
            release_fn = nullptr;
        }
    }

    // Non-copyable
    LoanedFrame(const LoanedFrame&)            = delete;
    LoanedFrame& operator=(const LoanedFrame&) = delete;
    LoanedFrame(LoanedFrame&&)                 = default;
    LoanedFrame& operator=(LoanedFrame&&)      = default;
};

// ── ILoaningBus ───────────────────────────────────────────────────────────────

// Optional zero-copy extension to IBus.
class ILoaningBus : public IBus {
public:
    // Returns a pre-allocated LoanedFrame from a pool.
    virtual std::pair<std::unique_ptr<LoanedFrame>, std::error_code> loan() = 0;
    // Transmits the loaned frame and calls release() on it.
    virtual std::error_code send_loaned(std::unique_ptr<LoanedFrame> f) = 0;
};

// ── IHealthProvider ───────────────────────────────────────────────────────────

// Re-export the relay health types into the can namespace for convenience.
using HealthStatus    = relay::HealthStatus;
using Health          = relay::Health;
using IHealthProvider = relay::IHealthProvider;

// ── IMetricsProvider ─────────────────────────────────────────────────────────

using Metrics          = relay::Metrics;
using IMetricsProvider = relay::IMetricsProvider;

// ── IDrainer ─────────────────────────────────────────────────────────────────

using IDrainer = relay::IDrainer;

} // namespace can
