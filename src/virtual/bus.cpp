// Copyright (c) 2026 Matt Jones. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include <can/virtual/bus.hpp>
#include <chrono>
#include <thread>

namespace can::virt {

static constexpr int kDefaultChanDepth = 64;

namespace {
    bool matches_any(const std::vector<Filter>& filters, const Frame& f) {
        if (filters.empty()) return true;
        for (const auto& fl : filters)
            if (fl.matches(f)) return true;
        return false;
    }
} // anonymous namespace

std::shared_ptr<Bus> Bus::create() {
    return std::shared_ptr<Bus>(new Bus());
}

// fusa:req REQ-VIRT-001
// fusa:req REQ-VIRT-002
// fusa:req REQ-VIRT-005
std::error_code Bus::send(Frame frame) {
    try {
        validate_frame(frame);
    } catch (...) {
        error_count_.fetch_add(1);
        return relay::make_error_code(relay::Errc::payload_too_large);
    }

    std::shared_lock<std::shared_mutex> lk(mu_);
    if (closed_) {
        error_count_.fetch_add(1);
        return relay::ErrClosed();
    }

    write_count_.fetch_add(1);
    bytes_written_.fetch_add(frame.data.size());

    for (const auto& s : subs_) {
        if (!matches_any(s.filters, frame)) continue;

        Chan<Frame>::SendResult result;
        switch (s.back_pressure) {
        case relay::BackPressurePolicy::DropNewest:
            result = s.ch->try_send(frame);
            if (result == Chan<Frame>::SendResult::Ok) {
                deliver_count_.fetch_add(1);
                bytes_delivered_.fetch_add(frame.data.size());
            } else if (result == Chan<Frame>::SendResult::Full) {
                drop_count_.fetch_add(1);
            }
            break;

        case relay::BackPressurePolicy::DropOldest:
            s.ch->send_drop_oldest(frame);
            deliver_count_.fetch_add(1);
            bytes_delivered_.fetch_add(frame.data.size());
            break;

        case relay::BackPressurePolicy::Block:
            s.ch->send(frame);
            deliver_count_.fetch_add(1);
            bytes_delivered_.fetch_add(frame.data.size());
            break;
        }
    }
    return {};
}

// fusa:req REQ-VIRT-003
// fusa:req REQ-VIRT-004
std::pair<std::shared_ptr<Chan<Frame>>, std::error_code>
Bus::subscribe(std::vector<Filter> filters,
               std::vector<relay::SubscriberOption> opts)
{
    relay::SubscriberConfig cfg = relay::apply_options(opts);
    int depth = cfg.effective_depth(kDefaultChanDepth);

    std::unique_lock<std::shared_mutex> lk(mu_);
    if (closed_) return {std::shared_ptr<Chan<Frame>>{}, relay::ErrClosed()};

    auto ch = std::make_shared<Chan<Frame>>(static_cast<std::size_t>(depth));
    subs_.push_back({std::move(filters), ch, cfg.back_pressure});
    return {ch, std::error_code{}};
}

std::error_code Bus::close() {
    std::unique_lock<std::shared_mutex> lk(mu_);
    if (closed_) return {};
    closed_ = true;
    for (auto& s : subs_) s.ch->close();
    subs_.clear();
    return {};
}

std::pair<std::unique_ptr<LoanedFrame>, std::error_code> Bus::loan() {
    {
        std::shared_lock<std::shared_mutex> lk(mu_);
        if (closed_) return {std::unique_ptr<LoanedFrame>{}, relay::ErrClosed()};
    }
    auto lf = std::make_unique<LoanedFrame>(Frame{}, nullptr);
    return {std::move(lf), std::error_code{}};
}

std::error_code Bus::send_loaned(std::unique_ptr<LoanedFrame> f) {
    auto err = send(f->frame);
    f->release();
    return err;
}

Health Bus::health() const {
    std::shared_lock<std::shared_mutex> lk(mu_);
    if (closed_) return {HealthStatus::Down, "bus closed"};
    return {HealthStatus::OK, {}};
}

Metrics Bus::metrics() const {
    return {
        write_count_.load(),
        deliver_count_.load(),
        drop_count_.load(),
        bytes_written_.load(),
        bytes_delivered_.load(),
        error_count_.load(),
    };
}

std::error_code Bus::close_with_drain(std::chrono::milliseconds timeout) {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (all_drained()) return close();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    auto err = close();
    return err ? err : relay::ErrTimeout();
}

bool Bus::all_drained() const {
    std::shared_lock<std::shared_mutex> lk(mu_);
    for (const auto& s : subs_)
        if (s.ch->size() > 0) return false;
    return true;
}

} // namespace can::virt
