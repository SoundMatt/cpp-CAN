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

namespace {

// ISO 11898-1 CAN-FD Data Length Code (DLC) mapping: the on-wire data
// length of a CAN-FD frame may only be one of these values (DLC 9–15 map
// to 12/16/20/24/32/48/64 respectively); no other length is representable
// on the wire, and the Linux kernel (and other CAN-FD stacks) rejects any
// canfd_frame.len outside this set.
bool is_canonical_fd_len(std::size_t len) noexcept {
    switch (len) {
        case 0: case 1: case 2: case 3: case 4: case 5: case 6: case 7: case 8:
        case 12: case 16: case 20: case 24: case 32: case 48: case 64:
            return true;
        default:
            return false;
    }
}

} // anonymous namespace

// ── validate_frame ────────────────────────────────────────────────────────────

// fusa:req REQ-CAN-009 REQ-CAN-010 REQ-CAN-011 REQ-CAN-012 REQ-CAN-013 REQ-CAN-014
// fusa:req REQ-SEC-001 REQ-SEC-002 REQ-SEC-003
void validate_frame(const Frame& f) {
    if (f.fd && f.xl)
        throw ErrInvalidFrame("FD and XL are mutually exclusive");
    if (f.xl) {
        if (f.ext)
            throw ErrInvalidFrame("CAN XL frame must not use extended ID");
        if (f.rtr)
            throw ErrInvalidFrame("CAN XL frame must not be RTR");
        if (f.brs)
            throw ErrInvalidFrame("CAN XL frame must not have BRS");
        if (f.id > kCANMaxStdID)
            throw ErrInvalidFrame("CAN XL priority ID exceeds 11 bits");
        if (f.data.empty() || f.data.size() > kCANXLMaxDataLen)
            throw ErrInvalidFrame("CAN XL data must be 1–2048 bytes");
    } else {
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
        if (f.fd && !is_canonical_fd_len(f.data.size()))
            throw ErrInvalidFrame(
                "CAN FD data length is not a canonical DLC length "
                "(must be one of 0-8,12,16,20,24,32,48,64 bytes)");
        if (f.brs && !f.fd)
            throw ErrInvalidFrame("BRS requires fd=true");
        if (f.esi && !f.fd)
            throw ErrInvalidFrame("ESI requires fd=true");
    }
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
    if (f.esi)       m.meta["can.esi"]  = "true";
    if (f.xl)        m.meta["can.xl"]   = "true";
    if (f.sdt != 0)  m.meta["can.sdt"]  = std::to_string(static_cast<unsigned>(f.sdt));
    if (f.vcid != 0) m.meta["can.vcid"] = std::to_string(static_cast<unsigned>(f.vcid));
    if (f.af != 0)   m.meta["can.af"]   = std::to_string(f.af);
    if (f.sec)       m.meta["can.sec"]  = "true";
    return m;
}

// fusa:req REQ-CAN-007 REQ-CAN-015
relay::Message Frame::to_message() const { return can::to_message(*this); }

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
    it = m.meta.find("can.esi");
    if (it != m.meta.end() && it->second == "true") f.esi = true;
    it = m.meta.find("can.xl");
    if (it != m.meta.end() && it->second == "true") f.xl = true;
    it = m.meta.find("can.sdt");
    if (it != m.meta.end()) {
        try { f.sdt = static_cast<uint8_t>(std::stoull(it->second)); }
        catch (...) { throw ErrInvalidFrame("invalid can.sdt: " + it->second); }
    }
    it = m.meta.find("can.vcid");
    if (it != m.meta.end()) {
        try { f.vcid = static_cast<uint8_t>(std::stoull(it->second)); }
        catch (...) { throw ErrInvalidFrame("invalid can.vcid: " + it->second); }
    }
    it = m.meta.find("can.af");
    if (it != m.meta.end()) {
        try { f.af = static_cast<uint32_t>(std::stoull(it->second)); }
        catch (...) { throw ErrInvalidFrame("invalid can.af: " + it->second); }
    }
    it = m.meta.find("can.sec");
    if (it != m.meta.end() && it->second == "true") f.sec = true;

    // Ensure a Frame reconstructed from arbitrary meta/id fields is
    // structurally valid (REQ-CAN-015) before handing it to callers that
    // may not themselves call validate_frame (e.g. direct from_message
    // callers outside CanAdapter::send).
    validate_frame(f);
    return f;
}

// ── RELAY adapter ── fusa:req REQ-CAN-016 ────────────────────────────────────

namespace {

class CanAdapter : public relay::INode {
public:
    explicit CanAdapter(std::shared_ptr<IBus> bus) : bus_(std::move(bus)) {}

    relay::Protocol protocol() const noexcept override { return relay::Protocol::CAN; }

    std::error_code send(relay::Context /*ctx*/, const relay::Message& msg) override {
        try {
            Frame f = from_message(msg);
            return bus_->send(std::move(f));
        } catch (const ErrInvalidFrame& e) {
            // Distinct from relay::Errc::payload_too_large: from_message's
            // validate_frame() rejects structurally-invalid frames for many
            // reasons besides oversize payloads (bad ID width, illegal flag
            // combinations, ...); collapsing all of them into
            // payload_too_large lost error fidelity for callers.
            return std::make_error_code(std::errc::invalid_argument);
        }
    }

    std::pair<std::shared_ptr<relay::Channel<relay::Message>>, std::error_code>
        subscribe(relay::SubscriberOptions opts = {}) override
    {
        auto [frames, err] = bus_->subscribe({}, {});
        if (err) return {nullptr, err};

        auto out = std::make_shared<relay::Channel<relay::Message>>(opts.channel_depth);

        // Bridge thread — drains the underlying CAN bus channel and republishes
        // as relay::Message, applying the requested back-pressure policy.
        std::thread([this, frames = std::move(frames), out,
                     bp = opts.back_pressure]() mutable
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

    std::error_code close() noexcept override { return bus_->close(); }

private:
    std::shared_ptr<IBus> bus_;
    std::atomic<uint64_t> seq_{0};
};

} // anonymous namespace

std::unique_ptr<relay::INode> adapt(std::shared_ptr<IBus> bus) {
    return std::make_unique<CanAdapter>(std::move(bus));
}

} // namespace can
