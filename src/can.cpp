// Copyright (c) 2026 Matt Jones. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include <can/can.hpp>
#include <atomic>
#include <chrono>
#include <stdexcept>
#include <string>
#include <thread>

namespace can {

// ── validate_frame ────────────────────────────────────────────────────────────

// fusa:req REQ-CAN-009 REQ-CAN-010 REQ-CAN-011 REQ-CAN-012 REQ-CAN-013 REQ-CAN-014
// fusa:req REQ-SEC-001 REQ-SEC-002 REQ-SEC-003
void validate_frame(const Frame& f) {
    if (f.ext && f.id > kCANMaxExtID)
        throw ErrInvalidFrame("extended ID exceeds 29 bits");
    if (!f.ext && f.id > kCANMaxStdID)
        throw ErrInvalidFrame("standard ID exceeds 11 bits");
    if (f.rtr && f.fd)
        throw ErrInvalidFrame("RTR frame cannot be CAN FD");
    if (f.rtr && !f.data.empty())
        throw ErrInvalidFrame("RTR frame must not carry data");
    if (!f.fd && f.data.size() > kCANMaxDataLen)
        throw ErrInvalidFrame("standard CAN frame data exceeds 8 bytes");
    if (f.fd && f.data.size() > kCANFDMaxDataLen)
        throw ErrInvalidFrame("CAN FD frame data exceeds 64 bytes");
    if (f.brs && !f.fd)
        throw ErrInvalidFrame("BRS requires fd=true");
}

// ── RELAY bridge: to_message / from_message ───────────────────────────────────

// fusa:req REQ-CAN-007 REQ-CAN-015
relay::Message to_message(const Frame& f) {
    relay::Message m;
    m.protocol  = relay::Protocol::CAN;
    m.id        = std::to_string(f.id);
    m.payload   = f.data;
    m.timestamp = std::chrono::system_clock::now();
    m.meta["can.ext"] = f.ext ? "true" : "false";
    m.meta["can.fd"]  = f.fd  ? "true" : "false";
    m.meta["can.rtr"] = f.rtr ? "true" : "false";
    m.meta["can.brs"] = f.brs ? "true" : "false";
    return m;
}

// fusa:req REQ-CAN-015
Frame from_message(const relay::Message& m) {
    unsigned long long id_val{};
    try {
        id_val = std::stoull(m.id);
    } catch (...) {
        throw ErrInvalidFrame("invalid CAN ID: " + m.id);
    }
    if (id_val > kCANMaxExtID)
        throw ErrInvalidFrame("invalid CAN ID: " + m.id);

    Frame f;
    f.id   = static_cast<uint32_t>(id_val);
    f.data = m.payload;

    auto it = m.meta.find("can.ext");
    if (it != m.meta.end() && it->second == "true") f.ext = true;
    it = m.meta.find("can.fd");
    if (it != m.meta.end() && it->second == "true") f.fd = true;
    it = m.meta.find("can.rtr");
    if (it != m.meta.end() && it->second == "true") f.rtr = true;
    it = m.meta.find("can.brs");
    if (it != m.meta.end() && it->second == "true") f.brs = true;
    return f;
}

// ── RELAY adapter ── fusa:req REQ-CAN-016 ────────────────────────────────────

namespace {

class CanAdapter : public relay::INode {
public:
    explicit CanAdapter(std::shared_ptr<IBus> bus) : bus_(std::move(bus)) {}

    relay::Protocol protocol() const noexcept override { return relay::Protocol::CAN; }

    std::error_code send(relay::Message msg) override {
        try {
            Frame f = from_message(msg);
            return bus_->send(std::move(f));
        } catch (const ErrInvalidFrame& e) {
            return relay::make_error_code(relay::Errc::payload_too_large);
        }
    }

    std::pair<std::shared_ptr<Chan<relay::Message>>, std::error_code>
        subscribe(std::vector<relay::SubscriberOption> opts = {}) override
    {
        relay::SubscriberConfig cfg = relay::apply_options(opts);

        auto [frames, err] = bus_->subscribe({}, {});
        if (err) return {nullptr, err};

        int depth = cfg.effective_depth(64);
        auto out = std::make_shared<Chan<relay::Message>>(static_cast<std::size_t>(depth));

        // Bridge goroutine
        std::thread([this, frames = std::move(frames), out,
                     bp = cfg.back_pressure]() mutable
        {
            while (true) {
                auto opt_f = frames->recv();
                if (!opt_f) break;  // upstream closed

                relay::Message msg = to_message(*opt_f);
                msg.seq = ++seq_;

                switch (bp) {
                case relay::BackPressurePolicy::DropNewest:
                    out->try_send(std::move(msg));
                    break;
                case relay::BackPressurePolicy::DropOldest:
                    out->send_drop_oldest(std::move(msg));
                    break;
                case relay::BackPressurePolicy::Block:
                    out->send(std::move(msg));
                    break;
                }
            }
            out->close();
        }).detach();

        return {out, {}};
    }

    std::error_code close() override { return bus_->close(); }

private:
    std::shared_ptr<IBus> bus_;
    std::atomic<uint64_t> seq_{0};
};

} // anonymous namespace

std::unique_ptr<relay::INode> adapt(std::shared_ptr<IBus> bus) {
    return std::make_unique<CanAdapter>(std::move(bus));
}

} // namespace can
